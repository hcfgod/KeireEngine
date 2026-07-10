#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GENERATOR="${1:-ninja}"
CONFIGURATION="${2:-Debug}"
TESTS_EXE="$ROOT/Build/Bin/$CONFIGURATION-linux-x86_64/Tests/Tests"

bash "$(dirname "${BASH_SOURCE[0]}")/build.sh" "$GENERATOR" "$CONFIGURATION" "Tests"

if [[ ! -x "$TESTS_EXE" ]]; then
    printf 'Tests executable was not found or is not executable: %s\n' "$TESTS_EXE" >&2
    exit 1
fi

printf '==> Running Tests %s\n' "$CONFIGURATION"
"$TESTS_EXE"
