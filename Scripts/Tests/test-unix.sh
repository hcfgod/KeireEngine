#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
assert_equal() { [[ "$1" == "$2" ]] || { printf '%s: expected %s, got %s\n' "$3" "$2" "$1" >&2; exit 1; }; }
assert_true() { "$@" || { printf 'Assertion failed: %s\n' "$*" >&2; exit 1; }; }
assert_false() { if "$@"; then printf 'Expected failure: %s\n' "$*" >&2; exit 1; fi; }

load_project_config "$ROOT"
assert_true test -n "$PROJECT_IDENTIFIER"
assert_true grep -Eq '^PROJECT_VERSION=[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.-]+)?$' "$ROOT/Config/Project.conf"
assert_true is_semantic_version '1.2.3-alpha.1+build.5'
assert_false is_semantic_version '01.2.3'
assert_false is_semantic_version '1.2.3-01'
assert_false is_semantic_version '1.2.3+'
assert_equal "$PROJECT_MACRO_PREFIX" "$(identifier_to_macro_prefix "$PROJECT_IDENTIFIER")" 'project macro prefix'
assert_equal "$(identifier_to_macro_prefix HTTPServer2Client)" HTTP_SERVER2_CLIENT 'macro prefix derivation'
assert_equal "$(normalize_architecture amd64)" x86_64 'x64 normalization'
assert_equal "$(normalize_architecture aarch64)" ARM64 'ARM normalization'
assert_equal "$(resolve_unix_toolset Linux default)" gcc 'Linux default toolset'
assert_equal "$(resolve_unix_toolset Mac default)" clang 'macOS default toolset'
assert_true version_at_least 16.0.1 16.0
assert_true version_at_least 17 16.9
assert_false version_at_least 15.9 16.0
assert_equal "$(printf 'Xcode 16.4\nBuild version 16F6\n' | extract_version)" 16.4 'multi-line version extraction'
assert_equal "$(package_name apt-get ninja)" ninja-build 'APT Ninja package'
assert_equal "$(package_name pacman ninja)" ninja 'pacman Ninja package'
assert_equal "$(package_name dnf cxx)" gcc-c++ 'DNF C++ package'
assert_equal "$(package_name pacman python)" python 'pacman Python package'
assert_equal "$(package_name apt-get uuid)" uuid-dev 'APT UUID development package'
assert_equal "$(package_name dnf uuid)" libuuid-devel 'DNF UUID development package'
assert_equal "$(package_name pacman uuid)" util-linux-libs 'pacman UUID development package'
assert_equal "$(package_name zypper uuid)" libuuid-devel 'Zypper UUID development package'
assert_equal "$(package_name apt-get llvm)" llvm 'LLVM tools package'
assert_equal "$(package_name pacman binutils)" binutils 'binutils package'
assert_equal "$(package_install_arguments pacman | tr '\n' ' ' | sed 's/ $//')" '-Syu --needed --noconfirm' 'pacman safe install arguments'
assert_equal "$(package_install_arguments apt-get)" -y 'APT install arguments'
assert_equal "$(package_install_arguments dnf)" -y 'DNF install arguments'
assert_equal "$(package_install_arguments zypper | tr '\n' ' ' | sed 's/ $//')" '--non-interactive install' 'Zypper install arguments'
assert_true mac_requires_full_xcode xcode4
assert_false mac_requires_full_xcode ninja
assert_equal "$(json_escape $'quote" slash\\ tab\t')" 'quote\" slash\\ tab\t' 'JSON escaping'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SPDLOG_COMMIT)" 79524ddd08a4ec981b7fea76afd08ee05f83755d 'spdlog lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" DOCTEST_COMMIT)" 2d0a9359a60c51affe2a9bebb1be1dca47868151 'doctest lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_COMMIT)" 8e37db5e797b6167f3a00d697d816a684bd259c7 'SDL lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" JSON_COMMIT)" 55f93686c01528224f448c19128836e7df245f72 'JSON lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" IMGUI_COMMIT)" b61e56346a92cfcaf1f43a545ca37b0b32239654 'Dear ImGui lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" ZSTD_COMMIT)" f8745da6ff1ad1e7bab384bd1f9d742439278e99 'Zstandard lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" ENTT_COMMIT)" b4e58bdd364ad72246c123a0c28538eab3252672 'EnTT lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" GLM_COMMIT)" 8d1fd52e5ab5590e2c81768ace50c72bae28f2ed 'GLM lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_COMMIT)" e55cf5e31ced6f3d1be5cc6d0c50e99384f9f4ba 'SDL_shadercross lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_DXC_COMMIT)" 2c84a1c5ab7091608c97df6ba5ccf46e71c322eb 'DXC recursive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_CROSS_COMMIT)" 1a6169566c73d3da552748fc372fe2bbb856e46e 'SPIRV-Cross recursive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT)" ad9184e76a66b1001c29db9b0a3e87f646c64de0 'SPIRV-Headers recursive lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT)" 0539c81f69a3daeb706fd3477dca61435b475156 'SPIRV-Tools recursive lock'
assert_true grep -q 'Vendor/imgui' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'Scripts/Premake/DearImGui.lua' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'imgui|zstd|entt|glm|SDL_shadercross)' "$ROOT/Scripts/Unix/vendor-update.sh"
assert_true grep -q 'Vendor/entt' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'Vendor/glm' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'imgui|zstd|entt|glm|SDL_shadercross)' "$ROOT/Scripts/Unix/vendor-update.sh"
assert_true grep -q 'Vendor/zstd' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'Scripts/Premake/Zstd.lua' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'imgui|zstd|entt|glm|SDL_shadercross)' "$ROOT/Scripts/Unix/vendor-update.sh"
assert_true grep -q 'Vendor/SDL_shadercross' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'SDL_SHADERCROSS_DXC_COMMIT' "$ROOT/Scripts/Unix/vendor.sh"
assert_true grep -q 'keire-dependency.stamp' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_DUMMYVIDEO=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_OFFSCREEN=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_GPU=ON' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL_RENDER=OFF' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'shader-compiler.sh' "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -q 'SDL3DebugLibrary' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -q 'SDL3ReleaseLibrary' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -q 'project_generation_fingerprint' "$ROOT/Scripts/Linux/generate.sh"
assert_true grep -q 'project(DearImGuiProject)' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'kind "StaticLib"' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'targetname(DearImGuiLibrary)' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'imgui_impl_sdl3.cpp' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'imgui_impl_sdlgpu3.cpp' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'imgui_stdlib.cpp' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'warnings "Off"' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q '../../Build/Projects/DearImGui' "$ROOT/Scripts/Premake/DearImGui.lua"
assert_true grep -q 'group "Dependencies"' "$ROOT/premake5.lua"
assert_true grep -q 'Scripts/Premake/DearImGui.lua' "$ROOT/premake5.lua"
assert_true grep -q 'Scripts/Premake/HeaderDependencies.lua' "$ROOT/premake5.lua"
assert_true grep -q 'project(EnTTProject)' "$ROOT/Scripts/Premake/HeaderDependencies.lua"
assert_true grep -q 'project(GLMProject)' "$ROOT/Scripts/Premake/HeaderDependencies.lua"
assert_true grep -q '../../Build/Projects/EnTT' "$ROOT/Scripts/Premake/HeaderDependencies.lua"
assert_true grep -q '../../Build/Projects/GLM' "$ROOT/Scripts/Premake/HeaderDependencies.lua"
assert_true grep -q 'links { DearImGuiProject, ZstdProject }' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'VendorIncludeDirs.entt' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'VendorIncludeDirs.glm' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'Source/ECS/Components/CameraComponent.cpp' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'Source/ECS/Components/MeshRendererComponent.cpp' "$ROOT/KeireCore/premake5.lua"
assert_true grep -q 'builtin-shaders.sh' "$ROOT/KeireCore/premake5.lua"
assert_true test -f "$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl"
assert_true grep -q 'BuiltinUnlitShaders.h' "$ROOT/KeireCore/Source/Rendering/RenderSystem.cpp"
assert_true grep -q 'renderer->Tint()' "$ROOT/KeireCore/Source/Rendering/RenderSystem.cpp"
assert_true grep -q 'ResolveLighting' "$ROOT/KeireCore/Source/Rendering/RenderSystem.cpp"
assert_true grep -q 'DirectionalLightComponent' "$ROOT/KeireCore/Source/Rendering/RenderSystem.cpp"
assert_true grep -q 'AmbientAndExposure' "$ROOT/KeireCore/Source/Rendering/RenderSystem.cpp"
assert_true grep -q 'LightDirection' "$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl"
assert_true grep -q 'worldNormal' "$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl"
assert_true grep -q 'BuiltinShaderUniformBufferCount(vertex)' "$ROOT/KeireCore/Source/Rendering/RenderSystem.cpp"
assert_false grep -q 'SDL_PushGPUFragmentUniformData' "$ROOT/KeireCore/Source/Rendering/RenderSystem.cpp"
assert_true grep -q 'Rendering.keiresettings' "$ROOT/KeireCore/Source/Rendering/RenderSettings.cpp"
assert_true test -f "$ROOT/Samples/KeireSandbox/ProjectSettings/Rendering.keiresettings"
assert_false grep -q 'Vendor/SDL/test' "$ROOT/KeireCore/Source/Rendering/RenderSystem.cpp"
assert_true grep -q 'SDL_GetBasePath()' "$ROOT/KeireCore/Source/Assets/RenderingAssets.cpp"
assert_true grep -q 'LogImportDiagnostic' "$ROOT/KeireCore/Source/Assets/AssetPipeline.cpp"
assert_true grep -q 'ReportError("Asset Import"' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true grep -q 'ImGuiDragDropFlags_SourceAllowNullID' "$ROOT/KeireCore/Source/Ui.cpp"
assert_true grep -q 'DrawAssetDragSource' "$ROOT/KeireClient/Source/Editor/AssetBrowserPanel.cpp"
assert_true grep -q 'DecodeDragPayload' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true grep -q 'PickSceneEntity' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true grep -q 'm_ComponentExpansion' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true grep -q 'Control Browser' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true grep -q 'BINDING PROCESSORS' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true grep -q 'UiKey::R' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true test -f "$ROOT/KeireClient/Source/Editor/SceneGizmoController.cpp"
assert_true grep -q 'RotationSnapDegrees' "$ROOT/KeireClient/Source/Editor/SceneGizmoController.cpp"
assert_true grep -q 'DrawCameraFrustum' "$ROOT/KeireClient/Source/Editor/SceneGizmoController.cpp"
assert_true grep -q 'DrawLightIcon' "$ROOT/KeireClient/Source/Editor/SceneGizmoController.cpp"
assert_true grep -q '"/select," + utf8Path' "$ROOT/KeireCore/Source/Process.cpp"
assert_false grep -q 'imgui.cpp' "$ROOT/KeireCore/premake5.lua"
assert_false grep -q 'AddDearImGuiSources' "$ROOT/Scripts/Premake/Common.lua"
assert_true grep -q 'LinkKeireCore()' "$ROOT/KeireClient/premake5.lua"
assert_true grep -q 'LinkKeireCore()' "$ROOT/KeireTests/premake5.lua"
assert_true test -z "$(find "$ROOT/KeireCore/Source" "$ROOT/KeireClient/Source" "$ROOT/KeireHub/Source" "$ROOT/KeireTests/Source" "$ROOT/AssetTool/Source" -type f -name '*.h' -print -quit)"
assert_true test -f "$ROOT/KeireCore/Include/Keire/Assets/Asset.h"
assert_true test -f "$ROOT/KeireCore/Source/Assets/AssetSystem.cpp"
assert_true test -f "$ROOT/KeireCore/Include/Keire/Input/Input.h"
assert_true test -f "$ROOT/KeireCore/Source/Input/InputSystem.cpp"
assert_true test -f "$ROOT/Samples/KeireSandbox/Assets/Input/DefaultInput.keireinput"
assert_true test -f "$ROOT/KeireCore/Include/Keire/Project/Project.h"
assert_true test -f "$ROOT/KeireCore/Include/Keire/Scenes/SceneSystem.h"
assert_true test -f "$ROOT/KeireHub/Source/HubApplication.cpp"
assert_true test -f "$ROOT/KeireClient/Source/Editor/AssetBrowserPanel.cpp"
assert_true test -f "$ROOT/KeireClient/Source/Editor/ConsolePanel.cpp"
assert_true test -f "$ROOT/KeireClient/Source/Editor/DiagnosticsPanel.cpp"
assert_true test -f "$ROOT/KeireClient/Source/Editor/ThumbnailService.cpp"
assert_true grep -q 'DisplayName(record.RelativePath)' "$ROOT/KeireClient/Source/Editor/AssetBrowserPanel.cpp"
assert_true grep -q 'TrashRecords()' "$ROOT/KeireClient/Source/Editor/AssetBrowserPanel.cpp"
assert_true grep -q 'AssetImportPolicy::KeepLastGood' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true grep -q 'class KEIRE_API UndoService' "$ROOT/KeireCore/Include/Keire/Undo.h"
assert_true grep -q 'm_InputForwardToConsole' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true grep -q 'coalescingWindowNanoseconds' "$ROOT/KeireClient/Source/EditorWorkspaceLayer.cpp"
assert_true grep -q 'CreateSystemTray' "$ROOT/KeireHub/Source/HubApplication.cpp"
assert_true grep -q 'Show Hub' "$ROOT/KeireHub/Source/HubApplication.cpp"
assert_true grep -q '"schemaVersion": 2' "$ROOT/Samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene"
assert_true grep -q '"components"' "$ROOT/Samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene"
assert_false grep -R -E '#include[[:space:]]*[<"]imgui|ImGui::|ImGui[A-Z]' "$ROOT/KeireClient"
assert_false grep -R -E 'SDL3/|nlohmann/json|imgui|entt/|glm/' "$ROOT/KeireCore/Include/Keire"
assert_true grep -q 'class KEIRE_API UiWorkspace' "$ROOT/KeireCore/Include/Keire/UiWorkspace.h"
assert_true grep -q 'BuildFactoryLayout' "$ROOT/KeireClient/Source/ClientApplication.cpp"
for exported_type in \
  Application ApplicationCommandLineArguments CommandLineError EventView EventSubscription EventBus Layer LayerStack \
  LoggerHandle Log Time UiError UiScope UiWindowScope UiChildScope UiMenuBarScope UiMenuScope UiTabBarScope \
  UiTabItemScope UiTreeNodeScope UiDisabledScope UiIdScope UiMainMenuBarScope UiComboScope UiPopupScope UiTableScope \
  UiDragSourceScope UiDragTargetScope UiPanelScope \
  UiFrame UiLayoutBuilder UiPanelRegistration UiWorkspace WindowError Window FolderDialogOperation WindowSystem ConfigurationError \
  Asset BinaryAsset TextAsset AssetLoadError AssetSystem AssetDatabase AssetCooker InputActionAsset \
  InputActionSubscription InputActionHandle InputActionContext InteractiveRebindOperation InputSystem InputCaptureOverride \
  Project ProjectRegistry EntityId ComponentTypeId Component ComponentRegistry Entity TransformComponent DirectionalLightComponent \
  UndoCommand UndoTransaction UndoContext UndoService CameraComponent MeshRendererComponent SceneAsset Scene SceneObjectHandle SceneRuntimeSession SceneLoadOperation SceneSystem \
  UiImage SaveFileDialogOperation SystemTray RenderSurface RenderView RenderSystem ShaderAsset MaterialAsset MeshAsset; do
  assert_true grep -R -E -q "class[[:space:]]+KEIRE_API[[:space:]]+$exported_type([^[:alnum:]_]|$)" "$ROOT/KeireCore/Include/Keire"
done
for exported_function in AssertionFailure GetName GetBuildInfo GetVersionString LoadWindowSpecification; do
  assert_true grep -R -E -q "KEIRE_API[^;{}]*$exported_function[[:space:]]*\\(" "$ROOT/KeireCore/Include/Keire"
done
assert_false grep -R -E -q 'KEIRE_API[^;{}]*(GetApplicationCommandLineDescription|CreateApplication)[[:space:]]*\(' "$ROOT/KeireCore/Include/Keire"
assert_true grep -q 'dear-imgui-LICENSE.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'IMGUI_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'lib\$imgui_library.a' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'zstandard-LICENSE.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'ZSTD_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'lib\$zstd_library.a' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'entt-LICENSE.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'ENTT_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'glm-COPYING.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'GLM_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'KeireShaderCompiler' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'SDL-shadercross-LICENSE.txt' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q 'SDL_SHADERCROSS_COMMIT' "$ROOT/Scripts/Unix/package.sh"
assert_true grep -q '@PROJECT_NAMESPACE@ImGui.a' "$ROOT/Config/PackageConfig.cmake.in"
assert_true grep -q '"${_imgui_sdk_library}" "${_zstd_sdk_library}" SDL3::SDL3-static' "$ROOT/Config/PackageConfig.cmake.in"
security_workflow="$ROOT/.github/workflows/security.yml"
grep -q '^  security-status:$' "$security_workflow" || fail 'Security activation sentinel is missing'
grep -q '^    if: always()$' "$security_workflow" || fail 'Security activation sentinel is not unconditional'
grep -q 'ENABLE_ADVANCED_SECURITY' "$security_workflow" || fail 'Advanced security opt-in variable is missing'
! grep -q 'continue-on-error' "$security_workflow" || fail 'Advanced security checks are not strict'

llvm_fixture="$(mktemp -d)"; old_path="$PATH"
printf '%s\n' '#!/usr/bin/env bash' 'printf "clang version 18.1.2\\n"' > "$llvm_fixture/clang++"
printf '%s\n' '#!/usr/bin/env bash' 'printf "LLVM version 17.0.6\\n"' > "$llvm_fixture/llvm-profdata"
printf '%s\n' '#!/usr/bin/env bash' 'printf "LLVM version 18.1.2\\n"' > "$llvm_fixture/llvm-profdata-18"
printf '%s\n' '#!/usr/bin/env bash' 'printf "LLVM version 18.1.2\\n"' > "$llvm_fixture/llvm-cov-18"
chmod +x "$llvm_fixture"/*; PATH="$llvm_fixture:$old_path"
assert_equal "$(resolve_llvm_tool llvm-profdata clang++)" "$llvm_fixture/llvm-profdata-18" 'matching llvm-profdata selection'
assert_equal "$(resolve_llvm_tool llvm-cov clang++)" "$llvm_fixture/llvm-cov-18" 'matching llvm-cov selection'
PATH="$old_path"; rm -rf "$llvm_fixture"

package_stage="$(mktemp -d)"
for path in bin/Client bin/Hub lib/libCore.a lib/libCoreImGui.a Config/Client.json include/Core/Core.h include/Core/Log.h include/Core/Api.h include/Core/Application.h include/Core/Assert.h include/Core/BuildInfo.h include/Core/EntryPoint.h include/Core/Event.h include/Core/Layer.h include/Core/Ref.h include/Core/Time.h include/Core/Project/Project.h include/Core/Scenes/Scene.h include/Core/Scenes/SceneAsset.h include/Core/Scenes/SceneSystem.h include/Core/Window.h include/Core/WindowConfig.h samples/KeireSandbox/ProjectSettings/Project.keireproject samples/KeireSandbox/ProjectSettings/Rendering.keiresettings samples/KeireSandbox/Assets/Input/DefaultInput.keireinput samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene examples/consumer/Main.cpp examples/consumer/Client.json examples/consumer/CMakeLists.txt examples/consumer/README.md examples/managed-consumer/ClientApplication.cpp examples/managed-consumer/CMakeLists.txt examples/managed-consumer/README.md lib/cmake/CrossPlatformCoreClientTemplate/CrossPlatformCoreClientTemplateConfig.cmake third-party/spdlog/spdlog.h third-party/SDL3/include/SDL3/SDL.h third-party/SDL3/lib/libSDL3.a third-party/SDL3/cmake/SDL3Config.cmake third-party/SDL3/licenses/SDL3/LICENSE.txt third-party/licenses/spdlog-LICENSE.txt third-party/licenses/fmt-LICENSE.rst third-party/licenses/doctest-LICENSE.txt third-party/licenses/nlohmann-json-LICENSE.MIT.txt third-party/licenses/dear-imgui-LICENSE.txt README.md LICENSE.txt THIRD_PARTY_NOTICES.md build-manifest.json; do
  mkdir -p "$package_stage/$(dirname "$path")"; : > "$package_stage/$path"
done
for path in bin/CoreAssetTool lib/libCoreZstd.a include/Core/Math/Math.h include/Core/ECS/Component.h include/Core/ECS/Entity.h include/Core/ECS/Components/TransformComponent.h include/Core/ECS/Components/DirectionalLightComponent.h include/Core/Assets/Asset.h include/Core/Assets/AssetSystem.h include/Core/Assets/AssetPipeline.h include/Core/Assets/InputActionAsset.h include/Core/Input/Input.h samples/KeireSandbox/Assets/Input/DefaultInput.keireinput.keiremeta third-party/licenses/zstandard-LICENSE.txt third-party/licenses/entt-LICENSE.txt third-party/licenses/glm-COPYING.txt; do
  mkdir -p "$package_stage/$(dirname "$path")"; : > "$package_stage/$path"
done
for path in bin/KeireShaderCompiler include/Core/Undo.h include/Core/ECS/Components/CameraComponent.h include/Core/ECS/Components/MeshRendererComponent.h include/Core/Rendering/RenderSystem.h include/Core/Assets/RenderingAssets.h samples/KeireSandbox/Assets/Shaders/DefaultUnlit.keireshader samples/KeireSandbox/Assets/Shaders/DefaultUnlit.hlsl samples/KeireSandbox/Assets/Materials/DefaultUnlit.keirematerial third-party/licenses/SDL-shadercross-LICENSE.txt third-party/licenses/DirectXShaderCompiler-LICENSE.txt third-party/licenses/DirectXShaderCompiler-ThirdPartyNotices.txt third-party/licenses/SPIRV-Cross-LICENSE.txt third-party/licenses/SPIRV-Headers-LICENSE.txt third-party/licenses/SPIRV-Tools-LICENSE.txt; do
  mkdir -p "$package_stage/$(dirname "$path")"; : > "$package_stage/$path"
done
: > "$package_stage/include/Core/Ui.h"
: > "$package_stage/include/Core/UiWorkspace.h"
assert_true validate_package_stage "$package_stage" Client Hub Core Core
for generated_path in \
  samples/KeireSandbox/Build/generated.make \
  samples/KeireSandbox/Logs/Core.log \
  samples/KeireSandbox/Library/AssetCatalog.json \
  samples/KeireSandbox/Temp/editor.tmp \
  samples/KeireSandbox/Assets/Scenes/SampleScene.recovery.json; do
  mkdir -p "$package_stage/$(dirname "$generated_path")"
  : > "$package_stage/$generated_path"
  assert_false assert_package_generated_data_free "$package_stage"
  rm "$package_stage/$generated_path"
done
mkdir -p "$package_stage/samples/KeireSandbox/Build"
: > "$package_stage/samples/KeireSandbox/Build/generated.txt"
package_contamination_archive="$(mktemp).tar.gz"
tar -C "$package_stage" -czf "$package_contamination_archive" .
assert_false assert_package_archive_generated_data_free "$package_contamination_archive"
rm -f "$package_contamination_archive" "$package_stage/samples/KeireSandbox/Build/generated.txt"
rm "$package_stage/lib/libCoreImGui.a"
assert_false validate_package_stage "$package_stage" Client Hub Core Core
: > "$package_stage/lib/libCoreImGui.a"
rm "$package_stage/third-party/licenses/dear-imgui-LICENSE.txt"
assert_false validate_package_stage "$package_stage" Client Hub Core Core
: > "$package_stage/third-party/licenses/dear-imgui-LICENSE.txt"
rm "$package_stage/third-party/licenses/spdlog-LICENSE.txt"
assert_false validate_package_stage "$package_stage" Client Hub Core Core
rm -rf "$package_stage"

tracked_sample_stage="$(mktemp -d)"
copy_tracked_tree "$ROOT" Samples/KeireSandbox "$tracked_sample_stage"
assert_true test -f "$tracked_sample_stage/ProjectSettings/Project.keireproject"
assert_true assert_package_generated_data_free "$tracked_sample_stage"
rm -rf "$tracked_sample_stage"

identity_fixture="$(mktemp -d)"
mkdir -p "$identity_fixture/Scripts/Unix" "$identity_fixture/Config"
cp "$ROOT/Scripts/Unix/common.sh" "$ROOT/Scripts/Unix/build-info.sh" "$identity_fixture/Scripts/Unix/"
cat > "$identity_fixture/Config/Project.conf" <<'IDENTITY_CONFIG'
PROJECT_IDENTIFIER=IdentityFixture
PROJECT_DISPLAY_NAME=Quoted "Kéire" \\ Client
PROJECT_VERSION=1.2.3-alpha.1+build.5
PROJECT_NAMESPACE=IdentityFixture
PROJECT_MACRO_PREFIX=IDENTITY_FIXTURE
CORE_TARGET=IdentityFixtureCore
CORE_DIRECTORY=IdentityFixtureCore
CLIENT_TARGET=IdentityFixtureClient
CLIENT_DIRECTORY=IdentityFixtureClient
HUB_TARGET=IdentityFixtureHub
HUB_DIRECTORY=IdentityFixtureHub
TESTS_TARGET=IdentityFixtureTests
TESTS_DIRECTORY=IdentityFixtureTests
ARTIFACT_PREFIX=identityfixture
REPOSITORY_SLUG=example/identity-fixture
IDENTITY_CONFIG
printf '%s\n' /Build/ .ninja_lock > "$identity_fixture/.gitignore"
git -C "$identity_fixture" init --quiet
git -C "$identity_fixture" config user.email scripts@example.invalid
git -C "$identity_fixture" config user.name 'Script Tests'
git -C "$identity_fixture" add .
git -C "$identity_fixture" commit --quiet -m first
first_commit="$(git -C "$identity_fixture" rev-parse HEAD)"
bash "$identity_fixture/Scripts/Unix/build-info.sh"
identity_header="$identity_fixture/Build/Generated/IdentityFixture/BuildInfo.generated.h"
assert_true grep -Fq '#define KEIRE_BUILD_PROJECT_VERSION "1.2.3-alpha.1+build.5"' "$identity_header"
assert_true grep -Fq '#define KEIRE_BUILD_PROJECT_NAME "Quoted \"Kéire\" \\\\ Client"' "$identity_header"
assert_true grep -Fq "#define KEIRE_BUILD_GIT_COMMIT \"$first_commit\"" "$identity_header"
assert_true grep -Fq '#define KEIRE_BUILD_GIT_DIRTY false' "$identity_header"
touch "$identity_fixture/.ninja_lock"
bash "$identity_fixture/Scripts/Unix/build-info.sh"
assert_true grep -Fq '#define KEIRE_BUILD_GIT_DIRTY false' "$identity_header"
touch "$identity_fixture/Build/identity-marker"
sleep 1
bash "$identity_fixture/Scripts/Unix/build-info.sh"
[[ "$identity_header" -ot "$identity_fixture/Build/identity-marker" ]] || fail 'Unchanged identity header was rewritten'
printf '%s\n' untracked > "$identity_fixture/untracked.txt"
bash "$identity_fixture/Scripts/Unix/build-info.sh"
assert_true grep -Fq '#define KEIRE_BUILD_GIT_DIRTY true' "$identity_header"
git -C "$identity_fixture" add .
git -C "$identity_fixture" commit --quiet -m second
second_commit="$(git -C "$identity_fixture" rev-parse HEAD)"
bash "$identity_fixture/Scripts/Unix/build-info.sh"
assert_true grep -Fq "#define KEIRE_BUILD_GIT_COMMIT \"$second_commit\"" "$identity_header"
assert_true grep -Fq '#define KEIRE_BUILD_GIT_DIRTY false' "$identity_header"
[[ "$first_commit" != "$second_commit" ]] || fail 'Identity did not refresh after a commit'
rm -rf "$identity_fixture"

parent_fixture="$(mktemp -d)"; fixture="$parent_fixture/Template"
trap 'rm -rf "$parent_fixture"' EXIT
mkdir -p "$fixture/archive"
printf '%s\n' premake > "$fixture/archive/premake5"
chmod 0644 "$fixture/archive/premake5"
assert_equal "$(find_premake_binary "$fixture/archive")" "$fixture/archive/premake5" 'non-executable Premake discovery'
mkdir -p "$fixture/Scripts/Unix" "$fixture/Config" "$fixture/Examples/Consumer" "$fixture/Examples/ManagedConsumer" "$fixture/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE" "$fixture/$CORE_DIRECTORY/Source" "$fixture/$CLIENT_DIRECTORY/Source" "$fixture/$HUB_DIRECTORY/Source" "$fixture/$TESTS_DIRECTORY/Source" "$fixture/Vendor" "$fixture/Build/Bin"
cp "$ROOT/Scripts/Unix/common.sh" "$ROOT/Scripts/Unix/rename.sh" "$ROOT/Scripts/Unix/clean.sh" "$fixture/Scripts/Unix/"
cp "$ROOT/Config/Project.conf" "$fixture/Config/Project.conf"
cp "$ROOT/Config/Client.json" "$fixture/Config/Client.json"
cp "$ROOT/Config/PackageConfig.cmake.in" "$fixture/Config/PackageConfig.cmake.in"
cp "$ROOT/premake5.lua" "$fixture/premake5.lua"
cp "$ROOT/Examples/Consumer/CMakeLists.txt" "$ROOT/Examples/Consumer/Main.cpp" "$fixture/Examples/Consumer/"
cp "$ROOT/Examples/ManagedConsumer/CMakeLists.txt" "$ROOT/Examples/ManagedConsumer/ClientApplication.cpp" "$fixture/Examples/ManagedConsumer/"
printf '%s\n' "#ifndef ${PROJECT_MACRO_PREFIX}_CORE_CORE_H" "#define ${PROJECT_MACRO_PREFIX}_CORE_CORE_H" "namespace $PROJECT_NAMESPACE { const char* GetName(); }" '#endif' > "$fixture/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE/Core.h"
printf '%s\n' "#ifndef ${PROJECT_MACRO_PREFIX}_CORE_LOG_H" "#define ${PROJECT_MACRO_PREFIX}_CORE_LOG_H" "namespace $PROJECT_NAMESPACE { class Log; }" '#endif' > "$fixture/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE/Log.h"
for source in "$fixture/$CORE_DIRECTORY/Source/Library.cpp" "$fixture/$CLIENT_DIRECTORY/Source/Main.cpp" "$fixture/$HUB_DIRECTORY/Source/Main.cpp" "$fixture/$TESTS_DIRECTORY/Source/Main.cpp"; do
  printf '#include "%s/Core.h"\n' "$PROJECT_NAMESPACE" > "$source"
done
printf '%s %s Scripts/Tests Core.log Client.log\n' "$PROJECT_IDENTIFIER" "$REPOSITORY_SLUG" > "$fixture/README.md"
printf '%s\n' vendor > "$fixture/Vendor/keep.txt"
printf '%s\n' build > "$fixture/Build/Bin/remove.txt"

git -C "$parent_fixture" init --quiet
git -C "$parent_fixture" config user.email scripts@example.invalid
git -C "$parent_fixture" config user.name 'Script Tests'
git -C "$parent_fixture" add Template
git -C "$parent_fixture" commit --quiet -m fixture
printf '%s\n' dirty >> "$fixture/README.md"

assert_false bash "$fixture/Scripts/Unix/rename.sh" ScriptFixture $'Bad\nName' example/script-fixture
bash "$fixture/Scripts/Unix/rename.sh" ScriptFixture 'Script "Fixturé" \\ Name' example/script-fixture >/dev/null
assert_false test -e "$fixture/.git"
assert_true test -f "$fixture/ScriptFixtureCore/Include/ScriptFixture/Core.h"
assert_true test -f "$fixture/ScriptFixtureHub/Source/Main.cpp"
assert_true grep -q '^CORE_TARGET=ScriptFixtureCore$' "$fixture/Config/Project.conf"
assert_true grep -q '^PROJECT_MACRO_PREFIX=SCRIPT_FIXTURE$' "$fixture/Config/Project.conf"
assert_true grep -q "^PROJECT_VERSION=$PROJECT_VERSION$" "$fixture/Config/Project.conf"
assert_true grep -Fq 'valid Semantic Version 2.0.0' "$fixture/premake5.lua"
assert_true grep -q 'find_package(ScriptFixture CONFIG REQUIRED)' "$fixture/Examples/Consumer/CMakeLists.txt"
assert_true grep -q 'ScriptFixture::Core' "$fixture/Examples/Consumer/CMakeLists.txt"
assert_true grep -q 'find_package(ScriptFixture CONFIG REQUIRED)' "$fixture/Examples/ManagedConsumer/CMakeLists.txt"
assert_true grep -q 'ScriptFixture::Core' "$fixture/Examples/ManagedConsumer/CMakeLists.txt"
assert_true grep -q '@PROJECT_NAMESPACE@::Core' "$fixture/Config/PackageConfig.cmake.in"
assert_true grep -Fq 'Scripts/Tests Core.log Client.log' "$fixture/README.md"
assert_true test -f "$fixture/Config/Client.json"
assert_false grep -R -q "$PROJECT_MACRO_PREFIX" "$fixture/ScriptFixtureCore/Include"
assert_true grep -q SCRIPT_FIXTURE_CORE_CORE_H "$fixture/ScriptFixtureCore/Include/ScriptFixture/Core.h"
bash "$fixture/Scripts/Unix/clean.sh" full >/dev/null
assert_false test -d "$fixture/Build/Bin"
assert_true test -f "$fixture/Vendor/keep.txt"
assert_true test -f "$fixture/ScriptFixtureCore/Source/Library.cpp"

repository_files="$(mktemp)"
find "$ROOT" -type f \
  -not -path '*/.git/*' -not -path '*/.vs/*' -not -path '*/Vendor/*' -not -path '*/Tools/*' -not -path '*/Build/*' -not -path '*/Logs/*' -not -path '*/Artifacts/*' -not -path '*/Scripts/Tests/*' > "$repository_files"
deprecated_pattern='\b(CORE|CLIENT)_(API|ASSERT|ASSERTIONS_ENABLED|TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL)\b'
while IFS= read -r file; do
  ! grep -En "$deprecated_pattern" "$file" || fail "Deprecated public macros remain in $file"
  for stale in '#include "KeireCore/' 'Scripts/KeireTests' 'Scripts\KeireTests' 'Scripts/Windows/Tests' 'Scripts/Unix/Tests' 'KeireCore.log' 'KeireClient.log'; do
    ! grep -Fn "$stale" "$file" || fail "Stale repository identity remains in $file: $stale"
  done
done < "$repository_files"
rm -f "$repository_files"
printf 'Unix script regression tests passed.\n'
