#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Tests; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
args=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --target Tests); [[ $CI -eq 1 ]] && args+=(--ci)
[[ $UPDATE -eq 1 ]] && args+=(--update)
[[ $FORCE -eq 1 ]] && args+=(--force)
bash "$ROOT/Scripts/Linux/build.sh" "${args[@]}"
executable="$ROOT/Build/Bin/$CONFIGURATION-linux-$(architecture_output_name "$ARCHITECTURE")/Tests/Tests"
[[ -x "$executable" ]] || { printf 'Tests executable not found: %s\n' "$executable" >&2; exit 1; }
printf '==> Running Tests %s for %s\n' "$CONFIGURATION" "$ARCHITECTURE"
(cd "$ROOT" && "$executable")
