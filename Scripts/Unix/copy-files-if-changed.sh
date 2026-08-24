#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_directory/generated-content-cache.sh"
repository_root="$(cd "$script_directory/../.." && pwd)"

source_directory="${1:?source directory is required}"
destination_directory="${2:?destination directory is required}"
pattern="${3:-*}"
destination_containment_root="${4:-$repository_root}"
case "$source_directory" in /*) ;; *) source_directory="$repository_root/${source_directory#./}" ;; esac
case "$destination_directory" in /*) ;; *) destination_directory="$repository_root/${destination_directory#./}" ;; esac
case "$destination_containment_root" in
  /*) ;;
  *) destination_containment_root="$repository_root/${destination_containment_root#./}" ;;
esac
resolved_source_directory="$(cd -P "$source_directory" && pwd -P)"

resolve_source_file() {
  local candidate="${1:?source file is required}" directory target hops=0
  while [[ -L "$candidate" ]]; do
    hops=$((hops + 1))
    [[ $hops -le 40 ]] || {
      printf 'Source symbolic-link chain is too deep: %s\n' "$1" >&2
      return 1
    }
    directory="$(cd -P "$(dirname "$candidate")" && pwd -P)" || return 1
    target="$(readlink "$candidate")" || return 1
    case "$target" in
      /*) candidate="$target" ;;
      *) candidate="$directory/$target" ;;
    esac
  done
  [[ -f "$candidate" ]] || return 1
  directory="$(cd -P "$(dirname "$candidate")" && pwd -P)" || return 1
  printf '%s/%s\n' "$directory" "$(basename "$candidate")"
}

source_files=(sentinel)
copy_sources=(sentinel)
while IFS= read -r -d '' source_file; do
  copy_source="$source_file"
  if [[ -L "$source_file" ]]; then
    copy_source="$(resolve_source_file "$source_file")" || {
      printf 'Could not resolve source symbolic link: %s\n' "$source_file" >&2
      exit 1
    }
    case "$copy_source" in
      "$resolved_source_directory"/*) ;;
      *) printf 'Source symbolic link escapes its directory: %s\n' "$source_file" >&2; exit 1 ;;
    esac
  fi
  source_files+=("$source_file")
  copy_sources+=("$copy_source")
done < <(find "$source_directory" -maxdepth 1 \( -type f -o -type l \) -name "$pattern" -print0)
[[ ${#source_files[@]} -gt 1 ]] || {
  printf "No files matching '%s' were found beneath %s.\n" "$pattern" "$source_directory" >&2
  exit 1
}
for ((index = 1; index < ${#source_files[@]}; ++index)); do
  generated_content_copy_file_if_changed "${copy_sources[index]}" \
    "$destination_directory/$(basename "${source_files[index]}")" "$destination_containment_root"
done
