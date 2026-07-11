#!/usr/bin/env bash
set -euo pipefail
PLATFORM="$1"; MODE="$2"; shift 2
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; [[ "$PLATFORM" == Mac ]] && GENERATOR=xcode4
CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Client; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"; load_project_config "$ROOT"; TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"
if [[ "$MODE" == test ]]; then TARGET="$TESTS_TARGET"; else TARGET="$CLIENT_TARGET"; fi
args=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --target "$TARGET")
[[ $CI -eq 1 ]] && args+=(--ci); [[ $UPDATE -eq 1 ]] && args+=(--update); [[ $FORCE -eq 1 ]] && args+=(--force)
bash "$ROOT/Scripts/$PLATFORM/build.sh" "${args[@]}"
system=linux; [[ "$PLATFORM" == Mac ]] && system=macosx
executable="$ROOT/Build/Bin/$CONFIGURATION-$system-$(architecture_output_name "$ARCHITECTURE")/$TARGET/$TARGET"
[[ -x "$executable" ]] || { printf 'Executable not found: %s\n' "$executable" >&2; exit 1; }
printf '==> Running %s %s for %s\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"
(cd "$ROOT" && "$executable")
