#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/generated-content-cache.sh"

source_path="${1:?source file is required}"
destination_path="${2:?destination file is required}"
case "$source_path" in /*) ;; *) source_path="$ROOT/${source_path#./}" ;; esac
case "$destination_path" in /*) ;; *) destination_path="$ROOT/${destination_path#./}" ;; esac
generated_content_copy_file_if_changed "$source_path" "$destination_path" "$ROOT"
