#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
helper="$ROOT/Scripts/Unix/generated-content-cache.sh"

assert_true() {
  "$@" || {
    printf 'Assertion failed: %s\n' "$*" >&2
    exit 1
  }
}

assert_false() {
  if "$@"; then
    printf 'Expected failure: %s\n' "$*" >&2
    exit 1
  fi
}

for generator in builtin-shaders builtin-skinning builtin-vfx builtin-occlusion builtin-spatial-selection; do
  assert_true grep -F -q 'generated-content-cache.sh' "$ROOT/Scripts/Unix/$generator.sh"
  assert_true grep -F -q 'generated_content_is_current' "$ROOT/Scripts/Unix/$generator.sh"
  assert_true grep -F -q 'generated_content_acquire_lock' "$ROOT/Scripts/Unix/$generator.sh"
  assert_true grep -F -q 'generated_content_write_stamp' "$ROOT/Scripts/Unix/$generator.sh"
done
assert_true grep -F -q 'kill -0 "$owner"' "$helper"
assert_true grep -F -q 'generated_content_read_pid "$lock/pid"' "$helper"
assert_false grep -F -q '< "$lock/pid"' "$helper"
for platform in Linux Mac; do
  assert_true grep -F -q 'bash "$ROOT/Scripts/Unix/prepare-generated-content.sh"' "$ROOT/Scripts/$platform/build.sh"
  assert_true grep -F -q 'generated_content_acquire_lock "$build_lock" 7200' "$ROOT/Scripts/$platform/build.sh"
  assert_true grep -F -q 'generated_content_release_lock "$build_lock"' "$ROOT/Scripts/$platform/build.sh"
done

fixture="$(mktemp -d)"
lock_held=0
waiter=""
cleanup() {
  if [[ $lock_held -eq 1 ]]; then
    generated_content_release_lock "$cache_lock"
  fi
  if [[ -n "$waiter" ]]; then
    kill "$waiter" 2>/dev/null || true
    wait "$waiter" 2>/dev/null || true
  fi
  rm -rf "$fixture"
}
trap cleanup EXIT

source "$helper"
source "$ROOT/Scripts/Unix/common.sh"
ninja_fixture="$fixture/FixtureAssetWorker"
mkdir -p "$ninja_fixture"
printf '%s\n' \
  "  prelinkcommands = sh -c 'bash ./Scripts/Unix/copy-files-if-changed.sh ./Build/Dependencies/ffmpeg/Debug/install/lib ./Build/Bin/Debug-linux-x86_64/FixtureAssetWorker && touch stamp'" \
  > "$ninja_fixture/FixtureAssetWorker.ninja"
assert_true validate_unix_asset_worker_ninja_commands "$fixture" Fixture
printf '%s\n' \
  "  prelinkcommands = sh -c 'bash ./Scripts/Unix/copy-files-if-changed.sh ./Build/Dependencies/ffmpeg/Debug/install/lib ./Build/Bin/Debug-linux-x86_64/FixtureAssetWorker '*' && touch stamp'" \
  > "$ninja_fixture/FixtureAssetWorker.ninja"
assert_false validate_unix_asset_worker_ninja_commands "$fixture" Fixture
cache_input="$fixture/input.txt"
cache_output="$fixture/output.h"
cache_stamp="$fixture/output.stamp"
cache_lock="$fixture/output.lock"
ready="$fixture/ready"
acquired="$fixture/acquired"
printf 'first\n' > "$cache_input"
printf 'generated\n' > "$cache_output"
fingerprint="$(generated_content_fingerprint test-v1 "$cache_input")"
assert_false generated_content_is_current "$cache_output" "$cache_stamp" "$fingerprint"
generated_content_write_stamp "$cache_stamp" "$fingerprint"
assert_true generated_content_is_current "$cache_output" "$cache_stamp" "$fingerprint"

generated_content_acquire_lock "$cache_lock"
lock_held=1
(
  printf 'ready\n' > "$ready"
  generated_content_acquire_lock "$cache_lock"
  printf 'acquired\n' > "$acquired"
  generated_content_release_lock "$cache_lock"
) &
waiter=$!
for _ in {1..400}; do
  [[ -f "$ready" ]] && break
  sleep 0.025
done
assert_true test -f "$ready"
sleep 0.2
assert_false test -f "$acquired"
generated_content_release_lock "$cache_lock"
lock_held=0
wait "$waiter"
waiter=""
assert_true test -f "$acquired"

race_bin="$fixture/race-bin"
race_lock="$fixture/race.lock"
mkdir -p "$race_bin" "$race_lock"
printf '%s\n' '#!/usr/bin/env bash' \
  'pid_file="${1:?PID file operand is required}"' \
  'rm -f "$pid_file"' \
  'rmdir "$(dirname "$pid_file")"' \
  'exit 1' \
  > "$race_bin/cat"
chmod +x "$race_bin/cat"
printf '%s\n' "$$" > "$race_lock/pid"
PATH="$race_bin:$PATH" generated_content_acquire_lock "$race_lock" 2
assert_true test -f "$race_lock/pid"
generated_content_release_lock "$race_lock"

stale_lock="$fixture/stale.lock"
stale_acquisitions="$fixture/stale-acquisitions"
mkdir "$stale_lock"
printf '%s\n' 99999999 > "$stale_lock/pid"
stale_waiters=(sentinel)
for iteration in {1..12}; do
  bash -c 'set -euo pipefail
    source "$1"
    generated_content_acquire_lock "$2" 5
    printf "%s\n" "$3" >> "$4"
    sleep 0.01
    generated_content_release_lock "$2"' _ \
    "$helper" "$stale_lock" "$iteration" "$stale_acquisitions" &
  stale_waiters+=("$!")
done
for stale_waiter in "${stale_waiters[@]:1}"; do
  wait "$stale_waiter"
done
assert_true test "$(wc -l < "$stale_acquisitions" | tr -d ' ')" -eq 12
assert_false test -e "$stale_lock"
assert_false test -e "$stale_lock.recovery"
assert_true test "$(find "$fixture" -maxdepth 1 -name 'stale.lock.stale.*' -print | wc -l | tr -d ' ')" -eq 0

dead_recovery_lock="$fixture/dead-recovery.lock"
mkdir "$dead_recovery_lock.recovery"
printf '%s\n' 99999999 > "$dead_recovery_lock.recovery/pid"
generated_content_acquire_lock "$dead_recovery_lock" 2
assert_true test -f "$dead_recovery_lock/pid"
assert_false test -e "$dead_recovery_lock.recovery"
assert_true test "$(find "$fixture" -maxdepth 1 -name 'dead-recovery.lock.recovery.stale.*' -print | \
  wc -l | tr -d ' ')" -eq 0
generated_content_release_lock "$dead_recovery_lock"

publication_lock="$fixture/publication.lock"
mkdir "$publication_lock"
publication_token="$(atomic_symlink_lock_acquire "$publication_lock")"
assert_true test -L "$publication_lock"
assert_true test "$(readlink "$publication_lock")" = "$publication_token"
atomic_symlink_lock_release "$publication_lock" "$publication_token"

ln -s 'keire-owner:99999999:dead:fixture' "$publication_lock"
publication_token="$(atomic_symlink_lock_acquire "$publication_lock")"
assert_true test "$(readlink "$publication_lock")" = "$publication_token"
atomic_symlink_lock_release "$publication_lock" "$publication_token"

live_publication_token="keire-owner:$$:live:fixture"
ln -s "$live_publication_token" "$publication_lock"
assert_false atomic_symlink_lock_acquire "$publication_lock"
assert_true test "$(readlink "$publication_lock")" = "$live_publication_token"
rm -f "$publication_lock"

aba_bin="$fixture/aba-bin"
mkdir "$aba_bin"
printf '%s\n' '#!/usr/bin/env bash' \
  'if [[ "$1" == "$ABA_LOCK" ]]; then' \
  '  rm -f "$1"' \
  '  ln -s "$ABA_REPLACEMENT_TOKEN" "$1"' \
  'fi' \
  'exec /bin/mv "$@"' \
  > "$aba_bin/mv"
chmod +x "$aba_bin/mv"
ln -s 'keire-owner:99999999:stale:fixture' "$publication_lock"
ABA_LOCK="$publication_lock" ABA_REPLACEMENT_TOKEN="$live_publication_token" \
  assert_false env PATH="$aba_bin:$PATH" ABA_LOCK="$publication_lock" \
    ABA_REPLACEMENT_TOKEN="$live_publication_token" bash -c \
      'source "$1"; atomic_symlink_lock_acquire "$2"' _ "$ROOT/Scripts/Unix/common.sh" "$publication_lock"
assert_true test "$(readlink "$publication_lock")" = "$live_publication_token"
rm -f "$publication_lock"

printf 'second\n' > "$cache_input"
changed="$(generated_content_fingerprint test-v1 "$cache_input")"
assert_false generated_content_is_current "$cache_output" "$cache_stamp" "$changed"

runtime_source="$fixture/runtime-source.so"
runtime_destination="$fixture/runtime-destination.so"
printf 'runtime-v1\n' > "$runtime_source"
generated_content_copy_file_if_changed "$runtime_source" "$runtime_destination" "$fixture"
assert_true cmp -s "$runtime_source" "$runtime_destination"
runtime_external="$fixture/runtime-external.so"
printf 'external-preserved\n' > "$runtime_external"
rm -f "$runtime_destination"
ln -s "$runtime_external" "$runtime_destination"
assert_false generated_content_copy_file_if_changed "$runtime_source" "$runtime_destination" "$fixture"
assert_true grep -F -q 'external-preserved' "$runtime_external"
rm -f "$runtime_destination"
mkdir "$runtime_destination"
assert_false generated_content_copy_file_if_changed "$runtime_source" "$runtime_destination" "$fixture"
rmdir "$runtime_destination"
printf 'runtime-v2\n' > "$runtime_source"
generated_content_copy_file_if_changed "$runtime_source" "$runtime_destination" "$fixture"
assert_true cmp -s "$runtime_source" "$runtime_destination"

runtime_source_directory="$fixture/runtime-source"
runtime_destination_directory="$fixture/runtime-destination"
mkdir -p "$runtime_source_directory"
printf 'library-a\n' > "$runtime_source_directory/liba.so"
printf 'library-b\n' > "$runtime_source_directory/libb.so"
ln -s liba.so "$runtime_source_directory/liba.so.1"
bash "$ROOT/Scripts/Unix/copy-files-if-changed.sh" "$runtime_source_directory" \
  "$runtime_destination_directory" '*' "$fixture"
assert_true cmp -s "$runtime_source_directory/liba.so" "$runtime_destination_directory/liba.so"
assert_true cmp -s "$runtime_source_directory/libb.so" "$runtime_destination_directory/libb.so"
assert_true cmp -s "$runtime_source_directory/liba.so.1" "$runtime_destination_directory/liba.so.1"
assert_false test -L "$runtime_destination_directory/liba.so.1"
assert_true test "$(find "$runtime_destination_directory" -maxdepth 1 -type f | wc -l | tr -d ' ')" -eq 3
printf 'outside\n' > "$fixture/outside.so"
ln -s "$fixture/outside.so" "$runtime_source_directory/external.so"
printf 'library-a-updated\n' > "$runtime_source_directory/liba.so"
printf 'library-b-updated\n' > "$runtime_source_directory/libb.so"
assert_false bash "$ROOT/Scripts/Unix/copy-files-if-changed.sh" "$runtime_source_directory" \
  "$runtime_destination_directory" '*' "$fixture"
assert_false test -e "$runtime_destination_directory/external.so"
assert_true grep -F -q 'library-a' "$runtime_destination_directory/liba.so"
assert_true grep -F -q 'library-b' "$runtime_destination_directory/libb.so"
assert_false grep -F -q 'updated' "$runtime_destination_directory/liba.so"
assert_false grep -F -q 'updated' "$runtime_destination_directory/libb.so"
ancestor_root="$fixture/ancestor-root"
ancestor_external="$fixture/ancestor-external"
mkdir -p "$ancestor_root" "$ancestor_external"
ln -s "$ancestor_external" "$ancestor_root/link"
assert_false generated_content_copy_file_if_changed "$runtime_source" \
  "$ancestor_root/link/nested/runtime.so" "$ancestor_root"
assert_false test -e "$ancestor_external/nested/runtime.so"
assert_true grep -F -q 'mktemp "$destination_directory/.keire-copy.XXXXXX"' "$helper"
assert_false grep -F -q '$destination.tmp.$$.$RANDOM' "$helper"

stage_root="$fixture/stage-root"
stage_source="$stage_root/Build/Dependencies/ffmpeg/Debug/install/lib"
stage_destination="$stage_root/Build/Bin/Debug-linux-x86_64/FixtureAssetWorker"
mkdir -p "$stage_root/Scripts" "$stage_source" "$stage_destination"
ln -s "$ROOT/Scripts/Unix" "$stage_root/Scripts/Unix"
printf 'worker\n' > "$stage_destination/FixtureAssetWorker"
chmod +x "$stage_destination/FixtureAssetWorker"
for family in avformat avcodec swresample avutil; do
  printf '%s-runtime\n' "$family" > "$stage_source/lib$family.so.1.2.3"
  ln -s "lib$family.so.1.2.3" "$stage_source/lib$family.so.1"
  ln -s "lib$family.so.1.2.3" "$stage_source/lib$family.so"
done
(cd "$stage_root" && bash ./Scripts/Unix/copy-files-if-changed.sh \
  ./Build/Dependencies/ffmpeg/Debug/install/lib \
  ./Build/Bin/Debug-linux-x86_64/FixtureAssetWorker)
assert_true test -f "$stage_destination/libavcodec.so.1"
printf 'stale\n' > "$stage_destination/libavformat.so.999"
printf 'unrelated\n' > "$stage_destination/libunrelated.so.1"
stage_unix_asset_worker_runtime "$stage_root" Debug linux x86_64 Fixture FixtureAssetWorker
for family in avformat avcodec swresample avutil; do
  assert_true test -f "$stage_destination/lib$family.so.1"
  assert_false test -L "$stage_destination/lib$family.so.1"
done
assert_false test -e "$stage_destination/libavformat.so.999"
assert_true test -f "$stage_destination/libunrelated.so.1"
rm -f "$stage_source/libavutil.so.1"
stage_unix_asset_worker_runtime "$stage_root" Debug linux x86_64 Fixture FixtureCore
assert_false stage_unix_asset_worker_runtime "$stage_root" Debug linux x86_64 Fixture FixtureAssetWorker

mac_stage_destination="$stage_root/Build/Bin/Debug-macosx-x86_64/FixtureAssetWorker"
mkdir -p "$mac_stage_destination"
printf 'worker\n' > "$mac_stage_destination/FixtureAssetWorker"
chmod +x "$mac_stage_destination/FixtureAssetWorker"
for family in avformat avcodec swresample avutil; do
  printf '%s-runtime\n' "$family" > "$stage_source/lib$family.1.2.3.dylib"
  ln -s "lib$family.1.2.3.dylib" "$stage_source/lib$family.1.dylib"
  ln -s "lib$family.1.2.3.dylib" "$stage_source/lib$family.dylib"
done
printf 'stale\n' > "$mac_stage_destination/libavformat.999.dylib"
stage_unix_asset_worker_runtime "$stage_root" Debug macosx x86_64 Fixture FixtureEditorTests
for family in avformat avcodec swresample avutil; do
  assert_true test -f "$mac_stage_destination/lib$family.1.dylib"
  assert_false test -L "$mac_stage_destination/lib$family.1.dylib"
done
assert_false test -e "$mac_stage_destination/libavformat.999.dylib"

printf '%s\n' 'Unix generated-content cache regression tests passed.'
