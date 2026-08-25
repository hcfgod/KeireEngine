#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/common.sh"

configuration="${1:-Debug}"
build="${2:-0}"
force="${3:-0}"
platform="${4:-}"
architecture="${5:-}"
toolset="${6:-}"
[[ "$configuration" == Debug || "$configuration" == Release ]] || {
  printf 'Coral configuration must be Debug or Release.\n' >&2
  exit 1
}
if [[ -z "$platform" ]]; then
  case "$(uname -s)" in
    Linux) platform=Linux ;;
    Darwin) platform=Mac ;;
    *) printf 'Coral is supported only on Linux and macOS.\n' >&2; exit 1 ;;
  esac
fi
case "$(uname -s)" in
  Linux) host_platform=Linux ;;
  Darwin) host_platform=Mac ;;
  *) printf 'Coral is supported only on Linux and macOS.\n' >&2; exit 1 ;;
esac
[[ "$platform" == "$host_platform" ]] || {
  printf 'Coral platform %s does not match this %s host.\n' "$platform" "$host_platform" >&2
  exit 1
}
architecture="$(normalize_architecture "${architecture:-$(native_architecture)}")"
if [[ "$platform" == Linux && "$architecture" != "$(native_architecture)" ]]; then
  printf 'Linux Coral cross-compilation is not configured for target %s on this host.\n' "$architecture" >&2
  exit 1
fi
if [[ -z "$toolset" ]]; then
  candidate_cxx="${CXX:-c++}"
  candidate_cxx_path="$(command -v "$candidate_cxx")" || {
    printf 'Coral C++ compiler is unavailable: %s.\n' "$candidate_cxx" >&2
    exit 1
  }
  candidate_cxx_version="$("$candidate_cxx_path" --version)" || exit 1
  candidate_cxx_version="$(printf '%s' "${candidate_cxx_version%%$'\n'*}" | tr '[:upper:]' '[:lower:]')"
  if [[ "$candidate_cxx_version" == *clang* ]]; then toolset=clang; else toolset=gcc; fi
fi
[[ "$toolset" == gcc || "$toolset" == clang ]] || {
  printf "Unsupported Coral toolset '%s'. Expected gcc or clang.\n" "$toolset" >&2
  exit 1
}

temporary_source=""
temporary_patched=""
coral_cache_lock_held=0
cleanup_coral_cache() {
  if [[ "$coral_cache_lock_held" -eq 1 ]]; then
    workspace_lock_release >&2 || true
    coral_cache_lock_held=0
  fi
  case "${temporary_source:-}" in
    "${source_root:-}/"*) rm -rf "$temporary_source" || true ;;
  esac
  case "${temporary_patched:-}" in
    "${build_root:-}/"*) rm -rf "$temporary_patched" || true ;;
  esac
}
trap cleanup_coral_cache EXIT

coral_url="$(config_value "$ROOT/Config/Dependencies.lock" CORAL_URL)"
coral_commit="$(config_value "$ROOT/Config/Dependencies.lock" CORAL_COMMIT)"
macos_deployment_target="$(config_value "$ROOT/Config/Dependencies.lock" MACOS_DEPLOYMENT_TARGET)"
dotnet_sdk_version="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_SDK_VERSION)"
patch_root="$ROOT/Patches/Coral"
patches=()
while IFS= read -r patch; do
  patches+=("$patch")
done < <(find "$patch_root" -maxdepth 1 -type f -name '*.patch' -print | sort)
((${#patches[@]} > 0)) || { printf 'The Kéire Coral patch set is empty.\n' >&2; exit 1; }
if command -v sha256sum >/dev/null 2>&1; then
  patch_digest="$({ for patch in "${patches[@]}"; do basename "$patch"; printf '\n'; cat "$patch"; done; } | sha256sum | awk '{print $1}')"
else
  patch_digest="$({ for patch in "${patches[@]}"; do basename "$patch"; printf '\n'; cat "$patch"; done; } | shasum -a 256 | awk '{print $1}')"
fi

source_root="${XDG_CACHE_HOME:-$HOME/.cache}/keire/dependency-sources"
source_path="$source_root/coral-$coral_commit"
if [[ -e "$source_path" || -L "$source_path" ]]; then
  locked_git_source_validate "$source_path" "$coral_commit" Coral
else
  mkdir -p "$source_root"
  workspace_lock_acquire "$source_root" "dependency-source-coral-$coral_commit" \
    ".locks/coral-$coral_commit.lock" >&2
  coral_cache_lock_held=1
  temporary_source="$source_root/coral-$coral_commit.tmp-$$"
  if [[ -e "$source_path" || -L "$source_path" ]]; then
    if ! locked_git_source_validate "$source_path" "$coral_commit" Coral; then
      workspace_lock_release >&2
      coral_cache_lock_held=0
      exit 1
    fi
  else
    case "$temporary_source" in
      "$source_root"/*) rm -rf "$temporary_source" ;;
      *) workspace_lock_release >&2; coral_cache_lock_held=0; exit 1 ;;
    esac
    if ! git clone --quiet --filter=blob:none --no-checkout "$coral_url" "$temporary_source" ||
       ! git -C "$temporary_source" fetch --quiet --depth 1 origin "$coral_commit" ||
       ! git -C "$temporary_source" checkout --quiet --detach "$coral_commit" ||
       ! locked_git_source_validate "$temporary_source" "$coral_commit" Coral ||
       ! mv "$temporary_source" "$source_path"; then
      case "$temporary_source" in "$source_root"/*) rm -rf "$temporary_source" ;; esac
      workspace_lock_release >&2
      coral_cache_lock_held=0
      printf 'Could not prepare locked Coral source at %s.\n' "$coral_commit" >&2
      exit 1
    fi
    temporary_source=""
  fi
  workspace_lock_release >&2
  coral_cache_lock_held=0
fi
locked_git_source_validate "$source_path" "$coral_commit" Coral

dotnet_path="$(command -v dotnet)" || {
  printf 'Coral requires the pinned .NET SDK %s.\n' "$dotnet_sdk_version" >&2
  exit 1
}
dotnet_root="$(pinned_dotnet_sdk_root "$dotnet_path" "$dotnet_sdk_version")" || {
  printf 'Coral requires the pinned .NET SDK %s from one canonical installation.\n' \
    "$dotnet_sdk_version" >&2
  exit 1
}
dotnet_executable="$dotnet_root/dotnet"

hash_coral_input_file() {
  local path="${1:?path is required}"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print $1}'
  else
    shasum -a 256 "$path" | awk '{print $1}'
  fi
}
hash_coral_input_text() {
  local value="${1-}"
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s' "$value" | sha256sum | awk '{print $1}'
  else
    printf '%s' "$value" | shasum -a 256 | awk '{print $1}'
  fi
}

sdk_major="${dotnet_sdk_version%%.*}"
bundled_versions="$dotnet_root/sdk/$dotnet_sdk_version/Microsoft.NETCoreSdk.BundledVersions.props"
[[ -f "$bundled_versions" && ! -L "$bundled_versions" ]] || {
  printf 'The pinned .NET SDK is missing Microsoft.NETCoreSdk.BundledVersions.props.\n' >&2
  exit 1
}
target_framework="net$sdk_major.0"
host_pack_metadata="$(dotnet_apphost_pack_metadata "$bundled_versions" "$target_framework")"
if [[ "$host_pack_metadata" != *$'\n'* ]]; then
  printf 'The pinned .NET SDK host-pack metadata is incomplete for %s.\n' "$target_framework" >&2
  exit 1
fi
host_pack_version="${host_pack_metadata%%$'\n'*}"
host_pack_rids="${host_pack_metadata#*$'\n'}"
[[ "$host_pack_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
  printf 'The pinned .NET SDK host-pack version is invalid: %s\n' "$host_pack_version" >&2
  exit 1
}
[[ -n "$host_pack_rids" && "$host_pack_rids" != *$'\n'* && "$host_pack_rids" != *$'\r'* ]] || {
  printf '%s\n' 'The pinned .NET SDK host-pack runtime identifiers are invalid.' >&2
  exit 1
}
host_architecture=x64
[[ "$architecture" == ARM64 ]] && host_architecture=arm64
if [[ "$platform" == Mac ]]; then
  nethost_rid="osx-$host_architecture"
else
  nethost_rid="linux-$host_architecture"
fi
case ";$host_pack_rids;" in
  *";$nethost_rid;"*) ;;
  *) printf 'The pinned .NET SDK host pack does not support %s.\n' "$nethost_rid" >&2; exit 1 ;;
esac
nethost_root="$dotnet_root/packs/Microsoft.NETCore.App.Host.$nethost_rid/$host_pack_version/runtimes/$nethost_rid/native"
nethost_library="$nethost_root/libnethost.a"
[[ -f "$nethost_library" && ! -L "$nethost_library" ]] || {
  printf 'The pinned .NET SDK host pack is missing nethost for %s %s.\n' \
    "$nethost_rid" "$host_pack_version" >&2
  exit 1
}
nethost_runtime=""
if [[ "$platform" == Mac ]]; then
  nethost_runtime="$nethost_root/libnethost.dylib"
  [[ -f "$nethost_runtime" && ! -L "$nethost_runtime" ]] || {
    printf 'The pinned .NET SDK host pack is missing the nethost runtime for %s %s.\n' \
      "$nethost_rid" "$host_pack_version" >&2
    exit 1
  }
fi
nethost_library="$(cd -P "$(dirname "$nethost_library")" && pwd -P)/$(basename "$nethost_library")"
nethost_library_hash="$(hash_coral_input_file "$nethost_library")"
nethost_runtime_identity=none
if [[ -n "$nethost_runtime" ]]; then
  nethost_runtime="$(cd -P "$(dirname "$nethost_runtime")" && pwd -P)/$(basename "$nethost_runtime")"
  nethost_runtime_identity="$nethost_runtime|$(hash_coral_input_file "$nethost_runtime")"
fi
nethost_identity="$nethost_rid|$host_pack_version|$nethost_library|$nethost_library_hash|$nethost_runtime_identity"

if [[ "$toolset" == clang ]]; then
  default_cc=clang
  default_cxx=clang++
else
  default_cc=gcc
  default_cxx=g++
fi
cc_path="$(command -v "${CC:-$default_cc}")" || {
  printf 'Coral C compiler is unavailable for toolset %s.\n' "$toolset" >&2
  exit 1
}
cxx_path="$(command -v "${CXX:-$default_cxx}")" || {
  printf 'Coral C++ compiler is unavailable for toolset %s.\n' "$toolset" >&2
  exit 1
}
cc_path="$(cd -P "$(dirname "$cc_path")" && pwd -P)/$(basename "$cc_path")"
cxx_path="$(cd -P "$(dirname "$cxx_path")" && pwd -P)/$(basename "$cxx_path")"
cc_version_output="$("$cc_path" --version)" || exit 1
cxx_version_output="$("$cxx_path" --version)" || exit 1
cc_version="${cc_version_output%%$'\n'*}"
cxx_version="${cxx_version_output%%$'\n'*}"
cc_version="${cc_version%$'\r'}"
cxx_version="${cxx_version%$'\r'}"
cxx_target_output="$("$cxx_path" -dumpmachine)" || exit 1
cxx_target="${cxx_target_output%%$'\n'*}"
cxx_target="${cxx_target%$'\r'}"
cxx_version_lower="$(printf '%s' "$cxx_version" | tr '[:upper:]' '[:lower:]')"
if [[ "$toolset" == clang && "$cxx_version_lower" != *clang* ]]; then
  printf 'Selected Coral C++ compiler does not identify as Clang: %s\n' "$cxx_version" >&2
  exit 1
fi
if [[ "$toolset" == gcc && "$cxx_version_lower" == *clang* ]]; then
  printf 'Selected Coral C++ compiler does not identify as GCC: %s\n' "$cxx_version" >&2
  exit 1
fi
export CC="$cc_path" CXX="$cxx_path"
compiler_identity="$cc_path|$cc_version|$cxx_path|$cxx_version|target:$cxx_target"
build_environment_descriptor="CFLAGS=${CFLAGS-}
CXXFLAGS=${CXXFLAGS-}
CPPFLAGS=${CPPFLAGS-}
LDFLAGS=${LDFLAGS-}"
compiler_identity="$compiler_identity|env:$(hash_coral_input_text "$build_environment_descriptor")"
macos_sdk_path=""
if [[ "$platform" == Mac ]]; then
  command -v xcrun >/dev/null 2>&1 || { printf 'Coral requires xcrun on macOS.\n' >&2; exit 1; }
  macos_sdk_path="$(xcrun --sdk macosx --show-sdk-path)" || exit 1
  macos_sdk_version="$(xcrun --sdk macosx --show-sdk-version)" || exit 1
  xcode_developer_root="$(xcode-select -p)" || exit 1
  compiler_identity="$compiler_identity|xcode:$xcode_developer_root|macos-sdk:$macos_sdk_version:$macos_sdk_path"
fi
workspace_key="$(workspace_identity "$ROOT")"
variant_key="$(coral_build_variant_key "$platform" "$architecture" "$toolset" "$compiler_identity" \
  "$dotnet_sdk_version" "$dotnet_root" "$macos_deployment_target" "$nethost_identity" "$workspace_key")"

build_root="${XDG_CACHE_HOME:-$HOME/.cache}/keire/dependency-builds"
cache_key="${coral_commit:0:12}-${patch_digest:0:16}-$variant_key"
patched="$build_root/coral-$cache_key"
stamp="$patched/keire-coral-patch.stamp"
expected_stamp="$coral_commit|$patch_digest|$variant_key|$nethost_rid|$host_pack_version"
mkdir -p "$build_root"
workspace_lock_acquire "$build_root" "coral-build-$cache_key" ".locks/coral-$cache_key.lock" >&2
coral_cache_lock_held=1
if [[ -L "$patched" || (-e "$patched" && ! -d "$patched") ]]; then
  printf 'Coral build cache is not an ordinary directory: %s\n' "$patched" >&2
  exit 1
fi
if [[ ! -f "$stamp" || "$(tr -d '\r\n' < "$stamp")" != "$expected_stamp" ]]; then
  temporary_patched="$build_root/coral-$cache_key.tmp-$$"
  case "$temporary_patched" in "$build_root"/*) rm -rf "$temporary_patched" ;; *) exit 1 ;; esac
  git clone --quiet --no-hardlinks --shared "$source_path" "$temporary_patched"
  for patch in "${patches[@]}"; do
    git -C "$temporary_patched" apply --whitespace=error-all "$patch"
  done
  printf '%s\n' "$expected_stamp" > "$temporary_patched/keire-coral-patch.stamp"
  if [[ -e "$patched" ]]; then
    case "$patched" in "$build_root"/*) rm -rf "$patched" ;; *) exit 1 ;; esac
  fi
  mv "$temporary_patched" "$patched"
  temporary_patched=""
  printf '==> Coral patch cache prepared at %s\n' "$patched"
else
  printf '==> Coral patch cache is current\n'
fi

if [[ "$build" == 1 ]]; then
  native_build="$patched/Build/$configuration"
  if [[ -L "$patched/Build" || (-e "$patched/Build" && ! -d "$patched/Build") ]]; then
    printf 'Coral native build parent is not an ordinary directory: %s\n' "$patched/Build" >&2
    exit 1
  fi
  if [[ ! -e "$patched/Build" ]] && ! mkdir "$patched/Build" 2>/dev/null && [[ ! -d "$patched/Build" ]]; then
    printf 'Could not create Coral native build parent: %s\n' "$patched/Build" >&2
    exit 1
  fi
  [[ -d "$patched/Build" && ! -L "$patched/Build" ]] || exit 1
  if [[ -L "$native_build" || (-e "$native_build" && ! -d "$native_build") ]]; then
    printf 'Coral native build path is not an ordinary directory: %s\n' "$native_build" >&2
    exit 1
  fi
  if [[ "$force" == 1 && -e "$native_build" ]]; then
    case "$native_build" in "$patched"/Build/*) rm -rf "$native_build" ;; *) exit 1 ;; esac
  fi
  if [[ ! -e "$native_build" ]] && ! mkdir "$native_build" 2>/dev/null && [[ ! -d "$native_build" ]]; then
    printf 'Could not create Coral native build path: %s\n' "$native_build" >&2
    exit 1
  fi
  [[ -d "$native_build" && ! -L "$native_build" ]] || exit 1
  cmake_options=(-DCMAKE_BUILD_TYPE="$configuration" -DCORAL_TESTING=OFF -DCORAL_EXAMPLE=OFF
    "-DDOTNET_EXE=$dotnet_executable")
  if [[ "$(uname -s)" == Darwin ]]; then
    cmake_architecture=x86_64
    [[ "$architecture" == ARM64 ]] && cmake_architecture=arm64
    cmake_options+=("-DCMAKE_OSX_ARCHITECTURES=$cmake_architecture"
      "-DCMAKE_OSX_SYSROOT=$macos_sdk_path"
      "-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_deployment_target")
  fi
  DOTNET_ROOT="$dotnet_root" PATH="$dotnet_root:$PATH" \
    cmake -S "$patched/cmake" -B "$native_build" -G Ninja "${cmake_options[@]}"
  DOTNET_ROOT="$dotnet_root" PATH="$dotnet_root:$PATH" \
    cmake --build "$native_build" --target Coral.Native --parallel "$(build_parallel_jobs)"
  [[ -f "$native_build/Coral.Managed.dll" ]] || {
    printf 'Patched Coral build did not produce Coral.Managed.dll.\n' >&2
    exit 1
  }
  printf '==> Patched Coral %s build is ready\n' "$configuration"
fi

workspace_lock_release >&2
coral_cache_lock_held=0

printf 'CORAL_SOURCE=%s\nCORAL_COMMIT=%s\nCORAL_PATCH_DIGEST=%s\nCORAL_BUILD_VARIANT=%s\n' \
  "$patched" "$coral_commit" "$patch_digest" "$variant_key"
printf 'CORAL_NETHOST_LIBRARY=%s\nCORAL_NETHOST_RUNTIME=%s\nCORAL_DOTNET_ROOT=%s\n' \
  "$nethost_library" "$nethost_runtime" "$dotnet_root"
trap - EXIT
