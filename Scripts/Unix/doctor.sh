#!/usr/bin/env bash
set -euo pipefail
PLATFORM="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"; source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; [[ "$PLATFORM" == Mac ]] && GENERATOR=xcode4
CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Client; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"; load_project_config "$ROOT"; TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"
printf '%s environment\n  Generator:    %s\n  Architecture: %s\n  Toolset:      %s\n' "$PROJECT_DISPLAY_NAME" "$GENERATOR" "$ARCHITECTURE" "$TOOLSET"
for command in git cmake ninja make gmake gcc g++ clang clang++ llvm-profdata llvm-cov python3; do printf '  %-14s %s\n' "$command" "$(command -v "$command" 2>/dev/null || printf 'not found')"; done
for dependency in spdlog doctest; do if [[ -d "$ROOT/Vendor/$dependency" ]]; then commit="$(git -C "$ROOT/Vendor/$dependency" rev-parse HEAD)"; else commit='not initialized'; fi; printf '  %-14s %s\n' "$dependency" "$commit"; done
