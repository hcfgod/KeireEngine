#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_directory/generated-content-cache.sh"

source_directory="${1:?source directory is required}"
destination_directory="${2:?destination directory is required}"
pattern="${3:-*}"
found=0
while IFS= read -r -d '' source_file; do
  found=1
  generated_content_copy_file_if_changed "$source_file" "$destination_directory/$(basename "$source_file")"
done < <(find "$source_directory" -maxdepth 1 -type f -name "$pattern" -print0)
[[ "$found" == 1 ]] || {
  printf "No files matching '%s' were found beneath %s.\n" "$pattern" "$source_directory" >&2
  exit 1
}
