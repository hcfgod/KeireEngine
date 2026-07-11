#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=xcode4; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Client; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
args=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --target Client); [[ $CI -eq 1 ]] && args+=(--ci)
[[ $UPDATE -eq 1 ]] && args+=(--update)
[[ $FORCE -eq 1 ]] && args+=(--force)
bash "$ROOT/Scripts/Mac/build.sh" "${args[@]}"
executable="$ROOT/Build/Bin/$CONFIGURATION-macosx-$(architecture_output_name "$ARCHITECTURE")/Client/Client"
[[ -x "$executable" ]] || { printf 'Client executable not found: %s\n' "$executable" >&2; exit 1; }
(cd "$ROOT" && "$executable")
