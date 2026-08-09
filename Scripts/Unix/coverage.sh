#!/usr/bin/env bash
set -euo pipefail
PLATFORM="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"; source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Coverage; ARCHITECTURE="$(native_architecture)"; TOOLSET=clang; TARGET=KeireTests; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"; load_project_config "$ROOT"; system=linux; [[ "$PLATFORM" == Mac ]] && system=macosx
output_arch="$(architecture_output_name "$ARCHITECTURE")"; directory="$ROOT/Build/Coverage/$system-$output_arch"; mkdir -p "$directory"; rm -f "$directory"/*.profraw
targets=("$TESTS_TARGET" "${PROJECT_NAMESPACE}EditorTests" "${PROJECT_NAMESPACE}HubTests" "$CLIENT_TARGET")
executables=()
for target in "${targets[@]}"; do
  args=(--generator ninja --configuration Coverage --architecture "$ARCHITECTURE" --toolset clang --target "$target")
  [[ $CI -eq 1 ]] && args+=(--ci)
  [[ $UPDATE -eq 1 ]] && args+=(--update)
  [[ $FORCE -eq 1 ]] && args+=(--force)
  bash "$ROOT/Scripts/$PLATFORM/build.sh" "${args[@]}"
  [[ "$PLATFORM" == Linux ]] && activate_linux_toolchain "$ROOT" "$TOOLSET"
  executable="$ROOT/Build/Bin/Coverage-$system-$output_arch/$target/$target"
  [[ -x "$executable" ]] || { printf 'Coverage executable was not found: %s\n' "$executable" >&2; exit 1; }
  executables+=("$executable")
  run_args=()
  if [[ "$target" == "$CLIENT_TARGET" ]]; then
    run_args=(--project "$ROOT/Samples/KeireSandbox" --smoke-project)
  fi
  (cd "$ROOT" && LLVM_PROFILE_FILE="$directory/%p.profraw" "$executable" "${run_args[@]}")
done
if [[ "$PLATFORM" == Mac ]]; then
  profdata=(xcrun llvm-profdata); cov=(xcrun llvm-cov)
else
  profdata=("$(resolve_llvm_tool llvm-profdata)"); cov=("$(resolve_llvm_tool llvm-cov)")
fi
profiles=("$directory"/*.profraw); [[ -e "${profiles[0]}" ]] || { printf 'No coverage profiles produced.\n' >&2; exit 1; }
"${profdata[@]}" merge -sparse "${profiles[@]}" -o "$directory/coverage.profdata"
common=(-instr-profile="$directory/coverage.profdata" "${executables[0]}")
for executable in "${executables[@]:1}"; do common+=(-object="$executable"); done
common+=(-ignore-filename-regex='Vendor|KeireTests|KeireEditorTests|KeireHubTests|KeireRenderTests')
core=(-instr-profile="$directory/coverage.profdata" "${executables[0]}" -ignore-filename-regex='Vendor|KeireTests|KeireEditorTests|KeireHubTests|KeireRenderTests')
"${cov[@]}" export -format=lcov "${common[@]}" > "$directory/coverage.info"
"${cov[@]}" show "${common[@]}" -format=html -output-dir="$directory/html"
aggregate_report="$("${cov[@]}" report "${common[@]}")"; printf '%s\n' "$aggregate_report"
core_report="$("${cov[@]}" report "${core[@]}")"
aggregate_line_coverage="$(printf '%s\n' "$aggregate_report" | awk '$1 == "TOTAL" { value=$10; sub(/%$/, "", value); print value }')"
core_line_coverage="$(printf '%s\n' "$core_report" | awk '$1 == "TOTAL" { value=$10; sub(/%$/, "", value); print value }')"
minimum_core_line_coverage=74.5
minimum_aggregate_line_coverage=63.0
printf '==> Line coverage: core %s%% (minimum %s%%), aggregate %s%% (minimum %s%%)\n' \
  "$core_line_coverage" "$minimum_core_line_coverage" "$aggregate_line_coverage" "$minimum_aggregate_line_coverage"
awk -v coverage="$core_line_coverage" -v minimum="$minimum_core_line_coverage" \
  'BEGIN { exit !(coverage + 0 >= minimum + 0) }' || {
  printf 'Core line coverage is %s%%, below %s%%.\n' "$core_line_coverage" "$minimum_core_line_coverage" >&2
  exit 1
}
awk -v coverage="$aggregate_line_coverage" -v minimum="$minimum_aggregate_line_coverage" \
  'BEGIN { exit !(coverage + 0 >= minimum + 0) }' || {
  printf 'Aggregate line coverage is %s%%, below %s%%.\n' "$aggregate_line_coverage" "$minimum_aggregate_line_coverage" >&2
  exit 1
}
printf '==> Coverage report: %s\n' "$directory/html/index.html"
