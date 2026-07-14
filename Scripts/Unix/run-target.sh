#!/usr/bin/env bash
set -euo pipefail
PLATFORM="$1"; MODE="$2"; shift 2
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; [[ "$PLATFORM" == Mac ]] && GENERATOR=xcode4
CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"; load_project_config "$ROOT"; TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"
if [[ "$MODE" == test ]]; then TARGET="$TESTS_TARGET"; else TARGET="$CLIENT_TARGET"; fi
args=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --target "$TARGET")
[[ $CI -eq 1 ]] && args+=(--ci); [[ $UPDATE -eq 1 ]] && args+=(--update); [[ $FORCE -eq 1 ]] && args+=(--force)
bash "$ROOT/Scripts/$PLATFORM/build.sh" "${args[@]}"
system=linux; [[ "$PLATFORM" == Mac ]] && system=macosx
executable="$ROOT/Build/Bin/$CONFIGURATION-$system-$(architecture_output_name "$ARCHITECTURE")/$TARGET/$TARGET"
[[ -x "$executable" ]] || { printf 'Executable not found: %s\n' "$executable" >&2; exit 1; }
printf '==> Running %s %s for %s\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"
if [[ "$MODE" == run && ($CI -eq 1 || "${KEIRE_SMOKE_WINDOW:-0}" == 1) ]]; then
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy "$executable" --smoke-window)
else
    (cd "$ROOT" && "$executable")
fi
if [[ "$MODE" == test && "$CONFIGURATION" =~ ^Debug ]]; then
    probe_output="$(mktemp)"
    set +e
    (cd "$ROOT" && "$executable" --core-assert-probe) 2>"$probe_output"
    probe_status=$?
    set -e
    [[ $probe_status -ne 0 ]] || { printf 'Assertion probe unexpectedly succeeded.\n' >&2; rm -f "$probe_output"; exit 1; }
    grep -q 'Assertion failed: false' "$probe_output"
    grep -q 'assertion probe' "$probe_output"
    rm -f "$probe_output"
fi
if [[ "$MODE" == run ]]; then
    cli_root="$(mktemp -d)"
    for option in --help -h --version -v; do (cd "$cli_root" && "$executable" "$option" >/dev/null); done
    set +e
    (cd "$cli_root" && "$executable" --invalid >/dev/null 2>&1)
    invalid_status=$?
    set -e
    [[ $invalid_status -eq 2 ]] || { printf 'KeireClient invalid option returned %s, expected 2.\n' "$invalid_status" >&2; rm -rf "$cli_root"; exit 1; }
    [[ ! -d "$cli_root/Logs" ]] || { printf 'Informational KeireClient commands created logs.\n' >&2; rm -rf "$cli_root"; exit 1; }
    rm -rf "$cli_root"
fi
