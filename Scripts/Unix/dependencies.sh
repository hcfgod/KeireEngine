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

locked_source() {
  local name="${1:?dependency name is required}"
  local url="${2:?dependency URL is required}"
  local commit="${3:?dependency commit is required}"
  local cache_root="${XDG_CACHE_HOME:-$HOME/.cache}/keire/dependency-sources"
  local source_path="$cache_root/$name-$commit"
  local temporary_path="$cache_root/$name-$commit.tmp-$$"

  if [[ -d "$source_path/.git" ]]; then
    local actual
    actual="$(git -C "$source_path" rev-parse HEAD 2>/dev/null || true)"
    [[ "$actual" == "$commit" ]] || {
      printf 'Locked %s source cache is not the expected commit: %s\n' "$name" "$source_path" >&2
      exit 1
    }
    printf '%s\n' "$source_path"
    return
  fi

  mkdir -p "$cache_root"
  case "$temporary_path" in
    "$cache_root"/*) rm -rf "$temporary_path" ;;
    *) printf 'Refusing to replace a dependency source outside %s.\n' "$cache_root" >&2; exit 1 ;;
  esac
  if ! git clone --quiet --filter=blob:none --no-checkout "$url" "$temporary_path" ||
     ! git -C "$temporary_path" fetch --quiet --depth 1 origin "$commit" ||
     ! git -C "$temporary_path" checkout --quiet --detach "$commit"; then
    case "$temporary_path" in "$cache_root"/*) rm -rf "$temporary_path" ;; esac
    printf 'Could not prepare locked %s source at %s.\n' "$name" "$commit" >&2
    exit 1
  fi
  mv "$temporary_path" "$source_path"
  printf '%s\n' "$source_path"
}

jolt_source="$(locked_source jolt "$jolt_url" "$jolt_commit")"
recast_source="$(locked_source recast "$recast_url" "$recast_commit")"
miniaudio_source="$(locked_source miniaudio "$miniaudio_url" "$miniaudio_commit")"
if [[ "$toolset" == clang ]]; then export CC=clang CXX=clang++; else export CC=gcc CXX=g++; fi
compiler="$($CXX --version | head -n 1)"
bridge="$ROOT/Scripts/Dependencies/CMakeLists.txt"
if command -v sha256sum >/dev/null 2>&1; then bridge_hash="$(sha256sum "$bridge" | awk '{print $1}')"; else bridge_hash="$(shasum -a 256 "$bridge" | awk '{print $1}')"; fi
options=(-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
  -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF
  -DSDL_RENDER=OFF -DSDL_GPU=ON -DSDL_DUMMYVIDEO=ON -DSDL_OFFSCREEN=ON -DSDL_INSTALL=ON
  -DSDL_INSTALL_DOCS=OFF -DSDL_DEPS_SHARED=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_LIBDIR=lib
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
  -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF
  -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF -DASSIMP_BUILD_OBJ_IMPORTER=ON -DASSIMP_BUILD_FBX_IMPORTER=ON
  -DASSIMP_BUILD_GLTF_IMPORTER=ON -DASSIMP_NO_EXPORT=ON
  -DASSIMP_BUILD_ZLIB=ON -DASSIMP_BUILD_DRACO=OFF -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_INSTALL=ON
  -DASSIMP_INJECT_DEBUG_POSTFIX=OFF -DASSIMP_IGNORE_GIT_HASH=ON -DLIBRARY_SUFFIX=
  -DJPH_BUILD_SHARED_LIBS=OFF -DENABLE_INSTALL=ON -DOVERRIDE_CXX_FLAGS=OFF
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
  options+=("-DCMAKE_OSX_ARCHITECTURES=$cmake_architecture")
fi
key="$sdl_commit|$assimp_commit|$jolt_commit|$recast_commit|$miniaudio_commit|$architecture|$toolset|$compiler|$bridge_hash|${options[*]}"
base="$ROOT/Build/Dependencies/$system-$output_arch-$toolset"

for configuration in Debug Release; do
  build="$base/$configuration"; install="$build/install"; library="$install/lib/libSDL3.a"; assimp_library="$install/lib/libassimp.a"; zlib_library="$install/lib/libzlibstatic.a"; jolt_library="$install/lib/libJolt.a"; miniaudio_library="$install/lib/libminiaudio.a"; stamp="$build/keire-dependency.stamp"
  recast_suffix=""; [[ "$configuration" == Debug ]] && recast_suffix="-d"
  recast_library="$install/lib/libRecast$recast_suffix.a"
  detour_library="$install/lib/libDetour$recast_suffix.a"
  crowd_library="$install/lib/libDetourCrowd$recast_suffix.a"
  tile_cache_library="$install/lib/libDetourTileCache$recast_suffix.a"
  if [[ "$force" != 1 && -f "$library" && -f "$assimp_library" && -f "$zlib_library" &&
        -f "$jolt_library" && -f "$recast_library" && -f "$detour_library" &&
        -f "$crowd_library" && -f "$tile_cache_library" && -f "$miniaudio_library" &&
        -f "$stamp" && "$(tr -d '\r\n' < "$stamp")" == "$key|$configuration" ]]; then
    printf '==> Native %s dependency cache is current\n' "$configuration"
    continue
  fi
  [[ "$build" == "$base/Debug" || "$build" == "$base/Release" ]] || { printf 'Refusing to replace dependency cache outside %s.\n' "$base" >&2; exit 1; }
  rm -rf "$build"
  mkdir -p "$build"
  cmake -S "$ROOT/Scripts/Dependencies" -B "$build" -G Ninja -DKEIRE_SDL_SOURCE="$ROOT/Vendor/SDL" -DKEIRE_ASSIMP_SOURCE="$ROOT/Vendor/assimp" -DKEIRE_JOLT_SOURCE="$jolt_source" -DKEIRE_RECAST_SOURCE="$recast_source" -DKEIRE_MINIAUDIO_SOURCE="$miniaudio_source" -DCMAKE_BUILD_TYPE="$configuration" -DCMAKE_INSTALL_PREFIX="$install" "${options[@]}"
  cmake --build "$build" --target install --parallel
  [[ -f "$library" && -f "$assimp_library" && -f "$zlib_library" && -f "$jolt_library" &&
     -f "$recast_library" && -f "$detour_library" && -f "$crowd_library" &&
     -f "$tile_cache_library" && -f "$miniaudio_library" &&
     -f "$install/include/assimp/Importer.hpp" && -f "$install/include/SDL3/SDL.h" &&
     -f "$install/cmake/SDL3Config.cmake" ]] || { printf 'Native %s install is incomplete.\n' "$configuration" >&2; exit 1; }
  printf '%s\n' "$key|$configuration" > "$stamp"
done

bash "$SCRIPT_DIR/shader-compiler.sh" "$platform" "$architecture" "$toolset" "$force"

coral_metadata="$(bash "$SCRIPT_DIR/coral.sh" Debug 1 "$force")"
printf '%s\n' "$coral_metadata"
coral_source="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_SOURCE=/{print substr($0, index($0, "=") + 1)}')"
coral_commit="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_COMMIT=/{print $2}')"
coral_patch_digest="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_PATCH_DIGEST=/{print $2}')"
coral_nethost_library="$(printf '%s\n' "$coral_metadata" | awk -F= '/^CORAL_NETHOST_LIBRARY=/{print substr($0, index($0, "=") + 1)}')"
coral_release_metadata="$(bash "$SCRIPT_DIR/coral.sh" Release 1 "$force")"
printf '%s\n' "$coral_release_metadata"

bash "$ROOT/Scripts/Unix/ffmpeg.sh" Debug
bash "$ROOT/Scripts/Unix/ffmpeg.sh" Release

coral_link="$ROOT/Build/Dependencies/coral-patched"
nethost_link="$ROOT/Build/Dependencies/coral-nethost"
dotnet_sdk_link="$ROOT/Build/Dependencies/dotnet-sdk"
mkdir -p "$ROOT/Build/Dependencies"
ln -sfn "$coral_source" "$coral_link"
ln -sfn "$(dirname "$coral_nethost_library")" "$nethost_link"
dotnet_sdk_directory="$(dotnet --list-sdks | awk '$1 ~ /^10[.]/ { line=$0 } END { sub(/^.*\\[/, "", line); sub(/\\].*$/, "", line); print line }')"
ln -sfn "$(dirname "$dotnet_sdk_directory")" "$dotnet_sdk_link"
nethost_name="$(basename "$coral_nethost_library")"

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
platform_links='{ "dl", "m", "pthread" }'
if [[ "$platform" == Mac ]]; then
  platform_links='{ "Cocoa.framework", "CoreVideo.framework", "IOKit.framework", "CoreFoundation.framework", "CoreAudio.framework", "AudioToolbox.framework", "ForceFeedback.framework", "Carbon.framework", "Metal.framework", "QuartzCore.framework", "UniformTypeIdentifiers.framework" }'
fi
cat > "$ROOT/Build/Generated/Dependencies.lua" <<EOF
DependencyManifest = {
    SDLCommit = "$sdl_commit",
    JSONCommit = "$json_commit",
    AssimpCommit = "$assimp_commit",
    JoltCommit = "$jolt_commit",
    RecastCommit = "$recast_commit",
    MiniaudioCommit = "$miniaudio_commit",
    CoralCommit = "$coral_commit",
    CoralPatchDigest = "$coral_patch_digest",
    CoralSource = "../Build/Dependencies/coral-patched",
    CoralInclude = "../Build/Dependencies/coral-patched/Coral.Native/Include",
    CoralDebugLibrary = "../Build/Dependencies/coral-patched/Build/Debug/libCoral.Native.a",
    CoralReleaseLibrary = "../Build/Dependencies/coral-patched/Build/Release/libCoral.Native.a",
    CoralManagedDebug = "../Build/Dependencies/coral-patched/Build/Debug",
    CoralManagedRelease = "../Build/Dependencies/coral-patched/Build/Release",
    CoralNetHostLibrary = "../Build/Dependencies/coral-nethost/$nethost_name",
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
    SDL3PlatformLinks = $platform_links
}
EOF
printf '==> Dependency manifest generated\n'
