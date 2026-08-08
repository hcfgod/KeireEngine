#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
load_project_config "$ROOT"
TOOLSET="$(resolve_unix_toolset Linux "$TOOLSET")"
validate_unix_combination Linux "$GENERATOR" "$TOOLSET"

bootstrap=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET")
[[ $UPDATE -eq 1 ]] && bootstrap+=(--update)
bash "$ROOT/Scripts/Linux/bootstrap.sh" "${bootstrap[@]}"
force_dependencies=0; [[ $FORCE -eq 1 ]] && force_dependencies=1
bash "$ROOT/Scripts/Unix/dependencies.sh" Linux "$ARCHITECTURE" "$TOOLSET" "$force_dependencies"

args=("--file=premake5.lua" "--arch=$(premake_architecture "$ARCHITECTURE")" "--toolset=$TOOLSET")
[[ $CI -eq 1 ]] && args+=(--ci)
mkdir -p "$ROOT/Build/Generated"
printf '==> Generating %s files for %s with toolset %s\n' "$GENERATOR" "$ARCHITECTURE" "$TOOLSET"
if [[ "$GENERATOR" == compilecommands ]]; then
    (cd "$ROOT" && "$ROOT/Tools/Linux/premake5" "${args[@]}" ninja)
else
    (cd "$ROOT" && "$ROOT/Tools/Linux/premake5" "${args[@]}" "$GENERATOR")
fi
if [[ "$GENERATOR" == ninja || "$GENERATOR" == compilecommands ]]; then
    python3 "$ROOT/Scripts/patch-ninja-depfiles.py" "$ROOT"
fi
if [[ "$GENERATOR" == compilecommands ]]; then
    rule_toolset="$TOOLSET"
    ninja -C "$ROOT" -f build.ninja -t compdb "cxx_$rule_toolset" > "$ROOT/Build/Generated/compile_commands.all.json"
    python3 "$ROOT/Scripts/Unix/filter-compdb.py" "$ROOT/Build/Generated/compile_commands.all.json" "$ROOT/compile_commands.json"
fi
printf '%s|%s|%s|%s|%s\n' "$GENERATOR" "$ARCHITECTURE" "$TOOLSET" "$CI" "$(project_generation_fingerprint "$ROOT")" > "$ROOT/Build/Generated/$GENERATOR.stamp"
