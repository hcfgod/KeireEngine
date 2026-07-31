#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIGURATION="${1:-Release}"
[[ "$CONFIGURATION" == Debug || "$CONFIGURATION" == Release ]] || {
  printf 'Usage: %s [Debug|Release]\n' "$0" >&2
  exit 2
}

source "$ROOT/Scripts/Unix/common.sh"
LOCK="$ROOT/Config/Dependencies.lock"
COMMIT="$(config_value "$LOCK" FFMPEG_COMMIT)"
SOURCE="$ROOT/Vendor/ffmpeg"
OUTPUT="$ROOT/Build/Dependencies/ffmpeg/$CONFIGURATION"
INSTALL="$OUTPUT/install"
STAMP="$OUTPUT/keire-ffmpeg.stamp"
EXPECTED="$COMMIT|$CONFIGURATION|shared-lgpl-avformat-avcodec-swresample-avutil-v2"

[[ -x "$SOURCE/configure" ]] || {
  printf 'Vendor/ffmpeg is unavailable. Initialize the locked submodule first.\n' >&2
  exit 1
}
[[ "$(git -C "$SOURCE" rev-parse HEAD)" == "$COMMIT" ]] || {
  printf 'Vendor/ffmpeg is not at locked commit %s.\n' "$COMMIT" >&2
  exit 1
}
if [[ -f "$INSTALL/include/libavformat/avformat.h" && -f "$STAMP" &&
      "$(cat "$STAMP")" == "$EXPECTED" ]]; then
  printf '==> Private FFmpeg %s build is current\n' "$CONFIGURATION"
  exit 0
fi
command -v make >/dev/null || {
  printf 'GNU Make is required to source-build private FFmpeg.\n' >&2
  exit 1
}

rm -rf "$OUTPUT"
mkdir -p "$OUTPUT"
debug_options=(--disable-debug)
if [[ "$CONFIGURATION" == Debug ]]; then
  debug_options=(--enable-debug=3 --disable-optimizations)
fi
platform_options=()
if [[ "$(uname -s)" == Darwin ]]; then
  platform_options=(--install-name-dir=@rpath)
fi
(
  cd "$OUTPUT"
  "$SOURCE/configure" \
    --prefix="$INSTALL" \
    --enable-shared \
    --disable-static \
    --disable-programs \
    --disable-doc \
    --disable-network \
    --disable-avdevice \
    --disable-avfilter \
    --disable-swscale \
    --disable-autodetect \
    --disable-gpl \
    --disable-nonfree \
    --disable-version3 \
    --enable-avformat \
    --enable-avcodec \
    --enable-swresample \
    --enable-avutil \
    --enable-encoder=flac \
    --enable-muxer=flac \
    "${platform_options[@]}" \
    "${debug_options[@]}"
  make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 1)"
  make install
)

mkdir -p "$INSTALL/share/licenses/ffmpeg"
cp "$SOURCE/COPYING.LGPLv2.1" "$SOURCE/COPYING.LGPLv3" "$INSTALL/share/licenses/ffmpeg/"
printf 'Source: Vendor/ffmpeg\nCommit: %s\nConfiguration: %s\n' "$COMMIT" "$CONFIGURATION" \
  >"$INSTALL/share/licenses/ffmpeg/SOURCE.txt"
printf '%s\n' "$EXPECTED" >"$STAMP"
