#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIGURATION="${1:-Release}"
DEPENDENCY_BUILD="${2:-}"
[[ "$CONFIGURATION" == Debug || "$CONFIGURATION" == Release ]] || {
  printf 'Usage: %s [Debug|Release] <native-release-build>\n' "$0" >&2
  exit 2
}
[[ -n "$DEPENDENCY_BUILD" ]] || {
  printf 'The native Release dependency build is required for private FFmpeg zlib support.\n' >&2
  exit 2
}

source "$ROOT/Scripts/Unix/common.sh"
LOCK="$ROOT/Config/Dependencies.lock"
COMMIT="$(config_value "$LOCK" FFMPEG_COMMIT)"
MACOS_DEPLOYMENT_TARGET="$(config_value "$LOCK" MACOS_DEPLOYMENT_TARGET)"
VENDOR_SOURCE="$ROOT/Vendor/ffmpeg"
OUTPUT="$ROOT/Build/Dependencies/ffmpeg/$CONFIGURATION"
INSTALL="$OUTPUT/install"
STAMP="$OUTPUT/keire-ffmpeg.stamp"
ZLIB_SOURCE_INCLUDE="$ROOT/Vendor/assimp/contrib/zlib"
ZLIB_GENERATED_INCLUDE="$DEPENDENCY_BUILD/Assimp/contrib/zlib"
ZLIB_LIBRARY="$DEPENDENCY_BUILD/install/lib/libzlibstatic.a"
ZLIB_STAMP="$DEPENDENCY_BUILD/keire-dependency.stamp"
[[ -f "$ZLIB_SOURCE_INCLUDE/zlib.h" && -f "$ZLIB_GENERATED_INCLUDE/zconf.h" &&
   -f "$ZLIB_LIBRARY" && -f "$ZLIB_STAMP" ]] || {
  printf 'The native Release dependency build is missing the zlib files required by private FFmpeg.\n' >&2
  exit 1
}
ZLIB_KEY="$(tr -d '\r\n' < "$ZLIB_STAMP")"
EXPECTED="$COMMIT|$CONFIGURATION|$MACOS_DEPLOYMENT_TARGET|$ZLIB_KEY|shared-lgpl-avformat-avcodec-swresample-avutil-zlib-exr-v5"

[[ -x "$VENDOR_SOURCE/configure" ]] || {
  printf 'Vendor/ffmpeg is unavailable. Initialize the locked submodule first.\n' >&2
  exit 1
}
[[ "$(git -C "$VENDOR_SOURCE" rev-parse HEAD)" == "$COMMIT" ]] || {
  printf 'Vendor/ffmpeg is not at locked commit %s.\n' "$COMMIT" >&2
  exit 1
}
if [[ -f "$INSTALL/include/libavformat/avformat.h" && -f "$OUTPUT/config_components.h" &&
      "$(grep -F '#define CONFIG_EXR_DECODER 1' "$OUTPUT/config_components.h" || true)" && -f "$STAMP" &&
      "$(cat "$STAMP")" == "$EXPECTED" ]]; then
  printf '==> Private FFmpeg %s build is current\n' "$CONFIGURATION"
  exit 0
fi
command -v make >/dev/null || {
  printf 'GNU Make is required to source-build private FFmpeg.\n' >&2
  exit 1
}
command -v tar >/dev/null || {
  printf 'tar is required to materialize canonical FFmpeg source bytes.\n' >&2
  exit 1
}

rm -rf "$OUTPUT"
SOURCE="$OUTPUT/source"
ZLIB_LINK_DIRECTORY="$OUTPUT/zlib-lib"
mkdir -p "$SOURCE"
mkdir -p "$ZLIB_LINK_DIRECTORY"
cp "$ZLIB_LIBRARY" "$ZLIB_LINK_DIRECTORY/libz.a"
# A Windows checkout may translate executable shell files to CRLF inside the submodule. Build from the locked Git
# object bytes so Linux and macOS never depend on host worktree line-ending policy and Vendor remains untouched.
git -C "$VENDOR_SOURCE" archive --format=tar "$COMMIT" | tar -xf - -C "$SOURCE"
[[ -x "$SOURCE/configure" ]] || {
  printf 'The canonical FFmpeg archive is missing its executable configure script.\n' >&2
  exit 1
}
debug_options=(--disable-debug)
if [[ "$CONFIGURATION" == Debug ]]; then
  debug_options=(--enable-debug=3 --disable-optimizations)
fi
platform_options=()
if [[ "$(uname -s)" == Darwin ]]; then
  platform_options=(--install-name-dir=@rpath
    --extra-cflags="-mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET"
    --extra-ldflags="-mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET")
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
    --enable-zlib \
    --enable-decoder=exr \
    --enable-encoder=flac \
    --enable-muxer=flac \
    --extra-cflags="-I$ZLIB_SOURCE_INCLUDE -I$ZLIB_GENERATED_INCLUDE" \
    --extra-ldflags="-L$ZLIB_LINK_DIRECTORY" \
    "${platform_options[@]}" \
    "${debug_options[@]}"
  (cd "$SOURCE" && find . -type d -exec mkdir -p "$OUTPUT"/{} \;)
  make -j"$(build_parallel_jobs)"
  make install
)

mkdir -p "$INSTALL/share/licenses/ffmpeg"
cp "$SOURCE/COPYING.LGPLv2.1" "$SOURCE/COPYING.LGPLv3" "$INSTALL/share/licenses/ffmpeg/"
printf 'Source: Vendor/ffmpeg\nCommit: %s\nConfiguration: %s\n' "$COMMIT" "$CONFIGURATION" \
  >"$INSTALL/share/licenses/ffmpeg/SOURCE.txt"
printf '%s\n' "$EXPECTED" >"$STAMP"
