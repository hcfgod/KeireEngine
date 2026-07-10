#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

safe_remove() {
    local path="$1"
    [[ -e "$path" ]] || return 0

    local resolved
    resolved="$(cd "$(dirname "$path")" && pwd)/$(basename "$path")"
    case "$resolved" in
        "$ROOT"/*)
            rm -rf "$resolved"
            printf 'Removed %s\n' "$resolved"
            ;;
        *)
            printf 'Refusing to remove path outside repository: %s\n' "$resolved" >&2
            exit 1
            ;;
    esac
}

safe_remove "$ROOT/Build/Bin"
safe_remove "$ROOT/Build/Intermediates"
safe_remove "$ROOT/Build/Generated"

find "$ROOT" -maxdepth 1 \( \
    -name '*.sln' -o \
    -name '*.slnx' -o \
    -name 'Makefile' -o \
    -name 'build.ninja' -o \
    -name 'compile_commands.json' -o \
    -name '*.xcodeproj' -o \
    -name '*.xcworkspace' \
\) -exec rm -rf {} +

find "$ROOT/Core" "$ROOT/Client" "$ROOT/Tests" -maxdepth 1 \( \
    -name '*.vcxproj' -o \
    -name '*.vcxproj.filters' -o \
    -name '*.vcxproj.user' -o \
    -name 'Makefile' -o \
    -name '*.make' -o \
    -name '*.ninja' -o \
    -name '*.xcodeproj' -o \
    -name '*.xcworkspace' \
\) -exec rm -rf {} +

printf '==> Clean complete\n'
