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

for generator in builtin-shaders builtin-skinning builtin-vfx; do
  assert_true grep -F -q 'generated-content-cache.sh' "$ROOT/Scripts/Unix/$generator.sh"
  assert_true grep -F -q 'generated_content_is_current' "$ROOT/Scripts/Unix/$generator.sh"
  assert_true grep -F -q 'generated_content_acquire_lock' "$ROOT/Scripts/Unix/$generator.sh"
  assert_true grep -F -q 'generated_content_write_stamp' "$ROOT/Scripts/Unix/$generator.sh"
done
assert_true grep -F -q 'kill -0 "$owner"' "$helper"
for platform in Linux Mac; do
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

printf 'second\n' > "$cache_input"
changed="$(generated_content_fingerprint test-v1 "$cache_input")"
assert_false generated_content_is_current "$cache_output" "$cache_stamp" "$changed"

runtime_source="$fixture/runtime-source.so"
runtime_destination="$fixture/runtime-destination.so"
printf 'runtime-v1\n' > "$runtime_source"
generated_content_copy_file_if_changed "$runtime_source" "$runtime_destination"
assert_true cmp -s "$runtime_source" "$runtime_destination"
printf 'runtime-v2\n' > "$runtime_source"
generated_content_copy_file_if_changed "$runtime_source" "$runtime_destination"
assert_true cmp -s "$runtime_source" "$runtime_destination"

printf '%s\n' 'Unix generated-content cache regression tests passed.'
