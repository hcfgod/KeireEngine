#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SKIP_SANITIZERS=0
INCLUDE_PACKAGE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-sanitizers) SKIP_SANITIZERS=1; shift ;;
        --include-package) INCLUDE_PACKAGE=1; shift ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

run() {
    local label="$1"
    shift
    printf '==> %s\n' "$label"
    "$@"
}

run 'Debug test matrix' bash "$ROOT/Scripts/project.sh" test --generator ninja --configuration Debug --toolset clang
run 'Release test matrix' bash "$ROOT/Scripts/project.sh" test --generator ninja --configuration Release --toolset clang
if [[ "$SKIP_SANITIZERS" -eq 0 ]]; then
    run 'AddressSanitizer test matrix' \
        bash "$ROOT/Scripts/project.sh" test --generator ninja --configuration DebugASan --toolset clang
fi
run 'Unix regression harness' bash "$ROOT/Scripts/Tests/test-unix.sh"
if [[ "$INCLUDE_PACKAGE" -eq 1 ]]; then
    run 'SDK package and consumer validation' \
        bash "$ROOT/Scripts/project.sh" package --generator ninja --configuration Release --toolset clang
fi

printf 'Production validation completed successfully.\n'
