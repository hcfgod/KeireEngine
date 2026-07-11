#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=xcode4; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Client; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
validate_unix_combination Mac "$GENERATOR" "$TOOLSET"
expected="$GENERATOR|$ARCHITECTURE|$TOOLSET|$CI"; stamp="$ROOT/Build/Generated/$GENERATOR.stamp"

case "$GENERATOR" in
    xcode4) generated="CrossPlatformCoreClientTemplate.xcworkspace" ;;
    ninja) generated=build.ninja ;;
    gmake) generated=Makefile ;;
    *) printf "Unsupported build generator '%s'.\n" "$GENERATOR" >&2; exit 1 ;;
esac
if [[ $FORCE -eq 1 || $UPDATE -eq 1 || ! -e "$ROOT/$generated" || ! -f "$stamp" || "$(tr -d '\r\n' < "$stamp")" != "$expected" ]]; then
    args=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET"); [[ $CI -eq 1 ]] && args+=(--ci)
    [[ $UPDATE -eq 1 ]] && args+=(--update)
    [[ $FORCE -eq 1 ]] && args+=(--force)
    bash "$ROOT/Scripts/Mac/generate.sh" "${args[@]}"
fi

case "$GENERATOR" in
    xcode4)
        printf '==> Building %s %s for %s with Xcode\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"
        if [[ -d "$ROOT/CrossPlatformCoreClientTemplate.xcworkspace" ]]; then
            xcodebuild -workspace "$ROOT/CrossPlatformCoreClientTemplate.xcworkspace" -scheme "$TARGET" -configuration "$CONFIGURATION"
        else
            xcodebuild -project "$ROOT/CrossPlatformCoreClientTemplate.xcodeproj" -scheme "$TARGET" -configuration "$CONFIGURATION"
        fi
        ;;
    ninja) printf '==> Building %s %s for %s with Ninja\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; ninja -C "$ROOT" -f build.ninja "${TARGET}_${CONFIGURATION}" ;;
    gmake) printf '==> Building %s %s for %s with GNU Make\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; gmake -C "$ROOT" "config=$(printf '%s' "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')" "$TARGET" ;;
esac
