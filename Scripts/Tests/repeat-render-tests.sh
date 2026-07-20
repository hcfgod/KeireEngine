#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIGURATION="${1:-Release}"
BACKEND="${2:-${KEIRE_GPU_TEST_BACKEND:-vulkan}}"
COUNT="${3:-25}"

case "$CONFIGURATION" in
    Debug|Release) ;;
    *) echo "Configuration must be Debug or Release." >&2; exit 2 ;;
esac
case "$BACKEND" in
    vulkan|metal) ;;
    *) echo "Backend must be vulkan or metal." >&2; exit 2 ;;
esac
case "$COUNT" in
    ''|*[!0-9]*) echo "Count must be a positive integer." >&2; exit 2 ;;
esac
if (( COUNT < 1 || COUNT > 100 )); then
    echo "Count must be in the range 1..100." >&2
    exit 2
fi

PLATFORM=linux
[[ "$(uname -s)" == Darwin ]] && PLATFORM=macosx
EXECUTABLE="$ROOT/Build/Bin/${CONFIGURATION}-${PLATFORM}-x86_64/KeireRenderTests/KeireRenderTests"
if [[ ! -x "$EXECUTABLE" ]]; then
    echo "KeireRenderTests is not built for $CONFIGURATION." >&2
    exit 1
fi

LOG_ROOT="$ROOT/Build/TestLogs/RenderRepeat/${CONFIGURATION}-${BACKEND}"
mkdir -p "$LOG_ROOT"
export KEIRE_GPU_TEST_BACKEND="$BACKEND"
export KEIRE_REQUIRE_GPU_TESTS=1

for ((iteration = 1; iteration <= COUNT; ++iteration)); do
    log="$LOG_ROOT/run-$(printf '%03d' "$iteration").log"
    echo "==> Render repeat $iteration/$COUNT ($CONFIGURATION, $BACKEND)"
    "$EXECUTABLE" --no-skip 2>&1 | tee "$log"
done

echo "==> $COUNT consecutive rendered-output runs passed."
