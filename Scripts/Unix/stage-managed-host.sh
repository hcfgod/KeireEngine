#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_directory/common.sh"

root="${1:?repository root is required}"
configuration="${2:?configuration is required}"
system="${3:?system is required}"
architecture="${4:?architecture is required}"
target="${5:?target is required}"

output_architecture="$(architecture_output_name "$architecture")"
target_directory="$root/Build/Bin/$configuration-$system-$output_architecture/$target"
[[ -d "$target_directory" ]] || exit 0

coral_configuration=Debug
[[ "$configuration" == Release || "$configuration" == Dist ]] && coral_configuration=Release
coral_directory="$root/Build/Dependencies/coral-patched/Build/$coral_configuration"
managed_directory="$target_directory/Managed"
dotnet_root="$root/Build/Dependencies/dotnet-sdk"
files=(Coral.Managed.dll Coral.Managed.deps.json Coral.Managed.runtimeconfig.json)
for file in "${files[@]}"; do
  [[ -f "$coral_directory/$file" ]] || {
    printf 'The patched Coral runtime output is missing: %s\n' "$file" >&2
    exit 1
  }
done
mkdir -p "$managed_directory"
for file in "${files[@]}"; do cp -f "$coral_directory/$file" "$managed_directory/$file"; done
cp -f "$root/Build/Managed/Keire.Managed.dll" "$managed_directory/Keire.Managed.dll"

hostfxr_directory="$(find "$dotnet_root/host/fxr" -mindepth 1 -maxdepth 1 -type d -print | sort | tail -n 1)"
core_runtime_directory="$(
  find "$dotnet_root/shared/Microsoft.NETCore.App" -mindepth 1 -maxdepth 1 -type d -print | sort | tail -n 1
)"
[[ -n "$hostfxr_directory" && -n "$core_runtime_directory" ]] || {
  printf 'The bundled .NET hostfxr or CoreCLR runtime is missing.\n' >&2
  exit 1
}
bundled_root="$managed_directory/Dotnet"
bundled_host="$bundled_root/host/fxr/$(basename "$hostfxr_directory")"
bundled_runtime="$bundled_root/shared/Microsoft.NETCore.App/$(basename "$core_runtime_directory")"
mkdir -p "$bundled_host" "$bundled_runtime"
cp -Rf "$hostfxr_directory/." "$bundled_host/"
cp -Rf "$core_runtime_directory/." "$bundled_runtime/"
for notice in LICENSE.txt ThirdPartyNotices.txt; do
  [[ -f "$dotnet_root/$notice" ]] && cp -f "$dotnet_root/$notice" "$bundled_root/$notice"
done
