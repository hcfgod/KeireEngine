#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/common.sh"

configuration="${1:-Debug}"
build="${2:-0}"
force="${3:-0}"
[[ "$configuration" == Debug || "$configuration" == Release ]] || {
  printf 'Coral configuration must be Debug or Release.\n' >&2
  exit 1
}

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
if [[ ! -d "$source_path/.git" ]]; then
  mkdir -p "$source_root"
  temporary_source="$source_root/coral-$coral_commit.tmp-$$"
  case "$temporary_source" in "$source_root"/*) rm -rf "$temporary_source" ;; *) exit 1 ;; esac
  trap 'case "${temporary_source:-}" in "$source_root"/*) rm -rf "$temporary_source" ;; esac' EXIT
  git clone --quiet --filter=blob:none --no-checkout "$coral_url" "$temporary_source"
  git -C "$temporary_source" fetch --quiet --depth 1 origin "$coral_commit"
  git -C "$temporary_source" checkout --quiet --detach "$coral_commit"
  mv "$temporary_source" "$source_path"
  temporary_source=""
  trap - EXIT
fi
[[ "$(git -C "$source_path" rev-parse HEAD)" == "$coral_commit" ]] || {
  printf 'Locked Coral source cache is not the expected commit: %s\n' "$source_path" >&2
  exit 1
}

build_root="${XDG_CACHE_HOME:-$HOME/.cache}/keire/dependency-builds"
cache_key="${coral_commit:0:12}-${patch_digest:0:16}-dotnet-$dotnet_sdk_version"
patched="$build_root/coral-$cache_key"
stamp="$patched/keire-coral-patch.stamp"
expected_stamp="$coral_commit|$patch_digest|dotnet-$dotnet_sdk_version"
if [[ ! -f "$stamp" || "$(tr -d '\r\n' < "$stamp")" != "$expected_stamp" ]]; then
  mkdir -p "$build_root"
  temporary_patched="$build_root/coral-$cache_key.tmp-$$"
  case "$temporary_patched" in "$build_root"/*) rm -rf "$temporary_patched" ;; *) exit 1 ;; esac
  trap 'case "${temporary_patched:-}" in "$build_root"/*) rm -rf "$temporary_patched" ;; esac' EXIT
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
  trap - EXIT
  printf '==> Coral patch cache prepared at %s\n' "$patched"
else
  printf '==> Coral patch cache is current\n'
fi

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

if [[ "$build" == 1 ]]; then
  native_build="$patched/Build/$configuration"
  if [[ "$force" == 1 && -e "$native_build" ]]; then
    case "$native_build" in "$patched"/Build/*) rm -rf "$native_build" ;; *) exit 1 ;; esac
  fi
  cmake_options=(-DCMAKE_BUILD_TYPE="$configuration" -DCORAL_TESTING=OFF -DCORAL_EXAMPLE=OFF
    "-DDOTNET_EXE=$dotnet_executable")
  if [[ "$(uname -s)" == Darwin ]]; then
    cmake_options+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_deployment_target")
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

nethost_library="$(find "$dotnet_root/packs" -type f -name libnethost.a -print | sort | tail -n 1)"
[[ -n "$nethost_library" ]] || { printf 'The .NET 10 SDK does not contain nethost.\n' >&2; exit 1; }
printf 'CORAL_SOURCE=%s\nCORAL_COMMIT=%s\nCORAL_PATCH_DIGEST=%s\nCORAL_NETHOST_LIBRARY=%s\n' \
  "$patched" "$coral_commit" "$patch_digest" "$nethost_library"
