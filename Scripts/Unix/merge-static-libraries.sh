#!/usr/bin/env bash
set -euo pipefail

[[ $# -ge 2 ]] || { printf 'Usage: %s <output> <input>...\n' "$0" >&2; exit 2; }
output="$1"
shift
for input in "$@"; do
  [[ -f "$input" ]] || { printf 'Static library input does not exist: %s\n' "$input" >&2; exit 1; }
done

mkdir -p "$(dirname "$output")"
temporary_directory="$(mktemp -d /tmp/keire-static-merge.XXXXXX)"
temporary="$temporary_directory/merged.a"
trap 'rm -rf "$temporary_directory"' EXIT
if [[ "$(uname -s)" == Darwin ]]; then
  xcrun libtool -static -o "$temporary" "$@"
else
  ar_command="${AR:-ar}"
  archive_index=0
  archive_inputs=()
  for input in "$@"; do
    input_path="$(cd "$(dirname "$input")" && pwd -P)/$(basename "$input")"
    archive_path="$temporary_directory/input-$archive_index.a"
    ln -s "$input_path" "$archive_path"
    archive_inputs+=("$archive_path")
    archive_index=$((archive_index + 1))
  done
  {
    printf 'create %s\n' "$temporary"
    for input in "${archive_inputs[@]}"; do printf 'addlib %s\n' "$input"; done
    printf 'save\nend\n'
  } | "$ar_command" -M
fi
mv -f "$temporary" "$output"
trap - EXIT
rm -rf "$temporary_directory"
