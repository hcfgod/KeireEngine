#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
load_project_config "$ROOT"
TOOLSET="$(resolve_unix_toolset Linux "$TOOLSET")"
[[ "$TARGET" == KeireClient ]] && TARGET="$CLIENT_TARGET"
[[ "$TARGET" == KeireHub ]] && TARGET="$HUB_TARGET"
[[ "$TARGET" == KeireTests ]] && TARGET="$TESTS_TARGET"
validate_unix_combination Linux "$GENERATOR" "$TOOLSET"
if [[ "$CONFIGURATION" == Coverage && ( "$GENERATOR" != ninja || "$TOOLSET" != clang ) ]]; then printf 'Coverage requires Ninja and Clang.\n' >&2; exit 1; fi

expected="$GENERATOR|$ARCHITECTURE|$TOOLSET|$CI|$(project_generation_fingerprint "$ROOT")"
stamp="$ROOT/Build/Generated/$GENERATOR.stamp"
generated=build.ninja; [[ "$GENERATOR" == gmake ]] && generated=Makefile
if [[ $FORCE -eq 1 || $UPDATE -eq 1 || ! -f "$ROOT/$generated" || ! -f "$stamp" || "$(tr -d '\r\n' < "$stamp")" != "$expected" ]]; then
    args=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET"); [[ $CI -eq 1 ]] && args+=(--ci)
    [[ $UPDATE -eq 1 ]] && args+=(--update)
    [[ $FORCE -eq 1 ]] && args+=(--force)
    bash "$ROOT/Scripts/Linux/generate.sh" "${args[@]}"
fi

case "$GENERATOR" in
    ninja) printf '==> Building %s %s for %s with Ninja\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; ninja -C "$ROOT" -f build.ninja "${TARGET}_${CONFIGURATION}" ;;
    gmake) printf '==> Building %s %s for %s with GNU Make\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; make -C "$ROOT" "config=$(printf '%s' "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')" "$TARGET" ;;
    *) printf "Unsupported build generator '%s'.\n" "$GENERATOR" >&2; exit 1 ;;
esac
bash "$ROOT/Scripts/Unix/stage-managed-host.sh" "$ROOT" "$CONFIGURATION" linux "$ARCHITECTURE" "$TARGET"
