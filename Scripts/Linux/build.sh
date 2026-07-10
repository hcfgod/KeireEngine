#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GENERATOR="${1:-ninja}"
CONFIGURATION="${2:-Debug}"
TARGET="${3:-Client}"

normalize_configuration() {
    case "$1" in
        Debug|debug) printf 'Debug' ;;
        Release|release) printf 'Release' ;;
        Dist|dist) printf 'Dist' ;;
        DebugASan|debugasan) printf 'DebugASan' ;;
        DebugUBSan|debugubsan) printf 'DebugUBSan' ;;
        DebugTSan|debugtsan) printf 'DebugTSan' ;;
        *)
            printf "Unsupported configuration '%s'. Expected Debug, Release, Dist, DebugASan, DebugUBSan, or DebugTSan.\n" "$1" >&2
            return 1
            ;;
    esac
}

CONFIGURATION="$(normalize_configuration "$CONFIGURATION")"

generate_if_needed() {
    local expected="$1"
    if [[ ! -e "$ROOT/$expected" ]]; then
        bash "$(dirname "${BASH_SOURCE[0]}")/generate.sh" "$GENERATOR"
    fi
}

case "$GENERATOR" in
    ninja)
        generate_if_needed "build.ninja"
        printf "==> Building %s %s with Ninja\n" "$TARGET" "$CONFIGURATION"
        ninja -C "$ROOT" -f build.ninja "${TARGET}_${CONFIGURATION}"
        ;;
    gmake)
        generate_if_needed "Makefile"
        printf "==> Building %s %s with GNU Make\n" "$TARGET" "$CONFIGURATION"
        make -C "$ROOT" "config=$(printf '%s' "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')" "$TARGET"
        ;;
    *)
        printf "Unsupported build generator '%s'.\n" "$GENERATOR" >&2
        exit 1
        ;;
esac
