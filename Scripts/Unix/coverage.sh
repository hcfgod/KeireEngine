#!/usr/bin/env bash
set -euo pipefail
PLATFORM="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"; source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Coverage; ARCHITECTURE="$(native_architecture)"; TOOLSET=clang; TARGET=Tests; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"; load_project_config "$ROOT"; system=linux; [[ "$PLATFORM" == Mac ]] && system=macosx
output_arch="$(architecture_output_name "$ARCHITECTURE")"; directory="$ROOT/Build/Coverage/$system-$output_arch"; mkdir -p "$directory"; rm -f "$directory"/*.profraw
args=(--generator ninja --configuration Coverage --architecture "$ARCHITECTURE" --toolset clang --target "$TESTS_TARGET"); [[ $CI -eq 1 ]] && args+=(--ci)
test_args=("${args[@]}"); [[ $UPDATE -eq 1 ]] && test_args+=(--update); [[ $FORCE -eq 1 ]] && test_args+=(--force)
bash "$ROOT/Scripts/$PLATFORM/build.sh" "${test_args[@]}"
tests="$ROOT/Build/Bin/Coverage-$system-$output_arch/$TESTS_TARGET/$TESTS_TARGET"; LLVM_PROFILE_FILE="$directory/%p.profraw" "$tests"
args[9]="$CLIENT_TARGET"; bash "$ROOT/Scripts/$PLATFORM/build.sh" "${args[@]}"
client="$ROOT/Build/Bin/Coverage-$system-$output_arch/$CLIENT_TARGET/$CLIENT_TARGET"; LLVM_PROFILE_FILE="$directory/%p.profraw" "$client"
if [[ "$PLATFORM" == Mac ]]; then
  profdata=(xcrun llvm-profdata); cov=(xcrun llvm-cov)
else
  profdata=("$(resolve_llvm_tool llvm-profdata)"); cov=("$(resolve_llvm_tool llvm-cov)")
fi
profiles=("$directory"/*.profraw); [[ -e "${profiles[0]}" ]] || { printf 'No coverage profiles produced.\n' >&2; exit 1; }
"${profdata[@]}" merge -sparse "${profiles[@]}" -o "$directory/coverage.profdata"
common=(-instr-profile="$directory/coverage.profdata" "$tests" -object="$client" -ignore-filename-regex='Vendor|Tests')
"${cov[@]}" export -format=lcov "${common[@]}" > "$directory/coverage.info"
"${cov[@]}" show "${common[@]}" -format=html -output-dir="$directory/html"
report="$("${cov[@]}" report "${common[@]}")"; printf '%s\n' "$report"
line_coverage="$(printf '%s\n' "$report" | awk '$1 == "TOTAL" { value=$10; sub(/%$/, "", value); print value }')"
awk -v coverage="$line_coverage" 'BEGIN { exit !(coverage + 0 >= 80.0) }' || { printf 'Coverage is %s%%, below 80%%.\n' "$line_coverage" >&2; exit 1; }
printf '==> Coverage report: %s\n' "$directory/html/index.html"
