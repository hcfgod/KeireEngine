#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/common.sh"
platform="${1:?platform is required}"
architecture="${2:?architecture is required}"
toolset="${3:?toolset is required}"
force="${4:-0}"
system=linux; [[ "$platform" == Mac ]] && system=macosx
output_arch="$(architecture_output_name "$architecture")"
sdl_commit="$(config_value "$ROOT/Config/Dependencies.lock" SDL_COMMIT)"
json_commit="$(config_value "$ROOT/Config/Dependencies.lock" JSON_COMMIT)"
if [[ "$toolset" == clang ]]; then export CC=clang CXX=clang++; else export CC=gcc CXX=g++; fi
compiler="$($CXX --version | head -n 1)"
bridge="$ROOT/Scripts/Dependencies/CMakeLists.txt"
if command -v sha256sum >/dev/null 2>&1; then bridge_hash="$(sha256sum "$bridge" | awk '{print $1}')"; else bridge_hash="$(shasum -a 256 "$bridge" | awk '{print $1}')"; fi
options=(-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
  -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF
  -DSDL_RENDER=OFF -DSDL_GPU=ON -DSDL_DUMMYVIDEO=ON -DSDL_OFFSCREEN=ON -DSDL_INSTALL=ON
  -DSDL_INSTALL_DOCS=OFF -DSDL_DEPS_SHARED=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_LIBDIR=lib)
if [[ "$platform" == Mac ]]; then
  cmake_architecture=x86_64; [[ "$architecture" == ARM64 ]] && cmake_architecture=arm64
  options+=("-DCMAKE_OSX_ARCHITECTURES=$cmake_architecture")
fi
key="$sdl_commit|$architecture|$toolset|$compiler|$bridge_hash|${options[*]}"
base="$ROOT/Build/Dependencies/$system-$output_arch-$toolset"

for configuration in Debug Release; do
  build="$base/$configuration"; install="$build/install"; library="$install/lib/libSDL3.a"; stamp="$build/keire-dependency.stamp"
  if [[ "$force" != 1 && -f "$library" && -f "$stamp" && "$(tr -d '\r\n' < "$stamp")" == "$key|$configuration" ]]; then
    printf '==> SDL %s dependency cache is current\n' "$configuration"
    continue
  fi
  [[ "$build" == "$base/Debug" || "$build" == "$base/Release" ]] || { printf 'Refusing to replace dependency cache outside %s.\n' "$base" >&2; exit 1; }
  rm -rf "$build"
  mkdir -p "$build"
  cmake -S "$ROOT/Scripts/Dependencies" -B "$build" -G Ninja -DKEIRE_SDL_SOURCE="$ROOT/Vendor/SDL" -DCMAKE_BUILD_TYPE="$configuration" -DCMAKE_INSTALL_PREFIX="$install" "${options[@]}"
  cmake --build "$build" --target install --parallel
  [[ -f "$library" && -f "$install/include/SDL3/SDL.h" && -f "$install/cmake/SDL3Config.cmake" ]] || { printf 'SDL %s install is incomplete.\n' "$configuration" >&2; exit 1; }
  printf '%s\n' "$key|$configuration" > "$stamp"
done

bash "$SCRIPT_DIR/shader-compiler.sh" "$platform" "$architecture" "$toolset" "$force"

mkdir -p "$ROOT/Build/Generated"
debug_install="../Build/Dependencies/$system-$output_arch-$toolset/Debug/install"
release_install="../Build/Dependencies/$system-$output_arch-$toolset/Release/install"
platform_links='{ "dl", "m", "pthread" }'
if [[ "$platform" == Mac ]]; then
  platform_links='{ "Cocoa.framework", "CoreVideo.framework", "IOKit.framework", "CoreFoundation.framework", "CoreAudio.framework", "AudioToolbox.framework", "ForceFeedback.framework", "Carbon.framework", "Metal.framework", "QuartzCore.framework", "UniformTypeIdentifiers.framework" }'
fi
cat > "$ROOT/Build/Generated/Dependencies.lua" <<EOF
DependencyManifest = {
    SDLCommit = "$sdl_commit",
    JSONCommit = "$json_commit",
    SDL3Include = "$debug_install/include",
    SDL3DebugLibrary = "$debug_install/lib/libSDL3.a",
    SDL3ReleaseLibrary = "$release_install/lib/libSDL3.a",
    SDL3PlatformLinks = $platform_links
}
EOF
printf '==> Dependency manifest generated\n'
