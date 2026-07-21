$ErrorActionPreference = "Stop"
$Windows = Resolve-Path (Join-Path $PSScriptRoot "..\Windows")
. (Join-Path $Windows "common.ps1")

function Assert-Equal($Actual, $Expected, [string]$Message) {
    if ($Actual -ne $Expected) { throw "$Message. Expected '$Expected', got '$Actual'." }
}
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    try { & $Action } catch { return }
    throw "$Message did not throw."
}
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "$Message failed." }
}

$project = Get-ProjectConfig
$generateScript = Get-Content (Join-Path $Windows "generate.ps1") -Raw
Assert-True ($generateScript.Contains('--file=premake5.lua')) "Unicode-safe relative Premake script path"
Assert-True ($generateScript.Contains('Get-ProjectGenerationFingerprint')) "Source-inventory project regeneration"
$bootstrapScript = Get-Content (Join-Path $Windows "bootstrap.ps1") -Raw
Assert-True ($bootstrapScript.Contains('GetTempPath') -and $bootstrapScript.Contains('$PremakeExe --version')) "Unicode-safe Premake version validation"
$menuScript = Get-Content (Join-Path $Windows "..\project.ps1") -Raw
Assert-True ($menuScript.Contains('$script:Target = $Project.CLIENT_TARGET')) "Post-rename client target refresh"
$launcherFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-launcher-exit-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force (Join-Path $launcherFixture "Scripts\Windows") | Out-Null
    Copy-Item (Join-Path $Windows "..\project.ps1") (Join-Path $launcherFixture "Scripts\project.ps1")
    @'
function Get-ProjectConfig {
    return [pscustomobject]@{ CLIENT_TARGET = "Client"; PROJECT_IDENTIFIER = "ExitFixture" }
}
function Normalize-Architecture([string]$Architecture) { return "x86_64" }
'@ | Set-Content (Join-Path $launcherFixture "Scripts\Windows\common.ps1") -Encoding UTF8
    'exit 23' | Set-Content (Join-Path $launcherFixture "Scripts\Windows\test.ps1") -Encoding ASCII
    $launcher = Start-Process -FilePath (Get-Command powershell.exe).Source -ArgumentList @(
        "-NoProfile", "-NonInteractive", "-File", (Join-Path $launcherFixture "Scripts\project.ps1"),
        "test", "-Generator", "ninja", "-Architecture", "x86_64", "-Toolset", "msc"
    ) -Wait -PassThru -WindowStyle Hidden
    Assert-Equal $launcher.ExitCode 23 "Top-level Windows launcher child exit propagation"
}
finally {
    Remove-Item $launcherFixture -Recurse -Force -ErrorAction SilentlyContinue
}
Assert-True (-not [string]::IsNullOrWhiteSpace($project.PROJECT_IDENTIFIER)) "Project manifest"
Assert-True ($project.PROJECT_VERSION -match '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') "Semantic project version"
Assert-True (Test-SemanticVersion "1.2.3-alpha.1+build.5") "Complete Semantic Version"
Assert-True (-not (Test-SemanticVersion "01.2.3")) "Semantic Version major leading zero rejection"
Assert-True (-not (Test-SemanticVersion "1.2.3-01")) "Semantic Version prerelease leading zero rejection"
Assert-True (-not (Test-SemanticVersion "1.2.3+")) "Empty Semantic Version build rejection"
Assert-Equal $project.PROJECT_MACRO_PREFIX (ConvertTo-MacroPrefix $project.PROJECT_IDENTIFIER) "Project macro prefix"
Assert-Equal (ConvertTo-MacroPrefix "HTTPServer2Client") "HTTP_SERVER2_CLIENT" "Macro prefix derivation"
$securityWorkflow = Get-Content (Join-Path (Get-RepositoryRoot) ".github\workflows\security.yml") -Raw
Assert-True ($securityWorkflow -match "(?m)^  security-status:\s*$") "Security activation sentinel"
Assert-True ($securityWorkflow -match "(?m)^    if: always\(\)\s*$") "Security sentinel always runs"
Assert-True ($securityWorkflow.Contains("ENABLE_ADVANCED_SECURITY")) "Advanced security opt-in variable"
Assert-True (-not $securityWorkflow.Contains("continue-on-error")) "Strict advanced security checks"
$emptyRepository = Join-Path ([IO.Path]::GetTempPath()) ("template-empty-git-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $emptyRepository | Out-Null
try {
    Assert-True (-not (Test-GitRepository $emptyRepository)) "Non-repository detection"
    & git -C $emptyRepository init --quiet
    Assert-Equal (Get-GitHeadCommit $emptyRepository) "uncommitted" "Empty Git commit fallback"
    Assert-True (Test-GitRepository $emptyRepository) "Empty Git repository detection"
}
finally {
    Remove-Item $emptyRepository -Recurse -Force -ErrorAction SilentlyContinue
}
Assert-Equal (Normalize-Architecture "amd64") "x86_64" "x64 normalization"
Assert-Equal (Normalize-Architecture "aarch64") "ARM64" "ARM normalization"
Assert-Equal (Resolve-WindowsToolset "vs2022" "default") "msc" "VS default toolset"
Assert-Equal (Resolve-WindowsToolset "ninja" "default") "msc" "Ninja default toolset"
Assert-Equal (Resolve-WindowsToolset "gmake" "default") "gcc" "GNU Make default toolset"
Assert-Throws { Assert-SupportedBuildCombination "vs2022" "DebugUBSan" "x86_64" "msc" } "MSVC UBSan validation"
Assert-Throws { Assert-SupportedBuildCombination "vs2022" "Coverage" "x86_64" "clang" } "Coverage generator validation"
$lock = Get-DependencyLock
Assert-Equal $lock.SPDLOG_COMMIT "79524ddd08a4ec981b7fea76afd08ee05f83755d" "spdlog lock"
Assert-Equal $lock.DOCTEST_COMMIT "2d0a9359a60c51affe2a9bebb1be1dca47868151" "doctest lock"
Assert-Equal $lock.SDL_COMMIT "8e37db5e797b6167f3a00d697d816a684bd259c7" "SDL lock"
Assert-Equal $lock.JSON_COMMIT "55f93686c01528224f448c19128836e7df245f72" "JSON lock"
Assert-Equal $lock.IMGUI_COMMIT "b61e56346a92cfcaf1f43a545ca37b0b32239654" "Dear ImGui lock"
Assert-Equal $lock.ZSTD_COMMIT "f8745da6ff1ad1e7bab384bd1f9d742439278e99" "Zstandard lock"
Assert-Equal $lock.ENTT_COMMIT "b4e58bdd364ad72246c123a0c28538eab3252672" "EnTT lock"
Assert-Equal $lock.GLM_COMMIT "8d1fd52e5ab5590e2c81768ace50c72bae28f2ed" "GLM lock"
Assert-Equal $lock.SDL_SHADERCROSS_COMMIT "e55cf5e31ced6f3d1be5cc6d0c50e99384f9f4ba" "SDL_shadercross lock"
Assert-Equal $lock.SDL_SHADERCROSS_DXC_COMMIT "2c84a1c5ab7091608c97df6ba5ccf46e71c322eb" "DXC recursive lock"
Assert-Equal $lock.SDL_SHADERCROSS_SPIRV_CROSS_COMMIT "1a6169566c73d3da552748fc372fe2bbb856e46e" "SPIRV-Cross recursive lock"
Assert-Equal $lock.SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT "ad9184e76a66b1001c29db9b0a3e87f646c64de0" "SPIRV-Headers recursive lock"
Assert-Equal $lock.SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT "0539c81f69a3daeb706fd3477dca61435b475156" "SPIRV-Tools recursive lock"
Assert-Equal $lock.ASSIMP_COMMIT "392a658f9c271be965271f45e7521a1b80ea4392" "Assimp lock"
Assert-Equal $lock.STB_COMMIT "31c1ad37456438565541f4919958214b6e762fb4" "stb lock"
$vendorScript = Get-Content (Join-Path $Windows "vendor.ps1") -Raw
$vendorUpdateScript = Get-Content (Join-Path $Windows "vendor-update.ps1") -Raw
Assert-True ($vendorScript.Contains('Vendor/imgui') -and $vendorScript.Contains('$Lock.IMGUI_COMMIT')) "Dear ImGui vendor mapping"
Assert-True ($vendorScript.Contains('Scripts\Premake\DearImGui.lua') -and $vendorScript.Contains('imgui_impl_sdlgpu3.cpp')) "Dear ImGui integration validation"
Assert-True ($vendorUpdateScript.Contains('"imgui"')) "Dear ImGui vendor update support"
Assert-True ($vendorScript.Contains('Vendor/zstd') -and $vendorScript.Contains('$Lock.ZSTD_COMMIT') -and $vendorScript.Contains('Scripts\Premake\Zstd.lua')) "Zstandard vendor mapping"
Assert-True ($vendorUpdateScript.Contains('"zstd"')) "Zstandard vendor update support"
Assert-True ($vendorScript.Contains('Vendor/entt') -and $vendorScript.Contains('$Lock.ENTT_COMMIT') -and $vendorScript.Contains('Vendor/glm') -and $vendorScript.Contains('$Lock.GLM_COMMIT')) "ECS and math vendor mappings"
Assert-True ($vendorUpdateScript.Contains('"entt"') -and $vendorUpdateScript.Contains('"glm"')) "ECS and math vendor update support"
Assert-True ($vendorScript.Contains('Vendor/SDL_shadercross') -and $vendorScript.Contains('SDL_SHADERCROSS_DXC_COMMIT') -and $vendorScript.Contains('SPIRV-Tools')) "Recursive shader compiler vendor mapping"
Assert-True ($vendorUpdateScript.Contains('"SDL_shadercross"')) "Shader compiler vendor update support"
Assert-True ($vendorScript.Contains('Vendor/assimp') -and $vendorScript.Contains('$Lock.ASSIMP_COMMIT') -and $vendorScript.Contains('Vendor/stb') -and $vendorScript.Contains('$Lock.STB_COMMIT')) "Asset importer vendor mappings"
$dependencyScript = Get-Content (Join-Path $Windows "dependencies.ps1") -Raw
Assert-True ($dependencyScript.Contains('$Lock.SDL_COMMIT') -and $dependencyScript.Contains('$compiler') -and $dependencyScript.Contains('keire-dependency.stamp')) "Dependency cache identity inputs"
Assert-True ($dependencyScript.Contains('"Debug", "Release"') -and $dependencyScript.Contains('SDL_DUMMYVIDEO=ON') -and $dependencyScript.Contains('SDL_OFFSCREEN=ON')) "SDL variants and headless drivers"
Assert-True ($dependencyScript.Contains('SDL_GPU=ON') -and $dependencyScript.Contains('SDL_RENDER=OFF')) "SDL GPU renderer policy"
Assert-True ($dependencyScript.Contains('shader-compiler.ps1')) "Host shader compiler bootstrap"
$premakePolicy = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\Common.lua") -Raw
Assert-True ($premakePolicy.Contains('SDL3DebugLibrary') -and $premakePolicy.Contains('SDL3ReleaseLibrary')) "Premake SDL variant selection"
$windowsCommon = Get-Content (Join-Path $Windows "common.ps1") -Raw
Assert-True ($windowsCommon.Contains('KEIRE_VSDEV_ENVIRONMENT_KEY')) "Idempotent Visual Studio environment setup"
$imguiPremake = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\DearImGui.lua") -Raw
Assert-True ($imguiPremake.Contains('project(DearImGuiProject)') -and $imguiPremake.Contains('kind "StaticLib"') -and $imguiPremake.Contains('targetname(DearImGuiLibrary)')) "Dear ImGui static project"
Assert-True ($imguiPremake.Contains('imgui_impl_sdl3.cpp') -and $imguiPremake.Contains('imgui_impl_sdlgpu3.cpp') -and $imguiPremake.Contains('imgui_stdlib.cpp') -and $imguiPremake.Contains('warnings "Off"')) "Premake Dear ImGui source policy"
Assert-True ($imguiPremake.Contains('../../Build/Projects/DearImGui') -and $imguiPremake.Contains('DependencyManifest.SDL3Include')) "Dear ImGui generated project and SDL wiring"
$rootPremake = Get-Content (Join-Path (Get-RepositoryRoot) "premake5.lua") -Raw
Assert-True ($rootPremake.Contains('group "Dependencies"') -and $rootPremake.Contains('Scripts/Premake/DearImGui.lua')) "Dear ImGui solution grouping"
$headerDependencies = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\HeaderDependencies.lua") -Raw
Assert-True ($rootPremake.Contains('Scripts/Premake/HeaderDependencies.lua') -and $headerDependencies.Contains('project(EnTTProject)') -and $headerDependencies.Contains('project(GLMProject)')) "Header-only dependency solution projects"
Assert-True ($headerDependencies.Contains('../../Build/Projects/EnTT') -and $headerDependencies.Contains('../../Build/Projects/GLM') -and $headerDependencies.Contains('warnings "Off"')) "Header-only dependency IDE and warning policy"
$corePremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireCore\premake5.lua") -Raw
$clientPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireClient\premake5.lua") -Raw
$testsPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireTests\premake5.lua") -Raw
Assert-True ($corePremake.Contains('links { DearImGuiProject, ZstdProject }') -and -not $corePremake.Contains('imgui.cpp') -and -not $premakePolicy.Contains('AddDearImGuiSources')) "Private dependency project ownership"
Assert-True ($corePremake.Contains('VendorIncludeDirs.entt') -and $corePremake.Contains('VendorIncludeDirs.glm') -and $corePremake.Contains('dependson { EnTTProject, GLMProject }')) "Private ECS and math build wiring"
Assert-True ($corePremake.Contains('Source/ECS/Components/CameraComponent.cpp') -and $corePremake.Contains('Source/ECS/Components/MeshRendererComponent.cpp')) "Explicit built-in component translation units"
Assert-True ($corePremake.Contains('builtin-shaders.ps1') -and (Test-Path (Join-Path (Get-RepositoryRoot) 'KeireCore\Shaders\BuiltinUnlit.hlsl'))) "First-party built-in shader generation"
$renderSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Rendering\RenderSystem.cpp') -Raw
Assert-True ($renderSource.Contains('BuiltinUnlitShaders.h') -and $renderSource.Contains('renderer->Tint()') -and -not $renderSource.Contains('Vendor/SDL/test')) "Mesh tint shader ownership and draw wiring"
$builtinShader = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Shaders\BuiltinUnlit.hlsl') -Raw
Assert-True ($renderSource.Contains('ResolveLighting') -and $renderSource.Contains('DirectionalLightComponent') -and $renderSource.Contains('AmbientAndExposure') -and $builtinShader.Contains('LightDirection') -and $builtinShader.Contains('worldNormal')) "Directional and ambient light wiring"
Assert-True ($renderSource.Contains('ReadbackRGBA8') -and (Test-Path (Join-Path (Get-RepositoryRoot) 'KeireRenderTests\Source\RenderedOutputTests.cpp'))) "Rendered output readback tests"
$testRunner = Get-Content (Join-Path $Windows 'test.ps1') -Raw
Assert-True ($testRunner.Contains('direct3d12') -and $testRunner.Contains('vulkan') -and $testRunner.Contains('KEIRE_REQUIRE_GPU_TESTS')) "Conditional Windows GPU test backends"
Assert-True ($renderSource.Contains('BuiltinShaderUniformBufferCount(vertex)') -and $renderSource.Contains('SDL_PushGPUFragmentUniformData')) "Built-in and asset-backed shader uniform bindings"
$renderSettingsSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Rendering\RenderSettings.cpp') -Raw
Assert-True ($renderSettingsSource.Contains('Rendering.keiresettings') -and (Test-Path (Join-Path (Get-RepositoryRoot) 'Samples\KeireSandbox\ProjectSettings\Rendering.keiresettings'))) "Persistent project rendering settings"
$renderingAssetsSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Assets\RenderingAssets.cpp') -Raw
$assetPipelineSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Assets\AssetPipeline.cpp') -Raw
$uiSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Ui.cpp') -Raw
$editorDiagnosticsSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireClient\Source\EditorWorkspaceLayer.cpp') -Raw
Assert-True ($renderingAssetsSource.Contains('SDL_GetBasePath()') -and $renderingAssetsSource.Contains('maximumAncestorDepth')) "Executable-relative shader compiler discovery"
Assert-True ($assetPipelineSource.Contains('LogImportDiagnostic') -and $editorDiagnosticsSource.Contains('ReportError("Asset Import"')) "Asset import diagnostics reach persistent and editor logs"
Assert-True ($uiSource.Contains('ImGuiDragDropFlags_SourceAllowNullID')) "Display-item drag sources remain assertion-safe"
Assert-True ($clientPremake.Contains('LinkKeireCore()') -and $testsPremake.Contains('LinkKeireCore()') -and $premakePolicy.Contains('ProjectConfig.CORE_TARGET') -and $premakePolicy.Contains('DearImGuiProject')) "Static dependency link closure"
$clientSources = (Get-ChildItem (Join-Path (Get-RepositoryRoot) "KeireClient") -File -Recurse | Get-Content -Raw) -join "`n"
Assert-True (-not ($clientSources -match '#include\s*[<\"]imgui|ImGui::|ImGui[A-Z]')) "KeireClient Dear ImGui isolation"
$sourceHeaders = Get-ChildItem (Get-RepositoryRoot) -Directory | Where-Object { $_.Name -in @("KeireCore", "KeireClient", "KeireHub", "KeireTests", "AssetTool", "KeireRuntime") } | ForEach-Object { Get-ChildItem (Join-Path $_.FullName "Source") -Filter "*.h" -File -Recurse -ErrorAction SilentlyContinue }
Assert-True (-not $sourceHeaders) "First-party headers live under Include, never Source"
Assert-True ((Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Assets\Asset.h")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Source\Assets\AssetSystem.cpp"))) "Asset subsystem directory organization"
Assert-True ((Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Input\Input.h")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Source\Input\InputSystem.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "Samples\KeireSandbox\Assets\Input\DefaultInput.keireinput"))) "Input subsystem and sample project organization"
Assert-True ((Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Project\Project.h")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Scenes\SceneSystem.h")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireHub\Source\HubApplication.cpp"))) "Project, scene, and hub organization"
$editorWorkspace = Get-Content (Join-Path (Get-RepositoryRoot) "KeireClient\Source\EditorWorkspaceLayer.cpp") -Raw
Assert-True ((Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\AssetBrowserPanel.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\ConsolePanel.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\DiagnosticsPanel.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\ThumbnailService.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\SceneGizmoController.cpp"))) "Focused editor panel, thumbnail, and scene-gizmo classes"
$sceneGizmoSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireClient\Source\Editor\SceneGizmoController.cpp') -Raw
Assert-True ($sceneGizmoSource.Contains('Move W') -and $sceneGizmoSource.Contains('RotationSnapDegrees') -and $sceneGizmoSource.Contains('DrawCameraFrustum') -and $sceneGizmoSource.Contains('DrawLightIcon')) "Scene transform tools, snapping, and component gizmos"
Assert-True ($clientSources.Contains('DisplayName(record.RelativePath)') -and $clientSources.Contains('TrashRecords()') -and $clientSources.Contains('AssetImportPolicy::KeepLastGood')) "Unity-style asset browser labels, trash, and best-effort import"
Assert-True ($clientSources.Contains('DrawAssetDragSource') -and $editorWorkspace.Contains('DecodeDragPayload') -and $editorWorkspace.Contains('PickSceneEntity')) "Thumbnail drag sources and typed Scene-view asset drops"
Assert-True ($editorWorkspace.Contains('m_ComponentExpansion') -and $editorWorkspace.Contains('Control Browser') -and $editorWorkspace.Contains('BINDING PROCESSORS') -and $editorWorkspace.Contains('UiKey::R')) "Foldable component cards, advanced Input Actions authoring, and redo shortcut"
$processSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Process.cpp') -Raw
Assert-True ($processSource.Contains('"/select," + utf8Path') -and $processSource.Contains('std::filesystem::weakly_canonical')) "Absolute platform file-manager reveal routing"
Assert-True ($editorWorkspace.Contains('m_InputForwardToConsole') -and $editorWorkspace.Contains('inputEpsilon') -and $editorWorkspace.Contains('coalescingWindowNanoseconds')) "Input Debugger noise filtering"
$hubSource = Get-Content (Join-Path (Get-RepositoryRoot) "KeireHub\Source\HubApplication.cpp") -Raw
Assert-True ($hubSource.Contains('CreateSystemTray') -and $hubSource.Contains('Show Hub') -and $hubSource.Contains('m_Tray->IsAvailable()')) "Project Hub tray backgrounding"
$sampleScene = Get-Content (Join-Path (Get-RepositoryRoot) "Samples\KeireSandbox\Assets\Scenes\SampleScene.keirescene") -Raw
Assert-True ($sampleScene.Contains('"schemaVersion": 2') -and $sampleScene.Contains('"components"') -and $sampleScene.Contains('Directional Light')) "Schema-v2 component sample scene"
$publicHeaders = (Get-ChildItem (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire") -File -Recurse | Get-Content -Raw) -join "`n"
Assert-True ($publicHeaders.Contains('class KEIRE_API UndoService') -and $editorWorkspace.Contains('m_ActiveUndoContext')) "Shared undo service and editor routing"
Assert-True (-not ($publicHeaders -match 'SDL3/|nlohmann/json|imgui|entt/|glm/|assimp/|stb_image')) "Public dependency isolation"
Assert-True ($publicHeaders.Contains('class KEIRE_API UiWorkspace') -and $clientSources.Contains('BuildFactoryLayout')) "Kéire workspace facade and factory layout wiring"
$exportedTypes = @(
    "Application", "ApplicationCommandLineArguments", "CommandLineError", "EventView", "EventSubscription",
    "EventBus", "Layer", "LayerStack", "LoggerHandle", "Log", "Time", "UiError", "UiScope", "UiWindowScope",
    "UiChildScope", "UiMenuBarScope", "UiMenuScope", "UiTabBarScope", "UiTabItemScope", "UiTreeNodeScope",
    "UiDisabledScope", "UiIdScope", "UiMainMenuBarScope", "UiComboScope", "UiPopupScope", "UiTableScope", "UiDragSourceScope", "UiDragTargetScope", "UiPanelScope", "UiFrame",
    "UiLayoutBuilder", "UiPanelRegistration", "UiWorkspace", "WindowError", "Window", "FolderDialogOperation", "WindowSystem", "ConfigurationError",
    "Asset", "BinaryAsset", "TextAsset", "AssetLoadError", "AssetSystem", "AssetDatabase", "AssetCooker", "InputActionAsset", "InputActionSubscription", "InputActionHandle", "InputActionContext", "InteractiveRebindOperation", "InputSystem", "InputCaptureOverride",
    "Project", "ProjectRegistry", "UndoCommand", "UndoTransaction", "UndoContext", "UndoService", "EntityId", "ComponentTypeId", "Component", "ComponentRegistry", "Entity", "TransformComponent", "DirectionalLightComponent", "CameraComponent", "MeshRendererComponent", "SceneAsset", "Scene", "SceneObjectHandle", "SceneRuntimeSession", "SceneLoadOperation", "SceneSystem", "UiImage", "SaveFileDialogOperation", "SystemTray", "RenderSurface", "RenderView", "RenderSystem", "ShaderAsset", "MaterialAsset", "MeshAsset", "Texture2DAsset"
)
foreach ($exportedType in $exportedTypes) {
    Assert-True ($publicHeaders -match "class\s+KEIRE_API\s+$exportedType\b") "KEIRE_API annotation for $exportedType"
}
foreach ($exportedFunction in @("AssertionFailure", "GetName", "GetBuildInfo", "GetVersionString", "LoadWindowSpecification")) {
    Assert-True ($publicHeaders -match "KEIRE_API[^;{}]*\b$exportedFunction\s*\(") "KEIRE_API annotation for $exportedFunction"
}
Assert-True (-not ($publicHeaders -match 'KEIRE_API[^;{}]*\b(?:GetApplicationCommandLineDescription|CreateApplication)\s*\(')) "Managed-client reverse API ownership"
$packageScript = Get-Content (Join-Path $Windows "package.ps1") -Raw
Assert-True ($packageScript.Contains('dear-imgui-LICENSE.txt') -and $packageScript.Contains('$Lock.IMGUI_COMMIT') -and $packageScript.Contains('$imguiLibraryName.lib')) "Dear ImGui package metadata and archive"
Assert-True ($packageScript.Contains('zstandard-LICENSE.txt') -and $packageScript.Contains('$Lock.ZSTD_COMMIT') -and $packageScript.Contains('$zstdLibraryName.lib')) "Zstandard package metadata and archive"
Assert-True ($packageScript.Contains('entt-LICENSE.txt') -and $packageScript.Contains('$Lock.ENTT_COMMIT') -and $packageScript.Contains('glm-COPYING.txt') -and $packageScript.Contains('$Lock.GLM_COMMIT')) "ECS and math package metadata and attribution"
Assert-True ($packageScript.Contains('KeireShaderCompiler.exe') -and $packageScript.Contains('SDL-shadercross-LICENSE.txt') -and $packageScript.Contains('$Lock.SDL_SHADERCROSS_COMMIT')) "Shader compiler package metadata and attribution"
Assert-True ($packageScript.Contains('assimp-LICENSE.txt') -and $packageScript.Contains('stb-LICENSE.txt') -and $packageScript.Contains('$Lock.ASSIMP_COMMIT') -and $packageScript.Contains('$Lock.STB_COMMIT')) "Asset importer package metadata and attribution"
$packageConfig = Get-Content (Join-Path (Get-RepositoryRoot) "Config\PackageConfig.cmake.in") -Raw
Assert-True ($packageConfig.Contains('@PROJECT_NAMESPACE@ImGui.lib') -and $packageConfig.Contains('@PROJECT_NAMESPACE@Zstd.a') -and $packageConfig.Contains('"${_assimp_sdk_library}" "${_assimp_zlib_sdk_library}" SDL3::SDL3-static')) "Private archive CMake transitive link"
Assert-True (-not $packageConfig.Contains('include;${_core_sdk_prefix}/third-party')) "SDK omits general third-party include path"
$publicLogHeader = Get-Content (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Log.h") -Raw
Assert-True (-not $publicLogHeader.Contains("spdlog/") -and -not $publicLogHeader.Contains("fmt::") -and $publicLogHeader.Contains("KEIRE_COMPILED_LOG_LEVEL")) "Public logging boundary is engine-owned"
$commonPremake = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\Common.lua") -Raw
Assert-True ($commonPremake.Contains('_DISABLE_STRING_ANNOTATION') -and $commonPremake.Contains('_DISABLE_VECTOR_ANNOTATION')) "MSVC sanitizer dependency ABI alignment"

$packageStage = Join-Path ([IO.Path]::GetTempPath()) ("template-package-test-" + [guid]::NewGuid().ToString("N"))
try {
    foreach ($path in @("bin\Client.exe", "bin\Hub.exe", "lib\Core.lib", "lib\CoreImGui.lib", "Config\Client.json", "include\Core\Core.h", "include\Core\Log.h", "include\Core\Api.h", "include\Core\Application.h", "include\Core\Assert.h", "include\Core\BuildInfo.h", "include\Core\EntryPoint.h", "include\Core\Event.h", "include\Core\Layer.h", "include\Core\Ref.h", "include\Core\Time.h", "include\Core\Project\Project.h", "include\Core\Scenes\Scene.h", "include\Core\Scenes\SceneAsset.h", "include\Core\Scenes\SceneSystem.h", "include\Core\Window.h", "include\Core\WindowConfig.h", "samples\KeireSandbox\ProjectSettings\Project.keireproject", "samples\KeireSandbox\ProjectSettings\Rendering.keiresettings", "samples\KeireSandbox\Assets\Input\DefaultInput.keireinput", "samples\KeireSandbox\Assets\Scenes\SampleScene.keirescene", "examples\consumer\Main.cpp", "examples\consumer\Client.json", "examples\consumer\CMakeLists.txt", "examples\consumer\README.md", "examples\managed-consumer\ClientApplication.cpp", "examples\managed-consumer\CMakeLists.txt", "examples\managed-consumer\README.md", "lib\cmake\CrossPlatformCoreClientTemplate\CrossPlatformCoreClientTemplateConfig.cmake", "third-party\spdlog\spdlog.h", "third-party\SDL3\include\SDL3\SDL.h", "third-party\SDL3\lib\SDL3-static.lib", "third-party\SDL3\cmake\SDL3Config.cmake", "third-party\SDL3\licenses\SDL3\LICENSE.txt", "third-party\licenses\spdlog-LICENSE.txt", "third-party\licenses\fmt-LICENSE.rst", "third-party\licenses\doctest-LICENSE.txt", "third-party\licenses\nlohmann-json-LICENSE.MIT.txt", "third-party\licenses\dear-imgui-LICENSE.txt", "README.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md", "build-manifest.json")) {
        $file = Join-Path $packageStage $path
        New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
        New-Item -ItemType File -Force $file | Out-Null
    }
    foreach ($path in @("bin\CoreAssetTool.exe", "lib\CoreZstd.lib", "include\Core\Math\Math.h", "include\Core\ECS\Component.h", "include\Core\ECS\Entity.h", "include\Core\ECS\Components\TransformComponent.h", "include\Core\ECS\Components\DirectionalLightComponent.h", "include\Core\Assets\Asset.h", "include\Core\Assets\AssetSystem.h", "include\Core\Assets\AssetPipeline.h", "include\Core\Assets\InputActionAsset.h", "include\Core\Input\Input.h", "samples\KeireSandbox\Assets\Input\DefaultInput.keireinput.keiremeta", "third-party\licenses\zstandard-LICENSE.txt", "third-party\licenses\entt-LICENSE.txt", "third-party\licenses\glm-COPYING.txt")) {
        $file = Join-Path $packageStage $path
        New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
        New-Item -ItemType File -Force $file | Out-Null
    }
    Remove-Item (Join-Path $packageStage "third-party\spdlog") -Recurse -Force
    foreach ($path in @("bin\KeireShaderCompiler.exe", "bin\dxcompiler.dll", "bin\dxil.dll", "lib\assimp.lib", "lib\zlibstatic.lib", "include\Core\Undo.h", "include\Core\ECS\Components\CameraComponent.h", "include\Core\ECS\Components\MeshRendererComponent.h", "include\Core\Rendering\RenderSystem.h", "include\Core\Assets\RenderingAssets.h", "samples\KeireSandbox\Assets\Shaders\DefaultUnlit.keireshader", "samples\KeireSandbox\Assets\Shaders\DefaultUnlit.hlsl", "samples\KeireSandbox\Assets\Materials\DefaultUnlit.keirematerial", "third-party\licenses\SDL-shadercross-LICENSE.txt", "third-party\licenses\DirectXShaderCompiler-LICENSE.txt", "third-party\licenses\DirectXShaderCompiler-ThirdPartyNotices.txt", "third-party\licenses\SPIRV-Cross-LICENSE.txt", "third-party\licenses\SPIRV-Headers-LICENSE.txt", "third-party\licenses\SPIRV-Tools-LICENSE.txt", "third-party\licenses\assimp-LICENSE.txt", "third-party\licenses\assimp-zlib-LICENSE.txt", "third-party\licenses\stb-LICENSE.txt")) {
        $file = Join-Path $packageStage $path
        New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
        New-Item -ItemType File -Force $file | Out-Null
    }
    $uiHeader = Join-Path $packageStage "include\Core\Ui.h"
    New-Item -ItemType Directory -Force (Split-Path $uiHeader) | Out-Null
    New-Item -ItemType File -Force $uiHeader | Out-Null
    New-Item -ItemType File -Force (Join-Path $packageStage "include\Core\UiWorkspace.h") | Out-Null
    New-Item -ItemType File -Force (Join-Path $packageStage "bin\CoreRuntime.exe") | Out-Null
    Assert-WindowsPackageStage $packageStage Client Hub Core Core
    foreach ($generatedPath in @(
        "samples\KeireSandbox\Build\generated.vcxproj",
        "samples\KeireSandbox\Logs\Core.log",
        "samples\KeireSandbox\Library\AssetCatalog.json",
        "samples\KeireSandbox\Temp\editor.tmp",
        "samples\KeireSandbox\Assets\Scenes\SampleScene.recovery.json"
    )) {
        $generatedFile = Join-Path $packageStage $generatedPath
        New-Item -ItemType Directory -Force (Split-Path $generatedFile) | Out-Null
        New-Item -ItemType File -Force $generatedFile | Out-Null
        Assert-Throws { Assert-WindowsPackageGeneratedDataFree $packageStage } "Generated package data rejection: $generatedPath"
        Remove-Item $generatedFile -Force
    }
    $archiveContamination = Join-Path ([IO.Path]::GetTempPath()) ("template-package-contamination-" + [guid]::NewGuid().ToString("N") + ".zip")
    try {
        $generatedFile = Join-Path $packageStage "samples\KeireSandbox\Build\generated.txt"
        New-Item -ItemType Directory -Force (Split-Path $generatedFile) | Out-Null
        New-Item -ItemType File -Force $generatedFile | Out-Null
        Compress-Archive (Join-Path $packageStage "*") $archiveContamination
        Assert-Throws { Assert-WindowsPackageArchiveGeneratedDataFree $archiveContamination } "Generated archive data rejection"
        Remove-Item $generatedFile -Force
    }
    finally {
        Remove-Item $archiveContamination -Force -ErrorAction SilentlyContinue
    }
    Remove-Item (Join-Path $packageStage "lib\CoreImGui.lib")
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Hub Core Core } "Missing Dear ImGui package archive validation"
    New-Item -ItemType File (Join-Path $packageStage "lib\CoreImGui.lib") | Out-Null
    Remove-Item (Join-Path $packageStage "third-party\licenses\dear-imgui-LICENSE.txt")
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Hub Core Core } "Missing Dear ImGui package license validation"
    New-Item -ItemType File (Join-Path $packageStage "third-party\licenses\dear-imgui-LICENSE.txt") | Out-Null
    Remove-Item (Join-Path $packageStage "third-party\licenses\spdlog-LICENSE.txt")
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Hub Core Core } "Missing package license validation"
}
finally {
    Remove-Item $packageStage -Recurse -Force -ErrorAction SilentlyContinue
}

$trackedSampleStage = Join-Path ([IO.Path]::GetTempPath()) ("template-tracked-sample-" + [guid]::NewGuid().ToString("N"))
try {
    Copy-WindowsTrackedTree (Get-RepositoryRoot) "Samples/KeireSandbox" $trackedSampleStage
    Assert-True (Test-Path (Join-Path $trackedSampleStage "ProjectSettings\Project.keireproject")) "Tracked sample copy includes project settings"
    Assert-WindowsPackageGeneratedDataFree $trackedSampleStage
}
finally {
    Remove-Item $trackedSampleStage -Recurse -Force -ErrorAction SilentlyContinue
}

$identityFixture = Join-Path ([IO.Path]::GetTempPath()) ("template-identity-test-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force (Join-Path $identityFixture "Scripts\Windows"), (Join-Path $identityFixture "Config") | Out-Null
    Copy-Item (Join-Path $Windows "common.ps1"), (Join-Path $Windows "build-info.ps1") (Join-Path $identityFixture "Scripts\Windows")
    $identityConfig = @(
        "PROJECT_IDENTIFIER=IdentityFixture", 'PROJECT_DISPLAY_NAME=Quoted "Kéire" \\ Client',
        "PROJECT_VERSION=1.2.3-alpha.1+build.5", "PROJECT_NAMESPACE=IdentityFixture", "PROJECT_MACRO_PREFIX=IDENTITY_FIXTURE",
        "CORE_TARGET=IdentityFixtureCore", "CORE_DIRECTORY=IdentityFixtureCore", "CLIENT_TARGET=IdentityFixtureClient", "CLIENT_DIRECTORY=IdentityFixtureClient", "HUB_TARGET=IdentityFixtureHub", "HUB_DIRECTORY=IdentityFixtureHub",
        "TESTS_TARGET=IdentityFixtureTests", "TESTS_DIRECTORY=IdentityFixtureTests", "ARTIFACT_PREFIX=identityfixture", "REPOSITORY_SLUG=example/identity-fixture"
    )
    [IO.File]::WriteAllLines((Join-Path $identityFixture "Config\Project.conf"), $identityConfig, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $identityFixture ".gitignore"), "/Build/`n.ninja_lock`n", [Text.UTF8Encoding]::new($false))
    & git -C $identityFixture init --quiet
    & git -C $identityFixture config user.email "scripts@example.invalid"
    & git -C $identityFixture config user.name "Script Tests"
    & git -C $identityFixture add .
    & git -C $identityFixture commit --quiet -m first
    $firstCommit = (& git -C $identityFixture rev-parse HEAD) -join ""
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    $identityHeader = Join-Path $identityFixture "Build\Generated\IdentityFixture\BuildInfo.generated.h"
    $firstIdentity = [IO.File]::ReadAllText($identityHeader, [Text.Encoding]::UTF8)
    Assert-True ($firstIdentity.Contains('#define KEIRE_BUILD_PROJECT_VERSION "1.2.3-alpha.1+build.5"')) "Semantic Version identity generation"
    Assert-True ($firstIdentity.Contains('#define KEIRE_BUILD_PROJECT_NAME "Quoted \"Kéire\" \\\\ Client"')) "C string identity escaping"
    Assert-True ($firstIdentity.Contains("#define KEIRE_BUILD_GIT_COMMIT `"$firstCommit`"")) "Clean Git identity"
    Assert-True ($firstIdentity.Contains("#define KEIRE_BUILD_GIT_DIRTY false")) "Clean Git dirty state"
    New-Item -ItemType File -Force (Join-Path $identityFixture ".ninja_lock") | Out-Null
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    Assert-True (([IO.File]::ReadAllText($identityHeader)).Contains("#define KEIRE_BUILD_GIT_DIRTY false")) "Ignored Ninja lock dirty state"
    $firstWriteTime = [IO.File]::GetLastWriteTimeUtc($identityHeader)
    Start-Sleep -Milliseconds 1100
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    Assert-Equal ([IO.File]::GetLastWriteTimeUtc($identityHeader)) $firstWriteTime "Unchanged identity header timestamp"
    Set-Content -LiteralPath (Join-Path $identityFixture "untracked.txt") -Value untracked
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    Assert-True (([IO.File]::ReadAllText($identityHeader)).Contains("#define KEIRE_BUILD_GIT_DIRTY true")) "Untracked Git dirty state"
    & git -C $identityFixture add .
    & git -C $identityFixture commit --quiet -m second
    $secondCommit = (& git -C $identityFixture rev-parse HEAD) -join ""
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    $secondIdentity = [IO.File]::ReadAllText($identityHeader, [Text.Encoding]::UTF8)
    Assert-True ($secondCommit -ne $firstCommit -and $secondIdentity.Contains($secondCommit)) "Identity refresh after commit"
    Assert-True ($secondIdentity.Contains("#define KEIRE_BUILD_GIT_DIRTY false")) "Committed Git clean state"
}
finally {
    Remove-Item -LiteralPath $identityFixture -Recurse -Force -ErrorAction SilentlyContinue
}

$parentFixture = Join-Path ([IO.Path]::GetTempPath()) ("template-script-test-" + [guid]::NewGuid().ToString("N"))
$fixture = Join-Path $parentFixture "Template"
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    $coreDirectory = $project.CORE_DIRECTORY
    $clientDirectory = $project.CLIENT_DIRECTORY
    $hubDirectory = $project.HUB_DIRECTORY
    $testsDirectory = $project.TESTS_DIRECTORY
    $projectNamespace = $project.PROJECT_NAMESPACE
    foreach ($directory in @("Scripts\Windows", "Config", "Examples\Consumer", "Examples\ManagedConsumer", "$coreDirectory\Include\$projectNamespace", "$coreDirectory\Source", "$clientDirectory\Source", "$hubDirectory\Source", "$testsDirectory\Source", "Vendor", "Build\Bin")) {
        New-Item -ItemType Directory -Force (Join-Path $fixture $directory) | Out-Null
    }
    Copy-Item (Join-Path $Windows "common.ps1"), (Join-Path $Windows "rename.ps1"), (Join-Path $Windows "clean.ps1"), (Join-Path $Windows "doctor.ps1") (Join-Path $fixture "Scripts\Windows")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\Project.conf") (Join-Path $fixture "Config\Project.conf")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\Client.json") (Join-Path $fixture "Config\Client.json")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\PackageConfig.cmake.in") (Join-Path $fixture "Config\PackageConfig.cmake.in")
    Copy-Item (Join-Path (Get-RepositoryRoot) "premake5.lua") (Join-Path $fixture "premake5.lua")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Examples\Consumer\CMakeLists.txt"), (Join-Path (Get-RepositoryRoot) "Examples\Consumer\Main.cpp") (Join-Path $fixture "Examples\Consumer")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Examples\ManagedConsumer\CMakeLists.txt"), (Join-Path (Get-RepositoryRoot) "Examples\ManagedConsumer\ClientApplication.cpp") (Join-Path $fixture "Examples\ManagedConsumer")
    Set-Content (Join-Path $fixture "$coreDirectory\Include\$projectNamespace\Core.h") @"
#ifndef $($project.PROJECT_MACRO_PREFIX)_CORE_CORE_H
#define $($project.PROJECT_MACRO_PREFIX)_CORE_CORE_H
namespace $projectNamespace { const char* GetName(); }
#endif
"@
    Set-Content (Join-Path $fixture "$coreDirectory\Include\$projectNamespace\Log.h") @"
#ifndef $($project.PROJECT_MACRO_PREFIX)_CORE_LOG_H
#define $($project.PROJECT_MACRO_PREFIX)_CORE_LOG_H
namespace $projectNamespace { class Log; }
#endif
"@
    Set-Content (Join-Path $fixture "$coreDirectory\Source\Library.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "$clientDirectory\Source\Main.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "$hubDirectory\Source\Main.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "$testsDirectory\Source\Main.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "README.md") "$($project.PROJECT_IDENTIFIER) $($project.REPOSITORY_SLUG) Scripts/Tests Core.log Client.log"
    Set-Content (Join-Path $fixture "Vendor\keep.txt") 'vendor'
    Set-Content (Join-Path $fixture "Build\Bin\remove.txt") 'build'

    & git -C $parentFixture init --quiet
    & git -C $parentFixture config user.email "scripts@example.invalid"
    & git -C $parentFixture config user.name "Script Tests"
    & git -C $parentFixture add Template
    & git -C $parentFixture commit --quiet -m fixture
    Assert-Equal (Get-GitWorktreeRoot $fixture).Path (Resolve-Path $parentFixture).Path "Parent Git worktree detection"
    Add-Content (Join-Path $fixture "README.md") "dirty"

    Assert-Throws { & (Join-Path $fixture "Scripts\Windows\rename.ps1") -Name ScriptFixture -DisplayName "Bad`nName" -Repository example/script-fixture } "Rename newline rejection"
    $unicodeDisplayName = 'Script "Fixturé" \\ Name'
    & (Join-Path $fixture "Scripts\Windows\rename.ps1") -Name ScriptFixture -DisplayName $unicodeDisplayName -Repository example/script-fixture
    Assert-True (-not (Test-Path (Join-Path $fixture ".git"))) "Nested Git repository prevention"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureCore\Include\ScriptFixture\Core.h")) "Rename structure"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureHub\Source\Main.cpp")) "Rename hub structure"
    $renamed = Get-Content (Join-Path $fixture "Config\Project.conf") -Raw -Encoding UTF8
    Assert-True ($renamed.Contains("CORE_TARGET=ScriptFixtureCore")) "Rename manifest"
    Assert-True ($renamed.Contains("PROJECT_MACRO_PREFIX=SCRIPT_FIXTURE")) "Rename macro manifest"
    Assert-True ($renamed.Contains("PROJECT_VERSION=$($project.PROJECT_VERSION)")) "Rename version preservation"
    $renamedPremake = Get-Content (Join-Path $fixture "premake5.lua") -Raw
    Assert-True ($renamedPremake.Contains("valid Semantic Version 2.0.0")) "Premake Semantic Version validation"
    $renamedConsumer = Get-Content (Join-Path $fixture "Examples\Consumer\CMakeLists.txt") -Raw
    Assert-True ($renamedConsumer.Contains("find_package(ScriptFixture CONFIG REQUIRED)")) "Renamed CMake package identity"
    Assert-True ($renamedConsumer.Contains("ScriptFixture::Core")) "Renamed CMake imported target"
    $renamedManagedConsumer = Get-Content (Join-Path $fixture "Examples\ManagedConsumer\CMakeLists.txt") -Raw
    Assert-True ($renamedManagedConsumer.Contains("find_package(ScriptFixture CONFIG REQUIRED)")) "Renamed managed CMake package identity"
    Assert-True ($renamedManagedConsumer.Contains("ScriptFixture::Core")) "Renamed managed CMake imported target"
    Assert-True ((Get-Content (Join-Path $fixture "Config\PackageConfig.cmake.in") -Raw).Contains("@PROJECT_NAMESPACE@::Core")) "Generic package template preservation"
    Assert-True ($renamed.Contains("PROJECT_DISPLAY_NAME=$unicodeDisplayName")) "UTF-8 display name preservation"
    Assert-True ((Get-Content (Join-Path $fixture "README.md") -Raw).Contains("dirty")) "Pre-existing edit preservation"
    Assert-True ((Get-Content (Join-Path $fixture "README.md") -Raw).Contains("Scripts/Tests Core.log Client.log")) "Stable generic path preservation"
    Assert-True (Test-Path (Join-Path $fixture "Config\Client.json")) "Stable client configuration path preservation"
    $renamedHeaders = (Get-ChildItem (Join-Path $fixture "ScriptFixtureCore\Include") -File -Recurse | Get-Content) -join "`n"
    Assert-True (-not $renamedHeaders.Contains($project.PROJECT_MACRO_PREFIX)) "Old include guard removal"
    Assert-True ($renamedHeaders.Contains("SCRIPT_FIXTURE_CORE_CORE_H")) "Renamed include guard"
    & (Join-Path $fixture "Scripts\Windows\doctor.ps1") -Generator ninja -Architecture x86_64 -Toolset clang

    & (Join-Path $fixture "Scripts\Windows\clean.ps1") -Scope full
    Assert-True (-not (Test-Path (Join-Path $fixture "Build\Bin"))) "Full clean build removal"
    Assert-True (Test-Path (Join-Path $fixture "Vendor\keep.txt")) "Full clean vendor preservation"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureCore\Source\Library.cpp")) "Full clean source preservation"
}
finally {
    if ($parentFixture.StartsWith([IO.Path]::GetTempPath()) -and (Test-Path $parentFixture)) {
        Remove-Item -LiteralPath $parentFixture -Recurse -Force
    }
}

$repositoryFiles = Get-ChildItem (Get-RepositoryRoot) -File -Recurse | Where-Object {
    $_.FullName -notmatch '[\\/](\.git|\.vs|Vendor|Tools|Build|Logs|Artifacts)[\\/]' -and $_.FullName -notmatch '[\\/]Scripts[\\/]Tests[\\/]'
}
$deprecatedNames = foreach ($prefix in @("CORE", "CLIENT")) {
    foreach ($suffix in @("API", "ASSERT", "ASSERTIONS_ENABLED", "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL")) { "${prefix}_${suffix}" }
}
foreach ($name in $deprecatedNames) {
    Assert-Equal (@($repositoryFiles | Select-String -Pattern "\b$([regex]::Escape($name))\b").Count) 0 "Deprecated public macro check for $name"
}
foreach ($stale in @('#include "KeireCore/', 'Scripts/KeireTests', 'Scripts\KeireTests', 'Scripts/Windows/Tests', 'Scripts/Unix/Tests', 'KeireCore.log', 'KeireClient.log')) {
    Assert-Equal (@($repositoryFiles | Select-String -SimpleMatch $stale).Count) 0 "Stale repository identity check for $stale"
}
Write-Host "Windows script regression tests passed."
