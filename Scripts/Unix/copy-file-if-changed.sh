#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/generated-content-cache.sh"

generated_content_copy_file_if_changed "${1:?source file is required}" "${2:?destination file is required}"
