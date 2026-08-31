#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/common.sh"
platform="${1:?platform is required}"
architecture="${2:?architecture is required}"
toolset="${3:?toolset is required}"
force="${4:-0}"
system=linux; [[ "$platform" == Mac ]] && system=macosx
output_arch="$(architecture_output_name "$architecture")"
sdl_commit="$(config_value "$ROOT/Config/Dependencies.lock" SDL_COMMIT)"
json_commit="$(config_value "$ROOT/Config/Dependencies.lock" JSON_COMMIT)"
assimp_commit="$(config_value "$ROOT/Config/Dependencies.lock" ASSIMP_COMMIT)"
jolt_url="$(config_value "$ROOT/Config/Dependencies.lock" JOLT_URL)"
jolt_commit="$(config_value "$ROOT/Config/Dependencies.lock" JOLT_COMMIT)"
recast_url="$(config_value "$ROOT/Config/Dependencies.lock" RECAST_URL)"
recast_commit="$(config_value "$ROOT/Config/Dependencies.lock" RECAST_COMMIT)"
miniaudio_url="$(config_value "$ROOT/Config/Dependencies.lock" MINIAUDIO_URL)"
miniaudio_commit="$(config_value "$ROOT/Config/Dependencies.lock" MINIAUDIO_COMMIT)"
freetype_url="$(config_value "$ROOT/Config/Dependencies.lock" FREETYPE_URL)"
freetype_commit="$(config_value "$ROOT/Config/Dependencies.lock" FREETYPE_COMMIT)"
harfbuzz_url="$(config_value "$ROOT/Config/Dependencies.lock" HARFBUZZ_URL)"
harfbuzz_commit="$(config_value "$ROOT/Config/Dependencies.lock" HARFBUZZ_COMMIT)"
fribidi_url="$(config_value "$ROOT/Config/Dependencies.lock" FRIBIDI_URL)"
fribidi_commit="$(config_value "$ROOT/Config/Dependencies.lock" FRIBIDI_COMMIT)"
libunibreak_url="$(config_value "$ROOT/Config/Dependencies.lock" LIBUNIBREAK_URL)"
libunibreak_commit="$(config_value "$ROOT/Config/Dependencies.lock" LIBUNIBREAK_COMMIT)"
libsodium_url="$(config_value "$ROOT/Config/Dependencies.lock" LIBSODIUM_URL)"
libsodium_commit="$(config_value "$ROOT/Config/Dependencies.lock" LIBSODIUM_COMMIT)"
macos_deployment_target="$(config_value "$ROOT/Config/Dependencies.lock" MACOS_DEPLOYMENT_TARGET)"
[[ "$macos_deployment_target" =~ ^[0-9]+\.[0-9]+$ ]] || {
  printf 'MACOS_DEPLOYMENT_TARGET must be a pinned major.minor version.\n' >&2
  exit 1
}
workspace_lock_acquire "$ROOT" dependencies >&2
dependencies_workspace_lock_held=1
cleanup_dependencies_workspace_lock() {
  if [[ "$dependencies_workspace_lock_held" -eq 1 ]]; then
    workspace_lock_release >&2 || true
    dependencies_workspace_lock_held=0
  fi
}
trap cleanup_dependencies_workspace_lock EXIT

assimp_patch_root="$ROOT/Patches/Assimp"
assimp_patches=()
while IFS= read -r patch; do
  assimp_patches+=("$patch")
done < <(find "$assimp_patch_root" -maxdepth 1 -type f -name '*.patch' -print | LC_ALL=C sort)
((${#assimp_patches[@]} > 0)) || { printf 'The Kéire Assimp patch set is empty.\n' >&2; exit 1; }
if command -v sha256sum >/dev/null 2>&1; then
  assimp_patch_digest="$({ for patch in "${assimp_patches[@]}"; do basename "$patch"; printf '\n'; cat "$patch"; done; } | sha256sum | awk '{print $1}')"
else
  assimp_patch_digest="$({ for patch in "${assimp_patches[@]}"; do basename "$patch"; printf '\n'; cat "$patch"; done; } | shasum -a 256 | awk '{print $1}')"
fi

validate_patched_assimp_source() {
  local source_path="${1:?source path is required}"
  local expected_stamp="$assimp_commit|$assimp_patch_digest"
  [[ -d "$source_path" && ! -L "$source_path" ]] || {
    printf 'Patched Assimp source is missing or unsafe: %s\n' "$source_path" >&2
    return 1
  }
  [[ "$(git -C "$source_path" rev-parse HEAD)" == "$assimp_commit" ]] || {
    printf 'Patched Assimp source is not based on locked commit %s.\n' "$assimp_commit" >&2
    return 1
  }
  [[ -f "$source_path/keire-assimp-patch.stamp" &&
     "$(tr -d '\r\n' < "$source_path/keire-assimp-patch.stamp")" == "$expected_stamp" ]] || {
    printf 'Patched Assimp source stamp does not match the locked commit and patch digest.\n' >&2
    return 1
  }
  local patch
  for patch in "${assimp_patches[@]}"; do
    git -C "$source_path" apply --reverse --check --whitespace=error-all -- "$patch" || return 1
  done
  git -C "$source_path" diff --check || return 1
  local expected_paths actual_paths untracked
  expected_paths="$(sed -n 's#^diff --git a/[^ ]* b/##p' "${assimp_patches[@]}" | LC_ALL=C sort -u)"
  actual_paths="$(git -C "$source_path" diff --name-only --no-ext-diff | LC_ALL=C sort -u)"
  [[ "$actual_paths" == "$expected_paths" ]] || {
    printf 'Patched Assimp source contains changes outside the committed patch set.\n' >&2
    return 1
  }
  untracked="$(git -C "$source_path" ls-files --others --exclude-standard)"
  [[ "$untracked" == keire-assimp-patch.stamp ]] || {
    printf 'Patched Assimp source contains unexpected untracked files.\n' >&2
    return 1
  }
}

prepare_patched_assimp_source() {
  local vendor_source="$ROOT/Vendor/assimp"
  local cache_root="$ROOT/Build/Dependencies/assimp-patched"
  local source_path="$cache_root/${assimp_commit:0:12}-${assimp_patch_digest:0:16}"
  local temporary_path="$cache_root/.tmp-${assimp_commit:0:12}-${assimp_patch_digest:0:16}-$$"
  [[ "$(git -C "$vendor_source" rev-parse HEAD)" == "$assimp_commit" &&
     -z "$(git -C "$vendor_source" status --porcelain --untracked-files=all)" ]] || {
    printf 'The Assimp submodule must match the locked commit and remain clean before downstream patches are applied.\n' >&2
    return 1
  }
  if [[ -e "$source_path" || -L "$source_path" ]]; then
    validate_patched_assimp_source "$source_path" || return 1
    printf '%s\n' "$source_path"
    return
  fi
  mkdir -p "$cache_root"
  case "$temporary_path" in "$cache_root"/*) rm -rf "$temporary_path" ;; *) return 1 ;; esac
  if ! git clone --quiet --no-hardlinks "$vendor_source" "$temporary_path"; then
    case "$temporary_path" in "$cache_root"/*) rm -rf "$temporary_path" ;; esac
    printf 'Could not clone the locked Assimp submodule.\n' >&2
    return 1
  fi
  local patch
  for patch in "${assimp_patches[@]}"; do
    if ! git -C "$temporary_path" apply --whitespace=error-all -- "$patch"; then
      case "$temporary_path" in "$cache_root"/*) rm -rf "$temporary_path" ;; esac
      printf 'Could not apply Assimp patch %s.\n' "$(basename "$patch")" >&2
      return 1
    fi
  done
  printf '%s\n' "$assimp_commit|$assimp_patch_digest" > "$temporary_path/keire-assimp-patch.stamp"
  if ! validate_patched_assimp_source "$temporary_path" || ! mv "$temporary_path" "$source_path"; then
    case "$temporary_path" in "$cache_root"/*) rm -rf "$temporary_path" ;; esac
    printf 'Could not publish patched Assimp source.\n' >&2
    return 1
  fi
  printf '%s\n' "$source_path"
}

locked_source() {
  local name="${1:?dependency name is required}"
  local url="${2:?dependency URL is required}"
  local commit="${3:?dependency commit is required}"
  local cache_root="${XDG_CACHE_HOME:-$HOME/.cache}/keire/dependency-sources"
  local source_path="$cache_root/$name-$commit"
  local temporary_path="$cache_root/$name-$commit.tmp-$$"

  if [[ -e "$source_path" || -L "$source_path" ]]; then
    locked_git_source_validate "$source_path" "$commit" "$name" || return 1
    printf '%s\n' "$source_path"
    return
  fi

  mkdir -p "$cache_root" || return 1
  (
    set -e
    local cache_lock_held=0
    cleanup_locked_source() {
      if [[ "$cache_lock_held" -eq 1 ]]; then
        workspace_lock_release >&2 || true
        cache_lock_held=0
      fi
      case "${temporary_path:-}" in "$cache_root"/*) rm -rf "$temporary_path" || true ;; esac
    }
    trap cleanup_locked_source EXIT
    workspace_lock_acquire "$cache_root" "dependency-source-$name-$commit" \
      ".locks/$name-$commit.lock" >&2 || exit 1
    cache_lock_held=1
    # A different worktree can publish the immutable checkout while this process waits for the shared cache lock.
    if [[ -e "$source_path" || -L "$source_path" ]]; then
      locked_git_source_validate "$source_path" "$commit" "$name" || exit 1
      workspace_lock_release >&2
      cache_lock_held=0
      printf '%s\n' "$source_path"
      trap - EXIT
      exit 0
    fi
    case "$temporary_path" in
      "$cache_root"/*) rm -rf "$temporary_path" ;;
      *) printf 'Refusing to replace a dependency source outside %s.\n' "$cache_root" >&2; exit 1 ;;
    esac
    if ! git clone --quiet --filter=blob:none --no-checkout "$url" "$temporary_path" ||
       ! git -C "$temporary_path" fetch --quiet --depth 1 origin "$commit" ||
       ! git -C "$temporary_path" checkout --quiet --detach "$commit" ||
       ! locked_git_source_validate "$temporary_path" "$commit" "$name"; then
      printf 'Could not prepare locked %s source at %s.\n' "$name" "$commit" >&2
      exit 1
    fi
    if ! mv "$temporary_path" "$source_path"; then
      printf 'Could not publish locked %s source at %s.\n' "$name" "$commit" >&2
      exit 1
    fi
    temporary_path=""
    locked_git_source_validate "$source_path" "$commit" "$name" || exit 1
    workspace_lock_release >&2
    cache_lock_held=0
    printf '%s\n' "$source_path"
    trap - EXIT
  )
}

jolt_source="$(locked_source jolt "$jolt_url" "$jolt_commit")"
recast_source="$(locked_source recast "$recast_url" "$recast_commit")"
miniaudio_source="$(locked_source miniaudio "$miniaudio_url" "$miniaudio_commit")"
freetype_source="$(locked_source freetype "$freetype_url" "$freetype_commit")"
harfbuzz_source="$(locked_source harfbuzz "$harfbuzz_url" "$harfbuzz_commit")"
fribidi_source="$(locked_source fribidi "$fribidi_url" "$fribidi_commit")"
libunibreak_source="$(locked_source libunibreak "$libunibreak_url" "$libunibreak_commit")"
libsodium_source="$(locked_source libsodium "$libsodium_url" "$libsodium_commit")"
assimp_patched_source="$(prepare_patched_assimp_source)"
if [[ "$toolset" == clang ]]; then export CC=clang CXX=clang++; else export CC=gcc CXX=g++; fi
compiler="$($CXX --version | head -n 1)"
bridge="$ROOT/Scripts/Dependencies/CMakeLists.txt"
bridge_capture="$ROOT/Scripts/Dependencies/RunAndCapture.cmake"
if command -v sha256sum >/dev/null 2>&1; then
  bridge_hash="$(sha256sum "$bridge" "$bridge_capture" | sha256sum | awk '{print $1}')"
else
  bridge_hash="$(shasum -a 256 "$bridge" "$bridge_capture" | shasum -a 256 | awk '{print $1}')"
fi
options=(-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
  -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=ON -DSDL_HAPTIC=ON -DSDL_HIDAPI=ON
  -DSDL_HIDAPI_JOYSTICK=ON -DSDL_HIDAPI_LIBUSB=OFF -DSDL_VIRTUAL_JOYSTICK=ON -DSDL_SENSOR=OFF
  -DSDL_RENDER=OFF -DSDL_GPU=ON -DSDL_DUMMYVIDEO=ON -DSDL_OFFSCREEN=ON -DSDL_INSTALL=ON
  -DSDL_INSTALL_DOCS=OFF -DSDL_INSTALL_CMAKEDIR_ROOT=cmake -DSDL_DEPS_SHARED=ON
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_LIBDIR=lib
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
  -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF
  -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF -DASSIMP_BUILD_OBJ_IMPORTER=ON -DASSIMP_BUILD_FBX_IMPORTER=ON
  -DASSIMP_BUILD_GLTF_IMPORTER=ON -DASSIMP_NO_EXPORT=ON
  -DASSIMP_BUILD_ZLIB=ON -DASSIMP_BUILD_DRACO=OFF -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_INSTALL=ON
  -DASSIMP_INJECT_DEBUG_POSTFIX=OFF -DASSIMP_IGNORE_GIT_HASH=ON -DLIBRARY_SUFFIX=
  -DJPH_BUILD_SHARED_LIBS=OFF -DCPP_RTTI_ENABLED=ON -DENABLE_INSTALL=ON -DOVERRIDE_CXX_FLAGS=OFF
  -DINTERPROCEDURAL_OPTIMIZATION=OFF -DENABLE_ALL_WARNINGS=OFF
  -DFLOATING_POINT_EXCEPTIONS_ENABLED=OFF -DUSE_SSE4_1=OFF -DUSE_SSE4_2=OFF
  -DUSE_AVX=OFF -DUSE_AVX2=OFF -DUSE_AVX512=OFF -DUSE_LZCNT=OFF
  -DUSE_TZCNT=OFF -DUSE_F16C=OFF -DUSE_FMADD=OFF
  -DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF -DPROFILER_IN_DEBUG_AND_RELEASE=OFF
  -DENABLE_OBJECT_STREAM=OFF -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF -DJPH_USE_DX12=OFF
  -DJPH_USE_VK=OFF -DJPH_USE_MTL=OFF -DJPH_USE_CPU_COMPUTE=OFF
  -DRECASTNAVIGATION_DEMO=OFF -DRECASTNAVIGATION_TESTS=OFF -DRECASTNAVIGATION_EXAMPLES=OFF
  -DRECASTNAVIGATION_DT_POLYREF64=ON -DMINIAUDIO_BUILD_EXAMPLES=OFF -DMINIAUDIO_BUILD_TESTS=OFF
  -DMINIAUDIO_BUILD_TOOLS=OFF -DMINIAUDIO_NO_EXTRA_NODES=ON -DMINIAUDIO_NO_LIBVORBIS=ON
  -DMINIAUDIO_NO_LIBOPUS=ON -DMINIAUDIO_INSTALL=ON)
if [[ "$platform" == Mac ]]; then
  cmake_architecture=x86_64; [[ "$architecture" == ARM64 ]] && cmake_architecture=arm64
  options+=("-DCMAKE_OSX_ARCHITECTURES=$cmake_architecture"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_deployment_target")
fi
key="$sdl_commit|$assimp_commit|$assimp_patch_digest|$jolt_commit|$recast_commit|$miniaudio_commit|$freetype_commit|$harfbuzz_commit|$fribidi_commit|$libunibreak_commit|$libsodium_commit|$architecture|$toolset|$compiler|$bridge_hash|${options[*]}"
base="$ROOT/Build/Dependencies/$system-$output_arch-$toolset"

validate_sdl_input_backends() {
  local build="${1:?build directory is required}"
  local configuration="${2:?configuration is required}"
  local configuration_name config macro
  configuration_name="$(printf '%s' "$configuration" | tr '[:upper:]' '[:lower:]')"
  config="$build/SDL/include-config-$configuration_name/build_config/SDL_build_config.h"
  [[ -f "$config" ]] || { printf 'SDL %s input-backend configuration is missing: %s\n' "$configuration" "$config" >&2; return 1; }
  for macro in SDL_JOYSTICK_HIDAPI SDL_JOYSTICK_VIRTUAL; do
    grep -Fq "#define $macro 1" "$config" || {
      printf 'SDL %s input-backend configuration is missing %s.\n' "$configuration" "$macro" >&2
      return 1
    }
  done
  for macro in SDL_JOYSTICK_DISABLED SDL_HAPTIC_DISABLED SDL_HIDAPI_DISABLED SDL_LIBUSB_DYNAMIC; do
    if grep -Eq "^#define[[:space:]]+$macro([[:space:]]|$)" "$config"; then
      printf 'SDL %s input-backend configuration unexpectedly defines %s.\n' "$configuration" "$macro" >&2
      return 1
    fi
  done
  if [[ "$platform" == Linux ]]; then
    for macro in SDL_JOYSTICK_LINUX SDL_HAPTIC_LINUX; do
      grep -Fq "#define $macro 1" "$config" || {
        printf 'SDL %s Linux input-backend configuration is missing %s.\n' "$configuration" "$macro" >&2
        return 1
      }
    done
    grep -Eq '^#define[[:space:]]+SDL_UDEV_DYNAMIC[[:space:]]+"[^"[:space:]]+"' "$config" || {
      printf 'SDL %s Linux input-backend configuration is missing dynamic libudev support.\n' "$configuration" >&2
      return 1
    }
  else
    for macro in SDL_JOYSTICK_IOKIT SDL_JOYSTICK_MFI SDL_HAPTIC_IOKIT; do
      grep -Fq "#define $macro 1" "$config" || {
        printf 'SDL %s macOS input-backend configuration is missing %s.\n' "$configuration" "$macro" >&2
        return 1
      }
    done
  fi
}

for configuration in Debug Release; do
  build="$base/$configuration"; install="$build/install"; library="$install/lib/libSDL3.a"; assimp_library="$install/lib/libassimp.a"; zlib_library="$install/lib/libzlibstatic.a"; jolt_library="$install/lib/libJolt.a"; miniaudio_library="$install/lib/libminiaudio.a"; freetype_library="$install/lib/libfreetype.a"; harfbuzz_library="$install/lib/libharfbuzz.a"; fribidi_library="$install/lib/libfribidi.a"; libunibreak_library="$install/lib/libunibreak.a"; stamp="$build/keire-dependency.stamp"
  sodium_runtime="$install/lib/libsodium.so"; [[ "$platform" == Mac ]] && sodium_runtime="$install/lib/libsodium.dylib"
  sodium_license="$install/share/licenses/libsodium/LICENSE"
  recast_suffix=""; [[ "$configuration" == Debug ]] && recast_suffix="-d"
  recast_library="$install/lib/libRecast$recast_suffix.a"
  detour_library="$install/lib/libDetour$recast_suffix.a"
  crowd_library="$install/lib/libDetourCrowd$recast_suffix.a"
  tile_cache_library="$install/lib/libDetourTileCache$recast_suffix.a"
  if [[ "$force" != 1 && -f "$library" && -f "$assimp_library" && -f "$zlib_library" &&
        -f "$jolt_library" && -f "$recast_library" && -f "$detour_library" &&
        -f "$crowd_library" && -f "$tile_cache_library" && -f "$miniaudio_library" &&
        -f "$freetype_library" && -f "$harfbuzz_library" && -f "$fribidi_library" &&
        -f "$libunibreak_library" &&
        -f "$sodium_runtime" && -f "$sodium_license" &&
        -f "$stamp" && "$(tr -d '\r\n' < "$stamp")" == "$key|$configuration" ]]; then
    validate_sdl_input_backends "$build" "$configuration"
    printf '==> Native %s dependency cache is current\n' "$configuration"
    continue
  fi
  [[ "$build" == "$base/Debug" || "$build" == "$base/Release" ]] || { printf 'Refusing to replace dependency cache outside %s.\n' "$base" >&2; exit 1; }
  rm -rf "$build"
  mkdir -p "$build"
  cmake -S "$ROOT/Scripts/Dependencies" -B "$build" -G Ninja -DKEIRE_SDL_SOURCE="$ROOT/Vendor/SDL" -DKEIRE_ASSIMP_SOURCE="$assimp_patched_source" -DKEIRE_JOLT_SOURCE="$jolt_source" -DKEIRE_RECAST_SOURCE="$recast_source" -DKEIRE_MINIAUDIO_SOURCE="$miniaudio_source" -DKEIRE_FREETYPE_SOURCE="$freetype_source" -DKEIRE_HARFBUZZ_SOURCE="$harfbuzz_source" -DKEIRE_FRIBIDI_SOURCE="$fribidi_source" -DKEIRE_LIBUNIBREAK_SOURCE="$libunibreak_source" -DCMAKE_BUILD_TYPE="$configuration" -DCMAKE_INSTALL_PREFIX="$install" "${options[@]}"
  cmake --build "$build" --target install --parallel "$(build_parallel_jobs)"
  validate_sdl_input_backends "$build" "$configuration"
  sodium_build="$build/libsodium"
  mkdir -p "$sodium_build"
  sodium_cflags="-O2"
  [[ "$configuration" == Debug ]] && sodium_cflags="-O0 -g"
  sodium_ldflags=""
  if [[ "$platform" == Mac ]]; then
    sodium_cflags="$sodium_cflags -arch $cmake_architecture -mmacosx-version-min=$macos_deployment_target"
    sodium_ldflags="-arch $cmake_architecture -mmacosx-version-min=$macos_deployment_target"
  fi
  (
    cd "$sodium_build"
    CC="$CC" CFLAGS="$sodium_cflags" LDFLAGS="$sodium_ldflags" \
      "$libsodium_source/configure" --prefix="$install" --disable-static --enable-shared \
      --disable-dependency-tracking
  )
  make -C "$sodium_build" -j2
  make -C "$sodium_build" install
  mkdir -p "$(dirname "$sodium_license")"
  cp "$libsodium_source/LICENSE" "$sodium_license"
  [[ -f "$library" && -f "$assimp_library" && -f "$zlib_library" && -f "$jolt_library" &&
     -f "$recast_library" && -f "$detour_library" && -f "$crowd_library" &&
     -f "$tile_cache_library" && -f "$miniaudio_library" && -f "$sodium_runtime" &&
     -f "$freetype_library" && -f "$harfbuzz_library" && -f "$fribidi_library" &&
     -f "$libunibreak_library" &&
     -f "$sodium_license" &&
     -f "$install/include/assimp/Importer.hpp" && -f "$install/include/SDL3/SDL.h" &&
     -f "$install/include/freetype2/ft2build.h" && -f "$install/include/harfbuzz/hb.h" &&
     -f "$install/include/fribidi/fribidi.h" && -f "$install/include/unibreak/linebreak.h" &&
     -f "$install/cmake/SDL3Config.cmake" ]] || { printf 'Native %s install is incomplete.\n' "$configuration" >&2; exit 1; }
  printf '%s\n' "$key|$configuration" > "$stamp"
done

bash "$SCRIPT_DIR/shader-compiler.sh" "$platform" "$architecture" "$toolset" "$force"

coral_metadata="$(bash "$SCRIPT_DIR/coral.sh" Debug 1 "$force" "$platform" "$architecture" "$toolset")"
printf '%s\n' "$coral_metadata"
coral_source="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_SOURCE=/{print substr($0, index($0, "=") + 1)}')"
coral_commit="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_COMMIT=/{print $2}')"
coral_patch_digest="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_PATCH_DIGEST=/{print $2}')"
coral_build_variant="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_BUILD_VARIANT=/{print $2}')"
coral_nethost_library="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_NETHOST_LIBRARY=/{print substr($0, index($0, "=") + 1)}')"
coral_nethost_runtime="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_NETHOST_RUNTIME=/{print substr($0, index($0, "=") + 1)}')"
coral_dotnet_root="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_DOTNET_ROOT=/{print substr($0, index($0, "=") + 1)}')"
coral_release_metadata="$(bash "$SCRIPT_DIR/coral.sh" Release 1 "$force" "$platform" "$architecture" "$toolset")"
printf '%s\n' "$coral_release_metadata"
coral_release_source="$(printf '%s\n' "$coral_release_metadata" | awk -F= '/^CORAL_SOURCE=/{print substr($0, index($0, "=") + 1)}')"
coral_release_variant="$(printf '%s\n' "$coral_release_metadata" | awk -F= '/^CORAL_BUILD_VARIANT=/{print $2}')"
coral_release_nethost_library="$(printf '%s\n' "$coral_release_metadata" | awk -F= '/^CORAL_NETHOST_LIBRARY=/{print substr($0, index($0, "=") + 1)}')"
coral_release_nethost_runtime="$(printf '%s\n' "$coral_release_metadata" | awk -F= '/^CORAL_NETHOST_RUNTIME=/{print substr($0, index($0, "=") + 1)}')"
coral_release_dotnet_root="$(printf '%s\n' "$coral_release_metadata" | awk -F= '/^CORAL_DOTNET_ROOT=/{print substr($0, index($0, "=") + 1)}')"
[[ "$coral_release_source" == "$coral_source" && "$coral_release_variant" == "$coral_build_variant" &&
   "$coral_release_nethost_library" == "$coral_nethost_library" &&
   "$coral_release_nethost_runtime" == "$coral_nethost_runtime" &&
   "$coral_release_dotnet_root" == "$coral_dotnet_root" ]] || {
  printf 'Coral Debug and Release metadata must resolve to one checkout-isolated build variant.\n' >&2
  exit 1
}

bash "$ROOT/Scripts/Unix/ffmpeg.sh" Debug "$base/Release" "$platform" "$architecture" "$toolset"
bash "$ROOT/Scripts/Unix/ffmpeg.sh" Release "$base/Release" "$platform" "$architecture" "$toolset"

coral_link="$ROOT/Build/Dependencies/coral-patched"
nethost_link="$ROOT/Build/Dependencies/coral-nethost"
dotnet_sdk_link="$ROOT/Build/Dependencies/dotnet-sdk"
mkdir -p "$ROOT/Build/Dependencies"
ln -sfn "$coral_source" "$coral_link"
ln -sfn "$(dirname "$coral_nethost_library")" "$nethost_link"
dotnet_root="$coral_dotnet_root"
[[ -x "$dotnet_root/dotnet" ]] || { printf 'Pinned Coral .NET SDK root is unavailable: %s\n' "$dotnet_root" >&2; exit 1; }
ln -sfn "$dotnet_root" "$dotnet_sdk_link"
nethost_name="$(basename "$coral_nethost_library")"
nethost_runtime_name="$nethost_name"
if [[ "$platform" == Mac ]]; then
  nethost_runtime_name="$(basename "$coral_nethost_runtime")"
  [[ -n "$coral_nethost_runtime" && -f "$coral_nethost_runtime" ]] || {
    printf 'Coral nethost runtime is missing: %s\n' "$coral_nethost_runtime" >&2
    exit 1
  }
fi

managed_output="$ROOT/Build/Managed"
mkdir -p "$managed_output"
"$dotnet_sdk_link/dotnet" build "$ROOT/KeireManaged/Keire.Managed.csproj" --configuration Release \
  --output "$managed_output" --nologo "/p:BaseIntermediateOutputPath=$managed_output/obj/"
[[ -f "$managed_output/Keire.Managed.dll" ]] || {
  printf 'Keire.Managed API build failed.\n' >&2
  exit 1
}

mkdir -p "$ROOT/Build/Generated"
debug_install="../Build/Dependencies/$system-$output_arch-$toolset/Debug/install"
release_install="../Build/Dependencies/$system-$output_arch-$toolset/Release/install"
sodium_extension=so
[[ "$platform" == Mac ]] && sodium_extension=dylib
platform_links='{ "dl", "m", "pthread" }'
if [[ "$platform" == Mac ]]; then
  platform_links='{ "Cocoa.framework", "CoreVideo.framework", "IOKit.framework", "CoreFoundation.framework", "CoreAudio.framework", "AudioToolbox.framework", "ForceFeedback.framework", "GameController.framework", "CoreHaptics.framework", "Carbon.framework", "Metal.framework", "QuartzCore.framework", "UniformTypeIdentifiers.framework", "UserNotifications.framework", "Security.framework" }'
fi
cat > "$ROOT/Build/Generated/Dependencies.lua" <<EOF
DependencyManifest = {
    MacOSDeploymentTarget = "$macos_deployment_target",
    SDLCommit = "$sdl_commit",
    JSONCommit = "$json_commit",
    AssimpCommit = "$assimp_commit",
    AssimpPatchDigest = "$assimp_patch_digest",
    JoltCommit = "$jolt_commit",
    RecastCommit = "$recast_commit",
    MiniaudioCommit = "$miniaudio_commit",
    FreeTypeCommit = "$freetype_commit",
    HarfBuzzCommit = "$harfbuzz_commit",
    FriBidiCommit = "$fribidi_commit",
    LibunibreakCommit = "$libunibreak_commit",
    SodiumCommit = "$libsodium_commit",
    CoralCommit = "$coral_commit",
    CoralPatchDigest = "$coral_patch_digest",
    CoralSource = "../Build/Dependencies/coral-patched",
    CoralInclude = "../Build/Dependencies/coral-patched/Coral.Native/Include",
    CoralDebugLibrary = "../Build/Dependencies/coral-patched/Build/Debug/libCoral.Native.a",
    CoralReleaseLibrary = "../Build/Dependencies/coral-patched/Build/Release/libCoral.Native.a",
    CoralManagedDebug = "../Build/Dependencies/coral-patched/Build/Debug",
    CoralManagedRelease = "../Build/Dependencies/coral-patched/Build/Release",
    CoralNetHostLibrary = "../Build/Dependencies/coral-nethost/$nethost_name",
    CoralNetHostRuntime = "../Build/Dependencies/coral-nethost/$nethost_runtime_name",
    SDL3Include = "$debug_install/include",
    SDL3DebugLibrary = "$debug_install/lib/libSDL3.a",
    SDL3ReleaseLibrary = "$release_install/lib/libSDL3.a",
    AssimpInclude = "$debug_install/include",
    AssimpDebugLibrary = "$debug_install/lib/libassimp.a",
    AssimpReleaseLibrary = "$release_install/lib/libassimp.a",
    AssimpZlibDebugLibrary = "$debug_install/lib/libzlibstatic.a",
    AssimpZlibReleaseLibrary = "$release_install/lib/libzlibstatic.a",
    JoltInclude = "$debug_install/include",
    JoltDebugLibrary = "$debug_install/lib/libJolt.a",
    JoltReleaseLibrary = "$release_install/lib/libJolt.a",
    RecastInclude = "$debug_install/include/recastnavigation",
    RecastDebugLibraries = { "$debug_install/lib/libRecast-d.a", "$debug_install/lib/libDetour-d.a", "$debug_install/lib/libDetourCrowd-d.a", "$debug_install/lib/libDetourTileCache-d.a" },
    RecastReleaseLibraries = { "$release_install/lib/libRecast.a", "$release_install/lib/libDetour.a", "$release_install/lib/libDetourCrowd.a", "$release_install/lib/libDetourTileCache.a" },
    MiniaudioInclude = "$debug_install/include/miniaudio",
    MiniaudioDebugLibrary = "$debug_install/lib/libminiaudio.a",
    MiniaudioReleaseLibrary = "$release_install/lib/libminiaudio.a",
    TypographyInclude = "$debug_install/include",
    FreeTypeInclude = "$debug_install/include/freetype2",
    FreeTypeDebugLibrary = "$debug_install/lib/libfreetype.a",
    FreeTypeReleaseLibrary = "$release_install/lib/libfreetype.a",
    HarfBuzzDebugLibrary = "$debug_install/lib/libharfbuzz.a",
    HarfBuzzReleaseLibrary = "$release_install/lib/libharfbuzz.a",
    FriBidiDebugLibrary = "$debug_install/lib/libfribidi.a",
    FriBidiReleaseLibrary = "$release_install/lib/libfribidi.a",
    LibunibreakDebugLibrary = "$debug_install/lib/libunibreak.a",
    LibunibreakReleaseLibrary = "$release_install/lib/libunibreak.a",
    SodiumDebugRuntime = "$debug_install/lib/libsodium.$sodium_extension",
    SodiumReleaseRuntime = "$release_install/lib/libsodium.$sodium_extension",
    SodiumLicense = "$release_install/share/licenses/libsodium/LICENSE",
    SDL3PlatformLinks = $platform_links
}
EOF
printf '==> Dependency manifest generated\n'
workspace_lock_release >&2
dependencies_workspace_lock_held=0
trap - EXIT
