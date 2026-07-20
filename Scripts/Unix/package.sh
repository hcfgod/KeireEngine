#!/usr/bin/env bash
set -euo pipefail
PLATFORM="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"; source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Release; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"; [[ "$CONFIGURATION" == Release || "$CONFIGURATION" == Dist ]] || { printf 'Package requires Release or Dist.\n' >&2; exit 1; }
load_project_config "$ROOT"; TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"; system=linux; os_name=linux; [[ "$PLATFORM" == Mac ]] && { system=macosx; os_name=macos; }
bash "$ROOT/Scripts/Unix/build-info.sh"
command -v cmake >/dev/null 2>&1 || { printf 'CMake 3.20 or newer is required for SDK package validation.\n' >&2; exit 1; }
common=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET"); [[ $CI -eq 1 ]] && common+=(--ci)
test_args=("${common[@]}"); [[ $UPDATE -eq 1 ]] && test_args+=(--update); [[ $FORCE -eq 1 ]] && test_args+=(--force)
bash "$ROOT/Scripts/$PLATFORM/test.sh" "${test_args[@]}"; KEIRE_SMOKE_WINDOW=1 bash "$ROOT/Scripts/$PLATFORM/run.sh" "${common[@]}"
asset_tool="${PROJECT_NAMESPACE}AssetTool"; bash "$ROOT/Scripts/$PLATFORM/build.sh" "${common[@]}" --target "$asset_tool"
bash "$ROOT/Scripts/$PLATFORM/build.sh" "${common[@]}" --target "$HUB_TARGET"
bash "$ROOT/Scripts/Unix/shader-compiler.sh" "$PLATFORM" "$ARCHITECTURE" "$TOOLSET"
output_arch="$(architecture_output_name "$ARCHITECTURE")"; name="$ARTIFACT_PREFIX-$os_name-$ARCHITECTURE-$CONFIGURATION"; stage="$ROOT/Artifacts/$name"
imgui_library="${PROJECT_NAMESPACE}ImGui"
zstd_library="${PROJECT_NAMESPACE}Zstd"
archive="$ROOT/Artifacts/$name.tar.gz"; symbols="$ROOT/Artifacts/$name-symbols.tar.gz"; symbol_stage="$ROOT/Artifacts/$name-symbols"
rm -rf "$stage" "$symbol_stage"; rm -f "$archive" "$archive.sha256" "$symbols" "$symbols.sha256"
mkdir -p "$stage/bin" "$stage/lib" "$stage/include" "$stage/Config" "$stage/samples" "$stage/third-party/spdlog" "$stage/third-party/licenses" "$stage/third-party/SDL3" "$stage/examples/consumer" "$stage/examples/managed-consumer" "$stage/lib/cmake/$PROJECT_IDENTIFIER"
client_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$CLIENT_TARGET/$CLIENT_TARGET"
hub_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$HUB_TARGET/$HUB_TARGET"
core_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$CORE_TARGET/lib$CORE_TARGET.a"
imgui_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/DearImGui/lib$imgui_library.a"
zstd_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/Zstd/lib$zstd_library.a"
cp "$client_source" "$hub_source" "$stage/bin/"; cp "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$asset_tool/$asset_tool" "$stage/bin/"; cp "$core_source" "$imgui_source" "$zstd_source" "$stage/lib/"
cp "$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler" "$stage/bin/"
find "$ROOT/Build/Tools/ShaderCompiler" -maxdepth 1 -type f \( -name '*.so*' -o -name '*.dylib' \) -exec cp {} "$stage/bin/" \;
cp "$ROOT/Config/Client.json" "$stage/Config/Client.json"
copy_tracked_tree "$ROOT" "Samples/KeireSandbox" "$stage/samples/KeireSandbox"
cp -R "$ROOT/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE" "$stage/include/"; cp -R "$ROOT/Vendor/spdlog/include/spdlog/"* "$stage/third-party/spdlog/"
cp "$ROOT/Vendor/spdlog/LICENSE" "$stage/third-party/licenses/spdlog-LICENSE.txt"
cp "$ROOT/Vendor/spdlog/include/spdlog/fmt/bundled/fmt.license.rst" "$stage/third-party/licenses/fmt-LICENSE.rst"
cp "$ROOT/Vendor/doctest/LICENSE.txt" "$stage/third-party/licenses/doctest-LICENSE.txt"
cp "$ROOT/Vendor/json/LICENSE.MIT" "$stage/third-party/licenses/nlohmann-json-LICENSE.MIT.txt"
cp "$ROOT/Vendor/imgui/LICENSE.txt" "$stage/third-party/licenses/dear-imgui-LICENSE.txt"
cp "$ROOT/Vendor/zstd/LICENSE" "$stage/third-party/licenses/zstandard-LICENSE.txt"
cp "$ROOT/Vendor/entt/LICENSE" "$stage/third-party/licenses/entt-LICENSE.txt"
cp "$ROOT/Vendor/glm/copying.txt" "$stage/third-party/licenses/glm-COPYING.txt"
cp "$ROOT/Vendor/SDL_shadercross/LICENSE.txt" "$stage/third-party/licenses/SDL-shadercross-LICENSE.txt"
cp "$ROOT/Vendor/SDL_shadercross/external/DirectXShaderCompiler/LICENSE.TXT" "$stage/third-party/licenses/DirectXShaderCompiler-LICENSE.txt"
cp "$ROOT/Vendor/SDL_shadercross/external/DirectXShaderCompiler/ThirdPartyNotices.txt" "$stage/third-party/licenses/DirectXShaderCompiler-ThirdPartyNotices.txt"
cp "$ROOT/Vendor/SDL_shadercross/external/SPIRV-Cross/LICENSE" "$stage/third-party/licenses/SPIRV-Cross-LICENSE.txt"
cp "$ROOT/Vendor/SDL_shadercross/external/SPIRV-Headers/LICENSE" "$stage/third-party/licenses/SPIRV-Headers-LICENSE.txt"
cp "$ROOT/Vendor/SDL_shadercross/external/SPIRV-Tools/LICENSE" "$stage/third-party/licenses/SPIRV-Tools-LICENSE.txt"
sdl_install="$ROOT/Build/Dependencies/$system-$output_arch-$TOOLSET/Release/install"
[[ -f "$sdl_install/lib/libSDL3.a" ]] || { printf 'Packaged SDL Release dependency is missing.\n' >&2; exit 1; }
cp -R "$sdl_install/"* "$stage/third-party/SDL3/"
cp "$ROOT/README.md" "$ROOT/LICENSE.txt" "$ROOT/THIRD_PARTY_NOTICES.md" "$stage/"
cp -R "$ROOT/Examples/Consumer/"* "$stage/examples/consumer/"
cp -R "$ROOT/Examples/ManagedConsumer/"* "$stage/examples/managed-consumer/"
sed -e "s/@CORE_TARGET@/$CORE_TARGET/g" -e "s/@PROJECT_NAMESPACE@/$PROJECT_NAMESPACE/g" -e "s/@PACKAGE_CONFIGURATION@/$CONFIGURATION/g" "$ROOT/Config/PackageConfig.cmake.in" > "$stage/lib/cmake/$PROJECT_IDENTIFIER/${PROJECT_IDENTIFIER}Config.cmake"
commit="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf unknown)"; spdlog="$(config_value "$ROOT/Config/Dependencies.lock" SPDLOG_COMMIT)"; doctest="$(config_value "$ROOT/Config/Dependencies.lock" DOCTEST_COMMIT)"; sdl="$(config_value "$ROOT/Config/Dependencies.lock" SDL_COMMIT)"; json="$(config_value "$ROOT/Config/Dependencies.lock" JSON_COMMIT)"; imgui="$(config_value "$ROOT/Config/Dependencies.lock" IMGUI_COMMIT)"; zstd="$(config_value "$ROOT/Config/Dependencies.lock" ZSTD_COMMIT)"; entt="$(config_value "$ROOT/Config/Dependencies.lock" ENTT_COMMIT)"; glm="$(config_value "$ROOT/Config/Dependencies.lock" GLM_COMMIT)"; shadercross="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_COMMIT)"; dxc="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_DXC_COMMIT)"; spirv_cross="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_CROSS_COMMIT)"; spirv_headers="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT)"; spirv_tools="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT)"
dirty=false; [[ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal 2>/dev/null || true)" ]] && dirty=true
platform_name=Linux; [[ "$PLATFORM" == Mac ]] && platform_name=macOS
if [[ "$TOOLSET" == clang ]]; then compiler="Clang $(clang++ -dumpversion)"; else compiler="GCC $(g++ -dumpfullversion -dumpversion)"; fi
printf '{\n  "project": "%s",\n  "version": "%s",\n  "commit": "%s",\n  "dirty": %s,\n  "platform": "%s",\n  "architecture": "%s",\n  "configuration": "%s",\n  "generator": "%s",\n  "toolset": "%s",\n  "compiler": "%s",\n  "spdlog": "%s",\n  "doctest": "%s",\n  "sdl": "%s",\n  "json": "%s",\n  "imgui": "%s",\n  "zstd": "%s",\n  "entt": "%s",\n  "glm": "%s",\n  "sdlShadercross": "%s",\n  "dxc": "%s",\n  "spirvCross": "%s",\n  "spirvHeaders": "%s",\n  "spirvTools": "%s"\n}\n' "$(json_escape "$PROJECT_IDENTIFIER")" "$(json_escape "$PROJECT_VERSION")" "$(json_escape "$commit")" "$dirty" "$(json_escape "$platform_name")" "$(architecture_output_name "$ARCHITECTURE")" "$(json_escape "$CONFIGURATION")" "$(json_escape "$GENERATOR")" "$(json_escape "$TOOLSET")" "$(json_escape "$compiler")" "$(json_escape "$spdlog")" "$(json_escape "$doctest")" "$(json_escape "$sdl")" "$(json_escape "$json")" "$(json_escape "$imgui")" "$(json_escape "$zstd")" "$(json_escape "$entt")" "$(json_escape "$glm")" "$(json_escape "$shadercross")" "$(json_escape "$dxc")" "$(json_escape "$spirv_cross")" "$(json_escape "$spirv_headers")" "$(json_escape "$spirv_tools")" > "$stage/build-manifest.json"
validate_package_stage "$stage" "$CLIENT_TARGET" "$HUB_TARGET" "$CORE_TARGET" "$PROJECT_NAMESPACE"
grep -Fq "\"imgui\": \"$imgui\"" "$stage/build-manifest.json" || { printf 'Packaged Dear ImGui identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"zstd\": \"$zstd\"" "$stage/build-manifest.json" || { printf 'Packaged Zstandard identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"entt\": \"$entt\"" "$stage/build-manifest.json" || { printf 'Packaged EnTT identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"glm\": \"$glm\"" "$stage/build-manifest.json" || { printf 'Packaged GLM identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"sdlShadercross\": \"$shadercross\"" "$stage/build-manifest.json" || { printf 'Packaged SDL_shadercross identity does not match the dependency lock.\n' >&2; exit 1; }
"$stage/bin/KeireShaderCompiler" --help 2>&1 | grep -Fq shadercross || { printf 'Packaged shader compiler validation failed.\n' >&2; exit 1; }
"$stage/bin/$asset_tool" --help | grep -Fq 'KeireAssetTool cook' || { printf 'Packaged asset tool validation failed.\n' >&2; exit 1; }
KEIRE_SHADER_COMPILER="$stage/bin/KeireShaderCompiler" "$stage/bin/$asset_tool" import --project "$stage/samples/KeireSandbox" | grep -Fq 'Imported' || { printf 'Packaged sample project asset validation failed.\n' >&2; exit 1; }
rm -rf "$stage/samples/KeireSandbox/Library" "$stage/samples/KeireSandbox/Logs" "$stage/samples/KeireSandbox/Build" "$stage/samples/KeireSandbox/Temp"
version_output="$("$stage/bin/$CLIENT_TARGET" --version)"
commit_prefix="${commit:0:12}"
expected_identity="$commit_prefix"; [[ "$dirty" == true ]] && expected_identity="${commit_prefix}-dirty"
[[ "$version_output" == *"$expected_identity"* ]] || { printf 'Packaged binary identity does not match build-manifest.json.\n' >&2; exit 1; }
[[ "$dirty" == true || "$version_output" != *"${commit_prefix}-dirty"* ]] || { printf 'Packaged binary reports a stale dirty state.\n' >&2; exit 1; }
assert_package_generated_data_free "$stage"

if [[ "$CONFIGURATION" == Release && "$PLATFORM" == Linux ]]; then
  command -v objcopy >/dev/null 2>&1 || { printf 'objcopy is required to package Linux Release symbols.\n' >&2; exit 1; }
  mkdir -p "$symbol_stage/KeireClient" "$symbol_stage/KeireHub" "$symbol_stage/KeireCore" "$symbol_stage/DearImGui" "$symbol_stage/Zstd"
  objcopy --only-keep-debug "$client_source" "$symbol_stage/KeireClient/$CLIENT_TARGET.debug"
  objcopy --only-keep-debug "$hub_source" "$symbol_stage/KeireHub/$HUB_TARGET.debug"
  cp "$core_source" "$symbol_stage/KeireCore/lib$CORE_TARGET.a"
  cp "$imgui_source" "$symbol_stage/DearImGui/lib$imgui_library.a"
  cp "$zstd_source" "$symbol_stage/Zstd/lib$zstd_library.a"
  objcopy --strip-debug "$stage/bin/$CLIENT_TARGET"
  objcopy --add-gnu-debuglink="$symbol_stage/KeireClient/$CLIENT_TARGET.debug" "$stage/bin/$CLIENT_TARGET"
  objcopy --strip-debug "$stage/bin/$HUB_TARGET"
  objcopy --add-gnu-debuglink="$symbol_stage/KeireHub/$HUB_TARGET.debug" "$stage/bin/$HUB_TARGET"
  objcopy --strip-debug "$stage/lib/lib$CORE_TARGET.a"
  objcopy --strip-debug "$stage/lib/lib$imgui_library.a"
  objcopy --strip-debug "$stage/lib/lib$zstd_library.a"
elif [[ "$CONFIGURATION" == Release && "$PLATFORM" == Mac ]]; then
  mkdir -p "$symbol_stage/KeireClient" "$symbol_stage/KeireHub"
  xcrun dsymutil "$client_source" -o "$symbol_stage/KeireClient/$CLIENT_TARGET.dSYM"
  xcrun dsymutil "$hub_source" -o "$symbol_stage/KeireHub/$HUB_TARGET.dSYM"
  xcrun strip -S "$stage/bin/$CLIENT_TARGET"
  xcrun strip -S "$stage/bin/$HUB_TARGET"
fi

tar -C "$stage" -czf "$archive" .
assert_package_archive_generated_data_free "$archive"
if command -v sha256sum >/dev/null 2>&1; then
  digest="$(sha256sum "$archive" | awk '{print $1}')"
else
  digest="$(shasum -a 256 "$archive" | awk '{print $1}')"
fi
printf '%s  %s\n' "$digest" "$(basename "$archive")" > "$archive.sha256"
if [[ -d "$symbol_stage" ]] && find "$symbol_stage" -type f -print -quit | grep -q .; then
  tar -C "$symbol_stage" -czf "$symbols" .
  if command -v sha256sum >/dev/null 2>&1; then symbol_digest="$(sha256sum "$symbols" | awk '{print $1}')"; else symbol_digest="$(shasum -a 256 "$symbols" | awk '{print $1}')"; fi
  printf '%s  %s\n' "$symbol_digest" "$(basename "$symbols")" > "$symbols.sha256"
fi
validation_root="$ROOT/Artifacts/$name-validation"; rm -rf "$validation_root"; mkdir -p "$validation_root/sdk"
tar -C "$validation_root/sdk" -xzf "$archive"
assert_package_generated_data_free "$validation_root/sdk"
cxx=g++; [[ "$TOOLSET" == clang ]] && cxx=clang++
consumer_compile=("$cxx" -std=c++20 -Wall -Wextra -Werror -DKEIRE_STATIC "-I$validation_root/sdk/include" "-I$validation_root/sdk/third-party" "$validation_root/sdk/examples/consumer/Main.cpp" "$validation_root/sdk/lib/lib$CORE_TARGET.a" "$validation_root/sdk/lib/lib$imgui_library.a" "$validation_root/sdk/lib/lib$zstd_library.a" "$validation_root/sdk/third-party/SDL3/lib/libSDL3.a" -o "$validation_root/consumer")
[[ "$CONFIGURATION" == Dist ]] && consumer_compile+=(-flto)
[[ "$PLATFORM" == Linux ]] && consumer_compile+=(-pthread -ldl -lm)
[[ "$PLATFORM" == Mac ]] && consumer_compile+=(-framework Cocoa -framework CoreVideo -framework IOKit -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework ForceFeedback -framework Carbon -framework Metal -framework QuartzCore -framework UniformTypeIdentifiers)
"${consumer_compile[@]}"
(cd "$validation_root" && ./consumer "$validation_root/sdk/examples/consumer/Client.json")
managed_compile=("$cxx" -std=c++20 -Wall -Wextra -Werror -DKEIRE_STATIC "-I$validation_root/sdk/include" "-I$validation_root/sdk/third-party" "$validation_root/sdk/examples/managed-consumer/ClientApplication.cpp" "$validation_root/sdk/lib/lib$CORE_TARGET.a" "$validation_root/sdk/lib/lib$imgui_library.a" "$validation_root/sdk/lib/lib$zstd_library.a" "$validation_root/sdk/third-party/SDL3/lib/libSDL3.a" -o "$validation_root/managed-consumer")
[[ "$CONFIGURATION" == Dist ]] && managed_compile+=(-flto)
[[ "$PLATFORM" == Linux ]] && managed_compile+=(-pthread -ldl -lm)
[[ "$PLATFORM" == Mac ]] && managed_compile+=(-framework Cocoa -framework CoreVideo -framework IOKit -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework ForceFeedback -framework Carbon -framework Metal -framework QuartzCore -framework UniformTypeIdentifiers)
"${managed_compile[@]}"
managed_help="$($validation_root/managed-consumer --help)"
[[ "$managed_help" == *--managed-smoke* ]] || { printf 'Managed SDK consumer help validation failed.\n' >&2; exit 1; }
(cd "$validation_root" && ./managed-consumer --managed-smoke)
cmake -S "$validation_root/sdk/examples/consumer" -B "$validation_root/cmake-build" -DCMAKE_PREFIX_PATH="$validation_root/sdk" -DCMAKE_BUILD_TYPE=Release
cmake --build "$validation_root/cmake-build" --config Release
(cd "$validation_root" && "$validation_root/cmake-build/SdkConsumer" "$validation_root/sdk/examples/consumer/Client.json")
cmake -S "$validation_root/sdk/examples/managed-consumer" -B "$validation_root/managed-cmake-build" -DCMAKE_PREFIX_PATH="$validation_root/sdk" -DCMAKE_BUILD_TYPE=Release
cmake --build "$validation_root/managed-cmake-build" --config Release
(cd "$validation_root" && "$validation_root/managed-cmake-build/ManagedSdkConsumer" --managed-smoke)
rm -rf "$validation_root"
printf '==> Package created: %s\n' "$archive"
