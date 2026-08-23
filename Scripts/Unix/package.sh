#!/usr/bin/env bash
set -euo pipefail
umask 0022
PLATFORM="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"; source "$ROOT/Scripts/Unix/common.sh"
stage_only=0
filtered_arguments=()
for argument in "$@"; do
  if [[ "$argument" == --stage-only ]]; then stage_only=1; else filtered_arguments+=("$argument"); fi
done
GENERATOR=ninja; CONFIGURATION=Release; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0; ALLOW_DIRTY=0
parse_build_arguments "${filtered_arguments[@]}"; [[ "$CONFIGURATION" == Release || "$CONFIGURATION" == Dist ]] || { printf 'Package requires Release or Dist.\n' >&2; exit 1; }
load_project_config "$ROOT"; TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"; system=linux; os_name=linux; [[ "$PLATFORM" == Mac ]] && { system=macosx; os_name=macos; }
macos_deployment_target="$(config_value "$ROOT/Config/Dependencies.lock" MACOS_DEPLOYMENT_TARGET)"
read -r dirty development_artifact < <(package_worktree_policy "$ROOT" "$ALLOW_DIRTY" "$CI")
bash "$ROOT/Scripts/Unix/build-info.sh"
command -v cmake >/dev/null 2>&1 || { printf 'CMake 3.20 or newer is required for SDK package validation.\n' >&2; exit 1; }
common=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET"); [[ $CI -eq 1 ]] && common+=(--ci)
test_args=("${common[@]}"); [[ $UPDATE -eq 1 ]] && test_args+=(--update); [[ $FORCE -eq 1 ]] && test_args+=(--force)
bash "$ROOT/Scripts/$PLATFORM/test.sh" "${test_args[@]}"
[[ "$PLATFORM" == Linux ]] && activate_linux_toolchain "$ROOT" "$TOOLSET"
KEIRE_SMOKE_WINDOW=1 bash "$ROOT/Scripts/$PLATFORM/run.sh" "${common[@]}"
asset_tool="${PROJECT_NAMESPACE}AssetTool"; bash "$ROOT/Scripts/$PLATFORM/build.sh" "${common[@]}" --target "$asset_tool"
asset_worker="${PROJECT_NAMESPACE}AssetWorker"; bash "$ROOT/Scripts/$PLATFORM/build.sh" "${common[@]}" --target "$asset_worker"
runtime="${PROJECT_NAMESPACE}Runtime"; bash "$ROOT/Scripts/$PLATFORM/build.sh" "${common[@]}" --target "$runtime"
bash "$ROOT/Scripts/$PLATFORM/build.sh" "${common[@]}" --target "$HUB_TARGET"
bash "$ROOT/Scripts/Unix/shader-compiler.sh" "$PLATFORM" "$ARCHITECTURE" "$TOOLSET"
output_arch="$(architecture_output_name "$ARCHITECTURE")"; name="$ARTIFACT_PREFIX-$os_name-$ARCHITECTURE-$CONFIGURATION"; stage="$ROOT/Artifacts/$name"
imgui_library="${PROJECT_NAMESPACE}ImGui"
zstd_library="${PROJECT_NAMESPACE}Zstd"
archive="$ROOT/Artifacts/$name.tar.gz"; symbols="$ROOT/Artifacts/$name-symbols.tar.gz"; symbol_stage="$ROOT/Artifacts/$name-symbols"
rm -rf "$stage" "$symbol_stage"
[[ $stage_only -eq 1 ]] || rm -f "$archive" "$archive.sha256" "$symbols" "$symbols.sha256"
mkdir -p "$stage/bin" "$stage/lib" "$stage/include" "$stage/Config" "$stage/samples" "$stage/content" "$stage/Docs/Diagnostics" "$stage/third-party/licenses" "$stage/third-party/SDL3" "$stage/examples/consumer" "$stage/examples/managed-consumer" "$stage/examples/source-module" "$stage/lib/cmake/$PROJECT_IDENTIFIER"
client_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$CLIENT_TARGET/$CLIENT_TARGET"
hub_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$HUB_TARGET/$HUB_TARGET"
core_archive_targets=("$CORE_TARGET")
for suffix in Assets Build World Rendering Scenes Scripting Ui Vfx; do core_archive_targets+=("$CORE_TARGET$suffix"); done
core_archive_sources=()
for core_archive_target in "${core_archive_targets[@]}"; do
  core_archive_sources+=("$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$core_archive_target/lib$core_archive_target.a")
done
core_source="$stage/lib/lib$CORE_TARGET.a"
imgui_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/DearImGui/lib$imgui_library.a"
zstd_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/Zstd/lib$zstd_library.a"
cp "$client_source" "$hub_source" "$stage/bin/"; cp "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$asset_tool/$asset_tool" "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$asset_worker/$asset_worker" "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$runtime/$runtime" "$stage/bin/"
bash "$ROOT/Scripts/Unix/merge-static-libraries.sh" "$core_source" "${core_archive_sources[@]}"
cp "$imgui_source" "$zstd_source" "$stage/lib/"
find "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$asset_worker" -maxdepth 1 \
  \( -type f -o -type l \) \
  \( -name 'libav*.so*' -o -name 'libav*.dylib' -o -name 'libswresample*.so*' -o -name 'libswresample*.dylib' \) \
  -exec cp -L {} "$stage/bin/" \;
cp -R "$ROOT/Build/Dependencies/ffmpeg/Release/install/share/licenses/ffmpeg" "$stage/third-party/licenses/"
cp -R "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$runtime/Managed" "$stage/bin/"
dependency_install="$ROOT/Build/Dependencies/$system-$output_arch-$TOOLSET/Release/install"
cp "$dependency_install/lib/libassimp.a" "$dependency_install/lib/libzlibstatic.a" "$dependency_install/lib/libJolt.a" "$dependency_install/lib/libRecast.a" "$dependency_install/lib/libDetour.a" "$dependency_install/lib/libDetourCrowd.a" "$dependency_install/lib/libDetourTileCache.a" "$dependency_install/lib/libminiaudio.a" "$stage/lib/"
cp "$ROOT/Build/Dependencies/coral-patched/Build/Release/libCoral.Native.a" "$stage/lib/"
cp "$ROOT/Build/Dependencies/coral-nethost/libnethost.a" "$stage/lib/"
cp "$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler" "$stage/bin/"
find "$ROOT/Build/Tools/ShaderCompiler" -maxdepth 1 -type f \( -name '*.so*' -o -name '*.dylib' \) -exec cp {} "$stage/bin/" \;
cp "$ROOT/Config/Client.json" "$stage/Config/Client.json"
copy_tracked_tree "$ROOT" "Samples/KeireSandbox" "$stage/samples/KeireSandbox"
cp -R "$ROOT/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE" "$stage/include/"
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
cp "$ROOT/Vendor/assimp/LICENSE" "$stage/third-party/licenses/assimp-LICENSE.txt"
cp "$ROOT/Vendor/assimp/contrib/zlib/LICENSE" "$stage/third-party/licenses/assimp-zlib-LICENSE.txt"
cp "$ROOT/Vendor/stb/LICENSE" "$stage/third-party/licenses/stb-LICENSE.txt"
cp "$dependency_install/share/licenses/keire/Jolt-LICENSE.txt" "$dependency_install/share/licenses/keire/Recast-LICENSE.txt" "$dependency_install/share/licenses/keire/miniaudio-LICENSE.txt" "$stage/third-party/licenses/"
cp "$ROOT/Build/Dependencies/coral-patched/LICENSE" "$stage/third-party/licenses/Coral-LICENSE.txt"
cp "$ROOT/Build/Dependencies/dotnet-sdk/LICENSE.txt" "$stage/third-party/licenses/dotnet-LICENSE.txt"
cp "$ROOT/Build/Dependencies/dotnet-sdk/ThirdPartyNotices.txt" "$stage/third-party/licenses/dotnet-ThirdPartyNotices.txt"
sdl_install="$dependency_install"
[[ -f "$sdl_install/lib/libSDL3.a" ]] || { printf 'Packaged SDL Release dependency is missing.\n' >&2; exit 1; }
mkdir -p "$stage/third-party/SDL3/include" "$stage/third-party/SDL3/lib" "$stage/third-party/SDL3/cmake" "$stage/third-party/SDL3/licenses"
cp -R "$sdl_install/include/SDL3" "$stage/third-party/SDL3/include/"
cp "$sdl_install/lib/libSDL3.a" "$stage/third-party/SDL3/lib/"
cp -R "$sdl_install/cmake/"* "$stage/third-party/SDL3/cmake/"
cp -R "$sdl_install/share/licenses/SDL3" "$stage/third-party/SDL3/licenses/"
cp "$ROOT/README.md" "$ROOT/LICENSE.txt" "$ROOT/THIRD_PARTY_NOTICES.md" "$stage/"
cp -R "$ROOT/Docs/Diagnostics/"* "$stage/Docs/Diagnostics/"
cp "$ROOT/Docs/PlayerBuilds.md" "$stage/Docs/"
cp "$ROOT/Config/SourceModules.premake.lua" "$stage/Config/"
cp -R "$ROOT/Examples/Consumer/"* "$stage/examples/consumer/"
cp -R "$ROOT/Examples/ManagedConsumer/"* "$stage/examples/managed-consumer/"
cp -R "$ROOT/Examples/SourceModule/"* "$stage/examples/source-module/"
sed -e "s/@CORE_TARGET@/$CORE_TARGET/g" -e "s/@PROJECT_NAMESPACE@/$PROJECT_NAMESPACE/g" -e "s/@PACKAGE_CONFIGURATION@/$CONFIGURATION/g" "$ROOT/Config/PackageConfig.cmake.in" > "$stage/lib/cmake/$PROJECT_IDENTIFIER/${PROJECT_IDENTIFIER}Config.cmake"
commit="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf unknown)"; spdlog="$(config_value "$ROOT/Config/Dependencies.lock" SPDLOG_COMMIT)"; doctest="$(config_value "$ROOT/Config/Dependencies.lock" DOCTEST_COMMIT)"; sdl="$(config_value "$ROOT/Config/Dependencies.lock" SDL_COMMIT)"; json="$(config_value "$ROOT/Config/Dependencies.lock" JSON_COMMIT)"; imgui="$(config_value "$ROOT/Config/Dependencies.lock" IMGUI_COMMIT)"; zstd="$(config_value "$ROOT/Config/Dependencies.lock" ZSTD_COMMIT)"; entt="$(config_value "$ROOT/Config/Dependencies.lock" ENTT_COMMIT)"; glm="$(config_value "$ROOT/Config/Dependencies.lock" GLM_COMMIT)"; shadercross="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_COMMIT)"; dxc="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_DXC_COMMIT)"; spirv_cross="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_CROSS_COMMIT)"; spirv_headers="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT)"; spirv_tools="$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT)"; assimp="$(config_value "$ROOT/Config/Dependencies.lock" ASSIMP_COMMIT)"; stb="$(config_value "$ROOT/Config/Dependencies.lock" STB_COMMIT)"; jolt="$(config_value "$ROOT/Config/Dependencies.lock" JOLT_COMMIT)"; recast="$(config_value "$ROOT/Config/Dependencies.lock" RECAST_COMMIT)"; miniaudio="$(config_value "$ROOT/Config/Dependencies.lock" MINIAUDIO_COMMIT)"; coral="$(config_value "$ROOT/Config/Dependencies.lock" CORAL_COMMIT)"
dotnet_runtime="$(find "$ROOT/Build/Dependencies/dotnet-sdk/shared/Microsoft.NETCore.App" -mindepth 1 -maxdepth 1 -type d -print | sort | tail -n 1)"
dotnet_runtime="$(basename "$dotnet_runtime")"
platform_name=Linux; [[ "$PLATFORM" == Mac ]] && platform_name=macOS
if [[ "$TOOLSET" == clang ]]; then compiler="Clang $(clang++ -dumpversion)"; else compiler="GCC $(g++ -dumpfullversion -dumpversion)"; fi
printf '{\n  "project": "%s",\n  "version": "%s",\n  "commit": "%s",\n  "dirty": %s,\n  "developmentArtifact": %s,\n  "platform": "%s",\n  "architecture": "%s",\n  "configuration": "%s",\n  "generator": "%s",\n  "toolset": "%s",\n  "compiler": "%s",\n  "spdlog": "%s",\n  "doctest": "%s",\n  "sdl": "%s",\n  "json": "%s",\n  "imgui": "%s",\n  "zstd": "%s",\n  "entt": "%s",\n  "glm": "%s",\n  "sdlShadercross": "%s",\n  "dxc": "%s",\n  "spirvCross": "%s",\n  "spirvHeaders": "%s",\n  "spirvTools": "%s",\n  "assimp": "%s",\n  "stb": "%s",\n  "jolt": "%s",\n  "recast": "%s",\n  "miniaudio": "%s",\n  "coral": "%s",\n  "dotnetRuntime": "%s"\n}\n' "$(json_escape "$PROJECT_IDENTIFIER")" "$(json_escape "$PROJECT_VERSION")" "$(json_escape "$commit")" "$dirty" "$development_artifact" "$(json_escape "$platform_name")" "$(architecture_output_name "$ARCHITECTURE")" "$(json_escape "$CONFIGURATION")" "$(json_escape "$GENERATOR")" "$(json_escape "$TOOLSET")" "$(json_escape "$compiler")" "$(json_escape "$spdlog")" "$(json_escape "$doctest")" "$(json_escape "$sdl")" "$(json_escape "$json")" "$(json_escape "$imgui")" "$(json_escape "$zstd")" "$(json_escape "$entt")" "$(json_escape "$glm")" "$(json_escape "$shadercross")" "$(json_escape "$dxc")" "$(json_escape "$spirv_cross")" "$(json_escape "$spirv_headers")" "$(json_escape "$spirv_tools")" "$(json_escape "$assimp")" "$(json_escape "$stb")" "$(json_escape "$jolt")" "$(json_escape "$recast")" "$(json_escape "$miniaudio")" "$(json_escape "$coral")" "$(json_escape "$dotnet_runtime")" > "$stage/build-manifest.json"
validate_package_stage "$stage" "$CLIENT_TARGET" "$HUB_TARGET" "$CORE_TARGET" "$PROJECT_NAMESPACE"
if [[ "$PLATFORM" == Mac ]]; then
  validate_macos_macho_minimum "$stage" "$macos_deployment_target" "$stage/bin/Managed/Dotnet"
fi
grep -Fq "\"commit\": \"$commit\"" "$stage/build-manifest.json" || { printf 'Package manifest commit does not match the packaging worktree HEAD.\n' >&2; exit 1; }
[[ "$commit" == "$(git -C "$ROOT" rev-parse HEAD)" ]] || { printf 'Packaging worktree HEAD changed while staging.\n' >&2; exit 1; }
grep -Fq "\"dirty\": $dirty" "$stage/build-manifest.json" || { printf 'Package manifest dirty flag is invalid.\n' >&2; exit 1; }
grep -Fq "\"developmentArtifact\": $development_artifact" "$stage/build-manifest.json" || { printf 'Package manifest development flag is invalid.\n' >&2; exit 1; }
[[ $ALLOW_DIRTY -eq 1 || "$dirty:$development_artifact" == false:false ]] || { printf 'Production package manifest is not clean.\n' >&2; exit 1; }
grep -Fq "\"imgui\": \"$imgui\"" "$stage/build-manifest.json" || { printf 'Packaged Dear ImGui identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"zstd\": \"$zstd\"" "$stage/build-manifest.json" || { printf 'Packaged Zstandard identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"entt\": \"$entt\"" "$stage/build-manifest.json" || { printf 'Packaged EnTT identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"glm\": \"$glm\"" "$stage/build-manifest.json" || { printf 'Packaged GLM identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"assimp\": \"$assimp\"" "$stage/build-manifest.json" || { printf 'Packaged Assimp identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"stb\": \"$stb\"" "$stage/build-manifest.json" || { printf 'Packaged stb identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"jolt\": \"$jolt\"" "$stage/build-manifest.json" || { printf 'Packaged Jolt identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"recast\": \"$recast\"" "$stage/build-manifest.json" || { printf 'Packaged Recast identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"miniaudio\": \"$miniaudio\"" "$stage/build-manifest.json" || { printf 'Packaged miniaudio identity does not match the dependency lock.\n' >&2; exit 1; }
grep -Fq "\"coral\": \"$coral\"" "$stage/build-manifest.json" || { printf 'Packaged Coral identity does not match the dependency lock.\n' >&2; exit 1; }
[[ "$dotnet_runtime" == 10.* ]] || { printf 'Packaged .NET runtime is not a .NET 10 runtime.\n' >&2; exit 1; }
grep -Fq "\"sdlShadercross\": \"$shadercross\"" "$stage/build-manifest.json" || { printf 'Packaged SDL_shadercross identity does not match the dependency lock.\n' >&2; exit 1; }
"$stage/bin/KeireShaderCompiler" --help 2>&1 | grep -Fq shadercross || { printf 'Packaged shader compiler validation failed.\n' >&2; exit 1; }
"$stage/bin/$asset_tool" --help | grep -Fq 'KeireAssetTool cook' || { printf 'Packaged asset tool validation failed.\n' >&2; exit 1; }
"$stage/bin/$asset_worker" --help | grep -Fq 'KeireAssetWorker' || { printf 'Packaged asset worker validation failed.\n' >&2; exit 1; }
DOTNET_ROOT="$ROOT/Build/Dependencies/dotnet-sdk" PATH="$ROOT/Build/Dependencies/dotnet-sdk:$PATH" KEIRE_SHADER_COMPILER="$stage/bin/KeireShaderCompiler" "$stage/bin/$asset_tool" cook --project "$stage/samples/KeireSandbox" --output "$stage/content/KeireSandbox" --profile Dist --target "$os_name" | grep -Fq 'Cooked' || { printf 'Packaged sample project asset validation failed.\n' >&2; exit 1; }
rm -rf "$stage/samples/KeireSandbox/Library" "$stage/samples/KeireSandbox/Logs" "$stage/samples/KeireSandbox/Build" "$stage/samples/KeireSandbox/Temp"
[[ -f "$stage/content/KeireSandbox/catalog.json" && -f "$stage/content/KeireSandbox/runtime-manifest.json" ]] || { printf 'Packaged cooked runtime content is incomplete.\n' >&2; exit 1; }
if [[ "$PLATFORM" == Linux && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  command -v xvfb-run >/dev/null 2>&1 || {
    printf 'Xvfb is required for the packaged Linux runtime GPU smoke on a headless host.\n' >&2
    exit 1
  }
  xvfb-run -a "$stage/bin/$runtime" --content "$stage/content/KeireSandbox" --frames 12 || {
    printf 'Packaged runtime smoke failed.\n' >&2
    exit 1
  }
else
  "$stage/bin/$runtime" --content "$stage/content/KeireSandbox" --frames 12 || { printf 'Packaged runtime smoke failed.\n' >&2; exit 1; }
fi
version_output="$("$stage/bin/$CLIENT_TARGET" --version)"
commit_prefix="${commit:0:12}"
expected_identity="$commit_prefix"; [[ "$dirty" == true ]] && expected_identity="${commit_prefix}-dirty"
[[ "$version_output" == *"$expected_identity"* ]] || { printf 'Packaged binary identity does not match build-manifest.json.\n' >&2; exit 1; }
[[ "$dirty" == true || "$version_output" != *"${commit_prefix}-dirty"* ]] || { printf 'Packaged binary reports a stale dirty state.\n' >&2; exit 1; }
assert_package_generated_data_free "$stage"
if [[ $stage_only -eq 1 ]]; then
  printf '==> Package stage created: %s\n' "$stage"
  exit 0
fi

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
if [[ "$PLATFORM" == Mac ]]; then
  validate_macos_macho_minimum "$validation_root/sdk" "$macos_deployment_target" \
    "$validation_root/sdk/bin/Managed/Dotnet"
fi
cxx=g++; [[ "$TOOLSET" == clang ]] && cxx=clang++
gameplay_libraries=("$validation_root/sdk/lib/libJolt.a" "$validation_root/sdk/lib/libRecast.a" "$validation_root/sdk/lib/libDetour.a" "$validation_root/sdk/lib/libDetourCrowd.a" "$validation_root/sdk/lib/libDetourTileCache.a" "$validation_root/sdk/lib/libminiaudio.a" "$validation_root/sdk/lib/libCoral.Native.a" "$validation_root/sdk/lib/libnethost.a")
macos_sdl_frameworks=(-framework Cocoa -framework CoreVideo -framework IOKit -framework CoreFoundation
  -framework CoreAudio -framework AudioToolbox -framework ForceFeedback -framework GameController
  -framework CoreHaptics -framework Carbon -framework Metal -framework QuartzCore
  -framework UniformTypeIdentifiers)
consumer_compile=("$cxx" -std=c++20 -Wall -Wextra -Werror -DKEIRE_STATIC "-I$validation_root/sdk/include" "$validation_root/sdk/examples/consumer/Source/Main.cpp" "$validation_root/sdk/lib/lib$CORE_TARGET.a" "$validation_root/sdk/lib/lib$imgui_library.a" "$validation_root/sdk/lib/lib$zstd_library.a" "$validation_root/sdk/lib/libassimp.a" "$validation_root/sdk/lib/libzlibstatic.a" "${gameplay_libraries[@]}" "$validation_root/sdk/third-party/SDL3/lib/libSDL3.a" -o "$validation_root/consumer")
[[ "$CONFIGURATION" == Dist ]] && consumer_compile+=(-flto)
[[ "$PLATFORM" == Linux ]] && consumer_compile+=(-pthread -ldl -lm)
[[ "$PLATFORM" == Mac ]] && consumer_compile+=("-mmacosx-version-min=$macos_deployment_target" "${macos_sdl_frameworks[@]}")
"${consumer_compile[@]}"
(cd "$validation_root" && ./consumer "$validation_root/sdk/examples/consumer/Client.json")
managed_compile=("$cxx" -std=c++20 -Wall -Wextra -Werror -DKEIRE_STATIC "-I$validation_root/sdk/include" "$validation_root/sdk/examples/managed-consumer/Source/ClientApplication.cpp" "$validation_root/sdk/lib/lib$CORE_TARGET.a" "$validation_root/sdk/lib/lib$imgui_library.a" "$validation_root/sdk/lib/lib$zstd_library.a" "$validation_root/sdk/lib/libassimp.a" "$validation_root/sdk/lib/libzlibstatic.a" "${gameplay_libraries[@]}" "$validation_root/sdk/third-party/SDL3/lib/libSDL3.a" -o "$validation_root/managed-consumer")
[[ "$CONFIGURATION" == Dist ]] && managed_compile+=(-flto)
[[ "$PLATFORM" == Linux ]] && managed_compile+=(-pthread -ldl -lm)
[[ "$PLATFORM" == Mac ]] && managed_compile+=("-mmacosx-version-min=$macos_deployment_target" "${macos_sdl_frameworks[@]}")
"${managed_compile[@]}"
managed_help="$("$validation_root/managed-consumer" --help)"
[[ "$managed_help" == *--managed-smoke* ]] || { printf 'Managed SDK consumer help validation failed.\n' >&2; exit 1; }
(cd "$validation_root" && ./managed-consumer --managed-smoke)
"$ROOT/Build/Dependencies/dotnet-sdk/dotnet" build \
  "$validation_root/sdk/examples/managed-consumer/ManagedApiConsumer.csproj" \
  --configuration Release --nologo \
  "-p:KeireManagedAssembly=$validation_root/sdk/bin/Managed/Keire.Managed.dll" \
  "-p:BaseOutputPath=$validation_root/managed-api-bin/" \
  "-p:BaseIntermediateOutputPath=$validation_root/managed-api-obj/"
cmake_platform_options=()
[[ "$PLATFORM" == Mac ]] && cmake_platform_options+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_deployment_target")
cmake -S "$validation_root/sdk/examples/consumer" -B "$validation_root/cmake-build" -DCMAKE_PREFIX_PATH="$validation_root/sdk" -DCMAKE_BUILD_TYPE=Release "${cmake_platform_options[@]}"
cmake --build "$validation_root/cmake-build" --config Release
(cd "$validation_root" && "$validation_root/cmake-build/SdkConsumer" "$validation_root/sdk/examples/consumer/Client.json")
cmake -S "$validation_root/sdk/examples/managed-consumer" -B "$validation_root/managed-cmake-build" -DCMAKE_PREFIX_PATH="$validation_root/sdk" -DCMAKE_BUILD_TYPE=Release "${cmake_platform_options[@]}"
cmake --build "$validation_root/managed-cmake-build" --config Release
(cd "$validation_root" && "$validation_root/managed-cmake-build/ManagedSdkConsumer" --managed-smoke)
cmake -S "$validation_root/sdk/examples/source-module" -B "$validation_root/module-cmake-build" -DCMAKE_PREFIX_PATH="$validation_root/sdk" -DCMAKE_BUILD_TYPE=Release "${cmake_platform_options[@]}"
cmake --build "$validation_root/module-cmake-build" --config Release
(cd "$validation_root" && "$validation_root/module-cmake-build/SourceModuleConsumer" --module-smoke)
rm -rf "$validation_root"
printf '==> Package created: %s\n' "$archive"
