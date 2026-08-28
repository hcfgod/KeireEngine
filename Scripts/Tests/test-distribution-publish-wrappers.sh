#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
wrapper="$root/Services/KeireDistributionService/scripts/publish-snapshot.sh"
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT

fail() {
  printf 'error: %s\n' "$1" >&2
  exit 1
}

expect_failure() {
  local expected="$1"
  shift
  rm -f -- "$capture"
  set +e
  "$@" >"$fixture/stdout.txt" 2>"$fixture/stderr.txt"
  local status=$?
  set -e
  [[ $status -eq 2 ]] || fail "expected exit 2, received $status"
  grep -Fq -- "$expected" "$fixture/stderr.txt" ||
    fail "missing expected failure: $expected"
  [[ ! -e $capture ]] || fail 'an invalid wrapper invocation reached the publisher'
}

assert_argument_pair() {
  local option="$1"
  local expected="$2"
  local index
  for ((index = 0; index + 1 < ${#captured_arguments[@]}; ++index)); do
    if [[ ${captured_arguments[$index]} == "$option" ]]; then
      [[ ${captured_arguments[$((index + 1))]} == "$expected" ]] ||
        fail "$option forwarded '${captured_arguments[$((index + 1))]}' instead of '$expected'"
      return
    fi
  done
  fail "missing forwarded option $option"
}

capture="$fixture/captured-arguments.txt"
fake_dotnet="$fixture/dotnet"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'set -euo pipefail' \
  'printf "%s\n" "$@" > "$KEIRE_PUBLISH_WRAPPER_CAPTURE"' >"$fake_dotnet"
chmod +x "$fake_dotnet"

common=("$fixture/prepared" "$fixture/distribution" release-test-sequence-18 \
  "$fixture/trusted-public-key.json")

export KEIRE_DOTNET="$fake_dotnet"
export KEIRE_PUBLISH_WRAPPER_CAPTURE="$capture"

expect_failure 'Activation requires an explicit --minimum-sequence floor.' \
  "$wrapper" "${common[@]}" --activate
expect_failure 'Minimum sequence must be an integer' \
  "$wrapper" "${common[@]}" --minimum-sequence 0
expect_failure 'Minimum sequence must be an integer' \
  "$wrapper" "${common[@]}" --minimum-sequence 9223372036854775808
expect_failure 'Minimum validity hours must be finite' \
  "$wrapper" "${common[@]}" --minimum-validity-hours NaN

"$wrapper" "${common[@]}" --minimum-validity-hours 48.5 --activate --minimum-sequence 18
mapfile -t captured_arguments <"$capture"
assert_argument_pair --minimum-sequence 18
assert_argument_pair --minimum-validity-hours 48.5
printf '%s\n' "${captured_arguments[@]}" | grep -Fxq -- '--activate' ||
  fail 'the Unix publish wrapper did not forward --activate'

rm -f -- "$capture"
"$wrapper" "${common[@]}"
mapfile -t captured_arguments <"$capture"
assert_argument_pair --minimum-validity-hours 24
if printf '%s\n' "${captured_arguments[@]}" | grep -Fxq -- '--minimum-sequence'; then
  fail 'a non-activating Unix publish invented a minimum sequence'
fi

printf 'Unix distribution publish wrapper checks passed.\n'
