#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=xcode4; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
load_project_config "$ROOT"
TOOLSET="$(resolve_unix_toolset Mac "$TOOLSET")"
[[ "$TARGET" == KeireClient ]] && TARGET="$CLIENT_TARGET"
[[ "$TARGET" == KeireHub ]] && TARGET="$HUB_TARGET"
[[ "$TARGET" == KeireTests ]] && TARGET="$TESTS_TARGET"
validate_unix_combination Mac "$GENERATOR" "$TOOLSET"
if [[ "$CONFIGURATION" == Coverage && ( "$GENERATOR" != ninja || "$TOOLSET" != clang ) ]]; then printf 'Coverage requires Ninja and Clang.\n' >&2; exit 1; fi
expected="$GENERATOR|$ARCHITECTURE|$TOOLSET|$CI|$(project_generation_fingerprint "$ROOT")"; stamp="$ROOT/Build/Generated/$GENERATOR.stamp"

case "$GENERATOR" in
    xcode4) generated="$PROJECT_IDENTIFIER.xcworkspace" ;;
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

bash "$ROOT/Scripts/Unix/build-info.sh"
bash "$ROOT/Scripts/Unix/build-managed.sh"

case "$GENERATOR" in
    xcode4)
        printf '==> Building %s %s for %s with Xcode\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"
        if [[ -d "$ROOT/$PROJECT_IDENTIFIER.xcworkspace" ]]; then
            xcodebuild -workspace "$ROOT/$PROJECT_IDENTIFIER.xcworkspace" -scheme "$TARGET" -configuration "$CONFIGURATION"
        else
            xcodebuild -project "$ROOT/$PROJECT_IDENTIFIER.xcodeproj" -scheme "$TARGET" -configuration "$CONFIGURATION"
        fi
        ;;
    ninja) printf '==> Building %s %s for %s with Ninja\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; ninja -C "$ROOT" -f build.ninja "${TARGET}_${CONFIGURATION}" ;;
    gmake) printf '==> Building %s %s for %s with GNU Make\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; gmake -C "$ROOT" "config=$(printf '%s' "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')" "$TARGET" ;;
esac
bash "$ROOT/Scripts/Unix/stage-managed-host.sh" "$ROOT" "$CONFIGURATION" macosx "$ARCHITECTURE" "$TARGET"
if [[ "$TARGET" == "$HUB_TARGET" ]]; then
    output_architecture="$(architecture_output_name "$ARCHITECTURE")"
    dependency_configuration=Debug
    [[ "$CONFIGURATION" == Release || "$CONFIGURATION" == Dist ]] && dependency_configuration=Release
    sodium_runtime="$ROOT/Build/Dependencies/macosx-$output_architecture-$TOOLSET/$dependency_configuration/install/lib/libsodium.dylib"
    hub_directory="$ROOT/Build/Bin/$CONFIGURATION-macosx-$output_architecture/$TARGET"
    [[ -f "$sodium_runtime" ]] || {
        printf 'The pinned Hub signature verifier runtime is missing: %s\n' "$sodium_runtime" >&2
        exit 1
    }
    cp -f "$sodium_runtime" "$hub_directory/libsodium.dylib"
    printf '==> Staged pinned Hub signature verifier for %s\n' "$TARGET"
fi
