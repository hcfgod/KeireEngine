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
if [[ "$toolset" == clang ]]; then
  export CC=clang CXX=clang++
else
  export CC=gcc CXX=g++
fi

sdl_install="$ROOT/Build/Dependencies/$system-$output_arch-$toolset/Release/install"
[[ -f "$sdl_install/cmake/SDL3Config.cmake" ]] || { printf 'Release SDL must be built before KeireShaderCompiler.\n' >&2; exit 1; }
cache_root="$ROOT/Build/Tools/ShaderCompiler/Cache/$system-$output_arch-$toolset"
install_root="$cache_root/install"
published_root="$ROOT/Build/Tools/ShaderCompiler"
published_compiler="$published_root/KeireShaderCompiler"
stamp="$cache_root/keire-shader-compiler.stamp"
configure_stamp="$cache_root/keire-shader-compiler.configure"
lock="$ROOT/Config/Dependencies.lock"
macos_deployment_target="$(config_value "$lock" MACOS_DEPLOYMENT_TARGET)"
key="$(config_value "$lock" SDL_SHADERCROSS_COMMIT)|$(config_value "$lock" SDL_SHADERCROSS_DXC_COMMIT)|$(config_value "$lock" SDL_SHADERCROSS_SPIRV_CROSS_COMMIT)|$(config_value "$lock" SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT)|$(config_value "$lock" SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT)|$(config_value "$lock" SDL_COMMIT)|$macos_deployment_target|$architecture|$toolset|$($CXX --version | head -n 1)"
if [[ "$force" != 1 && -x "$published_compiler" && -f "$stamp" && "$(tr -d '\r\n' < "$stamp")" == "$key" ]]; then
  printf '==> KeireShaderCompiler cache is current\n'
  exit 0
fi

allowed="$ROOT/Build/Tools/ShaderCompiler/Cache/"
[[ "$cache_root/" == "$allowed"* ]] || { printf 'Refusing to replace shader compiler cache outside %s.\n' "$allowed" >&2; exit 1; }
configured_key=""; [[ -f "$configure_stamp" ]] && configured_key="$(tr -d '\r\n' < "$configure_stamp")"
if [[ "$force" == 1 || ! -f "$cache_root/CMakeCache.txt" || ( -n "$configured_key" && "$configured_key" != "$key" ) ]]; then
  rm -rf "$cache_root"
fi
mkdir -p "$cache_root"
options=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_root" -DCMAKE_PREFIX_PATH="$sdl_install"
  -DSDLSHADERCROSS_VENDORED=ON -DSDLSHADERCROSS_DXC=ON -DSDLSHADERCROSS_SHARED=OFF
  -DSDLSHADERCROSS_STATIC=ON -DSDLSHADERCROSS_SPIRVCROSS_SHARED=OFF -DSDLSHADERCROSS_CLI=ON
  -DSDLSHADERCROSS_CLI_STATIC=ON -DSDLSHADERCROSS_TESTS=OFF -DSDLSHADERCROSS_INSTALL=ON
  -DSDLSHADERCROSS_INSTALL_RUNTIME=ON)
if [[ "$platform" == Mac ]]; then
  cmake_architecture=x86_64; [[ "$architecture" == ARM64 ]] && cmake_architecture=arm64
  options+=("-DCMAKE_OSX_ARCHITECTURES=$cmake_architecture"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_deployment_target")
fi
printf '==> Configuring the pinned host shader compiler\n'
cmake -S "$ROOT/Vendor/SDL_shadercross" -B "$cache_root" -G Ninja "${options[@]}"
printf '%s\n' "$key" > "$configure_stamp"
cmake --build "$cache_root" --target install --parallel
built_compiler="$(find "$install_root" -type f -name shadercross -print -quit)"
[[ -n "$built_compiler" ]] || { printf 'SDL_shadercross did not install its command-line compiler.\n' >&2; exit 1; }
mkdir -p "$published_root"
cp "$built_compiler" "$published_compiler"
chmod +x "$published_compiler"
find "$install_root" -type f \( -name '*.so*' -o -name '*.dylib' \) -exec cp {} "$published_root" \;
printf '%s\n' "$key" > "$stamp"
printf '==> KeireShaderCompiler published to %s\n' "$published_compiler"
