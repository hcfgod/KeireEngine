#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/common.sh"

host_bison_version=""
if [[ "$(uname -s)" == Darwin ]]; then
  export PERL5LIB="$ROOT/Scripts/Dependencies${PERL5LIB:+:$PERL5LIB}"
  perl -MJSON -e 1 || {
    printf 'The bundled Perl JSON compatibility module is unavailable.\n' >&2
    exit 1
  }
  bison_prefix="$(brew --prefix bison 2>/dev/null || true)"
  [[ -n "$bison_prefix" && -x "$bison_prefix/bin/bison" ]] || {
    printf 'Homebrew Bison is required to build the macOS host shader compiler. Run the platform bootstrap first.\n' >&2
    exit 1
  }
  export PATH="$bison_prefix/bin:$PATH"
  host_bison_version="$(bison --version | extract_version)"
  version_at_least "$host_bison_version" 3.0 || {
    printf 'Bison %s is older than the required 3.0 for the macOS host shader compiler.\n' "$host_bison_version" >&2
    exit 1
  }
fi
platform="${1:?platform is required}"
architecture="${2:?architecture is required}"
toolset="${3:?toolset is required}"
force="${4:-0}"
system=linux; [[ "$platform" == Mac ]] && system=macosx
output_arch="$(architecture_output_name "$architecture")"
# DirectXShaderCompiler is an LLVM-derived host tool. Building it with the bootstrapped Clang toolchain avoids
# GCC-version-specific optimizer failures while leaving the requested project toolset unchanged.
export CC=clang CXX=clang++
if ! command -v "$CC" >/dev/null 2>&1 || ! command -v "$CXX" >/dev/null 2>&1; then
  printf 'Clang is required to build the host shader compiler. Run the platform bootstrap first.\n' >&2
  exit 1
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
key="$(config_value "$lock" SDL_SHADERCROSS_COMMIT)|$(config_value "$lock" SDL_SHADERCROSS_DXC_COMMIT)|$(config_value "$lock" SDL_SHADERCROSS_SPIRV_CROSS_COMMIT)|$(config_value "$lock" SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT)|$(config_value "$lock" SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT)|$(config_value "$lock" SDL_COMMIT)|flat-runtime-v4|$macos_deployment_target|$architecture|$toolset|$host_bison_version|$($CXX --version | head -n 1)"
if [[ "$force" != 1 && -x "$published_compiler" && -f "$stamp" && "$(tr -d '\r\n' < "$stamp")" == "$key" ]] &&
   "$published_compiler" --help >/dev/null 2>&1; then
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
  -DCMAKE_INSTALL_BINDIR=. -DCMAKE_INSTALL_LIBDIR=.
  -DSDLSHADERCROSS_VENDORED=ON -DSDLSHADERCROSS_DXC=ON -DSDLSHADERCROSS_SHARED=OFF
  -DSDLSHADERCROSS_STATIC=ON -DSDLSHADERCROSS_SPIRVCROSS_SHARED=OFF -DSDLSHADERCROSS_CLI=ON
  -DSDLSHADERCROSS_CLI_STATIC=ON -DSDLSHADERCROSS_TESTS=OFF -DSDLSHADERCROSS_INSTALL=ON
  -DSDLSHADERCROSS_INSTALL_RUNTIME=ON -DSPIRV_WERROR=OFF)
if [[ "$platform" == Mac ]]; then
  cmake_architecture=x86_64; [[ "$architecture" == ARM64 ]] && cmake_architecture=arm64
  options+=("-DCMAKE_OSX_ARCHITECTURES=$cmake_architecture"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_deployment_target")
else
  options+=("-DCMAKE_BUILD_RPATH=\$ORIGIN")
fi
printf '==> Configuring the pinned host shader compiler\n'
cmake -S "$ROOT/Vendor/SDL_shadercross" -B "$cache_root" -G Ninja "${options[@]}"
printf '%s\n' "$key" > "$configure_stamp"
cmake --build "$cache_root" --target install --parallel "$(build_parallel_jobs)"
built_compiler="$(find "$install_root" -type f -name shadercross -print -quit)"
[[ -n "$built_compiler" ]] || { printf 'SDL_shadercross did not install its command-line compiler.\n' >&2; exit 1; }
mkdir -p "$published_root"
find "$published_root" -maxdepth 1 \( -type f -o -type l \) \
  \( -name '*.so*' -o -name '*.dylib*' \) -delete
cp "$built_compiler" "$published_compiler"
chmod +x "$published_compiler"
if [[ "$platform" == Mac ]]; then
  install_name_tool -add_rpath '@executable_path' "$published_compiler"
else
  readelf -d "$published_compiler" | grep -Eq '\((RUNPATH|RPATH)\).*[$]ORIGIN' || {
    printf 'Published KeireShaderCompiler is missing its $ORIGIN runtime search path.\n' >&2
    exit 1
  }
fi
while IFS= read -r -d '' runtime_library; do
  cp -L "$runtime_library" "$published_root/$(basename "$runtime_library")"
done < <(find "$install_root" \( -type f -o -type l \) \
  \( -name '*.so*' -o -name '*.dylib*' \) -print0)
if ! "$published_compiler" --help >/dev/null 2>&1; then
  printf 'Published KeireShaderCompiler cannot load its bundled runtime libraries.\n' >&2
  exit 1
fi
printf '%s\n' "$key" > "$stamp"
printf '==> KeireShaderCompiler published to %s\n' "$published_compiler"
