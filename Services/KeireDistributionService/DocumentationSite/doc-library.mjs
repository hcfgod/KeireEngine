const group = (label, icon, files) => ({ label, icon, files });

export const docGroups = [
    group("Start here", "rocket", [
        "README.md",
        "GettingStarted.md",
        "ProjectHub.md",
        "ProjectSystem.md",
        "ProjectSettings.md",
    ]),
    group("Engine foundations", "puzzle", [
        "Architecture.md",
        "RuntimeLifecycle.md",
        "ECSAndComponents.md",
        "SceneSystem.md",
        "GameplayFoundations.md",
        "InputSystem.md",
    ]),
    group("Editor and authoring", "seti:settings", [
        "UiWorkspace.md",
        "EditorPanels.md",
        "SceneAuthoring.md",
        "AssetBrowser.md",
        "InputActionsEditor.md",
        "InputDebugger.md",
        "UndoRedo.md",
        "AnimationRigging.md",
        "WeaponAuthoring.md",
    ]),
    group("Assets, rendering, and builds", "layers", [
        "AssetRuntime.md",
        "AssetPipeline.md",
        "AudioProduction.md",
        "BuiltinMeshes.md",
        "Rendering.md",
        "ShadersAndMaterials.md",
        "MaterialParityMatrix.md",
        "Vfx.md",
        "VfxBeyondParityRoadmap.md",
        "generated/VfxCapabilities.md",
        "VisualAuthoringInitiatives.md",
        "PlayerBuilds.md",
    ]),
    group("C# scripting", "seti:c-sharp", [
        "Scripting/README.md",
        "Scripting/GettingStarted.md",
        "Scripting/BehavioursAndLifecycle.md",
        "Scripting/SerializationAndInspector.md",
        "Scripting/EntitiesComponentsAndTransforms.md",
        "Scripting/AssetsAndScriptableObjects.md",
        "Scripting/GameplayServices.md",
        "Scripting/Audio.md",
        "Scripting/Animation.md",
        "Scripting/UiAndEvents.md",
        "Scripting/AsyncReloadAndDiagnostics.md",
        "Scripting/ApiIndex.md",
        "ManagedScripting.md",
    ]),
    group("Production and release", "approve-check", [
        "Profiling.md",
        "PerformanceGates.md",
        "TestingAndRelease.md",
        "PackageArchives.md",
        "AssetPackages.md",
        "MarketplaceLaunch.md",
        "ProductionReadinessReview.md",
        "Maintainability.md",
    ]),
    group("Diagnostics", "information", [
        "Diagnostics/README.md",
        "Diagnostics/KEIRE-AUDIO-0001.md",
        "Diagnostics/KEIRE-EXAMPLE-0001.md",
        "Diagnostics/KEIRE-REPLAY-0001.md",
        "Diagnostics/KEIRE-REPLAY-0002.md",
    ]),
];

const slugPart = (value) => value
    .replace(/([a-z0-9])([A-Z])/g, "$1-$2")
    .replace(/([A-Z]+)([A-Z][a-z])/g, "$1-$2")
    .replace(/[^A-Za-z0-9]+/g, "-")
    .replace(/^-|-$/g, "")
    .toLowerCase();

export function sourcePathToSlug(sourcePath) {
    const normalized = sourcePath.replaceAll("\\", "/");
    const parts = normalized.split("/");
    const fileName = parts.pop();
    if (!fileName?.toLowerCase().endsWith(".md")) {
        throw new Error(`Documentation source is not Markdown: ${sourcePath}`);
    }

    const stem = fileName.slice(0, -3);
    const routeParts = parts.map(slugPart);
    if (stem.toLowerCase() !== "readme") {
        routeParts.push(slugPart(stem));
    } else if (routeParts.length === 0) {
        routeParts.push("overview");
    }
    return `reference/${routeParts.join("/")}`;
}

export const allDocSources = docGroups.flatMap(({ files }) => files);

// Each published guide is tied to at least one repository authority. Source validation checks this inventory so a
// renamed or removed implementation boundary cannot silently leave an apparently current guide behind.
export const docAuthorities = {
    "README.md": ["AGENTS.md", "Config/Project.conf"],
    "GettingStarted.md": ["Scripts/project.ps1", "Scripts/project.sh", "Config/Dependencies.lock"],
    "ProjectHub.md": ["KeireHub/Source/HubProductUi.cpp", "KeireHubRuntime/Include/KeireHubRuntime"],
    "ProjectSystem.md": ["KeireCore/Include/Keire/Project/Project.h", "KeireCore/Source/Project/Project.cpp"],
    "ProjectSettings.md": ["KeireCore/Include/Keire/Project/ProjectAuthoringSettings.h", "KeireClient/Include/KeireClient/Editor/ProjectSettingsDocument.h"],
    "Architecture.md": ["premake5.lua", "KeireCore/Include/Keire/Application.h"],
    "RuntimeLifecycle.md": ["KeireCore/Source/Application.cpp", "KeireCore/Include/Keire/Application.h"],
    "ECSAndComponents.md": ["KeireCore/Include/Keire/ECS/Component.h", "KeireCore/Include/Keire/Scenes/Scene.h"],
    "SceneSystem.md": ["KeireCore/Include/Keire/Scenes/SceneSystem.h", "KeireCore/Include/Keire/Scenes/SceneAsset.h"],
    "GameplayFoundations.md": ["KeireCore/Include/Keire/Scenes/ScenePresentationRuntime.h", "KeireRuntime/Source/RuntimeApplication.cpp"],
    "InputSystem.md": ["KeireCore/Include/Keire/Input/Input.h", "KeireCore/Include/Keire/Assets/InputActionAsset.h"],
    "UiWorkspace.md": ["KeireCore/Include/Keire/UiWorkspace.h", "KeireCore/Include/Keire/Ui.h"],
    "EditorPanels.md": ["KeireClient/Include/KeireClient/Editor/EditorPanels.h", "KeireClient/Source/Editor"],
    "SceneAuthoring.md": ["KeireClient/Include/KeireClient/Editor/SceneDocument.h", "KeireClient/Source/Editor/SceneDocument.cpp"],
    "AssetBrowser.md": ["KeireClient/Include/KeireClient/Editor/AssetBrowserPanel.h", "KeireClient/Source/Editor/AssetBrowserPanel.cpp"],
    "InputActionsEditor.md": ["KeireClient/Include/KeireClient/Editor/InputActionsDocument.h", "KeireClient/Source/Editor/InputActionsPanel.cpp"],
    "InputDebugger.md": ["KeireClient/Source/ClientApplication.cpp", "KeireCore/Include/Keire/Input/Input.h"],
    "UndoRedo.md": ["KeireCore/Include/Keire/Undo.h", "KeireCore/Source/Undo.cpp"],
    "AnimationRigging.md": ["KeireCore/Include/Keire/Animation/RiggingSystem.h", "KeireClient/Include/KeireClient/Editor/RiggingStudioPanel.h"],
    "WeaponAuthoring.md": ["KeireManaged/ProductionWeapons.cs", "KeireManaged/WeaponSystem.cs"],
    "AssetRuntime.md": ["KeireCore/Include/Keire/Assets/AssetSystem.h", "KeireCore/Source/Assets"],
    "AssetPipeline.md": ["KeireCore/Include/Keire/Assets/AssetSystem.h", "KeireCore/Source/Assets/RenderableAssets.cpp"],
    "AudioProduction.md": ["KeireCore/Include/Keire/Audio/AudioSystem.h", "KeireClient/Source/Editor/AudioMixerPanel.cpp"],
    "BuiltinMeshes.md": ["KeireCore/Include/KeireInternal/Assets/BuiltinMeshes.h", "KeireCore/Source/Assets/BuiltinMeshes.cpp"],
    "Rendering.md": ["KeireCore/Include/Keire/Rendering/RenderSystem.h", "KeireCore/Source/Rendering"],
    "ShadersAndMaterials.md": ["KeireCore/Include/Keire/Rendering/ShaderGraph.h", "KeireClient/Source/Editor/ShaderGraphPanel.cpp"],
    "MaterialParityMatrix.md": ["KeireCore/Include/Keire/Rendering/MaterialEcosystem.h", "KeireTests/Source/Rendering/MaterialEcosystemTests.cpp"],
    "Vfx.md": ["KeireCore/Include/Keire/Vfx/VfxSystem.h", "KeireClient/Source/Editor/VfxEffectPanel.cpp"],
    "VfxBeyondParityRoadmap.md": ["Docs/VfxParityManifest.json", "Scripts/Vfx/runtime_vfx_catalog.py"],
    "generated/VfxCapabilities.md": ["Docs/VfxParityManifest.json", "Scripts/Vfx/generate_vfx_capabilities.py"],
    "VisualAuthoringInitiatives.md": ["Docs/VfxParityManifest.json", "KeireCore/Include/Keire/Rendering/MaterialGraph.h"],
    "PlayerBuilds.md": ["KeireCore/Include/Keire/Build/PlayerBuild.h", "KeireClient/Source/Editor/PlayerBuildService.cpp"],
    "Scripting/README.md": ["KeireManaged/Behaviour.cs", "KeireManaged/RuntimeApi.cs"],
    "Scripting/GettingStarted.md": ["KeireCore/Include/Keire/Scripting/ManagedAssemblyAsset.h", "KeireManaged/Keire.Managed.csproj"],
    "Scripting/BehavioursAndLifecycle.md": ["KeireManaged/Behaviour.cs", "KeireCore/Include/Keire/Scripting/ScriptSystem.h"],
    "Scripting/SerializationAndInspector.md": ["KeireManaged/SerializationAttributes.cs", "KeireManaged/ManagedObjectSerializer.cs"],
    "Scripting/EntitiesComponentsAndTransforms.md": ["KeireManaged/Handles.cs", "KeireManaged/BuiltInComponents.cs"],
    "Scripting/AssetsAndScriptableObjects.md": ["KeireManaged/ScriptableObject.cs", "KeireManaged/ManagedAssetRuntime.cs"],
    "Scripting/GameplayServices.md": ["KeireManaged/RuntimeApi.cs", "KeireManaged/NativeRuntime.cs"],
    "Scripting/Audio.md": ["KeireManaged/RuntimeApi.cs", "KeireCore/Include/Keire/Audio"],
    "Scripting/Animation.md": ["KeireManaged/RuntimeApi.cs", "KeireCore/Include/Keire/Animation"],
    "Scripting/UiAndEvents.md": ["KeireManaged/RuntimeUi.cs", "KeireManaged/Events.cs"],
    "Scripting/AsyncReloadAndDiagnostics.md": ["KeireManaged/Jobs.cs", "KeireManaged/BehaviourSynchronizationContext.cs"],
    "Scripting/ApiIndex.md": ["KeireManaged", "KeireManaged.Tests"],
    "ManagedScripting.md": ["KeireCore/Include/Keire/Scripting/ScriptSystem.h", "KeireCore/Source/Scripting"],
    "Profiling.md": ["KeireCore/Include/Keire/Diagnostics/Profiler.h", "KeireManaged/Profiler.cs"],
    "PerformanceGates.md": ["Config/PerformanceGates.json", "Scripts/Performance/validate_capture.py"],
    "TestingAndRelease.md": ["Scripts/Tests", "Scripts/Windows/validate-production.ps1", "Scripts/Unix/validate-production.sh"],
    "PackageArchives.md": ["KeireHubRuntime/Include/KeireHubRuntime/PackageArchive.h", "KeireHubPackagePublisher/Source/Main.cpp"],
    "AssetPackages.md": ["KeireCore/Include/Keire/Assets/AssetPackage.h", "KeireCore/Include/Keire/Project/ProjectPackageManager.h"],
    "MarketplaceLaunch.md": ["supabase/migrations", "supabase/functions", "Services/KeireDistributionService/scripts/start-windows-host.ps1"],
    "ProductionReadinessReview.md": ["Config/PerformanceGates.json", "Docs/VfxParityManifest.json"],
    "Maintainability.md": ["Config/SourceFileBudgets.json", "Scripts/Tests/check-source-budgets.py"],
    "Diagnostics/README.md": ["KeireCore/Include/Keire/Diagnostics/Diagnostic.h", "KeireCore/Source/Diagnostics"],
    "Diagnostics/KEIRE-AUDIO-0001.md": ["KeireCore/Include/Keire/Diagnostics/Diagnostic.h", "KeireCore/Source/Audio"],
    "Diagnostics/KEIRE-EXAMPLE-0001.md": ["KeireCore/Include/Keire/Diagnostics/Diagnostic.h", "SourceModules"],
    "Diagnostics/KEIRE-REPLAY-0001.md": ["KeireCore/Include/Keire/Diagnostics/Diagnostic.h", "KeireCore/Source/Replay"],
    "Diagnostics/KEIRE-REPLAY-0002.md": ["KeireCore/Include/Keire/Diagnostics/Diagnostic.h", "KeireCore/Source/Replay"],
};
