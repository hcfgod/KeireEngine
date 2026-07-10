#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORKSPACE_NAME="CrossPlatformCoreClientTemplate"
GENERATOR="${1:-xcode4}"
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
    xcode4)
        if [[ ! -d "$ROOT/$WORKSPACE_NAME.xcworkspace" && ! -d "$ROOT/$WORKSPACE_NAME.xcodeproj" ]]; then
            bash "$(dirname "${BASH_SOURCE[0]}")/generate.sh" xcode4
        fi

        printf "==> Building %s %s with Xcode\n" "$TARGET" "$CONFIGURATION"
        if [[ -d "$ROOT/$WORKSPACE_NAME.xcworkspace" ]]; then
            xcodebuild -workspace "$ROOT/$WORKSPACE_NAME.xcworkspace" -scheme "$TARGET" -configuration "$CONFIGURATION"
        else
            xcodebuild -project "$ROOT/$WORKSPACE_NAME.xcodeproj" -scheme "$TARGET" -configuration "$CONFIGURATION"
        fi
        ;;
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
