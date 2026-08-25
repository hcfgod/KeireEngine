#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIGURATION="${1:-Release}"
DEPENDENCY_BUILD="${2:-}"
PLATFORM="${3:-}"
ARCHITECTURE="${4:-}"
TOOLSET="${5:-}"
[[ "$CONFIGURATION" == Debug || "$CONFIGURATION" == Release ]] || {
  printf 'Usage: %s [Debug|Release] <native-release-build> <Linux|Mac> <architecture> <toolset>\n' "$0" >&2
  exit 2
}
[[ -n "$DEPENDENCY_BUILD" ]] || {
  printf 'The native Release dependency build is required for private FFmpeg zlib support.\n' >&2
  exit 2
}
[[ "$PLATFORM" == Linux || "$PLATFORM" == Mac ]] || {
  printf 'The private FFmpeg platform must be Linux or Mac.\n' >&2
  exit 2
}
[[ -n "$ARCHITECTURE" && -n "$TOOLSET" ]] || {
  printf 'The private FFmpeg architecture and toolset are required.\n' >&2
  exit 2
}

source "$ROOT/Scripts/Unix/common.sh"
ARCHITECTURE="$(normalize_architecture "$ARCHITECTURE")" || exit 2
case "$TOOLSET" in
  gcc|clang) ;;
  *)
    printf "Unsupported private FFmpeg toolset '%s'. Expected gcc or clang.\n" "$TOOLSET" >&2
    exit 2
    ;;
esac
LOCK="$ROOT/Config/Dependencies.lock"
COMMIT="$(config_value "$LOCK" FFMPEG_COMMIT)"
MACOS_DEPLOYMENT_TARGET="$(config_value "$LOCK" MACOS_DEPLOYMENT_TARGET)"
VENDOR_SOURCE="$ROOT/Vendor/ffmpeg"
OUTPUT="$ROOT/Build/Dependencies/ffmpeg/$CONFIGURATION"
SYSTEM=linux
[[ "$PLATFORM" == Mac ]] && SYSTEM=macosx
HOST_SYSTEM="$(uname -s)"
if [[ ( "$PLATFORM" == Linux && "$HOST_SYSTEM" != Linux ) ||
      ( "$PLATFORM" == Mac && "$HOST_SYSTEM" != Darwin ) ]]; then
  printf 'The requested FFmpeg platform %s does not match host system %s.\n' "$PLATFORM" "$HOST_SYSTEM" >&2
  exit 2
fi
OUTPUT_ARCHITECTURE="$(architecture_output_name "$ARCHITECTURE")"
FFMPEG_ARCHITECTURE=x86_64
MACOS_ARCHITECTURE=x86_64
if [[ "$OUTPUT_ARCHITECTURE" == AARCH64 ]]; then
  FFMPEG_ARCHITECTURE=aarch64
  MACOS_ARCHITECTURE=arm64
fi
CACHE_BASE="$ROOT/Build/Dependencies/ffmpeg-cache/$SYSTEM-$OUTPUT_ARCHITECTURE-$TOOLSET"
CACHE_OUTPUT="$CACHE_BASE/$CONFIGURATION"
INSTALL="$CACHE_OUTPUT/install"
STAMP="$CACHE_OUTPUT/keire-ffmpeg.stamp"
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

valid_ffmpeg_component_artifacts() {
  local path="${1:?FFmpeg output path is required}"
  local component link_artifact runtime runtime_found
  for component in avformat avcodec swresample avutil; do
    if [[ "$SYSTEM" == macosx ]]; then
      link_artifact="$path/install/lib/lib$component.dylib"
      [[ -f "$link_artifact" ]] || return 1
      runtime_found=false
      for runtime in "$path/install/lib/lib$component."*.dylib; do
        if [[ "$runtime" != "$link_artifact" && -f "$runtime" ]]; then
          runtime_found=true
          break
        fi
      done
    else
      link_artifact="$path/install/lib/lib$component.so"
      [[ -f "$link_artifact" ]] || return 1
      runtime_found=false
      for runtime in "$path/install/lib/lib$component.so."*; do
        if [[ -f "$runtime" ]]; then
          runtime_found=true
          break
        fi
      done
    fi
    [[ "$runtime_found" == true ]] || return 1
  done
}

valid_ffmpeg_output() {
  local path="${1:?FFmpeg output path is required}"
  local expected_stamp="${2:?FFmpeg stamp is required}"
  [[ -f "$path/install/include/libavformat/avformat.h" && -f "$path/config_components.h" &&
      "$(grep -F '#define CONFIG_EXR_DECODER 1' "$path/config_components.h" || true)" &&
      -f "$path/install/share/licenses/ffmpeg/COPYING.LGPLv2.1" &&
      -f "$path/install/share/licenses/ffmpeg/COPYING.LGPLv3" &&
      -f "$path/install/share/licenses/ffmpeg/SOURCE.txt" && -f "$path/keire-ffmpeg.stamp" &&
      "$(cat "$path/keire-ffmpeg.stamp")" == "$expected_stamp" ]] &&
    valid_ffmpeg_component_artifacts "$path"
}

remove_ffmpeg_output() {
  local path="${1:?FFmpeg output path is required}"
  local allowed_base="${2:?FFmpeg allowed base is required}"
  require_ffmpeg_output_path "$path" "$allowed_base"
  [[ ! -e "$path" ]] && return
  rm -rf "$path"
}

canonical_ffmpeg_path() {
  local path="${1:?FFmpeg path is required}" existing suffix="" component parent physical
  existing="${path%/}"
  [[ -n "$existing" ]] || existing=/
  while [[ ! -e "$existing" && ! -L "$existing" ]]; do
    component="${existing##*/}"
    parent="${existing%/*}"
    [[ -n "$parent" ]] || parent=/
    [[ "$parent" != "$existing" ]] || return 1
    suffix="/$component$suffix"
    existing="$parent"
  done
  [[ -d "$existing" ]] || return 1
  physical="$(cd -P "$existing" && pwd -P)" || return 1
  printf '%s%s\n' "${physical%/}" "$suffix"
}

require_ffmpeg_output_path() {
  local path="${1:?FFmpeg output path is required}"
  local allowed_base="${2:?FFmpeg allowed base is required}"
  local resolved_path resolved_base resolved_root
  resolved_path="$(canonical_ffmpeg_path "$path")" || {
    printf 'Could not resolve FFmpeg output path %s.\n' "$path" >&2
    exit 1
  }
  resolved_base="$(canonical_ffmpeg_path "$allowed_base")" || {
    printf 'Could not resolve FFmpeg output base %s.\n' "$allowed_base" >&2
    exit 1
  }
  resolved_root="$(cd -P "$ROOT" && pwd -P)"
  case "$resolved_base" in
    "$resolved_root"/*) ;;
    *) printf 'Refusing to use an FFmpeg output base outside the repository: %s.\n' "$allowed_base" >&2; exit 1 ;;
  esac
  case "$resolved_path" in
    "$resolved_base"/*) ;;
    *) printf 'Refusing to replace an FFmpeg build outside %s.\n' "$allowed_base" >&2; exit 1 ;;
  esac
}

set_relocatable_ffmpeg_manifests() {
  local install_root="${1:?FFmpeg install root is required}"
  local manifest
  for manifest in "$install_root"/lib/pkgconfig/*.pc; do
    [[ -f "$manifest" ]] || continue
    sed -i.bak \
      -e 's|^prefix=.*$|prefix=${pcfiledir}/../..|' \
      -e 's|^exec_prefix=.*$|exec_prefix=${prefix}|' \
      -e 's|^libdir=.*$|libdir=${prefix}/lib|' \
      -e 's|^includedir=.*$|includedir=${prefix}/include|' "$manifest"
    rm -f "$manifest.bak"
  done
}

publish_ffmpeg_output() {
  local source="${1:?FFmpeg cache source is required}"
  local destination="${2:?FFmpeg publication destination is required}"
  local publication_base="$ROOT/Build/Dependencies/ffmpeg"
  require_ffmpeg_output_path "$source" "$CACHE_BASE"
  require_ffmpeg_output_path "$destination" "$publication_base"
  set_relocatable_ffmpeg_manifests "$source/install"
  remove_ffmpeg_output "$destination" "$publication_base"
  mkdir -p "$destination"
  cp -a "$source/install" "$destination/"
  cp "$source/config_components.h" "$source/keire-ffmpeg.stamp" "$destination/"
}

require_ffmpeg_output_path "$CACHE_OUTPUT" "$CACHE_BASE"
require_ffmpeg_output_path "$OUTPUT" "$ROOT/Build/Dependencies/ffmpeg"

[[ -x "$VENDOR_SOURCE/configure" ]] || {
  printf 'Vendor/ffmpeg is unavailable. Initialize the locked submodule first.\n' >&2
  exit 1
}
[[ "$(git -C "$VENDOR_SOURCE" rev-parse HEAD)" == "$COMMIT" ]] || {
  printf 'Vendor/ffmpeg is not at locked commit %s.\n' "$COMMIT" >&2
  exit 1
}
if valid_ffmpeg_output "$CACHE_OUTPUT" "$EXPECTED"; then
  if ! valid_ffmpeg_output "$OUTPUT" "$EXPECTED"; then
    publish_ffmpeg_output "$CACHE_OUTPUT" "$OUTPUT"
    printf '==> Restored private FFmpeg %s from the %s-%s-%s cache\n' \
      "$CONFIGURATION" "$SYSTEM" "$OUTPUT_ARCHITECTURE" "$TOOLSET"
  else
    printf '==> Private FFmpeg %s build is current\n' "$CONFIGURATION"
  fi
  exit 0
fi
if valid_ffmpeg_output "$OUTPUT" "$EXPECTED"; then
  remove_ffmpeg_output "$CACHE_OUTPUT" "$CACHE_BASE"
  mkdir -p "$CACHE_OUTPUT"
  cp -a "$OUTPUT/install" "$CACHE_OUTPUT/"
  cp "$OUTPUT/config_components.h" "$OUTPUT/keire-ffmpeg.stamp" "$CACHE_OUTPUT/"
  printf '==> Adopted private FFmpeg %s into the %s-%s-%s cache\n' \
    "$CONFIGURATION" "$SYSTEM" "$OUTPUT_ARCHITECTURE" "$TOOLSET"
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

remove_ffmpeg_output "$CACHE_OUTPUT" "$CACHE_BASE"
SOURCE="$CACHE_OUTPUT/source"
ZLIB_LINK_DIRECTORY="$CACHE_OUTPUT/zlib-lib"
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
    --extra-cflags="-arch $MACOS_ARCHITECTURE -mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET"
    --extra-ldflags="-arch $MACOS_ARCHITECTURE -mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET")
fi
(
  cd "$CACHE_OUTPUT"
  "$SOURCE/configure" \
    --prefix="$INSTALL" \
    --arch="$FFMPEG_ARCHITECTURE" \
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
  (cd "$SOURCE" && find . -type d -exec mkdir -p "$CACHE_OUTPUT"/{} \;)
  make -j"$(build_parallel_jobs)"
  make install
)

mkdir -p "$INSTALL/share/licenses/ffmpeg"
cp "$SOURCE/COPYING.LGPLv2.1" "$SOURCE/COPYING.LGPLv3" "$INSTALL/share/licenses/ffmpeg/"
printf 'Source: Vendor/ffmpeg\nCommit: %s\nConfiguration: %s\n' "$COMMIT" "$CONFIGURATION" \
  >"$INSTALL/share/licenses/ffmpeg/SOURCE.txt"
printf '%s\n' "$EXPECTED" >"$STAMP"
publish_ffmpeg_output "$CACHE_OUTPUT" "$OUTPUT"
