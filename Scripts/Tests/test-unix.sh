#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
if [[ "$(uname -s)" == Darwin ]]; then
  if [[ -d /opt/homebrew/bin ]]; then
    export PATH="/opt/homebrew/bin:$PATH"
  elif [[ -d /usr/local/bin ]]; then
    export PATH="/usr/local/bin:$PATH"
  fi
  if [[ -d "$ROOT/Tools/Mac/python-packages" ]]; then
    export PYTHONPATH="$ROOT/Tools/Mac/python-packages${PYTHONPATH:+:$PYTHONPATH}"
  fi
fi
suite=all
while [[ $# -gt 0 ]]; do
  case "$1" in
    --suite) suite="${2:?--suite requires fast, integration, or all}"; shift 2 ;;
    *) printf 'Unknown test-unix option: %s\n' "$1" >&2; exit 2 ;;
  esac
done
[[ "$suite" == all || "$suite" == fast || "$suite" == integration ]] || { printf 'Invalid suite: %s\n' "$suite" >&2; exit 2; }
started=$SECONDS
run_fast=0; run_integration=0
[[ "$suite" == all || "$suite" == fast ]] && run_fast=1
[[ "$suite" == all || "$suite" == integration ]] && run_integration=1
assert_equal() { [[ "$1" == "$2" ]] || { printf '%s: expected %s, got %s\n' "$3" "$2" "$1" >&2; exit 1; }; }
assert_true() { "$@" || { printf 'Assertion failed: %s\n' "$*" >&2; exit 1; }; }
assert_false() { if "$@"; then printf 'Expected failure: %s\n' "$*" >&2; exit 1; fi; }
build_jobs_with_override() { KEIRE_BUILD_JOBS="$1" build_parallel_jobs; }
sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1"
  else
    shasum -a 256 "$1"
  fi | awk '{print $1}'
}

load_project_config "$ROOT"
if [[ $run_fast -eq 1 ]]; then
cxx20_probe_fixture="$(mktemp -d)"
cxx20_probe_compiler="$cxx20_probe_fixture/clang++"
cxx20_probe_arguments="$cxx20_probe_fixture/arguments"
printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail' \
  'printf '\''%s\n'\'' "$@" > "$KEIRE_CXX20_PROBE_ARGUMENTS"' \
  '[[ "${KEIRE_CXX20_PROBE_COMPILE_STATUS:-0}" -eq 0 ]] || {' \
  '  printf '\''fixture compiler rejection\n'\'' >&2' \
  '  exit "$KEIRE_CXX20_PROBE_COMPILE_STATUS"' \
  '}' \
  'output=""' \
  'while [[ $# -gt 0 ]]; do' \
  '  if [[ "$1" == -o ]]; then output="${2:?missing compiler output}"; shift 2; else shift; fi' \
  'done' \
  '[[ -n "$output" ]]' \
  'printf '\''%s\n'\'' '\''#!/usr/bin/env bash'\'' '\''exit "${KEIRE_CXX20_PROBE_RUNTIME_STATUS:-0}"'\'' > "$output"' \
  'chmod +x "$output"' > "$cxx20_probe_compiler"
chmod +x "$cxx20_probe_compiler"
export KEIRE_CXX20_PROBE_ARGUMENTS="$cxx20_probe_arguments"
probe_cxx20_thread_library "$cxx20_probe_compiler"
assert_true grep -Fx -q -- '-std=c++20' "$cxx20_probe_arguments"
assert_true grep -Fx -q -- '-pthread' "$cxx20_probe_arguments"
assert_false grep -F -q '_LIBCPP_ENABLE_EXPERIMENTAL' "$cxx20_probe_arguments"
set +e
KEIRE_CXX20_PROBE_COMPILE_STATUS=42 probe_cxx20_thread_library "$cxx20_probe_compiler" \
  >"$cxx20_probe_fixture/compile-diagnostic" 2>&1
cxx20_compile_status=$?
KEIRE_CXX20_PROBE_RUNTIME_STATUS=23 probe_cxx20_thread_library "$cxx20_probe_compiler" \
  >"$cxx20_probe_fixture/runtime-diagnostic" 2>&1
cxx20_runtime_status=$?
set -e
assert_equal "$cxx20_compile_status" 42 'C++20 thread-library compile probe status'
assert_true grep -F -q 'std::stop_token and std::jthread' "$cxx20_probe_fixture/compile-diagnostic"
assert_true grep -F -q 'sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer' \
  "$cxx20_probe_fixture/compile-diagnostic"
assert_true grep -F -q 'Do not enable _LIBCPP_ENABLE_EXPERIMENTAL' \
  "$cxx20_probe_fixture/compile-diagnostic"
assert_true grep -F -q 'fixture compiler rejection' "$cxx20_probe_fixture/compile-diagnostic"
assert_equal "$cxx20_runtime_status" 23 'C++20 thread-library runtime probe status'
assert_true grep -F -q 'compiled but could not run' "$cxx20_probe_fixture/runtime-diagnostic"
unset KEIRE_CXX20_PROBE_ARGUMENTS
rm -rf "$cxx20_probe_fixture"
homebrew_installer_fixture="$(mktemp -d)"
homebrew_installer_script="$homebrew_installer_fixture/install.sh"
homebrew_installer_output="$homebrew_installer_fixture/output"
printf '%s\n' '#!/usr/bin/env bash' \
  'printf "%s\n" "${NONINTERACTIVE:-unset}" > "$KEIRE_HOMEBREW_TEST_OUTPUT"' \
  > "$homebrew_installer_script"
export KEIRE_HOMEBREW_TEST_OUTPUT="$homebrew_installer_output"
run_homebrew_installer 1 "$homebrew_installer_script"
assert_equal "$(cat "$homebrew_installer_output")" 1 'CI Homebrew installer environment'
rm -f "$homebrew_installer_output"
set +e
NONINTERACTIVE=1 run_homebrew_installer 0 "$homebrew_installer_script" </dev/null \
  > "$homebrew_installer_fixture/diagnostic" 2>&1
homebrew_noninteractive_status=$?
set -e
assert_equal "$homebrew_noninteractive_status" 1 'local non-terminal Homebrew installer rejection status'
assert_false test -e "$homebrew_installer_output"
assert_true grep -F -q 'cannot request authorization without an interactive terminal' \
  "$homebrew_installer_fixture/diagnostic"
unset KEIRE_HOMEBREW_TEST_OUTPUT
rm -rf "$homebrew_installer_fixture"
dotnet_listing_fixture="$(mktemp -d)"
mkdir -p "$dotnet_listing_fixture/installation/sdk" "$dotnet_listing_fixture/unrelated/sdk"
ln -s "$dotnet_listing_fixture/installation" "$dotnet_listing_fixture/path-alias"
printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail' \
  'case "${1:-}" in' \
  "  --list-sdks) printf '%s\\n' '10.0.302 [$dotnet_listing_fixture/path-alias/sdk]' ;;" \
  "  --version) printf '%s\\n' \"\${KEIRE_DOTNET_FIXTURE_SELECTED:-10.0.302}\" ;;" \
  '  *) exit 2 ;;' 'esac' > "$dotnet_listing_fixture/installation/dotnet"
chmod +x "$dotnet_listing_fixture/installation/dotnet"
dotnet_listing="10.0.302 [$dotnet_listing_fixture/path-alias/sdk]"
assert_true dotnet_sdk_listing_matches_installation "$dotnet_listing" 10.0.302 \
  "$dotnet_listing_fixture/installation"
assert_false dotnet_sdk_listing_matches_installation "" 10.0.302 "$dotnet_listing_fixture/installation"
assert_false dotnet_sdk_listing_matches_installation "$dotnet_listing" 10.0.303 \
  "$dotnet_listing_fixture/installation"
assert_false dotnet_sdk_listing_matches_installation \
  "10.0.302 [$dotnet_listing_fixture/unrelated/sdk]" 10.0.302 "$dotnet_listing_fixture/installation"
assert_false dotnet_sdk_listing_matches_installation \
  "10.0.302-preview [$dotnet_listing_fixture/path-alias/sdk]" 10.0.302 \
  "$dotnet_listing_fixture/installation"
assert_equal "$(pinned_dotnet_sdk_root "$dotnet_listing_fixture/installation/dotnet" 10.0.302)" \
  "$(cd -P "$dotnet_listing_fixture/installation" && pwd -P)" 'pinned dotnet SDK provenance'
export KEIRE_DOTNET_FIXTURE_SELECTED=10.0.400
assert_false pinned_dotnet_sdk_root "$dotnet_listing_fixture/installation/dotnet" 10.0.302
unset KEIRE_DOTNET_FIXTURE_SELECTED
rm -rf "$dotnet_listing_fixture"
dotnet_host_pack_fixture="$(mktemp -d)"
printf '%s\n' \
  '<Project>' \
  '  <ItemGroup>' \
  '    <KnownAppHostPack' \
  '      Include="Microsoft.NETCore.App"' \
  '      TargetFramework="net10.0"' \
  '      AppHostPackVersion="10.0.10"' \
  '      AppHostRuntimeIdentifiers="linux-x64;linux-arm64;osx-x64;osx-arm64" />' \
  '    <KnownAppHostPack Include="Microsoft.NETCore.App" TargetFramework="net9.0"' \
  '      AppHostPackVersion="9.0.18" AppHostRuntimeIdentifiers="linux-x64" />' \
  '  </ItemGroup>' \
  '</Project>' > "$dotnet_host_pack_fixture/Microsoft.NETCoreSdk.BundledVersions.props"
dotnet_host_pack_metadata="$(dotnet_apphost_pack_metadata \
  "$dotnet_host_pack_fixture/Microsoft.NETCoreSdk.BundledVersions.props" net10.0)"
assert_equal "$dotnet_host_pack_metadata" \
  "$(printf '%s\n%s' 10.0.10 'linux-x64;linux-arm64;osx-x64;osx-arm64')" \
  'portable multiline .NET app-host pack discovery'
assert_false dotnet_apphost_pack_metadata \
  "$dotnet_host_pack_fixture/Microsoft.NETCoreSdk.BundledVersions.props" net8.0 2>/dev/null
assert_false dotnet_apphost_pack_metadata \
  "$dotnet_host_pack_fixture/Microsoft.NETCoreSdk.BundledVersions.props" '../escape' 2>/dev/null
rm -rf "$dotnet_host_pack_fixture"
workspace_identity_fixture="$(mktemp -d)"
mkdir -p "$workspace_identity_fixture/first" "$workspace_identity_fixture/second"
first_workspace_identity="$(workspace_identity "$workspace_identity_fixture/first")"
second_workspace_identity="$(workspace_identity "$workspace_identity_fixture/second")"
[[ "$first_workspace_identity" =~ ^[0-9a-f]{16}$ ]] || exit 1
assert_equal "$(workspace_identity "$workspace_identity_fixture/first/.")" \
  "$first_workspace_identity" 'stable Unix workspace identity'
assert_true test "$first_workspace_identity" != "$second_workspace_identity"
rm -rf "$workspace_identity_fixture"
fixture_nethost='linux-x64|10.0.10|fixture|hash|none'
coral_variant="$(coral_build_variant_key Linux x86_64 gcc \
  '/usr/bin/gcc|gcc fixture|/usr/bin/g++|g++ fixture' 10.0.302 /fixture/dotnet none \
  "$fixture_nethost" "$first_workspace_identity")"
[[ "$coral_variant" =~ ^linux-x86_64-[0-9a-f]{24}$ ]] || {
  printf 'Unexpected Unix Coral build variant key: %s\n' "$coral_variant" >&2
  exit 1
}
assert_equal "$(coral_build_variant_key Linux x86_64 gcc \
  '/usr/bin/gcc|gcc fixture|/usr/bin/g++|g++ fixture' 10.0.302 /fixture/dotnet none \
  "$fixture_nethost" "$first_workspace_identity")" \
  "$coral_variant" 'stable Unix Coral build variant identity'
assert_true test "$(coral_build_variant_key Linux ARM64 gcc \
  '/usr/bin/gcc|gcc fixture|/usr/bin/g++|g++ fixture' 10.0.302 /fixture/dotnet none \
  'linux-arm64|10.0.10|fixture|hash|none' "$first_workspace_identity")" != \
  "$coral_variant"
assert_true test "$(coral_build_variant_key Linux x86_64 clang \
  '/usr/bin/clang|clang fixture|/usr/bin/clang++|clang fixture' 10.0.302 /fixture/dotnet none \
  "$fixture_nethost" "$first_workspace_identity")" != \
  "$coral_variant"
assert_true test "$(coral_build_variant_key Linux x86_64 gcc \
  '/usr/bin/gcc|gcc changed|/usr/bin/g++|g++ changed' 10.0.302 /fixture/dotnet none \
  "$fixture_nethost" "$first_workspace_identity")" != \
  "$coral_variant"
assert_true test "$(coral_build_variant_key Linux x86_64 gcc \
  '/usr/bin/gcc|gcc fixture|/usr/bin/g++|g++ fixture' 10.0.303 /fixture/dotnet none \
  "$fixture_nethost" "$first_workspace_identity")" != \
  "$coral_variant"
assert_true test "$(coral_build_variant_key Linux x86_64 gcc \
  '/usr/bin/gcc|gcc fixture|/usr/bin/g++|g++ fixture' 10.0.302 /fixture/other-dotnet none \
  "$fixture_nethost" "$first_workspace_identity")" != \
  "$coral_variant"
assert_true test "$(coral_build_variant_key Linux x86_64 gcc \
  '/usr/bin/gcc|gcc fixture|/usr/bin/g++|g++ fixture' 10.0.302 /fixture/dotnet none \
  'linux-x64|10.0.11|fixture|hash|none' "$first_workspace_identity")" != "$coral_variant"
assert_true test "$(coral_build_variant_key Linux x86_64 gcc \
  '/usr/bin/gcc|gcc fixture|/usr/bin/g++|g++ fixture' 10.0.302 /fixture/dotnet none \
  "$fixture_nethost" "$second_workspace_identity")" != "$coral_variant"
mac_coral_variant="$(coral_build_variant_key Mac ARM64 clang \
  '/usr/bin/clang|clang fixture|/usr/bin/clang++|clang fixture' 10.0.302 /fixture/dotnet 13.5 \
  'osx-arm64|10.0.10|fixture|hash|runtime' "$first_workspace_identity")"
assert_true test "$(coral_build_variant_key Mac ARM64 clang \
  '/usr/bin/clang|clang fixture|/usr/bin/clang++|clang fixture' 10.0.302 /fixture/dotnet 14.0 \
  'osx-arm64|10.0.10|fixture|hash|runtime' "$first_workspace_identity")" != \
  "$mac_coral_variant"
assert_false coral_build_variant_key Linux x86_64 gcc compiler 10.0-preview /fixture/dotnet none \
  "$fixture_nethost" "$first_workspace_identity" 2>/dev/null
assert_false coral_build_variant_key Windows x86_64 gcc compiler 10.0.302 /fixture/dotnet none \
  "$fixture_nethost" "$first_workspace_identity" 2>/dev/null
ffmpeg_argument_fixture="$(mktemp -d)"
touch "$ffmpeg_argument_fixture/sentinel"
ffmpeg_test_platform=Linux
[[ "$(uname -s)" == Darwin ]] && ffmpeg_test_platform=Mac
set +e
ffmpeg_architecture_error="$(bash "$ROOT/Scripts/Unix/ffmpeg.sh" Debug "$ffmpeg_argument_fixture" \
  "$ffmpeg_test_platform" '../../../escape' gcc 2>&1)"
ffmpeg_architecture_status=$?
ffmpeg_toolset_error="$(bash "$ROOT/Scripts/Unix/ffmpeg.sh" Debug "$ffmpeg_argument_fixture" \
  "$ffmpeg_test_platform" x86_64 '../../../escape' 2>&1)"
ffmpeg_toolset_status=$?
set -e
assert_equal "$ffmpeg_architecture_status" 2 'unsafe Unix FFmpeg architecture rejection status'
assert_equal "$ffmpeg_toolset_status" 2 'unsafe Unix FFmpeg toolset rejection status'
[[ "$ffmpeg_architecture_error" == *"Unsupported architecture"* ]] || {
  printf 'Unix FFmpeg architecture validation did not fail closed: %s\n' "$ffmpeg_architecture_error" >&2
  exit 1
}
[[ "$ffmpeg_toolset_error" == *"Unsupported private FFmpeg toolset"* ]] || {
  printf 'Unix FFmpeg toolset validation did not fail closed: %s\n' "$ffmpeg_toolset_error" >&2
  exit 1
}
assert_true test -f "$ffmpeg_argument_fixture/sentinel"
rm -rf "$ffmpeg_argument_fixture"
generation_inventory_fixture="$(mktemp -d)"
mkdir -p "$generation_inventory_fixture/FirstParty/Nested" \
  "$generation_inventory_fixture/FirstParty/Build/Nested" \
  "$generation_inventory_fixture/.git/Nested" "$generation_inventory_fixture/Build/Nested" \
  "$generation_inventory_fixture/Library/Nested" "$generation_inventory_fixture/Vendor/Nested" \
  "$generation_inventory_fixture/Tools/Nested"
touch "$generation_inventory_fixture/premake5.lua" \
  "$generation_inventory_fixture/FirstParty/Nested/premake5.lua" \
  "$generation_inventory_fixture/FirstParty/Build/Nested/premake5.lua" \
  "$generation_inventory_fixture/.git/Nested/premake5.lua" \
  "$generation_inventory_fixture/Build/Nested/premake5.lua" \
  "$generation_inventory_fixture/Library/Nested/premake5.lua" \
  "$generation_inventory_fixture/Vendor/Nested/premake5.lua" \
  "$generation_inventory_fixture/Tools/Nested/premake5.lua"
generation_inventory_actual="$(project_generation_premake_inputs "$generation_inventory_fixture" | LC_ALL=C sort)"
generation_inventory_expected="$(printf '%s\n' \
  "$generation_inventory_fixture/FirstParty/Nested/premake5.lua" \
  "$generation_inventory_fixture/premake5.lua" | LC_ALL=C sort)"
assert_equal "$generation_inventory_actual" "$generation_inventory_expected" \
  'pruned Unix project-generation Premake inventory'
rm -rf "$generation_inventory_fixture"
binary_output_fixture="$(mktemp -d)"
binary_output_external="$(mktemp -d)"
binary_identity_stamp="$binary_output_fixture/generation.stamp"
mkdir -p "$binary_output_fixture/Build/Bin/Debug-linux-x86_64" \
  "$binary_output_fixture/Build/Bin/Release-linux-x86_64" \
  "$binary_output_fixture/Build/Bin/Debug-linux-AARCH64"
touch "$binary_output_fixture/Build/Bin/Debug-linux-x86_64/sentinel" \
  "$binary_output_fixture/Build/Bin/Release-linux-x86_64/sentinel" \
  "$binary_output_fixture/Build/Bin/Debug-linux-AARCH64/sentinel" \
  "$binary_output_external/sentinel"
printf 'ninja|x86_64|gcc|off|0|fingerprint\n' > "$binary_identity_stamp"
invalidate_incompatible_binary_outputs "$binary_output_fixture" linux x86_64 gcc "$binary_identity_stamp"
assert_true test -f "$binary_output_fixture/Build/Bin/Debug-linux-x86_64/sentinel"
printf 'ninja|x86_64|clang|off|0|fingerprint\n' > "$binary_identity_stamp"
invalidate_incompatible_binary_outputs "$binary_output_fixture" linux x86_64 gcc "$binary_identity_stamp"
assert_false test -e "$binary_output_fixture/Build/Bin/Debug-linux-x86_64"
assert_false test -e "$binary_output_fixture/Build/Bin/Release-linux-x86_64"
assert_true test -f "$binary_output_fixture/Build/Bin/Debug-linux-AARCH64/sentinel"
assert_true test -f "$binary_output_external/sentinel"
mkdir -p "$binary_output_fixture/Build/Bin/Debug-linux-x86_64"
touch "$binary_output_fixture/Build/Bin/Debug-linux-x86_64/sentinel"
rm -f "$binary_identity_stamp"
invalidate_incompatible_binary_outputs "$binary_output_fixture" linux x86_64 gcc "$binary_identity_stamp"
assert_false test -e "$binary_output_fixture/Build/Bin/Debug-linux-x86_64"
ln -s "$binary_output_external" "$binary_output_fixture/Build/Bin/Debug-linux-x86_64"
printf 'ninja|x86_64|clang|off|0|fingerprint\n' > "$binary_identity_stamp"
set +e
invalidate_incompatible_binary_outputs "$binary_output_fixture" linux x86_64 gcc \
  "$binary_identity_stamp" >/dev/null 2>&1
binary_reparse_status=$?
set -e
assert_equal "$binary_reparse_status" 1 'symbolic generated binary output rejection status'
assert_true test -f "$binary_output_external/sentinel"
rm -f "$binary_output_fixture/Build/Bin/Debug-linux-x86_64"
rm -rf "$binary_output_fixture" "$binary_output_external"
workspace_lock_fixture="$(mktemp -d)"
workspace_owner_race="$workspace_lock_fixture/owner-race"
workspace_owner_race_bin="$workspace_lock_fixture/owner-race-bin"
mkdir -p "$workspace_owner_race" "$workspace_owner_race_bin"
printf '%s\n' token=vanishing > "$workspace_owner_race/owner"
printf '%s\n' '#!/usr/bin/env bash' 'rm -f "${1:?owner file is required}"' 'exit 1' \
  > "$workspace_owner_race_bin/cat"
chmod +x "$workspace_owner_race_bin/cat"
workspace_owner_race_value="$(PATH="$workspace_owner_race_bin:$PATH" \
  workspace_lock_owner_value "$workspace_owner_race" token)"
assert_equal "$workspace_owner_race_value" '' 'concurrently removed workspace owner metadata'
export KEIRE_WORKSPACE_LOCK_TIMEOUT_SECONDS=1
export KEIRE_WORKSPACE_LOCK_STALE_SECONDS=10
export KEIRE_WORKSPACE_LOCK_HEARTBEAT_SECONDS=1
workspace_lock_acquire "$workspace_lock_fixture" first
assert_equal "$KEIRE_WORKSPACE_LOCK_OWNED" 1 'initial Unix workspace lock acquisition'
assert_equal "$(workspace_lock_owner_value "$KEIRE_WORKSPACE_LOCK_PATH" platform)" unix \
  'Unix workspace lock protocol metadata'
(
  workspace_lock_acquire "$workspace_lock_fixture" nested
  assert_equal "$KEIRE_WORKSPACE_LOCK_OWNED" 0 'inherited Unix workspace lock reentry'
  workspace_lock_release
)
set +e
(
  unset KEIRE_WORKSPACE_LOCK_TOKEN
  workspace_lock_acquire "$workspace_lock_fixture" contender
) >/dev/null 2>&1
workspace_lock_contender_status=$?
set -e
assert_equal "$workspace_lock_contender_status" 1 'concurrent Unix workspace lock timeout'
workspace_lock_release
workspace_lock_stale="$workspace_lock_fixture/Tools/.locks/project-command.lock"
mkdir "$workspace_lock_stale"
printf '%s\n' token=expired platform=windows pid=123 host=fixture command=test started=expired > \
  "$workspace_lock_stale/owner"
: > "$workspace_lock_stale/heartbeat"
python3 -c 'import os, sys, time; os.utime(sys.argv[1], (time.time() - 20, time.time() - 20))' \
  "$workspace_lock_stale/heartbeat"
workspace_lock_acquire "$workspace_lock_fixture" recovery
assert_equal "$KEIRE_WORKSPACE_LOCK_OWNED" 1 'expired cross-platform workspace lock recovery'
workspace_lock_release
assert_false test -e "$workspace_lock_stale"
workspace_lock_acquire "$workspace_lock_fixture" dependency-source-fixture \
  '.locks/dependency-fixture.lock'
assert_equal "$KEIRE_WORKSPACE_LOCK_PATH" \
  "$workspace_lock_fixture/.locks/dependency-fixture.lock" 'Unix shared-cache lock path'
set +e
(
  unset KEIRE_WORKSPACE_LOCK_TOKEN
  workspace_lock_acquire "$workspace_lock_fixture" dependency-source-contender \
    '.locks/dependency-fixture.lock'
) >/dev/null 2>&1
workspace_cache_contender_status=$?
set -e
assert_equal "$workspace_cache_contender_status" 1 'concurrent Unix shared-cache lock timeout'
workspace_lock_release
workspace_lock_external="$(mktemp -d)"
rmdir "$workspace_lock_fixture/.locks"
touch "$workspace_lock_external/sentinel"
ln -s "$workspace_lock_external" "$workspace_lock_fixture/.locks"
set +e
workspace_lock_acquire "$workspace_lock_fixture" redirected '.locks/redirected.lock' >/dev/null 2>&1
workspace_lock_redirected_status=$?
set -e
assert_equal "$workspace_lock_redirected_status" 1 \
  'symbolic Unix shared-cache lock parent rejection'
assert_true test -f "$workspace_lock_external/sentinel"
rm -f "$workspace_lock_fixture/.locks"
rm -rf "$workspace_lock_external"
set +e
workspace_lock_acquire "$workspace_lock_fixture" escape '../outside.lock' >/dev/null 2>&1
workspace_lock_escape_status=$?
set -e
assert_equal "$workspace_lock_escape_status" 1 'escaping Unix shared-cache lock path rejection'
rm -rf "$workspace_lock_fixture"
unset KEIRE_WORKSPACE_LOCK_TIMEOUT_SECONDS KEIRE_WORKSPACE_LOCK_STALE_SECONDS \
  KEIRE_WORKSPACE_LOCK_HEARTBEAT_SECONDS
locked_source_fixture="$(mktemp -d)"
git -C "$locked_source_fixture" init --quiet
printf '%s\n' fixture > "$locked_source_fixture/source.txt"
git -C "$locked_source_fixture" add source.txt
git -C "$locked_source_fixture" -c user.name=fixture -c user.email=fixture@example.invalid \
  commit --quiet -m fixture
locked_source_commit="$(git -C "$locked_source_fixture" rev-parse HEAD)"
locked_git_source_validate "$locked_source_fixture" "$locked_source_commit" fixture
locked_source_link="$locked_source_fixture-link"
ln -s "$locked_source_fixture" "$locked_source_link"
set +e
locked_git_source_validate "$locked_source_link" "$locked_source_commit" fixture >/dev/null 2>&1
locked_source_link_status=$?
set -e
assert_equal "$locked_source_link_status" 1 'symbolic locked-source cache rejection'
rm -f "$locked_source_link"
printf '%s\n' dirty > "$locked_source_fixture/untracked.txt"
set +e
locked_git_source_validate "$locked_source_fixture" "$locked_source_commit" fixture >/dev/null 2>&1
locked_source_dirty_status=$?
set -e
assert_equal "$locked_source_dirty_status" 1 'untracked locked-source cache rejection'
rm -f "$locked_source_fixture/untracked.txt"
printf '%s\n' modified > "$locked_source_fixture/source.txt"
set +e
locked_git_source_validate "$locked_source_fixture" "$locked_source_commit" fixture >/dev/null 2>&1
locked_source_modified_status=$?
set -e
assert_equal "$locked_source_modified_status" 1 'modified locked-source cache rejection'
rm -rf "$locked_source_fixture"
assert_false grep -R -E -q 'declare[[:space:]]+-A|local[[:space:]]+-A' \
  "$ROOT/Scripts/Unix" "$ROOT/Scripts/Linux" "$ROOT/Scripts/Mac"
python3 "$ROOT/Scripts/Tests/check-repository-layout.py"
bash "$ROOT/Scripts/Tests/test-generated-content-cache-unix.sh"
python3 "$ROOT/Scripts/Packaging/sync-sandbox-template.py" --check
bash "$ROOT/Scripts/Tests/test-clean-unix.sh"
bash "$ROOT/Scripts/Tests/test-editor-package-unix.sh"
bash "$ROOT/Scripts/Tests/test-hub-package-unix.sh"
python3 "$ROOT/Scripts/Tests/test-prepare-distribution-snapshot.py"
python3 "$ROOT/Scripts/Tests/test-website.py"
if command -v node >/dev/null 2>&1; then
  node "$ROOT/Scripts/Tests/test-website-downloads.mjs"
  node "$ROOT/Scripts/Tests/test-website-contact.mjs"
  node "$ROOT/Scripts/Tests/test-website-contact-function.mjs"
  node "$ROOT/Scripts/Tests/test-website-docs.mjs"
fi
python3 "$ROOT/Scripts/Tests/test-marketplace-migrations.py"
python3 "$ROOT/Scripts/Tests/test-marketplace-edge.py"
python3 "$ROOT/Scripts/Tests/test-marketplace-upload-sample.py"
assert_true grep -Fq 'mkdir -p -- "$package_directory/Web"' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq '"$npm_command" --prefix "$documentation_site" run build' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq '"$npm_command" --prefix "$documentation_site" test' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq 'cp -R -- "$documentation_output" "$package_directory/Web/"' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq 'install-web-runtime.sh' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq 'node_modules/beautiful-mermaid/LICENSE' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq 'KeireMarketplaceValidator/KeireMarketplaceValidator.csproj' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq 'KeireMarketplaceValidatorBroker/KeireMarketplaceValidatorBroker.csproj' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq 'KeireMarketplacePublicationSigner/KeireMarketplacePublicationSigner.csproj' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq 'keire-marketplace-validator.service.example' \
  "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
assert_true grep -Fq 'PrivateNetwork=true' \
  "$ROOT/Services/KeireDistributionService/Deployment/keire-marketplace-validator.service.example"
assert_true grep -Fq 'RestrictAddressFamilies=AF_UNIX' \
  "$ROOT/Services/KeireDistributionService/Deployment/keire-marketplace-validator.service.example"
assert_true grep -Fq 'KEIRE_VALIDATOR_ATTESTATION_PRIVATE_KEY' \
  "$ROOT/Services/KeireDistributionService/Deployment/marketplace-validator.env.example"
assert_true grep -Fq 'KEIRE_VALIDATOR_ATTESTATION_PUBLIC_KEY' \
  "$ROOT/Services/KeireDistributionService/Deployment/marketplace-validator-broker.env.example"
assert_true grep -Fq 'KEIRE_MARKETPLACE_PUBLICATION_PRIVATE_KEY' \
  "$ROOT/Services/KeireDistributionService/Deployment/marketplace-publication-signer.env.example"
assert_true grep -Fq 'NoNewPrivileges=true' \
  "$ROOT/Services/KeireDistributionService/Deployment/keire-marketplace-publication-signer.service.example"
for script in monitor-distribution backup-distribution backup-distribution-rclone restore-distribution \
  restore-distribution-rclone; do
  assert_true grep -Fq "$script.sh" "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
  bash -n "$ROOT/Services/KeireDistributionService/scripts/$script.sh"
done
bash "$ROOT/Scripts/Tests/test-rclone-distribution-backup-unix.sh"
for script in start-wsl2-host-bridge install-wsl2-host-bridge; do
  assert_true grep -Fq "$script.sh" "$ROOT/Services/KeireDistributionService/scripts/package-service.sh"
  bash -n "$ROOT/Services/KeireDistributionService/scripts/$script.sh"
done
assert_true grep -Fq 'DynamicUser=yes' \
  "$ROOT/Services/KeireDistributionService/scripts/install-wsl2-host-bridge.sh"
assert_true grep -Fq 'AmbientCapabilities=CAP_NET_BIND_SERVICE' \
  "$ROOT/Services/KeireDistributionService/scripts/install-wsl2-host-bridge.sh"
bash "$ROOT/Services/KeireDistributionService/scripts/install-wsl2-host-bridge.sh" \
  --host distribution.example.test --upstream-port 50255 --validate-only
assert_true grep -Fq '@distribution_api path /v1 /v1/* /v2 /v2/* /health/live /health/ready' \
  "$ROOT/Services/KeireDistributionService/Deployment/Caddyfile.example"
python3 "$ROOT/Scripts/Tests/test-supabase-config.py"
python3 "$ROOT/Scripts/Tests/test-patch-ninja-depfiles.py"
python3 "$ROOT/Scripts/Tests/test-ninja-compiler-cache.py"
bash "$ROOT/Scripts/Tests/test-installer-unix.sh"
bash "$ROOT/Scripts/Tests/test-hub-installer-unix.sh"
bash "$ROOT/Scripts/Tests/test-macos-packaging-unix.sh"
assert_true test -n "$PROJECT_IDENTIFIER"
managed_fixture="$(mktemp -d)"
trap 'rm -rf "$managed_fixture"' EXIT
mkdir -p "$managed_fixture/Scripts/Unix" "$managed_fixture/Build/Dependencies/dotnet-sdk" \
  "$managed_fixture/KeireManaged"
cp "$ROOT/Scripts/Unix/build-managed.sh" "$managed_fixture/Scripts/Unix/build-managed.sh"
printf '%s\n' marker-one > "$managed_fixture/KeireManaged/RuntimeApi.cs"
cat > "$managed_fixture/Build/Dependencies/dotnet-sdk/dotnet" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
output=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --output) output="${2:?--output requires a directory}"; shift 2 ;;
    *) shift ;;
  esac
done
[[ -n "$output" ]]
mkdir -p "$output"
cp "$root/KeireManaged/RuntimeApi.cs" "$output/Keire.Managed.dll"
printf '%s\n' invoked >> "$root/invocations"
EOF
chmod +x "$managed_fixture/Build/Dependencies/dotnet-sdk/dotnet"
bash "$managed_fixture/Scripts/Unix/build-managed.sh"
assert_equal "$(cat "$managed_fixture/Build/Managed/Keire.Managed.dll")" marker-one \
  'initial managed runtime API build'
sleep 1
printf '%s\n' marker-two > "$managed_fixture/KeireManaged/RuntimeApi.cs"
bash "$managed_fixture/Scripts/Unix/build-managed.sh"
assert_equal "$(cat "$managed_fixture/Build/Managed/Keire.Managed.dll")" marker-two \
  'managed runtime API rebuild after source edit'
bash "$managed_fixture/Scripts/Unix/build-managed.sh"
assert_equal "$(wc -l < "$managed_fixture/invocations" | tr -d ' ')" 2 \
  'incremental managed runtime API launcher invocation count'
sleep 1
mkdir "$managed_fixture/KeireManaged/Gameplay"
printf '%s\n' extra > "$managed_fixture/KeireManaged/Gameplay/Extra.cs"
bash "$managed_fixture/Scripts/Unix/build-managed.sh"
sleep 1
rm "$managed_fixture/KeireManaged/Gameplay/Extra.cs"
rmdir "$managed_fixture/KeireManaged/Gameplay"
bash "$managed_fixture/Scripts/Unix/build-managed.sh"
assert_equal "$(wc -l < "$managed_fixture/invocations" | tr -d ' ')" 4 \
  'managed runtime API source inventory invocation count'
rm -rf "$managed_fixture"
trap - EXIT
launcher_fixture="$(mktemp -d)"
mkdir -p "$launcher_fixture/Scripts/Unix" "$launcher_fixture/Scripts/Linux" \
  "$launcher_fixture/Scripts/Mac"
cp "$ROOT/Scripts/project.sh" "$launcher_fixture/Scripts/project.sh"
cat > "$launcher_fixture/Scripts/Unix/common.sh" <<'EOF'
native_architecture() { printf '%s' x86_64; }
normalize_architecture() { printf '%s' "$1"; }
load_project_config() { PROJECT_IDENTIFIER=ExitFixture; CLIENT_TARGET=Client; }
resolve_unix_toolset() { printf '%s' "$2"; }
validate_unix_combination() { :; }
normalize_configuration() { printf '%s' "$1"; }
workspace_lock_acquire() { :; }
workspace_lock_release() { :; }
EOF
printf '%s\n' '#!/usr/bin/env bash' 'exit 23' > "$launcher_fixture/Scripts/Linux/test.sh"
chmod +x "$launcher_fixture/Scripts/Linux/test.sh"
cp "$launcher_fixture/Scripts/Linux/test.sh" "$launcher_fixture/Scripts/Mac/test.sh"
set +e
bash "$launcher_fixture/Scripts/project.sh" test --generator ninja --configuration Debug --architecture x86_64 --toolset clang
launcher_exit=$?
set -e
rm -rf "$launcher_fixture"
assert_equal "$launcher_exit" 23 'top-level Unix launcher child exit propagation'
assert_true grep -Eq '^PROJECT_VERSION=[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.-]+)?$' "$ROOT/Config/Project.conf"
assert_true is_semantic_version '1.2.3-alpha.1+build.5'
assert_false is_semantic_version '01.2.3'
assert_false is_semantic_version '1.2.3-01'
assert_false is_semantic_version '1.2.3+'
assert_equal "$PROJECT_MACRO_PREFIX" "$(identifier_to_macro_prefix "$PROJECT_IDENTIFIER")" 'project macro prefix'
assert_equal "$(identifier_to_macro_prefix HTTPServer2Client)" HTTP_SERVER2_CLIENT 'macro prefix derivation'
assert_equal "$(normalize_architecture amd64)" x86_64 'x64 normalization'
assert_equal "$(normalize_architecture aarch64)" ARM64 'ARM normalization'
assert_equal "$(resolve_unix_toolset Linux default)" gcc 'Linux default toolset'
assert_equal "$(resolve_unix_toolset Mac default)" clang 'macOS default toolset'
assert_true version_at_least 16.0.1 16.0
assert_true version_at_least 17 16.9
assert_false version_at_least 15.9 16.0
assert_equal "$(printf 'Xcode 16.4\nBuild version 16F6\n' | extract_version)" 16.4 'multi-line version extraction'
version_probe_fixture="$(mktemp -d)"
cat > "$version_probe_fixture/version-tool" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ ! -f premake5.lua ]] || exit 23
printf '%s\n' 'fixture tool 5.0.0-beta8'
EOF
chmod +x "$version_probe_fixture/version-tool"
touch "$version_probe_fixture/premake5.lua"
version_probe_output="$(cd "$version_probe_fixture" &&
  tool_version_from_temporary_directory "$version_probe_fixture/version-tool" --version)"
assert_equal "$version_probe_output" 'fixture tool 5.0.0-beta8' 'isolated tool version query'
rm -rf "$version_probe_fixture"
dotnet_fixture="$(mktemp -d)"
mkdir -p "$dotnet_fixture/root/sdk"
cat > "$dotnet_fixture/dotnet" <<EOF
#!/usr/bin/env bash
printf '%s\n' '9.0.305 [/unused/dotnet/sdk]' '10.0.100 [$dotnet_fixture/root/sdk]'
EOF
chmod +x "$dotnet_fixture/dotnet"
assert_equal "$(dotnet_sdk_root "$dotnet_fixture/dotnet" 10)" "$dotnet_fixture/root" \
  'dotnet SDK root parsing'
assert_false dotnet_sdk_root "$dotnet_fixture/dotnet" 11
rm -rf "$dotnet_fixture"
assert_equal "$(package_name apt-get ninja)" ninja-build 'APT Ninja package'
assert_equal "$(package_name pacman ninja)" ninja 'pacman Ninja package'
assert_equal "$(package_name zypper ninja)" ninja 'Zypper Ninja package'
assert_equal "$(package_name dnf cxx)" gcc-c++ 'DNF C++ package'
assert_equal "$(package_name apt-get modern-cxx)" 'gcc-12 g++-12' 'APT modern GCC packages'
assert_equal "$(package_name dnf modern-cxx)" 'gcc-toolset-12-gcc gcc-toolset-12-gcc-c++' 'DNF modern GCC packages'
assert_equal "$(package_name pacman python)" python 'pacman Python package'
assert_equal "$(package_name apt-get perl-json)" libjson-perl 'APT Perl JSON package'
assert_equal "$(package_name dnf perl-json)" perl-JSON 'DNF Perl JSON package'
assert_equal "$(package_name pacman perl-json)" perl-json 'pacman Perl JSON package'
assert_equal "$(package_name zypper perl-json)" perl-JSON 'Zypper Perl JSON package'
assert_equal "$(package_name dnf perl-open)" perl-open 'DNF Perl open package'
assert_equal "$(package_name apt-get perl-open)" perl 'APT Perl open package'
assert_true grep -Fqw libicu-dev <<< "$(package_name apt-get dotnet-runtime-deps)"
assert_true grep -Fqw openssl-libs <<< "$(package_name dnf dotnet-runtime-deps)"
assert_true grep -Fqw icu <<< "$(package_name pacman dotnet-runtime-deps)"
assert_true grep -Fqw libopenssl3 <<< "$(package_name zypper dotnet-runtime-deps)"
assert_equal "$(package_name apt-get curl-dev)" libcurl4-openssl-dev 'APT libcurl development package'
assert_equal "$(package_name dnf curl-dev)" libcurl-devel 'DNF libcurl development package'
assert_equal "$(package_name pacman curl-dev)" curl 'pacman libcurl development package'
assert_equal "$(package_name zypper curl-dev)" libcurl-devel 'Zypper libcurl development package'
assert_equal "$(package_name apt-get uuid)" uuid-dev 'APT UUID development package'
assert_equal "$(package_name dnf uuid)" libuuid-devel 'DNF UUID development package'
assert_equal "$(package_name pacman uuid)" util-linux-libs 'pacman UUID development package'
assert_equal "$(package_name zypper uuid)" libuuid-devel 'Zypper UUID development package'
assert_true grep -Fqw libgbm-devel <<< "$(package_name zypper sdl-video)"
assert_false grep -Fqw Mesa-libgbm-devel <<< "$(package_name zypper sdl-video)"
assert_true grep -Fqw libxtst-dev <<< "$(package_name apt-get sdl-video)"
assert_true grep -Fqw libXtst-devel <<< "$(package_name dnf sdl-video)"
assert_true grep -Fqw libxtst <<< "$(package_name pacman sdl-video)"
assert_equal "$(package_name apt-get native-dialog)" zenity 'APT native dialog package'
assert_equal "$(package_name dnf native-dialog)" zenity 'DNF native dialog package'
assert_equal "$(package_name pacman native-dialog)" zenity 'Pacman native dialog package'
assert_equal "$(package_name zypper native-dialog)" zenity 'Zypper native dialog package'
assert_true grep -Fqw libXtst-devel <<< "$(package_name zypper sdl-video)"
assert_equal "$(package_name apt-get llvm)" llvm 'LLVM tools package'
assert_equal "$(package_name pacman binutils)" binutils 'binutils package'
assert_equal "$(package_install_arguments pacman | tr '\n' ' ' | sed 's/ $//')" '-Syu --needed --noconfirm' 'pacman safe install arguments'
assert_equal "$(package_install_arguments apt-get)" -y 'APT install arguments'
assert_equal "$(package_install_arguments dnf | tr '\n' ' ' | sed 's/ $//')" 'install -y' 'DNF install arguments'
assert_equal "$(package_install_arguments zypper | tr '\n' ' ' | sed 's/ $//')" '--non-interactive install' 'Zypper install arguments'
assert_equal "$(KEIRE_BUILD_JOBS=3 build_parallel_jobs)" 3 'explicit Unix build parallelism'
assert_false build_jobs_with_override 0
assert_false build_jobs_with_override invalid
package_policy_fixture="$(mktemp -d)"
git -C "$package_policy_fixture" init --quiet
git -C "$package_policy_fixture" config user.email tests@keire.invalid
git -C "$package_policy_fixture" config user.name 'Kéire Tests'
printf '%s\n' clean > "$package_policy_fixture/tracked.txt"
git -C "$package_policy_fixture" add tracked.txt
git -C "$package_policy_fixture" commit --quiet -m fixture
assert_equal "$(package_worktree_policy "$package_policy_fixture" 0 0)" 'false false' 'clean production package policy'
assert_true grep -Fq 'git_worktree_status "$ROOT"' "$ROOT/Scripts/Unix/build-info.sh"
windows_git_failure_is_not_native_fallback() (
  uname() { printf '%s\n' 'Linux microsoft-standard-WSL2'; }
  wslpath() { [[ "$1" == -m ]] && printf '%s\n' 'C:/fixture'; }
  git.exe() { return 1; }
  git() { return 0; }
  ! git_worktree_status /mnt/c/fixture
)
assert_true windows_git_failure_is_not_native_fallback
printf '%s\n' dirty > "$package_policy_fixture/untracked.txt"
assert_false package_worktree_policy "$package_policy_fixture" 0 0
assert_equal "$(package_worktree_policy "$package_policy_fixture" 1 0)" 'true true' 'local dirty development package policy'
assert_false package_worktree_policy "$package_policy_fixture" 1 1
rm -rf "$package_policy_fixture"
assert_true mac_requires_full_xcode xcode4
assert_false mac_requires_full_xcode ninja
assert_equal "$(json_escape $'quote" slash\\ tab\t')" 'quote\" slash\\ tab\t' 'JSON escaping'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SPDLOG_COMMIT)" 79524ddd08a4ec981b7fea76afd08ee05f83755d 'spdlog lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" DOCTEST_COMMIT)" 2d0a9359a60c51affe2a9bebb1be1dca47868151 'doctest lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_COMMIT)" 11a9d3212ef3063a2755982ce71a26b365cef32a 'SDL lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" JSON_COMMIT)" 55f93686c01528224f448c19128836e7df245f72 'JSON lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" IMGUI_COMMIT)" b61e56346a92cfcaf1f43a545ca37b0b32239654 'Dear ImGui lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" ZSTD_COMMIT)" f8745da6ff1ad1e7bab384bd1f9d742439278e99 'Zstandard lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" ENTT_COMMIT)" 85c6bba014049b5de8fad49d25424df2f1f6a8c1 'EnTT lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" GLM_COMMIT)" 33b0eb9fa336ffd8551024b1d2690e418014553b 'GLM lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_COMMIT)" e55cf5e31ced6f3d1be5cc6d0c50e99384f9f4ba 'SDL_shadercross lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_DXC_COMMIT)" 2c84a1c5ab7091608c97df6ba5ccf46e71c322eb 'DXC recursive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_CROSS_COMMIT)" 1a6169566c73d3da552748fc372fe2bbb856e46e 'SPIRV-Cross recursive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT)" ad9184e76a66b1001c29db9b0a3e87f646c64de0 'SPIRV-Headers recursive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT)" 0539c81f69a3daeb706fd3477dca61435b475156 'SPIRV-Tools recursive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" ASSIMP_COMMIT)" 392a658f9c271be965271f45e7521a1b80ea4392 'Assimp lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" STB_COMMIT)" 2c980bb59875b0d32144a71867fbdebb2f77cd20 'stb lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" FFMPEG_COMMIT)" 89153eb701d372f54a5d7d29de5067abc09e11d3 'FFmpeg lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" LIBSODIUM_COMMIT)" 77e1ce5d6dee871c49ef211222ba18ef0c486bda 'libsodium lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" TRACY_COMMIT)" 05cceee0df3b8d7c6fa87e9638af311dbabc63cb 'Tracy lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_SDK_VERSION)" 10.0.302 '.NET SDK lock'
assert_true grep -F -q '<RuntimeFrameworkVersion>10.0.10</RuntimeFrameworkVersion>' \
  "$ROOT/Services/KeireDistributionService/Source/KeireDistributionPublisher/KeireDistributionPublisher.csproj"
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" PYYAML_VERSION)" 6.0.3 'PyYAML lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" PYYAML_SOURCE_SHA256)" \
  d76623373421df22fb4cf8817020cbb7ef15c725b9d5e45f17e189bfc384190f 'PyYAML source lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_MACOS_X86_64_SHA512)" \
  48d5861dc0d6c9c782c6d163d6b334ecac2ebd65a1ae59e9ce5b93dd080a31d7ecfc4e4d47e0e35b201ce63661218d641e154022266294a3a8b84593a019cfbc \
  '.NET macOS x86_64 SDK archive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_MACOS_ARM64_SHA512)" \
  b2286dec9177e8b5543ff2fe95c84db358b87ec2a36a0d34a29033d70279940fd1134af56c4299648f8950db2d6ce35237698cf2818d9abc670c2c1664c92ac0 \
  '.NET macOS ARM64 SDK archive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" CMAKE_VERSION)" 3.31.12 'CMake version lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" CMAKE_LINUX_X86_64_SHA256)" \
  0dc2e9a6860f06bf10bd8fadc03e35d9eeb4df46e33763a7e480e987758f385c 'CMake x86_64 archive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" NASM_VERSION)" 3.02 'NASM source lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" PATCHELF_VERSION)" 0.19.0 'patchelf source lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" PATCHELF_SOURCE_SHA256)" \
  4782e58d7dd5deae3f8d215f86f94fda0e3d072b583f5ad443846f3381fb472a 'patchelf source hash lock'
assert_true grep -F -q 'version_at_least "$actual" "$PATCHELF_VERSION"' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -q 'Vendor/imgui' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'Scripts/Premake/DearImGui.lua' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'imgui|zstd|entt|glm|SDL_shadercross|assimp|stb)' "$ROOT/Scripts/Unix/vendor-update.sh"
assert_true grep -q 'Vendor/entt' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'Vendor/glm' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'imgui|zstd|entt|glm|SDL_shadercross|assimp|stb)' "$ROOT/Scripts/Unix/vendor-update.sh"
assert_true grep -q 'Vendor/zstd' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'Scripts/Premake/Zstd.lua' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'imgui|zstd|entt|glm|SDL_shadercross|assimp|stb)' "$ROOT/Scripts/Unix/vendor-update.sh"
assert_true grep -q 'Vendor/SDL_shadercross' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'Vendor/assimp' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'Vendor/stb' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'Vendor/ffmpeg' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'SDL_SHADERCROSS_DXC_COMMIT' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -F -q -- '--include-profile-dependencies' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -F -q 'Build/Dependencies/tracy' "$ROOT/Scripts/Unix/vendor.sh" \
  "$ROOT/Scripts/Unix/vendor-update.sh"
assert_false grep -F -q 'Vendor/tracy' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -F -q '"$path" == Build/Dependencies/tracy' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -F -q 'fetch --no-tags origin "$commit"' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -F -q 'if [[ "$DEPENDENCY" == Tracy ]]' "$ROOT/Scripts/Unix/vendor-update.sh"
assert_true grep -F -q 'git add Config/Dependencies.lock' "$ROOT/Scripts/Unix/vendor-update.sh"
assert_true grep -F -q 'git add Vendor/%s Config/Dependencies.lock' "$ROOT/Scripts/Unix/vendor-update.sh"
assert_true grep -q 'keire-dependency.stamp' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q -- '-DKEIRE_ASSIMP_SOURCE="$ROOT/Vendor/assimp"' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q 'locked_git_source_validate "$source_path" "$commit" "$name" || return 1' \
  "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q '".locks/$name-$commit.lock"' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q 'locked_git_source_validate "$source_path" "$coral_commit" Coral' \
  "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -F -q '".locks/coral-$cache_key.lock"' "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -F -q 'expected_stamp="$coral_commit|$patch_digest|$variant_key|' \
  "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -F -q 'workspace_key="$(workspace_identity "$ROOT")"' "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -F -q 'Microsoft.NETCoreSdk.BundledVersions.props' "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -F -q 'dotnet_apphost_pack_metadata "$bundled_versions" "$target_framework"' \
  "$ROOT/Scripts/Unix/coral.sh"
assert_false grep -F -q "RS='/>'" "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -F -q 'CORAL_DOTNET_ROOT=%s' "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -F -q 'workspace_lock_acquire "$ROOT" dependencies' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q 'Coral Debug and Release metadata must resolve to one checkout-isolated build variant' \
  "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q '"$platform" "$architecture" "$toolset"' \
  "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q 'cache_root="$ROOT/Build/Tools/ShaderCompiler/Cache/' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -q 'SDL_DUMMYVIDEO=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_OFFSCREEN=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_GPU=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_RENDER=OFF' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_JOYSTICK=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_HAPTIC=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_HIDAPI=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_HIDAPI_JOYSTICK=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_HIDAPI_LIBUSB=OFF' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_VIRTUAL_JOYSTICK=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_false grep -q 'SDL_JOYSTICK=OFF' "$ROOT/Scripts/Unix/dependencies.sh"
assert_false grep -q 'SDL_HAPTIC=OFF' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'validate_sdl_input_backends' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_JOYSTICK_LINUX SDL_HAPTIC_LINUX' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_JOYSTICK_IOKIT SDL_JOYSTICK_MFI SDL_HAPTIC_IOKIT' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_UDEV_DYNAMIC' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_INSTALL_CMAKEDIR_ROOT=cmake' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_INSTALL_CMAKEDIR_ROOT=cmake' "$ROOT/Scripts/Windows/dependencies.ps1"
assert_true grep -q 'CPP_RTTI_ENABLED=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'CPP_RTTI_ENABLED=ON' "$ROOT/Scripts/Windows/dependencies.ps1"
assert_true grep -q 'shader-compiler.sh' "$ROOT/Scripts/Unix/dependencies.sh"
assert_false grep -F -q 'ensure_command patchelf patchelf' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'ensure_command awk awk' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'ensure_command find findutils' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'ensure_command python3 python' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'ensure_command bison bison' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'ensure_command flex flex' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'install_nasm' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'install_patchelf' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'install_logical_packages perl-json' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'install_logical_packages perl-open' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'install_logical_packages sdl-input' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'apt-get:sdl-input) printf '\''libudev-dev' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'dnf:sdl-input) printf '\''systemd-devel' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'pacman:sdl-input) printf '\''systemd' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'zypper:sdl-input) printf '\''libudev-devel' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'ensure_command cmp diffutils' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'perl -Mopen=:std -e 1' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'install_dotnet_sdk' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'install_gcc_toolchain' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'ensure_host_clang_toolchain' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'version_at_least "$clang_version" 14' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'version_at_least "$clang_version" 16' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'brew list --versions bison' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'check_version Bison "$("$bison_executable" --version | extract_version)" 3.0' \
  "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'brew_install pkg-config pkgconf' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'check_version pkg-config "$(pkg-config --version)" 0.29.2' \
  "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'brew_install rg ripgrep' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'brew_install python3 python' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q '[[ ! -L "$install_root" ]]' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'ensure_command rg ripgrep' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'install_dotnet_sdk()' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'install_pyyaml()' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'publish_cached_directory()' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_equal "$(grep -F -c 'publish_cached_directory "$candidate" "$install_root"' \
  "$ROOT/Scripts/Mac/bootstrap.sh")" 2 'atomic macOS cache publication call sites'
assert_true grep -F -q 'PUBLICATION_BACKUP="$parent/.$name.backup.$$.$RANDOM"' \
  "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'shasum -a 256 "$archive"' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'python_packages_link="$ROOT/Tools/Mac/python-packages"' \
  "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'DOTNET_MACOS_X86_64_SHA512' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'DOTNET_MACOS_ARM64_SHA512' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'shasum -a 512 "$archive"' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_equal "$(grep -F -c \
  'dotnet_sdk_listing_matches_installation "$listing" "$DOTNET_SDK_VERSION"' \
  "$ROOT/Scripts/Mac/bootstrap.sh")" 2 '.NET exact version and canonical path validation'
assert_true grep -F -q 'resolved_reported_sdk="$(cd -P "$reported_sdk" && pwd -P)"' \
  "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q '[[ ! -L "$cache_root" ]]' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_equal "$(grep -F -c '[[ ! -e "$dotnet_link" || -L "$dotnet_link" ]]' \
  "$ROOT/Scripts/Mac/bootstrap.sh")" 2 '.NET launcher replacement guards'
assert_true grep -F -q 'ln -sfn "$install_root/dotnet" "$dotnet_link"' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'install_dotnet_sdk' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'brew_install nasm nasm' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'check_version NASM "$(nasm -v | extract_version)" 2.14' \
  "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'run_homebrew_installer "$CI" "$script"' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_false grep -F -q 'NONINTERACTIVE=1 /bin/bash "$script"' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'run_homebrew_installer()' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q '[[ -t 0 ]]' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'NONINTERACTIVE=1 /bin/bash "$installer"' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'probe_cxx20_thread_library clang++' "$ROOT/Scripts/Mac/bootstrap.sh"
assert_true grep -F -q 'probe_cxx20_thread_library()' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q '"$compiler" -std=c++20 -pthread' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'Do not enable _LIBCPP_ENABLE_EXPERIMENTAL' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q '[[ $CI -eq 1 ]] && bootstrap+=(--ci)' "$ROOT/Scripts/Mac/generate.sh"
assert_true grep -F -q 'export PATH="$ROOT/Tools/Mac:$PATH"' \
  "$ROOT/Scripts/Mac/bootstrap.sh" "$ROOT/Scripts/Mac/generate.sh"
assert_true grep -F -q 'xcode_arguments=(-scheme "$TARGET" -configuration "$CONFIGURATION")' \
  "$ROOT/Scripts/Mac/build.sh"
assert_true grep -F -q 'ninja_arguments=(-C "$ROOT" -f build.ninja)' "$ROOT/Scripts/Mac/build.sh"
assert_true grep -F -q 'ninja_arguments+=("${TARGET}_${CONFIGURATION}")' "$ROOT/Scripts/Mac/build.sh"
assert_false grep -E -q '(xcode|ninja)_profile=\(\)' "$ROOT/Scripts/Mac/build.sh"
assert_true bash -n "$ROOT/Scripts/Mac/build.sh"
assert_true grep -F -q 'gcc-environment.sh' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'activate_linux_toolchain' "$ROOT/Scripts/Linux/build.sh" \
  "$ROOT/Scripts/Linux/generate.sh" "$ROOT/Scripts/Unix/run-target.sh" "$ROOT/Scripts/Unix/package.sh" \
  "$ROOT/Scripts/Unix/package-editor.sh" "$ROOT/Scripts/Unix/package-hub.sh" \
  "$ROOT/Scripts/Unix/package-installer.sh" "$ROOT/Scripts/Unix/package-hub-installer.sh" \
  "$ROOT/Scripts/Unix/coverage.sh"
assert_true grep -F -q 'install_logical_packages curl-dev' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'DOTNET_LINUX_X86_64_SHA512' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'pattern="${3:-*}"' "$ROOT/Scripts/Unix/copy-files-if-changed.sh"
assert_true grep -F -q '                   workerRuntimeDirectory' "$ROOT/KeireAssetWorker/premake5.lua"
assert_false grep -F -q 'workerRuntimeDirectory .. " '\''*'\''"' "$ROOT/KeireAssetWorker/premake5.lua"
assert_true grep -F -q 'validate_unix_asset_worker_ninja_commands "$ROOT" "$PROJECT_NAMESPACE"' \
  "$ROOT/Scripts/Linux/generate.sh" "$ROOT/Scripts/Mac/generate.sh"
assert_true grep -F -q '$(build_parallel_jobs)' "$ROOT/Scripts/Linux/bootstrap.sh" \
  "$ROOT/Scripts/Linux/build.sh" "$ROOT/Scripts/Unix/dependencies.sh" "$ROOT/Scripts/Unix/coral.sh" \
  "$ROOT/Scripts/Unix/ffmpeg.sh" "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q 'stage_unix_asset_worker_runtime "$ROOT" "$CONFIGURATION" linux' \
  "$ROOT/Scripts/Linux/build.sh"
assert_true grep -F -q 'stage_unix_asset_worker_runtime "$ROOT" "$CONFIGURATION" macosx' \
  "$ROOT/Scripts/Mac/build.sh"
assert_true grep -F -q 'pinned_dotnet_sdk_root "$dotnet_path" "$dotnet_sdk_version"' \
  "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -F -q '"-DDOTNET_EXE=$dotnet_executable"' "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -F -q '"$dotnet_sdk_version" "$dotnet_root" "$macos_deployment_target"' \
  "$ROOT/Scripts/Unix/coral.sh"
assert_equal "$(grep -F -c 'DOTNET_ROOT="$dotnet_root" PATH="$dotnet_root:$PATH"' \
  "$ROOT/Scripts/Unix/coral.sh")" 2 'Coral pinned .NET configure and build environment'
assert_true grep -F -q 'ubuntu-22.04 ubuntu-24.04 ubuntu-26.04 debian-12 fedora arch tumbleweed rocky-9' \
  "$ROOT/Scripts/Tests/test-linux-distros.sh"
assert_true test -f "$ROOT/Scripts/setup-linux.sh"
assert_true bash -n "$ROOT/Scripts/setup-linux.sh"
assert_true bash -n "$ROOT/Scripts/Tests/test-linux-distros.sh"
assert_true bash "$ROOT/Scripts/setup-linux.sh" --help
assert_true grep -F -q 'TOOLSET=gcc' "$ROOT/Scripts/setup-linux.sh"
assert_true grep -F -q 'Native compiler toolset (default: gcc)' "$ROOT/Scripts/setup-linux.sh"
assert_true grep -F -q 'for candidate in apt-get dnf pacman zypper' "$ROOT/Scripts/setup-linux.sh"
assert_true grep -F -q 'bash "$ROOT/Scripts/project.sh" bootstrap' "$ROOT/Scripts/setup-linux.sh"
assert_true grep -F -q 'bash "$ROOT/Scripts/project.sh" doctor' "$ROOT/Scripts/setup-linux.sh"
assert_true grep -F -q 'bash "$ROOT/Scripts/project.sh" test' "$ROOT/Scripts/setup-linux.sh"
assert_true grep -F -q 'container_command=(bash Scripts/setup-linux.sh --generator ninja --toolset gcc)' \
  "$ROOT/Scripts/Tests/test-linux-distros.sh"
assert_true grep -F -q -- '--volume "$cache_prefix-build:/work/Build"' \
  "$ROOT/Scripts/Tests/test-linux-distros.sh"
assert_true grep -F -q 'building the pinned source' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'PREMAKE_LINUX_SOURCE_SHA256' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'NINJA_SOURCE_SHA256' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'NASM_SOURCE_SHA256' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'PATCHELF_SOURCE_SHA256' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'distro repositories did not provide Ninja 1.11 or newer' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'CMAKE_LINUX_X86_64_SHA256' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'distro repositories did not provide CMake 3.24 or newer' "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'have g++ && actual="$(g++ -dumpfullversion -dumpversion 2>/dev/null)"' \
  "$ROOT/Scripts/Linux/bootstrap.sh"
assert_true grep -F -q 'export PATH="$ROOT/Tools/Linux:$PATH"' "$ROOT/Scripts/project.sh" \
  "$ROOT/Scripts/Linux/bootstrap.sh" "$ROOT/Scripts/Linux/generate.sh" "$ROOT/Scripts/Linux/build.sh"
assert_true grep -F -q -- '--smoke-window) SMOKE_WINDOW=1' "$ROOT/Scripts/project.sh"
assert_true grep -F -q -- 'bash Scripts/project.sh run "${project_arguments[@]}" --ci --smoke-window' \
  "$ROOT/Scripts/Tests/test-linux-distros.sh"
assert_true grep -F -q -- 'matrix_arguments+=(--ci)' "$ROOT/Scripts/Tests/test-linux-distros.sh"
assert_true grep -F -q -- '-DCMAKE_INSTALL_BINDIR=. -DCMAKE_INSTALL_LIBDIR=.' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q -- '-DSPIRV_WERROR=OFF' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q 'export CC=clang CXX=clang++' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q 'flat-runtime-v4' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q 'target_compile_options(zlibstatic PRIVATE -UTARGET_OS_MAC)' \
  "$ROOT/Scripts/Dependencies/CMakeLists.txt"
assert_true grep -F -q 'readelf -d "$published_compiler"' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q "install_name_tool -add_rpath '@executable_path'" "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q 'cp -L "$runtime_library"' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q '"$published_compiler" --help' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_false grep -F -q 'for (index =' "$ROOT/Scripts/Unix/builtin-skinning.sh" \
  "$ROOT/Scripts/Unix/builtin-vfx.sh" "$ROOT/Scripts/Unix/builtin-occlusion.sh"
assert_true grep -F -q 'for (field =' "$ROOT/Scripts/Unix/builtin-skinning.sh"
assert_true grep -F -q 'for (field =' "$ROOT/Scripts/Unix/builtin-vfx.sh"
assert_true grep -F -q 'for (field =' "$ROOT/Scripts/Unix/builtin-occlusion.sh"
while IFS= read -r source_file; do
  if grep -E -q 'std::ranges::(all_of|any_of|none_of|find|find_if|sort|stable_sort|count|count_if|equal|transform|for_each|min_element|max_element|clamp)' "$source_file"; then
    assert_true grep -F -q '#include <algorithm>' "$source_file"
  fi
  if grep -E -q 'std::(memcpy|memmove|memset|memcmp|strlen|strcmp|strchr|strerror)' "$source_file"; then
    assert_true grep -F -q '#include <cstring>' "$source_file"
  fi
  if grep -E -q 'std::(round|floor|ceil|trunc|sqrt|pow|sin|cos|tan|asin|acos|atan|atan2|abs|fabs|fmod|isfinite|isnan|isinf|exp|log|log2|log10)[[:space:]]*\(' "$source_file"; then
    assert_true grep -F -q '#include <cmath>' "$source_file"
  fi
done < <(find "$ROOT/AssetTool" "$ROOT/KeireAssetWorker" "$ROOT/KeireClient" "$ROOT/KeireCore" \
  "$ROOT/KeireEditorTests" "$ROOT/KeireHub" "$ROOT/KeireHubRuntime" "$ROOT/KeireHubTests" \
  "$ROOT/KeireHubWorker" "$ROOT/KeireRuntime" "$ROOT/KeireTests" -type f \( -name '*.cpp' -o -name '*.h' \))
assert_true grep -q 'libsodium.*configure' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'LIBSODIUM_COMMIT' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q 'generated_content_copy_file_if_changed "$sodium_runtime" "$target_directory/libsodium.so" "$ROOT"' "$ROOT/Scripts/Linux/build.sh"
assert_true grep -F -q 'generated_content_copy_file_if_changed "$sodium_runtime" "$target_directory/libsodium.dylib" "$ROOT"' "$ROOT/Scripts/Mac/build.sh"
assert_true grep -F -q '"$ROOT/Scripts/Unix/dependencies.sh" Linux "$ARCHITECTURE" "$TOOLSET" 0' "$ROOT/Scripts/Linux/generate.sh"
assert_true grep -F -q '"$ROOT/Scripts/Unix/dependencies.sh" Mac "$ARCHITECTURE" "$TOOLSET" 0' "$ROOT/Scripts/Mac/generate.sh"
assert_false grep -q 'force_dependencies=' "$ROOT/Scripts/Linux/generate.sh" "$ROOT/Scripts/Mac/generate.sh"
assert_true grep -F -q '"$runtime_staging_target" == "$CLIENT_TARGET"' "$ROOT/Scripts/Linux/build.sh"
assert_true grep -F -q '"$runtime_staging_target" == "$CLIENT_TARGET"' "$ROOT/Scripts/Mac/build.sh"
assert_true grep -q 'DependencyManifest.SodiumDebugRuntime' "$ROOT/KeireHub/premake5.lua"
coral_bootstrap_patch="$ROOT/Patches/Coral/0004-keire-apply-host-settings-before-discovery.patch"
settings_line="$(grep -n 'm_Settings = std::move(InSettings);' "$coral_bootstrap_patch" | head -n 1 | cut -d: -f1)"
discovery_line="$(grep -n 'if (!LoadHostFXR())' "$coral_bootstrap_patch" | head -n 1 | cut -d: -f1)"
assert_true test -n "$settings_line"
assert_true test -n "$discovery_line"
assert_true test "$settings_line" -lt "$discovery_line"
coral_warning_patch="$ROOT/Patches/Coral/0005-keire-warning-clean-native-host.patch"
assert_true grep -F -q 'memcpy(buffer, InString.data(), InString.size() * sizeof(UCChar))' "$coral_warning_patch"
assert_true grep -F -q 'buffer[InString.size()] = {};' "$coral_warning_patch"
assert_true grep -F -q 'reinterpret_cast<const UCChar*>(UINTPTR_MAX)' "$coral_warning_patch"
assert_true grep -F -q 'target_compile_options(Coral.Native PRIVATE /wd4996)' "$coral_warning_patch"
assert_true grep -F -q 'PERL5LIB="$ROOT/Scripts/Dependencies${PERL5LIB:+:$PERL5LIB}"' \
  "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q 'bison_prefix="$(brew --prefix bison 2>/dev/null || true)"' \
  "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q 'export PATH="$bison_prefix/bin:$PATH"' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -F -q 'version_at_least "$host_bison_version" 3.0' "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true env PERL5LIB="$ROOT/Scripts/Dependencies" perl -MJSON -e \
  'die unless decode_json("{\"ready\":true}")->{ready}'
assert_true grep -q 'SDL3DebugLibrary' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -q 'SDL3ReleaseLibrary' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'filter "configurations:Profile"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q '"KEIRE_PROFILE_TELEMETRY"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q '"TRACY_ON_DEMAND"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q '"TRACY_ONLY_LOCALHOST"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'local directory, library = resolved:match("^(.*)/(lib[^/]+%.a)$")' \
  "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'return ":" .. library' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'os.host() ~= "macosx"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'os.host() == "macosx"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'linkoptions { '\''"'\'' .. resolved .. '\''"'\'' }' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'filter { "system:linux", "toolset:gcc or clang" }' \
  "$ROOT/Scripts/Premake/Common.lua"
assert_false grep -F -q 'filter { "system:linux or macosx", "toolset:gcc or clang" }' \
  "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'linkoptions { "-Wl,-rpath,@executable_path" }' \
  "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'local function LinkCoralNetHost()' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'LinkDependency(DependencyManifest.CoralNetHostRuntime)' \
  "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'filter { "system:windows or linux" }' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'macRuntimeDirectory .. "/libnethost.dylib"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'CoralNetHostRuntime = "../Build/Dependencies/coral-nethost/$nethost_runtime_name"' \
  "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q '"UserNotifications.framework", "Security.framework"' \
  "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q 'buildoptions { "-Wno-error=tsan" }' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'LinkDependencies(DependencyManifest.RecastDebugLibraries)' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'LinkDependency(DependencyManifest.SDL3DebugLibrary)' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -q 'project_generation_fingerprint' "$ROOT/Scripts/Linux/generate.sh"
assert_true grep -F -q 'invalidate_incompatible_binary_outputs "$ROOT" linux' \
  "$ROOT/Scripts/Linux/generate.sh"
assert_true grep -F -q 'invalidate_incompatible_binary_outputs "$ROOT" macosx' \
  "$ROOT/Scripts/Mac/generate.sh"
assert_true grep -F -q '> "$ROOT/Build/Generated/ninja.stamp"' "$ROOT/Scripts/Linux/generate.sh"
assert_true grep -F -q '> "$ROOT/Build/Generated/ninja.stamp"' "$ROOT/Scripts/Mac/generate.sh"
assert_true grep -F -q 'invalidate_incompatible_binary_outputs()' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'rm -rf -- "$resolved_path"' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'project_generation_premake_inputs "$root" | LC_ALL=C sort' \
  "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q -- '\) -prune \) -o -type f -name '\''premake5.lua'\'' -print' \
  "$ROOT/Scripts/Unix/common.sh"
assert_true grep -q 'KeireHubRuntime KeireHubTests KeireHubWorker' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -q 'find "$root/Scripts/Premake" -type f -name '\''\*.lua'\''' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'Scripts/Unix/dependencies.sh Scripts/Windows/dependencies.ps1' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'find "$root/Scripts/Dependencies" -type f -print' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -q 'run_args=(--project "$ROOT/Samples/KeireSandbox" --smoke-project)' "$ROOT/Scripts/Unix/coverage.sh"
assert_true grep -q 'minimum_core_line_coverage=74.5' "$ROOT/Scripts/Unix/coverage.sh"
assert_true grep -q 'minimum_aggregate_line_coverage=63.0' "$ROOT/Scripts/Unix/coverage.sh"
assert_true grep -q 'project(DearImGuiProject)' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'kind "StaticLib"' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'targetname(DearImGuiLibrary)' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'imgui_impl_sdl3.cpp' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'imgui_impl_sdlgpu3.cpp' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'imgui_stdlib.cpp' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'warnings "Off"' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q '../../Build/Projects/DearImGui' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'group "Dependencies"' "$ROOT/premake5.lua"
assert_true grep -q 'Scripts/Premake/DearImGui.lua' "$ROOT/premake5.lua"
assert_true grep -q 'Scripts/Premake/HeaderDependencies.lua' "$ROOT/premake5.lua"
assert_true grep -q 'project(EnTTProject)' "$ROOT/Scripts/Premake/HeaderDependencies.lua"
assert_true grep -q 'project(GLMProject)' "$ROOT/Scripts/Premake/HeaderDependencies.lua"
assert_true grep -q '../../Build/Projects/EnTT' "$ROOT/Scripts/Premake/HeaderDependencies.lua"
assert_true grep -q '../../Build/Projects/GLM' "$ROOT/Scripts/Premake/HeaderDependencies.lua"
assert_true grep -q 'links { DearImGuiProject, ZstdProject }' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'VendorIncludeDirs.entt' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'VendorIncludeDirs.glm' "$ROOT/KeireCore/premake5.lua"
assert_false grep -F -q 'dependson { EnTTProject, GLMProject }' "$ROOT/KeireCore/premake5.lua"
assert_true grep -F -q 'pchheader "KeireInternal/KeireCorePch.h"' "$ROOT/KeireCore/premake5.lua"
assert_true grep -F -q 'buildoptions { "-include KeireInternal/KeireCorePch.h" }' "$ROOT/KeireCore/premake5.lua"
assert_true grep -F -q 'removebuildoptions { "/MP" }' "$ROOT/KeireCore/premake5.lua"
assert_false grep -F -q 'buildoptions { "/MP1" }' "$ROOT/KeireCore/premake5.lua"
assert_true grep -F -q 'CoreArchiveTargets' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'linkgroups "On"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'premake.override(ninjaCpp, "linkrule"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'return "rm -f $out && " .. command' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'del /F /Q \"$out\" & if exist \"$out\" exit /B 1' \
  "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'filter { "action:ninja", "system:linux or macosx" }' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'enablepch "Off"' "$ROOT/Scripts/Premake/Common.lua"
for core_archive in Assets Build World Rendering Scenes Scripting Ui Vfx; do
  assert_true test -f "$ROOT/KeireCore/Source/Pch/KeireCore${core_archive}Pch.cpp"
done
assert_true grep -F -q 'pchheader "KeireClient/ClientPch.h"' "$ROOT/KeireClient/premake5.lua"
assert_true grep -F -q 'buildoptions { "-include KeireClient/ClientPch.h" }' "$ROOT/KeireClient/premake5.lua"
assert_false grep -F -q 'dependson { AssetWorkerTarget, AssetToolTarget, RuntimeTarget }' "$ROOT/KeireClient/premake5.lua"
assert_true grep -F -q 'dependson { ProjectConfig.CLIENT_TARGET, AssetToolTarget, RuntimeTarget }' \
  "$ROOT/Scripts/Premake/EditorDev.lua"
assert_true grep -q 'Source/ECS/Components/CameraComponent.cpp' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'Source/ECS/Components/MeshRendererComponent.cpp' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'builtin-shaders.sh' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'touch-ninja-stamp.ps1' "$ROOT/KeireCore/premake5.lua"
assert_false grep -q 'touch-ninja-stamp.ps1' "$ROOT/KeireAssetWorker/premake5.lua"
assert_true test -f "$ROOT/Scripts/Windows/touch-ninja-stamp.ps1"
assert_true test -f "$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl"
assert_true grep -R -q 'BuiltinUnlitShaders.h' "$ROOT/KeireCore/Source/Rendering"
assert_true grep -F -q 'builtin-occlusion.sh' "$ROOT/KeireCore/premake5.lua"
assert_true test -f "$ROOT/KeireCore/Shaders/BuiltinOcclusionDepth.hlsl"
assert_true test -f "$ROOT/KeireCore/Shaders/BuiltinOcclusionDebugPyramid.hlsl"
assert_true test -f "$ROOT/KeireCore/Shaders/BuiltinOcclusionDebugBounds.hlsl"
assert_true grep -R -q 'renderer->Tint()' "$ROOT/KeireCore/Source/Rendering"
assert_true grep -R -q 'ResolveLighting' "$ROOT/KeireCore/Source/Rendering"
assert_true grep -R -q 'DirectionalLightComponent' "$ROOT/KeireCore/Source/Rendering"
assert_true grep -R -q 'AmbientAndExposure' "$ROOT/KeireCore/Source/Rendering" \
  "$ROOT/KeireCore/Include/KeireInternal/Rendering"
assert_true grep -q 'LightDirection' "$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl"
assert_true grep -q 'worldNormal' "$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl"
assert_true grep -R -q 'ReadbackRGBA8' "$ROOT/KeireCore/Source/Rendering"
assert_true test -f "$ROOT/KeireRenderTests/Source/RenderedOutputTests.cpp"
assert_true grep -q 'backends=(vulkan)' "$ROOT/Scripts/Unix/run-target.sh"
assert_true grep -q 'backends=(metal)' "$ROOT/Scripts/Unix/run-target.sh"
assert_true grep -R -q 'BuiltinShaderUniformBufferCount(vertex)' "$ROOT/KeireCore/Source/Rendering"
assert_true grep -R -q 'SDL_PushGPUFragmentUniformData' "$ROOT/KeireCore/Source/Rendering"
assert_true test "$(wc -l < "$ROOT/KeireCore/Source/Rendering/RenderSystem.cpp" | tr -d ' ')" -lt 700
assert_true test "$(wc -l < "$ROOT/KeireCore/Source/Assets/AssetPipeline.cpp" | tr -d ' ')" -lt 600
assert_false grep -q 'recursive_mutex' "$ROOT/KeireCore/Include/KeireInternal/Assets/AssetDatabaseImplementation.h"
assert_true grep -q 'Rendering.keiresettings' "$ROOT/KeireCore/Source/Rendering/RenderSettings.cpp"
assert_true test -f "$ROOT/Samples/KeireSandbox/ProjectSettings/Rendering.keiresettings"
assert_false grep -R -q 'Vendor/SDL/test' "$ROOT/KeireCore/Source/Rendering"
assert_true grep -q 'SDL_GetBasePath()' "$ROOT/KeireCore/Source/Assets/RenderingAssets.cpp"
assert_true grep -q 'ImGuiDragDropFlags_SourceAllowNullID' "$ROOT/KeireCore/Source/Ui.cpp"
assert_true test -f "$ROOT/KeireClient/Source/Editor/SceneGizmoController.cpp"
assert_true grep -q '"/select," + utf8Path' "$ROOT/KeireCore/Source/Process.cpp"
assert_false grep -q 'imgui.cpp' "$ROOT/KeireCore/premake5.lua"
assert_false grep -q 'AddDearImGuiSources' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -q 'LinkKeireCore()' "$ROOT/KeireClient/premake5.lua"
assert_true grep -q 'LinkKeireCore()' "$ROOT/KeireTests/premake5.lua"
assert_true grep -q 'AddKeireManagedRuntimeDependency()' "$ROOT/KeireClient/premake5.lua"
assert_true grep -q 'AddKeireManagedRuntimeDependency()' "$ROOT/KeireTests/premake5.lua"
assert_true grep -q 'AddKeireManagedHostStaging()' "$ROOT/KeireClient/premake5.lua"
assert_true grep -q 'AddKeireManagedRuntimeDependency()' "$ROOT/AssetTool/premake5.lua"
assert_true grep -q 'AddKeireManagedHostStaging()' "$ROOT/AssetTool/premake5.lua"
assert_true grep -q 'specification.RuntimeHostDirectory = managedHost' "$ROOT/AssetTool/Source/Main.cpp"
assert_true grep -q 'specification.RuntimeRootDirectory = managedHost / "Dotnet"' "$ROOT/AssetTool/Source/Main.cpp"
assert_true grep -q 'Scripts/Unix/stage-managed-host.sh' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -F -q 'copy_tree_if_changed "$core_runtime_directory" "$bundled_runtime"' \
  "$ROOT/Scripts/Unix/stage-managed-host.sh"
assert_equal "$(managed_host_staging_targets KeireClient KeireClient KeireHub Keire | tr '\n' ',')" \
  'KeireAssetTool,KeireRuntime,KeireClient,' \
  'Editor builds refresh managed hosts for their executable dependencies'
assert_equal "$(managed_host_staging_targets KeireHub KeireClient KeireHub Keire | tr '\n' ',')" \
  'KeireAssetTool,KeireRuntime,KeireClient,KeireHub,' \
  'Hub builds refresh managed hosts for the editor dependency chain'
assert_equal "$(managed_host_staging_targets KeireEditorDev KeireClient KeireHub Keire | tr '\n' ',')" \
  'KeireAssetTool,KeireRuntime,KeireClient,' \
  'Complete editor aggregates refresh managed hosts without staging their proxy target'
assert_true grep -q 'dependson { KeireManagedProject }' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'links { KeireManagedProject }' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'kind "StaticLib"' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'kind "Utility"' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'ManagedBuildAnchor.cpp' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'ProjectConfig.PROJECT_NAMESPACE .. "ManagedRuntimeApi"' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'externalanglebrackets "On"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -q 'externalwarnings "Off"' "$ROOT/Scripts/Premake/Common.lua"
assert_false grep -q '"/external:W0"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'objdir ("../../Build/Intermediates/"' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -F -q 'IntermediateOutputDir = OutputDir .. "-" .. SelectedToolset' "$ROOT/premake5.lua"
assert_true grep -F -q '/Build/Intermediates/" .. IntermediateOutputDir' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q '/Build/Intermediates/" .. IntermediateOutputDir' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -F -q '/Build/Bin/" .. OutputDir' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -q 'addManagedBuildInput(managedSourceRoot)' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'os.matchdirs' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'buildinputs(managedBuildInputs)' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'buildoutputs { managedOutput }' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'linkbuildoutputs "Off"' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -q 'Scripts/Unix/build-managed.sh' "$ROOT/Scripts/Premake/Managed.lua"
assert_true grep -F -q -- '-newer "$assembly"' "$ROOT/Scripts/Unix/build-managed.sh"
assert_true test -f "$ROOT/Scripts/Premake/ManagedBuildAnchor.cpp"
assert_false grep -q 'Scripts/Unix/build-managed.sh' "$ROOT/Scripts/Linux/build.sh"
assert_false grep -q 'Scripts/Unix/build-managed.sh' "$ROOT/Scripts/Mac/build.sh"
assert_false grep -q 'Scripts/Unix/build-info.sh' "$ROOT/Scripts/Linux/build.sh"
assert_false grep -q 'Scripts/Unix/build-info.sh' "$ROOT/Scripts/Mac/build.sh"
assert_true grep -q 'resolve_compiler_cache' "$ROOT/Scripts/Linux/build.sh"
assert_true grep -q 'PROFILE_BUILD' "$ROOT/Scripts/Linux/build.sh"
assert_true grep -q 'KeireManaged KeireManaged.Tests SourceModules Scripts/Premake' "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q -- "-name '*.csproj'" "$ROOT/Scripts/Unix/common.sh"
assert_true grep -F -q 'dependson { AssetWorkerTarget }' "$ROOT/AssetTool/premake5.lua"
assert_true grep -F -q 'AddKeireManagedRuntimeDependency()' "$ROOT/KeireRuntime/premake5.lua"
assert_true grep -F -q 'AddKeireManagedHostStaging()' "$ROOT/KeireRuntime/premake5.lua"
assert_true grep -F -q 'filter { "system:linux"' "$ROOT/KeireAssetWorker/premake5.lua"
assert_true grep -F -q '"-Wl,-rpath,'\''$$ORIGIN'\''"' "$ROOT/KeireAssetWorker/premake5.lua"
assert_true grep -q 'SelectedToolset ~= "msc"' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -F -q 'filter { "system:macosx"' "$ROOT/KeireAssetWorker/premake5.lua"
assert_true grep -F -q '"-Wl,-rpath,@loader_path"' "$ROOT/KeireAssetWorker/premake5.lua"
assert_false grep -F -q '"system:linux or macosx"' "$ROOT/KeireAssetWorker/premake5.lua"
assert_true grep -F -q -- '--install-name-dir=@rpath' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'git -C "$VENDOR_SOURCE" archive --format=tar "$COMMIT"' \
  "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q -- '--enable-zlib' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q -- '--enable-decoder=exr' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q '#define CONFIG_EXR_DECODER 1' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'ffmpeg.sh" Debug "$base/Release" "$platform" "$architecture" "$toolset"' \
  "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -F -q 'ffmpeg-cache/$SYSTEM-$OUTPUT_ARCHITECTURE-$TOOLSET' \
  "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'publish_ffmpeg_output()' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'require_ffmpeg_output_path "$CACHE_OUTPUT" "$CACHE_BASE"' \
  "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'resolved_root="$(cd -P "$ROOT" && pwd -P)"' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'prefix=${pcfiledir}/../..' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'Adopted private FFmpeg' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'valid_ffmpeg_component_artifacts()' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'for component in avformat avcodec swresample avutil' \
  "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'lib$component.dylib' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'lib$component.so' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'install/share/licenses/ffmpeg/COPYING.LGPLv2.1' \
  "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'install/share/licenses/ffmpeg/COPYING.LGPLv3' \
  "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'install/share/licenses/ffmpeg/SOURCE.txt' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -F -q 'receiveStatus == AVERROR(EAGAIN)' \
  "$ROOT/KeireAssetWorker/Source/FfmpegTextureImportBackend.cpp"
assert_true grep -F -q 'frame.format == AV_PIX_FMT_GRAYF16' \
  "$ROOT/KeireAssetWorker/Source/FfmpegTextureImportBackend.cpp"
assert_true grep -F -q 'FileName = "avformat-63.dll"' "$ROOT/Scripts/Windows/ffmpeg-runtime-contract.ps1"
assert_true grep -F -q 'FileName = "avcodec-63.dll"' "$ROOT/Scripts/Windows/ffmpeg-runtime-contract.ps1"
assert_true grep -F -q 'FileName = "swresample-7.dll"' "$ROOT/Scripts/Windows/ffmpeg-runtime-contract.ps1"
assert_true grep -F -q 'FileName = "avutil-61.dll"' "$ROOT/Scripts/Windows/ffmpeg-runtime-contract.ps1"
assert_true grep -F -q '"bin\$($component.Component).lib"' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_false grep -F -q 'lib\avformat.lib' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q '$Toolset -eq "gcc"' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q 'do not support the gcc toolset' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q 'Enter-WindowsToolEnvironment "vs2022" "msc" $Architecture' \
  "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q 'if (Test-FfmpegOutput $AlternateOutput $AlternateExpected)' \
  "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q '$AlternateConfiguration = if ($Configuration -eq "Debug")' \
  "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q 'Reused the identical private FFmpeg' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q '$forceFfmpegSourceBuild = $false' "$ROOT/Scripts/Windows/dependencies.ps1"
assert_true grep -F -q 'ffmpeg-cache\windows-$OutputArchitecture-msc-producer-$Toolset' \
  "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q 'function Publish-FfmpegOutput' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q 'function Assert-FfmpegOutputPath' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q '[IO.FileAttributes]::ReparsePoint' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q 'prefix=${pcfiledir}/../..' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q 'Adopted private FFmpeg' "$ROOT/Scripts/Windows/ffmpeg.ps1"
assert_true grep -F -q -- '-Architecture $Architecture' "$ROOT/Scripts/Windows/dependencies.ps1"
assert_true grep -F -q -- '-Toolset $Toolset' "$ROOT/Scripts/Windows/dependencies.ps1"
assert_true test -z "$(find "$ROOT/KeireCore/Source" "$ROOT/KeireClient/Source" "$ROOT/KeireHub/Source" "$ROOT/KeireTests/Source" "$ROOT/AssetTool/Source" "$ROOT/KeireRuntime/Source" -type f -name '*.h' -print -quit)"
assert_true test -f "$ROOT/KeireCore/Include/Keire/Assets/Asset.h"
assert_true test -f "$ROOT/KeireCore/Source/Assets/AssetSystem.cpp"
assert_true test -f "$ROOT/KeireCore/Include/Keire/Input/Input.h"
assert_true test -f "$ROOT/KeireCore/Source/Input/InputSystem.cpp"
assert_true test -f "$ROOT/Samples/KeireSandbox/Assets/Input/DefaultInput.keireinput"
assert_true test -f "$ROOT/KeireCore/Include/Keire/Project/Project.h"
assert_true test -f "$ROOT/KeireCore/Include/Keire/Scenes/SceneSystem.h"
assert_true test -f "$ROOT/KeireHub/Source/HubApplication.cpp"
assert_true grep -F -q 'links { HubRuntimeTarget }' "$ROOT/KeireHub/premake5.lua"
assert_false grep -F -q 'dependson { ProjectConfig.CLIENT_TARGET }' "$ROOT/KeireHub/premake5.lua"
assert_true test -f "$ROOT/KeireClient/Source/Editor/AssetBrowserPanel.cpp"
assert_true test -f "$ROOT/KeireClient/Source/Editor/ConsolePanel.cpp"
assert_true test -f "$ROOT/KeireClient/Source/Editor/DiagnosticsPanel.cpp"
assert_true test -f "$ROOT/KeireClient/Source/Editor/ThumbnailService.cpp"
workspace_lines="$(wc -l < "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp" | tr -d ' ')"
assert_true test "$workspace_lines" -lt 1500
assert_false grep -E -q 'Storage\(\)|friend[[:space:]]+class[[:space:]]+::EditorWorkspaceLayer' \
  "$ROOT/KeireClient/Include/KeireClient/Editor/SceneDocument.h" \
  "$ROOT/KeireClient/Include/KeireClient/Editor/InputActionsDocument.h"
assert_true test -f "$ROOT/KeireClient/Source/Editor/HierarchyPanel.cpp"
for panel in HierarchyPanel InspectorPanel AssetInspectorPanel SceneViewportPanel InputActionsPanel ProjectSettingsPanel AssetBrowserPanel; do
  assert_true test -f "$ROOT/KeireClient/Source/Editor/${panel}.cpp"
done
assert_true grep -q 'class AssetInspectorPanel final' \
  "$ROOT/KeireClient/Include/KeireClient/Editor/EditorPanels.h"
assert_true grep -q 'AssetInspectorPanel::Draw' "$ROOT/KeireClient/Source/Editor/AssetInspectorPanel.cpp"
assert_false grep -E -q 'if[[:space:]]*\(auto[[:space:]]+[[:alnum:]_]+[[:space:]]*=[[:space:]]*ui\.BeginPanel\([^;]+;[[:space:]]*![[:alnum:]_]+\)' \
  "$ROOT/KeireClient/Source/Editor/HierarchyPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/InspectorPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/AssetInspectorPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/SceneViewportPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/InputActionsPanel.cpp"
assert_false grep -R -q '#include "KeireClient/EditorWorkspaceLayer.h"' \
  "$ROOT/KeireClient/Source/Editor/HierarchyPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/InspectorPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/AssetInspectorPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/SceneViewportPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/InputActionsPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/ProjectSettingsPanel.cpp" \
  "$ROOT/KeireClient/Source/Editor/AssetBrowserPanel.cpp"
assert_false grep -E -q 'friend[[:space:]]+class[[:space:]]+KeireEditor::|Draw(Scene|Hierarchy|Inspector)Content' \
  "$ROOT/KeireClient/Include/KeireClient/EditorWorkspaceLayer.h"
assert_true grep -q 'DisplayName(record.RelativePath)' "$ROOT/KeireClient/Source/Editor/AssetBrowserPanel.cpp"
assert_true grep -q 'TrashRecords()' "$ROOT/KeireClient/Source/Editor/AssetBrowserPanel.cpp"
assert_true grep -q 'class KEIRE_API UndoService' "$ROOT/KeireCore/Include/Keire/Undo.h"
assert_true grep -q 'CreateSystemTray' "$ROOT/KeireHub/Source/HubApplication.cpp"
assert_true grep -q 'Show Hub' "$ROOT/KeireHub/Source/HubApplication.cpp"
assert_true grep -q 'PollActivation' "$ROOT/KeireHub/Source/HubApplication.cpp"
assert_true grep -q 'HubInstanceCoordinator' "$ROOT/KeireHub/Source/HubInstance.cpp"
assert_true grep -q 'AddKeireApplicationIcon' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -q "windows-resource-update" "$ROOT/Scripts/Windows/player-support.ps1"
assert_true grep -q 'create-build-support' "$ROOT/Scripts/Unix/player-support.sh"
assert_true grep -q 'signature_key_id' "$ROOT/Scripts/Unix/player-support.sh"
assert_true grep -q -- '--manifest-output' "$ROOT/Scripts/Unix/player-support.sh"
for package_script in package.sh package-editor.sh package-hub.sh package-installer.sh package-hub-installer.sh; do
  assert_true grep -Fqx 'umask 0022' "$ROOT/Scripts/Unix/$package_script"
done
assert_true test -f "$ROOT/Config/Branding/Keire.ico"
assert_true test -f "$ROOT/Config/Branding/Keire.res"
assert_true python3 -c \
  'import json, sys; assert int(json.load(open(sys.argv[1], encoding="utf-8"))["schemaVersion"]) >= 2' \
  "$ROOT/Samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene"
assert_true grep -q '"components"' "$ROOT/Samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene"
assert_true test -z "$(find "$ROOT/Samples/KeireSandbox/Assets" -type f \
  \( -name '*.tmp.*.keiremeta' -o -name '*~.keiremeta' \) -print -quit)"
sample_audio="$ROOT/Samples/KeireSandbox/Assets/Audio"
assert_true test -f "$sample_audio/InterfaceConfirm.wav"
assert_true test -f "$sample_audio/SpatialEmitter.wav"
assert_equal "$(find "$sample_audio" -maxdepth 1 -type f -name '*.wav' | wc -l | tr -d ' ')" 2 \
  'repository-owned sample audio sources'
for wave_file in "$sample_audio/InterfaceConfirm.wav" "$sample_audio/SpatialEmitter.wav"; do
  assert_true python3 -c \
    'import pathlib, sys; header = pathlib.Path(sys.argv[1]).read_bytes()[:12]; assert header[:4] == b"RIFF" and header[8:] == b"WAVE"' \
    "$wave_file"
done
assert_true grep -q 'f42ee69b-dc11-4212-ae66-17bff0be7945' "$sample_audio/InterfaceConfirm.wav.keiremeta"
assert_true grep -q 'd09e3f28-06e6-49eb-a714-0261348f5eee' "$sample_audio/SpatialEmitter.wav.keiremeta"
assert_true test -z "$(find "$ROOT/Samples/KeireSandbox/Assets" -type f \
  \( -name '*.mp4' -o -name '*.mkv' -o -name '*.webm' \) -print -quit)"
humanoid_model="$ROOT/Samples/KeireSandbox/Assets/Meshes/T-Pose.fbx"
humanoid_animation="$ROOT/Samples/KeireSandbox/Assets/Meshes/Idle.fbx"
assert_true test -f "$humanoid_model"
assert_true test -f "$humanoid_animation"
duplicate_fbx_hashes="$(
  while IFS= read -r -d '' source; do
    sha256_file "$source"
  done < <(find "$ROOT/Samples/KeireSandbox/Assets" -type f -name '*.fbx' -print0) |
    sort | uniq -d | wc -l | tr -d ' '
)"
assert_equal "$duplicate_fbx_hashes" 0 'duplicate FBX content'
assert_true grep -q '51cd8956-a6c4-4d63-b990-7d86829f92ff' "$humanoid_model.keiremeta"
assert_true grep -q '"contentType": "model"' "$humanoid_model.keiremeta"
assert_true grep -q 'c8bf2eaf-9146-5b53-85c8-c3e6dc9b8f08' "$humanoid_model.keiremeta"
assert_true grep -q '78c8dbe3-2951-54b9-b34e-9221c49c506b' "$humanoid_model.keiremeta"
assert_true grep -q '"mesh": "51cd8956-a6c4-4d63-b990-7d86829f92ff"' "$ROOT/Samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene"
assert_true grep -q '"skeleton": "c8bf2eaf-9146-5b53-85c8-c3e6dc9b8f08"' "$ROOT/Samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene"
assert_true grep -q '"skinnedMesh": "78c8dbe3-2951-54b9-b34e-9221c49c506b"' "$ROOT/Samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene"
assert_true grep -q '51116f66-15b6-4dee-acdf-653223e2f491' "$humanoid_animation.keiremeta"
assert_true grep -q '"contentType": "animation"' "$humanoid_animation.keiremeta"
assert_true grep -q '803c0e5b-d937-521c-821e-92de5a986179' "$humanoid_animation.keiremeta"
assert_true grep -q '"clip": "803c0e5b-d937-521c-821e-92de5a986179"' "$ROOT/Samples/KeireSandbox/Assets/NewAnimatorController.keireanimgraph"
assert_true grep -q '9ec01b6f-4862-443e-8cfd-efa2f23ef04a' "$ROOT/Samples/KeireSandbox/Assets/NewAnimatorController.keireanimgraph.keiremeta"
assert_true grep -q '"graph": "9ec01b6f-4862-443e-8cfd-efa2f23ef04a"' "$ROOT/Samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene"
assert_false grep -E -q 'c506e2a8-62f9-44f0-8831-b66755cc9b9b|070fedd0-9e84-435e-83ae-21b4530159f3' \
  "$ROOT/Samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene"
assert_false grep -R -E '#include[[:space:]]*[<"]imgui|ImGui::|ImGui[A-Z]' "$ROOT/KeireClient"
assert_false grep -R -E 'SDL3/|nlohmann/json|imgui|entt/|glm/|assimp/|stb_image' "$ROOT/KeireCore/Include/Keire"
assert_true grep -q 'client_build_args=.*--target.*CLIENT_TARGET' "$ROOT/Scripts/Unix/run-target.sh"
assert_true grep -F -q 'SDL_VIDEODRIVER=dummy "$client_executable" --smoke-workspace' \
  "$ROOT/Scripts/Unix/run-target.sh"
assert_true grep -F -q 'Keire::ApplicationCommandLineOption{"--smoke-workspace"' \
  "$ROOT/KeireClient/Source/ClientApplication.cpp"
assert_true grep -F -q 'commandLine.SmokeWorkspace ? UiMode::Headless' \
  "$ROOT/KeireClient/Source/ClientApplication.cpp"
assert_true grep -F -q "bash -c 'ulimit -c 0;" "$ROOT/Scripts/Unix/run-target.sh"
assert_true grep -F -q 'Config/LeakSanitizer.supp' "$ROOT/Scripts/Unix/run-target.sh"
assert_true grep -F -q 'leak:libcoreclr.so' "$ROOT/Config/LeakSanitizer.supp"
assert_true grep -F -q 'leak:System.Private.CoreLib.dll' "$ROOT/Config/LeakSanitizer.supp"
assert_false grep -F -q 'detect_leaks=0' "$ROOT/Scripts/Unix/run-target.sh" "$ROOT/Config/LeakSanitizer.supp"
assert_true grep -F -q 'halt_on_error=1:print_stacktrace=1' "$ROOT/Scripts/Unix/run-target.sh"
assert_true grep -F -q '#define STBIR_NO_SIMD' "$ROOT/KeireHubRuntime/Source/ProjectThumbnailDecode.cpp"
assert_true grep -q 'hub_tests_target="${PROJECT_NAMESPACE}HubTests"' "$ROOT/Scripts/Unix/run-target.sh"
assert_true grep -q '"$hub_tests"' "$ROOT/Scripts/Unix/run-target.sh"
assert_false grep -q 'HubInstance.cpp' "$ROOT/KeireEditorTests/premake5.lua"
assert_false grep -q 'HubRuntimeTarget' "$ROOT/KeireEditorTests/premake5.lua"
assert_false grep -q -- '--hub-instance-secondary' "$ROOT/KeireEditorTests/Source/Main.cpp"
assert_true grep -q 'HubInstance.cpp' "$ROOT/KeireHubTests/premake5.lua"
assert_true grep -q -- '--hub-instance-secondary' "$ROOT/KeireHubTests/Source/Main.cpp"
assert_true grep -q 'class KEIRE_API UiWorkspace' "$ROOT/KeireCore/Include/Keire/UiWorkspace.h"
assert_true grep -q 'BuildFactoryLayout' "$ROOT/KeireClient/Source/ClientApplication.cpp"
assert_true grep -q -- '--worker-timeout-seconds' "$ROOT/AssetTool/Source/Main.cpp"
assert_true grep -q -- 'extract-asset-package' "$ROOT/AssetTool/Source/Main.cpp"
assert_true grep -q -- 'ExtractAssetPackageToStaging' "$ROOT/AssetTool/Source/Main.cpp"
for exported_type in \
  Application ApplicationCommandLineArguments CommandLineError EventView EventSubscription EventBus Layer LayerStack \
  LoggerHandle Log Time UiError UiScope UiWindowScope UiChildScope UiMenuBarScope UiMenuScope UiTabBarScope \
  UiTabItemScope UiTreeNodeScope UiDisabledScope UiIdScope UiMainMenuBarScope UiComboScope UiPopupScope UiTableScope \
  UiDragSourceScope UiDragTargetScope UiPanelScope \
  UiFrame UiLayoutBuilder UiPanelRegistration UiWorkspace WindowError Window FolderDialogOperation WindowSystem ConfigurationError \
  Asset BinaryAsset TextAsset AssetLoadError AssetSystem AssetDatabase AssetCooker InputActionAsset \
  InputActionSubscription InputActionHandle InputActionContext InteractiveRebindOperation InputSystem InputCaptureOverride \
  Project ProjectRegistry EntityId ComponentTypeId Component ComponentRegistry Entity TransformComponent DirectionalLightComponent \
  UndoCommand UndoTransaction UndoContext UndoService CameraComponent MeshRendererComponent SceneAsset Scene SceneObjectHandle SceneRuntimeSession SceneLoadOperation SceneSystem \
  UiImage SaveFileDialogOperation SystemTray RenderSurface RenderView RenderSystem ShaderAsset MaterialAsset MeshAsset Texture2DAsset; do
  assert_true grep -R -E -q "class[[:space:]]+KEIRE_API[[:space:]]+$exported_type([^[:alnum:]_]|$)" "$ROOT/KeireCore/Include/Keire"
done
for exported_function in AssertionFailure GetName GetBuildInfo GetVersionString LoadWindowSpecification; do
  assert_true grep -R -E -q "KEIRE_API[^;{}]*${exported_function}[[:space:]]*\\(" "$ROOT/KeireCore/Include/Keire"
done
assert_false grep -R -E -q 'KEIRE_API[^;{}]*(GetApplicationCommandLineDescription|CreateApplication)[[:space:]]*\(' "$ROOT/KeireCore/Include/Keire"
assert_true grep -q 'dear-imgui-LICENSE.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -F -q 'macos_sdl_frameworks=' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -F -q -- '-framework GameController' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -F -q -- '-framework CoreHaptics' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -F -q 'merge-static-libraries.sh' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -F -q 'core_archive_targets=("$CORE_TARGET")' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -F -q 'ar_command="${AR:-ar}"' "$ROOT/Scripts/Unix/merge-static-libraries.sh"
missing_archive_directory="$(mktemp -d)"
missing_archive_output="$missing_archive_directory/merged.a"
assert_false bash "$ROOT/Scripts/Unix/merge-static-libraries.sh" "$missing_archive_output" \
  "$missing_archive_output.missing"
rm -rf "$missing_archive_directory"
assert_true grep -q 'IMGUI_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'lib\$imgui_library.a' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'zstandard-LICENSE.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'ZSTD_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'lib\$zstd_library.a' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'entt-LICENSE.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'ENTT_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'glm-COPYING.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'GLM_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'KeireShaderCompiler' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'SDL-shadercross-LICENSE.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -F -q 'share/licenses/SDL3' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'SDL_SHADERCROSS_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'asset_worker' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -F -q -- '-exec cp -L {} "$stage/bin/"' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'developmentArtifact' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'manifest commit does not match' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -F -q 'package_worktree_policy "$ROOT" "$ALLOW_DIRTY" "$CI"' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -Fq 'ManagedApiConsumer.csproj' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -Fq 'KeireManagedAssembly=' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'package-editor' "$ROOT/Scripts/project.sh"
assert_true grep -q -- '-Configuration Dist' "$ROOT/Scripts/Windows/package-editor.ps1"
assert_true grep -q -- '--configuration Dist' "$ROOT/Scripts/Unix/package-editor.sh"
assert_true grep -q -- '--stage-only' "$ROOT/Scripts/Unix/package-editor.sh"
assert_true grep -q 'KEIRE_DISTRIBUTION_TRUSTED_KEYS' "$ROOT/Scripts/Unix/package-hub.sh"
assert_true grep -Fq 'for trusted_key in "${distribution_trusted_keys[@]}"' \
  "$ROOT/Scripts/Unix/package-hub.sh"
assert_true grep -q 'Build/Dependencies/dotnet-sdk' "$ROOT/Scripts/Unix/package-editor.sh"
assert_true grep -F -q 'cp -RL "$dotnet_source/." "$dotnet_destination/"' \
  "$ROOT/Scripts/Unix/package-editor.sh"
assert_true grep -q 'Build/Distributions' "$ROOT/Scripts/Unix/package-editor.sh"
assert_true grep -q 'validate_editor_package_stage' "$ROOT/Scripts/Unix/package-editor.sh"
assert_true grep -Fq 'xvfb-run -a "$stage/bin/$runtime"' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'editor product manifest must be a clean schema-2' \
  "$ROOT/KeireHubPackagePublisher/Source/Main.cpp"
assert_true grep -q '@PROJECT_NAMESPACE@ImGui.a' "$ROOT/Config/PackageConfig.cmake.in"
assert_true grep -q '"${_assimp_sdk_library}" "${_assimp_zlib_sdk_library}"' "$ROOT/Config/PackageConfig.cmake.in"
assert_true grep -q '"${_jolt_sdk_library}" "${_recast_sdk_libraries}" "${_miniaudio_sdk_library}"' "$ROOT/Config/PackageConfig.cmake.in"
assert_true grep -q 'SDL3::SDL3-static' "$ROOT/Config/PackageConfig.cmake.in"
security_workflow="$ROOT/.github/workflows/security.yml"
grep -q '^  security-status:$' "$security_workflow" || fail 'Security activation sentinel is missing'
grep -q '^    if: always()$' "$security_workflow" || fail 'Security activation sentinel is not unconditional'
grep -q 'ENABLE_ADVANCED_SECURITY' "$security_workflow" || fail 'Advanced security opt-in variable is missing'
! grep -q 'continue-on-error' "$security_workflow" || fail 'Advanced security checks are not strict'
python3 "$ROOT/Scripts/Tests/check-text-integrity.py"
python3 "$ROOT/Scripts/Tests/check-source-budgets.py"
python3 "$ROOT/Scripts/Tests/validate-workflows.py"

llvm_fixture="$(mktemp -d)"; old_path="$PATH"
printf '%s\n' '#!/usr/bin/env bash' 'printf "clang version 18.1.2\\n"' > "$llvm_fixture/clang++"
printf '%s\n' '#!/usr/bin/env bash' 'printf "LLVM version 17.0.6\\n"' > "$llvm_fixture/llvm-profdata"
printf '%s\n' '#!/usr/bin/env bash' 'printf "LLVM version 18.1.2\\n"' > "$llvm_fixture/llvm-profdata-18"
printf '%s\n' '#!/usr/bin/env bash' 'printf "LLVM version 18.1.2\\n"' > "$llvm_fixture/llvm-cov-18"
chmod +x "$llvm_fixture"/*; PATH="$llvm_fixture:$old_path"
assert_equal "$(resolve_llvm_tool llvm-profdata clang++)" "$llvm_fixture/llvm-profdata-18" 'matching llvm-profdata selection'
assert_equal "$(resolve_llvm_tool llvm-cov clang++)" "$llvm_fixture/llvm-cov-18" 'matching llvm-cov selection'
PATH="$old_path"; rm -rf "$llvm_fixture"

package_stage="$(mktemp -d)"
! grep -Fq 'include;${_core_sdk_prefix}/third-party' "$ROOT/Config/PackageConfig.cmake.in" || fail 'SDK package must not export a general third-party include path'
! grep -Eq 'spdlog/|fmt::' "$ROOT/KeireCore/Include/Keire/Log.h" || fail 'Public logging header must not expose spdlog or fmt'
grep -Fq KEIRE_COMPILED_LOG_LEVEL "$ROOT/KeireCore/Include/Keire/Log.h" || fail 'Public logging header must use the Kéire compile level'
for path in bin/Client bin/Hub lib/libCore.a lib/libCoreImGui.a Config/Client.json include/Core/Core.h include/Core/Log.h include/Core/Api.h include/Core/Application.h include/Core/Assert.h include/Core/BuildInfo.h include/Core/EntryPoint.h include/Core/Event.h include/Core/Layer.h include/Core/Ref.h include/Core/Time.h include/Core/Project/Project.h include/Core/Scenes/Scene.h include/Core/Scenes/SceneAsset.h include/Core/Scenes/SceneSystem.h include/Core/Window.h include/Core/WindowConfig.h samples/KeireSandbox/ProjectSettings/Project.keireproject samples/KeireSandbox/ProjectSettings/Rendering.keiresettings samples/KeireSandbox/Assets/Input/DefaultInput.keireinput samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene examples/consumer/Source/Main.cpp examples/consumer/Client.json examples/consumer/CMakeLists.txt examples/consumer/README.md examples/managed-consumer/Source/ClientApplication.cpp examples/managed-consumer/CMakeLists.txt examples/managed-consumer/ManagedApiConsumer.csproj examples/managed-consumer/ManagedPresentationAssets.cs examples/managed-consumer/README.md lib/cmake/CrossPlatformCoreClientTemplate/CrossPlatformCoreClientTemplateConfig.cmake third-party/spdlog/spdlog.h third-party/SDL3/include/SDL3/SDL.h third-party/SDL3/lib/libSDL3.a third-party/SDL3/cmake/SDL3Config.cmake third-party/SDL3/licenses/SDL3/LICENSE.txt third-party/licenses/spdlog-LICENSE.txt third-party/licenses/fmt-LICENSE.rst third-party/licenses/doctest-LICENSE.txt third-party/licenses/nlohmann-json-LICENSE.MIT.txt third-party/licenses/dear-imgui-LICENSE.txt README.md LICENSE.txt THIRD_PARTY_NOTICES.md build-manifest.json; do
  mkdir -p "$package_stage/$(dirname "$path")"; : > "$package_stage/$path"
done
for path in bin/CoreAssetTool bin/CoreAssetWorker lib/libCoreZstd.a include/Core/Math/Math.h include/Core/ECS/Component.h include/Core/ECS/Entity.h include/Core/ECS/Components/TransformComponent.h include/Core/ECS/Components/DirectionalLightComponent.h include/Core/ECS/Components/AudioComponents.h include/Core/ECS/Components/RuntimeUiComponents.h include/Core/Assets/Asset.h include/Core/Assets/AssetSystem.h include/Core/Assets/AssetPipeline.h include/Core/Assets/InputActionAsset.h include/Core/Input/Input.h samples/KeireSandbox/Assets/Input/DefaultInput.keireinput.keiremeta third-party/licenses/zstandard-LICENSE.txt third-party/licenses/entt-LICENSE.txt third-party/licenses/glm-COPYING.txt; do
  mkdir -p "$package_stage/$(dirname "$path")"; : > "$package_stage/$path"
done
rm -rf "$package_stage/third-party/spdlog"
for path in bin/KeireShaderCompiler lib/libassimp.a lib/libzlibstatic.a include/Core/Undo.h include/Core/ECS/Components/CameraComponent.h include/Core/ECS/Components/MeshRendererComponent.h include/Core/Rendering/RenderSystem.h include/Core/Assets/RenderingAssets.h samples/KeireSandbox/Assets/Shaders/DefaultUnlit.keireshader samples/KeireSandbox/Assets/Shaders/DefaultUnlit.hlsl samples/KeireSandbox/Assets/Materials/DefaultUnlit.keirematerial third-party/licenses/SDL-shadercross-LICENSE.txt third-party/licenses/DirectXShaderCompiler-LICENSE.txt third-party/licenses/DirectXShaderCompiler-ThirdPartyNotices.txt third-party/licenses/SPIRV-Cross-LICENSE.txt third-party/licenses/SPIRV-Headers-LICENSE.txt third-party/licenses/SPIRV-Tools-LICENSE.txt third-party/licenses/assimp-LICENSE.txt third-party/licenses/assimp-zlib-LICENSE.txt third-party/licenses/stb-LICENSE.txt; do
  mkdir -p "$package_stage/$(dirname "$path")"; : > "$package_stage/$path"
done
for path in bin/Managed/Coral.Managed.dll bin/Managed/Keire.Managed.dll Config/SourceModules.premake.lua \
  examples/source-module/Source/ClientApplication.cpp examples/source-module/Source/GameplayModule.cpp \
  examples/source-module/Include/GameplayModule.h examples/source-module/CMakeLists.txt \
  examples/source-module/README.md Docs/PlayerBuilds.md Docs/Diagnostics/KEIRE-AUDIO-0001.md \
  Docs/Diagnostics/KEIRE-REPLAY-0001.md Docs/Diagnostics/KEIRE-REPLAY-0002.md \
  third-party/licenses/Coral-LICENSE.txt third-party/licenses/dotnet-LICENSE.txt \
  third-party/licenses/dotnet-ThirdPartyNotices.txt third-party/licenses/Jolt-LICENSE.txt \
  third-party/licenses/Recast-LICENSE.txt third-party/licenses/miniaudio-LICENSE.txt lib/libJolt.a \
  lib/libRecast.a lib/libDetour.a lib/libDetourCrowd.a lib/libDetourTileCache.a lib/libminiaudio.a \
  lib/libCoral.Native.a lib/libnethost.a; do
  mkdir -p "$package_stage/$(dirname "$path")"; : > "$package_stage/$path"
done
: > "$package_stage/include/Core/Ui.h"
: > "$package_stage/include/Core/UiWorkspace.h"
: > "$package_stage/bin/CoreRuntime"
mkdir -p "$package_stage/include/Core/Build"
: > "$package_stage/include/Core/Build/PlayerBuild.h"
assert_true validate_package_stage "$package_stage" Client Hub Core Core
assert_true assert_package_generated_data_free "$package_stage"
for generated_path in \
  samples/KeireSandbox/Build/generated.make \
  samples/KeireSandbox/Logs/Core.log \
  samples/KeireSandbox/Library/AssetCatalog.json \
  samples/KeireSandbox/Temp/editor.tmp \
  samples/KeireSandbox/Assets/Scenes/SampleScene.recovery.json; do
  mkdir -p "$package_stage/$(dirname "$generated_path")"
  : > "$package_stage/$generated_path"
  assert_false assert_package_generated_data_free "$package_stage"
  rm "$package_stage/$generated_path"
done
mkdir -p "$package_stage/samples/KeireSandbox/Build"
: > "$package_stage/samples/KeireSandbox/Build/generated.txt"
package_contamination_archive="$(mktemp).tar.gz"
tar -C "$package_stage" -czf "$package_contamination_archive" .
assert_false assert_package_archive_generated_data_free "$package_contamination_archive"
rm -f "$package_contamination_archive" "$package_stage/samples/KeireSandbox/Build/generated.txt"
rm "$package_stage/lib/libCoreImGui.a"
assert_false validate_package_stage "$package_stage" Client Hub Core Core
: > "$package_stage/lib/libCoreImGui.a"
rm "$package_stage/third-party/licenses/dear-imgui-LICENSE.txt"
assert_false validate_package_stage "$package_stage" Client Hub Core Core
: > "$package_stage/third-party/licenses/dear-imgui-LICENSE.txt"
rm "$package_stage/third-party/licenses/spdlog-LICENSE.txt"
assert_false validate_package_stage "$package_stage" Client Hub Core Core
rm -rf "$package_stage"

tracked_sample_stage="$(mktemp -d)"
copy_tracked_tree "$ROOT" Samples/KeireSandbox "$tracked_sample_stage"
assert_true test -f "$tracked_sample_stage/ProjectSettings/Project.keireproject"
assert_true assert_package_generated_data_free "$tracked_sample_stage"
rm -rf "$tracked_sample_stage"
printf 'Fast Unix script checks completed in %ss.\n' "$((SECONDS - started))"
fi

if [[ $run_integration -eq 1 ]]; then
integration_started=$SECONDS
identity_fixture="$(mktemp -d)"
mkdir -p "$identity_fixture/Scripts/Unix" "$identity_fixture/Config"
cp "$ROOT/Scripts/Unix/common.sh" "$ROOT/Scripts/Unix/build-info.sh" "$identity_fixture/Scripts/Unix/"
cat > "$identity_fixture/Config/Project.conf" <<'IDENTITY_CONFIG'
PROJECT_IDENTIFIER=IdentityFixture
PROJECT_DISPLAY_NAME=Quoted "Kéire" \\ Client
PROJECT_VERSION=1.2.3-alpha.1+build.5
PROJECT_NAMESPACE=IdentityFixture
PROJECT_MACRO_PREFIX=IDENTITY_FIXTURE
CORE_TARGET=IdentityFixtureCore
CORE_DIRECTORY=IdentityFixtureCore
CLIENT_TARGET=IdentityFixtureClient
CLIENT_DIRECTORY=IdentityFixtureClient
HUB_TARGET=IdentityFixtureHub
HUB_DIRECTORY=IdentityFixtureHub
TESTS_TARGET=IdentityFixtureTests
TESTS_DIRECTORY=IdentityFixtureTests
ARTIFACT_PREFIX=identityfixture
REPOSITORY_SLUG=example/identity-fixture
IDENTITY_CONFIG
printf '%s\n' /Build/ .ninja_lock > "$identity_fixture/.gitignore"
git -C "$identity_fixture" init --quiet
git -C "$identity_fixture" config user.email scripts@example.invalid
git -C "$identity_fixture" config user.name 'Script Tests'
git -C "$identity_fixture" add .
git -C "$identity_fixture" commit --quiet -m first
first_commit="$(git -C "$identity_fixture" rev-parse HEAD)"
bash "$identity_fixture/Scripts/Unix/build-info.sh"
identity_header="$identity_fixture/Build/Generated/IdentityFixture/BuildInfo.generated.h"
assert_true grep -Fq '#define KEIRE_BUILD_PROJECT_VERSION "1.2.3-alpha.1+build.5"' "$identity_header"
assert_true grep -Fq '#define KEIRE_BUILD_PROJECT_NAME "Quoted \"Kéire\" \\\\ Client"' "$identity_header"
assert_false grep -Fq '\1777777777777777777' "$identity_header"
assert_true grep -Fq "#define KEIRE_BUILD_GIT_COMMIT \"$first_commit\"" "$identity_header"
assert_true grep -Fq '#define KEIRE_BUILD_GIT_DIRTY false' "$identity_header"
touch "$identity_fixture/.ninja_lock"
bash "$identity_fixture/Scripts/Unix/build-info.sh"
assert_true grep -Fq '#define KEIRE_BUILD_GIT_DIRTY false' "$identity_header"
sleep 1
touch "$identity_fixture/Build/identity-marker"
bash "$identity_fixture/Scripts/Unix/build-info.sh"
assert_true test "$identity_header" -ot "$identity_fixture/Build/identity-marker"
printf '%s\n' untracked > "$identity_fixture/untracked.txt"
bash "$identity_fixture/Scripts/Unix/build-info.sh"
assert_true grep -Fq '#define KEIRE_BUILD_GIT_DIRTY true' "$identity_header"
git -C "$identity_fixture" add .
git -C "$identity_fixture" commit --quiet -m second
second_commit="$(git -C "$identity_fixture" rev-parse HEAD)"
bash "$identity_fixture/Scripts/Unix/build-info.sh"
assert_true grep -Fq "#define KEIRE_BUILD_GIT_COMMIT \"$second_commit\"" "$identity_header"
assert_true grep -Fq '#define KEIRE_BUILD_GIT_DIRTY false' "$identity_header"
[[ "$first_commit" != "$second_commit" ]] || fail 'Identity did not refresh after a commit'
rm -rf "$identity_fixture"

parent_fixture="$(mktemp -d)"; fixture="$parent_fixture/Template"
trap 'rm -rf "$parent_fixture"' EXIT
mkdir -p "$fixture/archive"
printf '%s\n' premake > "$fixture/archive/premake5"
chmod 0644 "$fixture/archive/premake5"
assert_equal "$(find_premake_binary "$fixture/archive")" "$fixture/archive/premake5" 'non-executable Premake discovery'
mkdir -p "$fixture/Scripts/Unix" "$fixture/Config" "$fixture/Examples/Consumer/Source" "$fixture/Examples/ManagedConsumer/Source" "$fixture/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE" "$fixture/$CORE_DIRECTORY/Source" "$fixture/$CLIENT_DIRECTORY/Source" "$fixture/$HUB_DIRECTORY/Source" "$fixture/$TESTS_DIRECTORY/Source" "$fixture/Vendor" "$fixture/Build/Bin"
cp "$ROOT/Scripts/Unix/common.sh" "$ROOT/Scripts/Unix/rename.sh" "$ROOT/Scripts/Unix/clean.sh" "$fixture/Scripts/Unix/"
cp "$ROOT/Config/Project.conf" "$fixture/Config/Project.conf"
cp "$ROOT/Config/Client.json" "$fixture/Config/Client.json"
cp "$ROOT/Config/PackageConfig.cmake.in" "$fixture/Config/PackageConfig.cmake.in"
cp "$ROOT/premake5.lua" "$fixture/premake5.lua"
cp "$ROOT/Examples/Consumer/CMakeLists.txt" "$fixture/Examples/Consumer/"
cp "$ROOT/Examples/Consumer/Source/Main.cpp" "$fixture/Examples/Consumer/Source/"
cp "$ROOT/Examples/ManagedConsumer/CMakeLists.txt" "$fixture/Examples/ManagedConsumer/"
cp "$ROOT/Examples/ManagedConsumer/Source/ClientApplication.cpp" "$fixture/Examples/ManagedConsumer/Source/"
printf '%s\n' "#ifndef ${PROJECT_MACRO_PREFIX}_CORE_CORE_H" "#define ${PROJECT_MACRO_PREFIX}_CORE_CORE_H" "namespace $PROJECT_NAMESPACE { const char* GetName(); }" '#endif' > "$fixture/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE/Core.h"
printf '%s\n' "#ifndef ${PROJECT_MACRO_PREFIX}_CORE_LOG_H" "#define ${PROJECT_MACRO_PREFIX}_CORE_LOG_H" "namespace $PROJECT_NAMESPACE { class Log; }" '#endif' > "$fixture/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE/Log.h"
for source in "$fixture/$CORE_DIRECTORY/Source/Library.cpp" "$fixture/$CLIENT_DIRECTORY/Source/Main.cpp" "$fixture/$HUB_DIRECTORY/Source/Main.cpp" "$fixture/$TESTS_DIRECTORY/Source/Main.cpp"; do
  printf '#include "%s/Core.h"\n' "$PROJECT_NAMESPACE" > "$source"
done
printf '%s %s Scripts/Tests Core.log Client.log\n' "$PROJECT_IDENTIFIER" "$REPOSITORY_SLUG" > "$fixture/README.md"
printf '%s\n' vendor > "$fixture/Vendor/keep.txt"
printf '%s\n' build > "$fixture/Build/Bin/remove.txt"

git -C "$parent_fixture" init --quiet
git -C "$parent_fixture" config user.email scripts@example.invalid
git -C "$parent_fixture" config user.name 'Script Tests'
git -C "$parent_fixture" add Template
git -C "$parent_fixture" commit --quiet -m fixture
printf '%s\n' dirty >> "$fixture/README.md"

assert_false bash "$fixture/Scripts/Unix/rename.sh" ScriptFixture $'Bad\nName' example/script-fixture
bash "$fixture/Scripts/Unix/rename.sh" ScriptFixture 'Script "Fixturé" \\ Name' example/script-fixture >/dev/null
assert_false test -e "$fixture/.git"
assert_true test -f "$fixture/ScriptFixtureCore/Include/ScriptFixture/Core.h"
assert_true test -f "$fixture/ScriptFixtureHub/Source/Main.cpp"
assert_true grep -q '^CORE_TARGET=ScriptFixtureCore$' "$fixture/Config/Project.conf"
assert_true grep -q '^PROJECT_MACRO_PREFIX=SCRIPT_FIXTURE$' "$fixture/Config/Project.conf"
assert_true grep -q "^PROJECT_VERSION=$PROJECT_VERSION$" "$fixture/Config/Project.conf"
assert_true grep -Fq 'valid Semantic Version 2.0.0' "$fixture/premake5.lua"
assert_true grep -q 'find_package(ScriptFixture CONFIG REQUIRED)' "$fixture/Examples/Consumer/CMakeLists.txt"
assert_true grep -q 'ScriptFixture::Core' "$fixture/Examples/Consumer/CMakeLists.txt"
assert_true grep -q 'find_package(ScriptFixture CONFIG REQUIRED)' "$fixture/Examples/ManagedConsumer/CMakeLists.txt"
assert_true grep -q 'ScriptFixture::Core' "$fixture/Examples/ManagedConsumer/CMakeLists.txt"
assert_true grep -q '@PROJECT_NAMESPACE@::Core' "$fixture/Config/PackageConfig.cmake.in"
assert_true grep -Fq 'Scripts/Tests Core.log Client.log' "$fixture/README.md"
assert_true test -f "$fixture/Config/Client.json"
assert_false grep -R -q "$PROJECT_MACRO_PREFIX" "$fixture/ScriptFixtureCore/Include"
assert_true grep -q SCRIPT_FIXTURE_CORE_CORE_H "$fixture/ScriptFixtureCore/Include/ScriptFixture/Core.h"
bash "$fixture/Scripts/Unix/clean.sh" full >/dev/null
assert_false test -d "$fixture/Build/Bin"
assert_true test -f "$fixture/Vendor/keep.txt"
assert_true test -f "$fixture/ScriptFixtureCore/Source/Library.cpp"
rm -rf "$parent_fixture"
printf 'Unix script integration fixtures completed in %ss.\n' "$((SECONDS - integration_started))"
fi

if [[ $run_fast -eq 1 ]]; then
  command -v rg >/dev/null 2>&1 || fail 'ripgrep is required for repository identity validation'
  search_globs=(--glob '!.git/**' --glob '!.vs/**' --glob '!Vendor/**' --glob '!Tools/**' --glob '!Build/**' --glob '!Logs/**' --glob '!Artifacts/**' --glob '!Scripts/Tests/**')
  ! rg -n "${search_globs[@]}" '\b(CORE|CLIENT)_(API|ASSERT|ASSERTIONS_ENABLED|TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL)\b' "$ROOT" || fail 'Deprecated public macros remain'
  ! rg -n -F "${search_globs[@]}" -e '#include "KeireCore/' -e 'Scripts/KeireTests' -e 'Scripts\KeireTests' -e 'Scripts/Windows/Tests' -e 'Scripts/Unix/Tests' -e 'KeireCore.log' -e 'KeireClient.log' "$ROOT" || fail 'Stale repository identity remains'
fi
bash "$ROOT/Scripts/Tests/test-player-support-runtime-unix.sh"
printf 'Unix %s script regression tests passed in %ss.\n' "$suite" "$((SECONDS - started))"
