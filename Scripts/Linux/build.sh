#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
source "$ROOT/Scripts/Unix/generated-content-cache.sh"
build_lock="$ROOT/Build/.locks/native-build.lock"
generated_content_acquire_lock "$build_lock" 7200 \
  '==> Another build is using this checkout; waiting for it to finish'
export PATH="$ROOT/Tools/Linux:$PATH"
GENERATOR=ninja; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; COMPILER_CACHE=auto; PROFILE_BUILD=0; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
build_started="$SECONDS"
build_succeeded=0
parse_build_arguments "$@"
load_project_config "$ROOT"
[[ "$CONFIGURATION" == Profile ]] && bash "$ROOT/Scripts/Unix/vendor.sh" --include-profile-dependencies
TOOLSET="$(resolve_unix_toolset Linux "$TOOLSET")"
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
validate_unix_combination Linux "$GENERATOR" "$TOOLSET"
if [[ "$CONFIGURATION" == Coverage && ( "$GENERATOR" != ninja || "$TOOLSET" != clang ) ]]; then printf 'Coverage requires Ninja and Clang.\n' >&2; exit 1; fi

expected="$GENERATOR|$ARCHITECTURE|$TOOLSET|$COMPILER_CACHE|$CI|$(project_generation_fingerprint "$ROOT")"
stamp="$ROOT/Build/Generated/$GENERATOR.stamp"
generated=build.ninja; [[ "$GENERATOR" == gmake ]] && generated=Makefile
if [[ $FORCE -eq 1 || $UPDATE -eq 1 || ! -f "$ROOT/$generated" || ! -f "$stamp" || "$(tr -d '\r\n' < "$stamp")" != "$expected" ]]; then
    args=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --compiler-cache "$COMPILER_CACHE"); [[ $CI -eq 1 ]] && args+=(--ci)
    [[ $UPDATE -eq 1 ]] && args+=(--update)
    bash "$ROOT/Scripts/Linux/generate.sh" "${args[@]}"
fi
activate_linux_toolchain "$ROOT" "$TOOLSET"

# Refresh fingerprinted headers before Ninja examines dependencies; its prebuild stamp has no shader inputs.
if [[ "$GENERATOR" == ninja ]]; then
    bash "$ROOT/Scripts/Unix/prepare-generated-content.sh"
fi

case "$GENERATOR" in
    ninja) printf '==> Building %s %s for %s with Ninja\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; ninja_profile=(); [[ "$PROFILE_BUILD" == 1 ]] && ninja_profile=(-d stats); ninja -j "$(build_parallel_jobs)" -C "$ROOT" -f build.ninja "${ninja_profile[@]}" "${TARGET}_${CONFIGURATION}" ;;
    gmake) printf '==> Building %s %s for %s with GNU Make\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; make -j "$(build_parallel_jobs)" -C "$ROOT" "config=$(printf '%s' "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')" "$TARGET" ;;
    *) printf "Unsupported build generator '%s'.\n" "$GENERATOR" >&2; exit 1 ;;
esac
stage_unix_asset_worker_runtime "$ROOT" "$CONFIGURATION" linux "$ARCHITECTURE" "$PROJECT_NAMESPACE" "$TARGET"
if [[ "$GENERATOR" == ninja ]]; then
    while IFS= read -r managed_host_target; do
        include_editor_api=false
        managed_host_includes_editor_api "$managed_host_target" "$CLIENT_TARGET" "$PROJECT_NAMESPACE" &&
          include_editor_api=true
        bash "$ROOT/Scripts/Unix/stage-managed-host.sh" "$ROOT" "$CONFIGURATION" linux "$ARCHITECTURE" \
          "$managed_host_target" "$include_editor_api"
    done < <(managed_host_staging_targets "$TARGET" "$CLIENT_TARGET" "$HUB_TARGET" "$PROJECT_NAMESPACE")
fi
runtime_staging_target="$TARGET"
[[ "$TARGET" == "${PROJECT_NAMESPACE}EditorDev" ]] && runtime_staging_target="$CLIENT_TARGET"
if [[ "$GENERATOR" == ninja && ( "$runtime_staging_target" == "$HUB_TARGET" || "$runtime_staging_target" == "$CLIENT_TARGET" ) ]]; then
    output_architecture="$(architecture_output_name "$ARCHITECTURE")"
    dependency_configuration=Debug
    [[ "$CONFIGURATION" == Release || "$CONFIGURATION" == Profile || "$CONFIGURATION" == Dist ]] && dependency_configuration=Release
    sodium_runtime="$ROOT/Build/Dependencies/linux-$output_architecture-$TOOLSET/$dependency_configuration/install/lib/libsodium.so"
    target_directory="$ROOT/Build/Bin/$CONFIGURATION-linux-$output_architecture/$runtime_staging_target"
    [[ -f "$sodium_runtime" ]] || {
        printf 'The pinned marketplace signature verifier runtime is missing: %s\n' "$sodium_runtime" >&2
        exit 1
    }
    generated_content_copy_file_if_changed "$sodium_runtime" "$target_directory/libsodium.so" "$ROOT"
    printf '==> Staged pinned marketplace signature verifier for %s\n' "$runtime_staging_target"
fi
build_succeeded=1
