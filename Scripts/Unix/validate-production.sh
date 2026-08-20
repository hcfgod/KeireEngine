#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SKIP_SANITIZERS=0
INCLUDE_PACKAGE=0
INCLUDE_GRAPHICS_SMOKES=0
PERFORMANCE_SNAPSHOT=''
PERFORMANCE_HISTORY=''
PERFORMANCE_METADATA=''
PERFORMANCE_PROFILE='sandbox-vfx-reference'

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-sanitizers) SKIP_SANITIZERS=1; shift ;;
        --include-package) INCLUDE_PACKAGE=1; shift ;;
        --include-graphics-smokes) INCLUDE_GRAPHICS_SMOKES=1; shift ;;
        --performance-snapshot) PERFORMANCE_SNAPSHOT="$2"; shift 2 ;;
        --performance-history) PERFORMANCE_HISTORY="$2"; shift 2 ;;
        --performance-metadata) PERFORMANCE_METADATA="$2"; shift 2 ;;
        --performance-profile) PERFORMANCE_PROFILE="$2"; shift 2 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

run() {
    local label="$1"
    shift
    printf '==> %s\n' "$label"
    "$@"
}

run 'VFX parity manifest validation' python3 "$ROOT/Scripts/Vfx/validate_vfx_parity_manifest.py"
run 'VFX parity projection validation' python3 "$ROOT/Scripts/Vfx/reconcile_vfx_manifest.py" --check
run 'Generated VFX capability validation' python3 "$ROOT/Scripts/Vfx/generate_vfx_capabilities.py" --check
run 'VFX parity tooling tests' python3 "$ROOT/Scripts/Vfx/test_vfx_parity_tooling.py"
run 'Performance gate tooling tests' python3 "$ROOT/Scripts/Performance/test_validate_capture.py"
if [[ -n "$PERFORMANCE_SNAPSHOT" || -n "$PERFORMANCE_HISTORY" || -n "$PERFORMANCE_METADATA" ]]; then
    if [[ -z "$PERFORMANCE_SNAPSHOT" || -z "$PERFORMANCE_HISTORY" || -z "$PERFORMANCE_METADATA" ]]; then
        printf 'Performance snapshot, history, and metadata must be supplied together.\n' >&2
        exit 2
    fi
    run 'Reference-hardware performance gate' python3 "$ROOT/Scripts/Performance/validate_capture.py" \
        --snapshot "$PERFORMANCE_SNAPSHOT" --history "$PERFORMANCE_HISTORY" \
        --metadata "$PERFORMANCE_METADATA" --profile "$PERFORMANCE_PROFILE"
fi

run 'Debug test matrix' bash "$ROOT/Scripts/project.sh" test --generator ninja --configuration Debug --toolset clang
run 'Release test matrix' bash "$ROOT/Scripts/project.sh" test --generator ninja --configuration Release --toolset clang
if [[ "$SKIP_SANITIZERS" -eq 0 ]]; then
    run 'AddressSanitizer test matrix' \
        bash "$ROOT/Scripts/project.sh" test --generator ninja --configuration DebugASan --toolset clang
fi
run 'Unix regression harness' bash "$ROOT/Scripts/Tests/test-unix.sh"
if [[ "$INCLUDE_GRAPHICS_SMOKES" -eq 1 ]]; then
    run 'Release project-aware graphics smoke' \
        bash "$ROOT/Scripts/project.sh" run --generator ninja --configuration Release --toolset clang --smoke-project
fi
if [[ "$INCLUDE_PACKAGE" -eq 1 ]]; then
    run 'SDK package and consumer validation' \
        bash "$ROOT/Scripts/project.sh" package --generator ninja --configuration Release --toolset clang
fi

printf 'Production validation completed successfully.\n'
