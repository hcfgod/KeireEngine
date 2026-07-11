#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Client; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
validate_unix_combination Linux "$GENERATOR" "$TOOLSET"

bootstrap=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET")
[[ $UPDATE -eq 1 ]] && bootstrap+=(--update)
[[ $FORCE -eq 1 ]] && bootstrap+=(--force)
bash "$ROOT/Scripts/Linux/bootstrap.sh" "${bootstrap[@]}"

args=("--file=$ROOT/premake5.lua" "--arch=$(premake_architecture "$ARCHITECTURE")" "--toolset=$TOOLSET")
[[ $CI -eq 1 ]] && args+=(--ci)
mkdir -p "$ROOT/Build/Generated"
printf '==> Generating %s files for %s with toolset %s\n' "$GENERATOR" "$ARCHITECTURE" "$TOOLSET"
if [[ "$GENERATOR" == compilecommands ]]; then
    (cd "$ROOT" && "$ROOT/Tools/Linux/premake5" "${args[@]}" ninja)
    rule_toolset="$TOOLSET"; [[ "$rule_toolset" == default ]] && rule_toolset=gcc
    ninja -C "$ROOT" -f build.ninja -t compdb "cxx_$rule_toolset" > "$ROOT/Build/Generated/compile_commands.all.json"
    python3 "$ROOT/Scripts/Unix/filter-compdb.py" "$ROOT/Build/Generated/compile_commands.all.json" "$ROOT/compile_commands.json"
else
    (cd "$ROOT" && "$ROOT/Tools/Linux/premake5" "${args[@]}" "$GENERATOR")
fi
printf '%s|%s|%s|%s\n' "$GENERATOR" "$ARCHITECTURE" "$TOOLSET" "$CI" > "$ROOT/Build/Generated/$GENERATOR.stamp"
