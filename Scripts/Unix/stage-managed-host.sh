#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_directory/common.sh"

copy_file_if_changed() {
  local source="$1" destination="$2"
  if [[ -f "$destination" ]]; then
    if [[ ! "$source" -nt "$destination" && ! "$destination" -nt "$source" ]]; then
      return
    fi
    if cmp -s "$source" "$destination"; then
      touch -r "$source" "$destination"
      return
    fi
  fi
  mkdir -p "$(dirname "$destination")"
  cp -p "$source" "$destination"
}

copy_tree_if_changed() {
  local source_root="$1" destination_root="$2" source_file relative_path
  while IFS= read -r -d '' source_file; do
    relative_path="${source_file#"$source_root"/}"
    copy_file_if_changed "$source_file" "$destination_root/$relative_path"
  done < <(find "$source_root" -type f -print0)
}

root="${1:?repository root is required}"
configuration="${2:?configuration is required}"
system="${3:?system is required}"
architecture="${4:?architecture is required}"
target="${5:?target is required}"
include_editor_api="${6:-false}"

output_architecture="$(architecture_output_name "$architecture")"
target_directory="$root/Build/Bin/$configuration-$system-$output_architecture/$target"
[[ -d "$target_directory" ]] || exit 0

coral_configuration=Debug
[[ "$configuration" == Release || "$configuration" == Profile || "$configuration" == Dist ]] && coral_configuration=Release
coral_directory="$root/Build/Dependencies/coral/Build/$coral_configuration"
managed_directory="$target_directory/Managed"
dotnet_root="$root/Build/Dependencies/dotnet-sdk"
files=(Coral.Managed.dll Coral.Managed.deps.json Coral.Managed.runtimeconfig.json)
for file in "${files[@]}"; do
  [[ -f "$coral_directory/$file" ]] || {
    printf 'The Coral runtime output is missing: %s\n' "$file" >&2
    exit 1
  }
done
mkdir -p "$managed_directory"
for file in "${files[@]}"; do
  copy_file_if_changed "$coral_directory/$file" "$managed_directory/$file"
done
copy_file_if_changed "$root/Build/Managed/Keire.Managed.dll" "$managed_directory/Keire.Managed.dll"
if [[ "$include_editor_api" == true ]]; then
  copy_file_if_changed "$root/Build/Managed/Keire.Editor.Managed.dll" \
    "$managed_directory/Keire.Editor.Managed.dll"
  copy_file_if_changed "$root/Build/Managed/Keire.Managed.Generators.dll" \
    "$managed_directory/Keire.Managed.Generators.dll"
fi

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
copy_tree_if_changed "$hostfxr_directory" "$bundled_host"
copy_tree_if_changed "$core_runtime_directory" "$bundled_runtime"
for notice in LICENSE.txt ThirdPartyNotices.txt; do
  [[ -f "$dotnet_root/$notice" ]] && copy_file_if_changed "$dotnet_root/$notice" "$bundled_root/$notice"
done
