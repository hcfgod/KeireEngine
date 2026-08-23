#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=xcode4; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; COMPILER_CACHE=auto; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
load_project_config "$ROOT"
TOOLSET="$(resolve_unix_toolset Mac "$TOOLSET")"
COMPILER_CACHE="$(resolve_compiler_cache "$GENERATOR" "$COMPILER_CACHE")"
validate_unix_combination Mac "$GENERATOR" "$TOOLSET"

bootstrap=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET")
[[ $UPDATE -eq 1 ]] && bootstrap+=(--update)
bash "$ROOT/Scripts/Mac/bootstrap.sh" "${bootstrap[@]}"
# Forced project generation must not invalidate native/codec caches. Use a forced bootstrap only when deliberately
# replacing pinned third-party outputs.
bash "$ROOT/Scripts/Unix/dependencies.sh" Mac "$ARCHITECTURE" "$TOOLSET" 0

args=("--file=premake5.lua" "--arch=$(premake_architecture "$ARCHITECTURE")" "--toolset=$TOOLSET")
[[ $CI -eq 1 ]] && args+=(--ci)
mkdir -p "$ROOT/Build/Generated"
printf '==> Generating %s files for %s with toolset %s\n' "$GENERATOR" "$ARCHITECTURE" "$TOOLSET"
if [[ "$GENERATOR" == compilecommands ]]; then
    (cd "$ROOT" && "$ROOT/Tools/Mac/premake5" "${args[@]}" ninja)
else
    (cd "$ROOT" && "$ROOT/Tools/Mac/premake5" "${args[@]}" "$GENERATOR")
fi
if [[ "$GENERATOR" == ninja || "$GENERATOR" == compilecommands ]]; then
    python3 "$ROOT/Scripts/patch-ninja-depfiles.py" "$ROOT"
    python3 "$ROOT/Scripts/patch-ninja-compiler-cache.py" "$ROOT" --cache "$COMPILER_CACHE"
fi
if [[ "$GENERATOR" == compilecommands ]]; then
    ninja -C "$ROOT" -f build.ninja -t compdb cxx_clang > "$ROOT/Build/Generated/compile_commands.all.json"
    python3 "$ROOT/Scripts/Unix/filter-compdb.py" "$ROOT/Build/Generated/compile_commands.all.json" "$ROOT/compile_commands.json"
fi
printf '%s|%s|%s|%s|%s|%s\n' "$GENERATOR" "$ARCHITECTURE" "$TOOLSET" "$COMPILER_CACHE" "$CI" "$(project_generation_fingerprint "$ROOT")" > "$ROOT/Build/Generated/$GENERATOR.stamp"
