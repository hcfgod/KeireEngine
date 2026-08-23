#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
source "$ROOT/Scripts/Unix/generated-content-cache.sh"
build_lock="$ROOT/Build/.locks/native-build.lock"
generated_content_acquire_lock "$build_lock" 7200 \
  '==> Another build is using this checkout; waiting for it to finish'
trap 'generated_content_release_lock "$build_lock"' EXIT
export PATH="$ROOT/Tools/Linux:$PATH"
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
activate_linux_toolchain "$ROOT" "$TOOLSET"

bash "$ROOT/Scripts/Unix/build-info.sh"
bash "$ROOT/Scripts/Unix/build-managed.sh"

case "$GENERATOR" in
    ninja) printf '==> Building %s %s for %s with Ninja\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; ninja -j "$(build_parallel_jobs)" -C "$ROOT" -f build.ninja "${TARGET}_${CONFIGURATION}" ;;
    gmake) printf '==> Building %s %s for %s with GNU Make\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"; make -j "$(build_parallel_jobs)" -C "$ROOT" "config=$(printf '%s' "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')" "$TARGET" ;;
    *) printf "Unsupported build generator '%s'.\n" "$GENERATOR" >&2; exit 1 ;;
esac
while IFS= read -r managed_host_target; do
    bash "$ROOT/Scripts/Unix/stage-managed-host.sh" "$ROOT" "$CONFIGURATION" linux "$ARCHITECTURE" "$managed_host_target"
done < <(managed_host_staging_targets "$TARGET" "$CLIENT_TARGET" "$HUB_TARGET" "$PROJECT_NAMESPACE")
if [[ "$TARGET" == "$HUB_TARGET" || "$TARGET" == "$CLIENT_TARGET" ]]; then
    output_architecture="$(architecture_output_name "$ARCHITECTURE")"
    dependency_configuration=Debug
    [[ "$CONFIGURATION" == Release || "$CONFIGURATION" == Dist ]] && dependency_configuration=Release
    sodium_runtime="$ROOT/Build/Dependencies/linux-$output_architecture-$TOOLSET/$dependency_configuration/install/lib/libsodium.so"
    target_directory="$ROOT/Build/Bin/$CONFIGURATION-linux-$output_architecture/$TARGET"
    [[ -f "$sodium_runtime" ]] || {
        printf 'The pinned marketplace signature verifier runtime is missing: %s\n' "$sodium_runtime" >&2
        exit 1
    }
    generated_content_copy_file_if_changed "$sodium_runtime" "$target_directory/libsodium.so"
    printf '==> Staged pinned marketplace signature verifier for %s\n' "$TARGET"
fi
