#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
source "$ROOT/Scripts/Unix/generated-content-cache.sh"
build_lock="$ROOT/Build/.locks/native-build.lock"
generated_content_acquire_lock "$build_lock" 7200 \
  '==> Another build is using this checkout; waiting for it to finish'
GENERATOR=xcode4; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; COMPILER_CACHE=auto; PROFILE_BUILD=0; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
build_started="$SECONDS"
build_succeeded=0
parse_build_arguments "$@"
load_project_config "$ROOT"
[[ "$CONFIGURATION" == Profile ]] && bash "$ROOT/Scripts/Unix/vendor.sh" --include-profile-dependencies
TOOLSET="$(resolve_unix_toolset Mac "$TOOLSET")"
COMPILER_CACHE="$(resolve_compiler_cache "$GENERATOR" "$COMPILER_CACHE")"
write_build_profile() {
    [[ "$PROFILE_BUILD" == 1 ]] || return 0
    local directory="$ROOT/Build/Reports/BuildProfiles"
    mkdir -p "$directory"
    printf '{\n  "schemaVersion": 1,\n  "generator": "%s",\n  "configuration": "%s",\n  "architecture": "%s",\n  "toolset": "%s",\n  "compilerCache": "%s",\n  "target": "%s",\n  "succeeded": %s,\n  "elapsedSeconds": %s\n}\n' \
      "$GENERATOR" "$CONFIGURATION" "$ARCHITECTURE" "$TOOLSET" "$COMPILER_CACHE" "$TARGET" \
      "$([[ "$build_succeeded" == 1 ]] && printf true || printf false)" "$((SECONDS - build_started))" \
      > "$directory/latest-$GENERATOR-$TARGET-$CONFIGURATION.json"
    printf '==> Build profile: %s\n' "$directory/latest-$GENERATOR-$TARGET-$CONFIGURATION.json"
}
trap 'status=$?; generated_content_release_lock "$build_lock"; write_build_profile; exit "$status"' EXIT
[[ "$TARGET" == KeireClient ]] && TARGET="$CLIENT_TARGET"
[[ "$TARGET" == KeireHub ]] && TARGET="$HUB_TARGET"
[[ "$TARGET" == KeireTests ]] && TARGET="$TESTS_TARGET"
validate_unix_combination Mac "$GENERATOR" "$TOOLSET"
if [[ "$CONFIGURATION" == Coverage && ( "$GENERATOR" != ninja || "$TOOLSET" != clang ) ]]; then printf 'Coverage requires Ninja and Clang.\n' >&2; exit 1; fi
expected="$GENERATOR|$ARCHITECTURE|$TOOLSET|$COMPILER_CACHE|$CI|$(project_generation_fingerprint "$ROOT")"; stamp="$ROOT/Build/Generated/$GENERATOR.stamp"

case "$GENERATOR" in
    xcode4) generated="$PROJECT_IDENTIFIER.xcworkspace" ;;
    ninja) generated=build.ninja ;;
    gmake) generated=Makefile ;;
    *) printf "Unsupported build generator '%s'.\n" "$GENERATOR" >&2; exit 1 ;;
esac
if [[ $FORCE -eq 1 || $UPDATE -eq 1 || ! -e "$ROOT/$generated" || ! -f "$stamp" || "$(tr -d '\r\n' < "$stamp")" != "$expected" ]]; then
    args=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --compiler-cache "$COMPILER_CACHE"); [[ $CI -eq 1 ]] && args+=(--ci)
    [[ $UPDATE -eq 1 ]] && args+=(--update)
    bash "$ROOT/Scripts/Mac/generate.sh" "${args[@]}"
fi

case "$GENERATOR" in
    xcode4)
        printf '==> Building %s %s for %s with Xcode\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"
        xcode_arguments=(-scheme "$TARGET" -configuration "$CONFIGURATION")
        [[ "$PROFILE_BUILD" == 1 ]] && xcode_arguments+=(-showBuildTimingSummary)
        if [[ -d "$ROOT/$PROJECT_IDENTIFIER.xcworkspace" ]]; then
            xcodebuild -workspace "$ROOT/$PROJECT_IDENTIFIER.xcworkspace" "${xcode_arguments[@]}"
        else
            xcodebuild -project "$ROOT/$PROJECT_IDENTIFIER.xcodeproj" "${xcode_arguments[@]}"
        fi
        ;;
    ninja)
        printf '==> Building %s %s for %s with Ninja\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"
        ninja_arguments=(-C "$ROOT" -f build.ninja)
        [[ "$PROFILE_BUILD" == 1 ]] && ninja_arguments+=(-d stats)
        ninja_arguments+=("${TARGET}_${CONFIGURATION}")
        # Refresh fingerprinted headers before Ninja examines dependencies; its prebuild stamp has no shader inputs.
        bash "$ROOT/Scripts/Unix/prepare-generated-content.sh"
        ninja "${ninja_arguments[@]}"
        ;;
    gmake) printf '==> Building %s %s for %s with GNU Make\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; gmake -C "$ROOT" "config=$(printf '%s' "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')" "$TARGET" ;;
esac
stage_unix_asset_worker_runtime "$ROOT" "$CONFIGURATION" macosx "$ARCHITECTURE" "$PROJECT_NAMESPACE" "$TARGET"
if [[ "$GENERATOR" == ninja ]]; then
    while IFS= read -r managed_host_target; do
        include_editor_api=false
        managed_host_includes_editor_api "$managed_host_target" "$CLIENT_TARGET" "$PROJECT_NAMESPACE" &&
          include_editor_api=true
        bash "$ROOT/Scripts/Unix/stage-managed-host.sh" "$ROOT" "$CONFIGURATION" macosx "$ARCHITECTURE" \
          "$managed_host_target" "$include_editor_api"
    done < <(managed_host_staging_targets "$TARGET" "$CLIENT_TARGET" "$HUB_TARGET" "$PROJECT_NAMESPACE")
fi
runtime_staging_target="$TARGET"
[[ "$TARGET" == "${PROJECT_NAMESPACE}EditorDev" ]] && runtime_staging_target="$CLIENT_TARGET"
if [[ "$GENERATOR" == ninja && ( "$runtime_staging_target" == "$HUB_TARGET" || "$runtime_staging_target" == "$CLIENT_TARGET" ) ]]; then
    output_architecture="$(architecture_output_name "$ARCHITECTURE")"
    dependency_configuration=Debug
    [[ "$CONFIGURATION" == Release || "$CONFIGURATION" == Profile || "$CONFIGURATION" == Dist ]] && dependency_configuration=Release
    sodium_runtime="$ROOT/Build/Dependencies/macosx-$output_architecture-$TOOLSET/$dependency_configuration/install/lib/libsodium.dylib"
    target_directory="$ROOT/Build/Bin/$CONFIGURATION-macosx-$output_architecture/$runtime_staging_target"
    [[ -f "$sodium_runtime" ]] || {
        printf 'The pinned marketplace signature verifier runtime is missing: %s\n' "$sodium_runtime" >&2
        exit 1
    }
    generated_content_copy_file_if_changed "$sodium_runtime" "$target_directory/libsodium.dylib" "$ROOT"
    printf '==> Staged pinned marketplace signature verifier for %s\n' "$runtime_staging_target"
fi
build_succeeded=1
