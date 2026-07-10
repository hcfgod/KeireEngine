#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GENERATOR="${1:-ninja}"
PREMAKE="$ROOT/Tools/Linux/premake5"

case "$GENERATOR" in
    ninja|gmake|compilecommands) ;;
    *)
        printf "Unsupported Linux generator '%s'.\n" "$GENERATOR" >&2
        exit 1
        ;;
esac

bash "$(dirname "${BASH_SOURCE[0]}")/bootstrap.sh" --generator "$GENERATOR"

printf "==> Generating project files with Premake action '%s'\n" "$GENERATOR"
if [[ "$GENERATOR" == "compilecommands" ]]; then
    "$PREMAKE" --file="$ROOT/premake5.lua" ninja
    ninja -C "$ROOT" -f build.ninja -t compdb > "$ROOT/compile_commands.json"
    printf 'Generated compile_commands.json\n'
else
    "$PREMAKE" --file="$ROOT/premake5.lua" "$GENERATOR"
fi
