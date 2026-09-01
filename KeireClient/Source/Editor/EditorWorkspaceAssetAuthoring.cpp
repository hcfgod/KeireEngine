#include "KeireClient/EditorWorkspaceLayer.h"

#include "Keire/Assets/BuiltinAssetRegistry.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AnimatorControllerPanel.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AssetPackageAuthoring.h"
#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/AudioMixerPanel.h"
#include "KeireClient/Editor/ConsolePanel.h"
#include "KeireClient/Editor/DiagnosticsPanel.h"
#include "KeireClient/Editor/EditModeVfxPreview.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/EditorDocumentWorkspaceCoordinator.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/ExternalEditorProfiles.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/PrefabAuthoring.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ScenePlayChanges.h"
#include "KeireClient/Editor/ScenePlayChangesPanel.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphPublication.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using KeireEditor::Detail::IsCSharpIdentifier;
    using KeireEditor::Detail::ReadBytes;
    using KeireEditor::Detail::TextBytes;
    using KeireEditor::Detail::WriteBytesAtomically;
} // namespace

void EditorWorkspaceLayer::CommitMaterialDraft()
{
    if (!m_MaterialDocument->Dirty() || !m_MaterialDocument->Asset() || m_MaterialDocument->SourcePath().empty())
        return;
    try
    {
        const auto asset = m_MaterialDocument->Asset();
        const auto path = m_MaterialDocument->SourcePath();
        const std::vector<std::byte> before(m_MaterialDocument->BaselineSource().begin(),
                                            m_MaterialDocument->BaselineSource().end());
        const std::vector<std::byte> after(m_MaterialDocument->DraftSource().begin(),
                                           m_MaterialDocument->DraftSource().end());
        const auto apply = [this, asset, path](const std::vector<std::byte>& source)
        {
            WriteBytesAtomically(path, source);
            const auto definition = Keire::MaterialAsset::DecodeSource(source);
            if (const auto assets = Owner().Assets())
                (void)assets->PublishDevelopmentAsset(asset, Keire::CreateRef<Keire::MaterialAsset>(definition));
            if (m_MaterialDocument->Asset() == asset)
                m_MaterialDocument->AcceptSavedSource(source);
            QueueMaterialCatalogRefresh(asset);
        };
        WriteBytesAtomically(path, after);
        if (const auto undo = m_AssetBrowserPanel->UndoContext(); undo && undo->IsOpen())
        {
            undo->RecordApplied(Keire::CreateUndoCommand(
                "Edit Material", [apply, after] { apply(after); }, [apply, before] { apply(before); },
                before.size() + after.size(), [path] { return std::filesystem::is_regular_file(path); }));
            m_ActiveUndoContext = undo;
        }
        m_MaterialDocument->AcceptSavedSource(after);
        m_InspectorPanel->AdvanceEditSerial();
        QueueMaterialCatalogRefresh(asset);
        m_AssetStatus = "Saved material properties; catalog persistence is running in the background.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Material save failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::CookAssets()
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return;
    try
    {
        Keire::AssetBuildProfile profile;
        profile.Name = "Dist";
        profile.Strict = true;
        const auto project = Owner().GetProject();
        if (project)
        {
            const auto buildScenes = Keire::EnabledPlayerBuildScenes(m_PlayerBuildScenes);
            if (buildScenes.empty())
                throw std::runtime_error("Enable at least one scene in Build Settings before cooking assets.");
            profile.Roots.insert(profile.Roots.end(), buildScenes.begin(), buildScenes.end());
            if (project->Descriptor().DefaultInput)
                profile.Roots.push_back(project->Descriptor().DefaultInput);
            const auto authoring = Keire::LoadProjectAuthoringSettings(project->Root());
            if (authoring.DefaultMixer)
                profile.Roots.push_back(authoring.DefaultMixer);
        }
        const bool containsManagedData =
            std::ranges::any_of(m_AssetDatabase->Records(), [](const Keire::AssetSourceRecord& record)
                                { return record.Type == Keire::ManagedDataAsset::StaticType(); });
        if (containsManagedData)
        {
            const auto scripts = Owner().Scripts();
            if (!scripts || scripts->ReloadStatus().State != Keire::ManagedReloadState::Active)
            {
                throw std::runtime_error("Build Scripts successfully before strict cooking managed data assets.");
            }
            const auto diagnostics = scripts->ManagedAssetTypeDiagnostics();
            if (!diagnostics.empty())
            {
                throw std::runtime_error("Managed type discovery contains an invalid ScriptableObject type: " +
                                         diagnostics.front().Message);
            }
            profile.ManagedTypeDiscoveryComplete = true;
            profile.ManagedTypeCatalog = Keire::EncodeManagedAssetTypeCatalog(scripts->ManagedAssetTypes());
        }
        const auto output =
            project ? project->Root() / "Build/CookedAssets/Dist" : std::filesystem::path("Build/CookedAssets/Dist");
        m_AssetOperations->QueueCook(std::move(profile), output);
        m_AssetStatus = "Asset cooking is running in the isolated worker.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Asset cook failed: ") + error.what());
    }
    catch (...)
    {
        SetAssetError("Asset cook failed with an unknown error.");
    }
}

bool EditorWorkspaceLayer::CreateInputActions(Keire::InputActionAssetDefinition definition,
                                              const std::string_view baseName, const bool requireExactName)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        auto destination = directory / (std::string(baseName) + ".keireinput");
        if (requireExactName && m_AssetDatabase->Find(destination))
            throw std::runtime_error("An Input Actions asset with that name already exists in this folder.");
        if (!requireExactName)
            for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
                destination = directory / (std::string(baseName) + " " + std::to_string(copy) + ".keireinput");
        definition.Name = destination.stem().string();
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::InputActionAsset::Encode(definition), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenInputActions, .UndoName = "Create Input Actions"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Input asset creation failed: ") + error.what());
        return false;
    }
}

void EditorWorkspaceLayer::GenerateManagedIdeWorkspace()
{
    if (!m_AssetDatabase || !Owner().GetProject())
        throw std::logic_error("Open a project before generating the script workspace.");
    const auto scripts = Owner().Scripts();
    if (!scripts || !scripts->IsOpen())
        throw std::runtime_error("The managed scripting service is unavailable.");

    Keire::ManagedBuildRequest request;
    const auto projectRoot = Owner().GetProject()->Root();
    for (const auto& record : m_AssetDatabase->Records())
    {
        if (record.Type != Keire::ManagedAssemblyAsset::StaticType())
            continue;
        const auto assembly =
            Keire::ManagedAssemblyAsset::Decode(ReadBytes(projectRoot / "Assets" / record.RelativePath));
        request.Assemblies.push_back({record.Id, assembly->Definition()});
    }
    if (request.Assemblies.empty())
        throw std::runtime_error("Create a managed assembly before opening a C# project.");
    const auto workspace = scripts->GenerateIdeWorkspace(request, projectRoot.filename().string());
    m_AssetStatus = "Generated Visual Studio workspace " + workspace.Solution.filename().string() + ".";
}

void EditorWorkspaceLayer::ExtendManagedAssemblySourceRoot(const Keire::AssetId assembly,
                                                           const std::filesystem::path& sourceRoot)
{
    if (!m_AssetDatabase || !Owner().GetProject())
        throw std::logic_error("Open a project before extending managed assembly source coverage.");
    const auto record = m_AssetDatabase->Find(assembly);
    if (!record || record->Type != Keire::ManagedAssemblyAsset::StaticType())
        throw std::runtime_error("The selected managed assembly is no longer available.");
    auto definition =
        Keire::ManagedAssemblyAsset::Decode(ReadBytes(Owner().GetProject()->Root() / "Assets" / record->RelativePath))
            ->Definition();
    if (!KeireEditor::ExtendManagedAssemblySourceRoots(definition, sourceRoot))
        return;
    m_AssetDatabase->ReplaceAssetSource(assembly, Keire::ManagedAssemblyAsset::Encode(definition));
    RefreshAssetBrowserRecords();
}

bool EditorWorkspaceLayer::CreateCSharpScript(const std::string_view name, const bool scriptableObject)
{
    if (!m_AssetDatabase || !m_AssetOperations || !Owner().GetProject())
        return false;
    try
    {
        if (!IsCSharpIdentifier(name))
            throw std::invalid_argument("C# script names must be valid type identifiers.");
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        std::vector<KeireEditor::ManagedScriptAssemblyCandidate> assemblies;
        const auto projectRoot = Owner().GetProject()->Root();
        for (const auto& record : m_AssetDatabase->Records())
        {
            if (record.Type != Keire::ManagedAssemblyAsset::StaticType())
                continue;
            const auto assembly =
                Keire::ManagedAssemblyAsset::Decode(ReadBytes(projectRoot / "Assets" / record.RelativePath));
            assemblies.push_back({record.Id, assembly->Definition()});
        }
        const auto placement = KeireEditor::ResolveManagedScriptPlacement(assemblies, directory);

        const auto destination = directory / (std::string(name) + ".cs");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A script with that name already exists in " + directory.generic_string() + ".");

        const auto templateKind = scriptableObject ? KeireEditor::ManagedScriptTemplateKind::ScriptableObject
                                                   : KeireEditor::ManagedScriptTemplateKind::Behaviour;
        const auto source = KeireEditor::BuildManagedScriptSource(templateKind, placement.RootNamespace, name,
                                                                  Keire::AssetId::Generate());
        m_AssetOperations->QueueCreateAsset(
            destination, TextBytes(source), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenExternal,
             .UndoName = scriptableObject ? "Create C# ScriptableObject Class" : "Create C# Script",
             .ManagedAssembly = placement.SourceRootToAdd.empty() ? Keire::AssetId{} : placement.Assembly,
             .ManagedSourceRoot = placement.SourceRootToAdd});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(
            std::string(scriptableObject ? "ScriptableObject class creation failed: " : "Script creation failed: ") +
            error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateAudioMixer(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Audio Mixer name must be one non-empty path component.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keiremixer");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("An Audio Mixer with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::AudioMixerAsset::Encode(Keire::AudioMixerAsset::DefaultDefinition()), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Audio Mixer"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Audio Mixer creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreatePhysicsMaterial(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Physics Material name must be one non-empty path component.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keirephysicsmaterial");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A Physics Material with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::PhysicsMaterialAsset::Encode({}), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Physics Material"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Physics Material creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateVfxEffect(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("VFX Effect name must be one non-empty path component.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keirevfx");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A VFX Effect with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::VfxEffectAsset::Encode(Keire::VfxEffectAsset::DefaultDefinition()), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create VFX Effect"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("VFX Effect creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateMaterialGraph(const std::string_view name, const Keire::AssetId shaderAsset)
{
    (void)shaderAsset;
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Material name must be one non-empty path component.");
        auto definition = Keire::CreateOpenPbrMaterial();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + std::string(Keire::MaterialAssetSourceExtension));
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A Material with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(destination, Keire::MaterialGraphAsset::EncodeSource(definition), {},
                                            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenMaterialGraph,
                                             .UndoName = "Create Material",
                                             .Reason = "material-creation"});
        m_AssetStatus = "Creating and opening " + destination.generic_string() + ".";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Material Graph creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateShaderGraph(const std::string_view name,
                                             const Keire::ShaderGraphTemplate graphTemplate)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Shader Graph name must be one non-empty path component.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keireshadergraph");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A Shader Graph with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::ShaderGraphAsset::EncodeSource(Keire::CreateShaderGraphTemplate(graphTemplate)), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenShaderGraph,
             .UndoName = "Create Shader Graph",
             .Reason = "shader-graph-creation"});
        m_AssetStatus = "Creating and opening " + destination.generic_string() + ".";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Shader Graph creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateReusableGraph(const std::string_view name, const Keire::ShaderGraphPurpose purpose)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Reusable graph name must be one non-empty path component.");

        if (purpose == Keire::ShaderGraphPurpose::Shader)
            throw std::invalid_argument("Use Shader Graph creation for shader assets.");
        const auto bytes = Keire::ShaderSubgraphAsset::Encode(Keire::CreateDefaultGraphFunction(purpose));

        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination =
            directory / (std::string(name) + std::string(Keire::ShaderSubgraphAssetSourceExtension));
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A reusable graph with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(
            destination, bytes, {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Reusable Graph"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Reusable graph creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateMaterialParameterCollection(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Material Parameter Collection name must be one non-empty path component.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination =
            directory / (std::string(name) + std::string(Keire::MaterialParameterCollectionAssetSourceExtension));
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A Material Parameter Collection with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(destination, Keire::MaterialParameterCollectionAsset::Encode({}), {},
                                            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal,
                                             .UndoName = "Create Material Parameter Collection"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Material Parameter Collection creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateMaterialInstance(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Material Instance name must be one non-empty path component.");
        const auto parent = m_AssetDatabase->Find(m_SelectedAsset);
        if (!parent || (parent->Type != Keire::MaterialAsset::StaticType() &&
                        parent->Type != Keire::MaterialGraphAsset::StaticType() &&
                        parent->Type != Keire::MaterialInstanceAsset::StaticType()))
            throw std::runtime_error("Select a Material, Material Graph, or Material Instance to use as the parent.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keirematerialinstance");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A Material Instance with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::MaterialInstanceAsset::EncodeSource({.Parent = parent->Id}), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Material Instance"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Material Instance creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreatePrefabFromSelection(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations || !m_SceneDocument)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto scene = m_SceneDocument->EditingScene();
        if (!scene)
            throw std::runtime_error("Open a scene before creating a prefab.");
        const auto selections = m_SceneDocument->Selections();
        if (selections.empty())
            throw std::runtime_error("Select one or more scene roots before creating a prefab.");
        const auto snapshot = scene->Snapshot();
        std::set<Keire::AssetId> selected;
        for (const auto selection : selections)
            selected.insert(selection);
        std::vector<Keire::AssetId> roots;
        for (const auto selection : selected)
        {
            const auto object = std::ranges::find(snapshot.Objects, selection, &Keire::SceneObjectDefinition::Id);
            if (object != snapshot.Objects.end() && (!object->Parent || !selected.contains(object->Parent)))
                roots.push_back(selection);
        }
        const auto definition = KeireEditor::CreatePrefabFromSelection(snapshot, roots, std::string(name));
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keireprefab");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A prefab with that name already exists in this folder.");
        (void)CreatePrefabAsset(destination, definition);
        m_AssetStatus = "Created prefab " + destination.generic_string() + " from the scene selection.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Prefab creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreatePrefabVariant(const Keire::AssetId basePrefab, const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto base = m_AssetDatabase->Find(basePrefab);
        if (!base || base->Type != Keire::PrefabAsset::StaticType())
            throw std::invalid_argument("Prefab variants require an available prefab base.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keireprefab");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A prefab with that name already exists in this folder.");
        const auto definition = KeireEditor::CreatePrefabVariant(basePrefab, std::string(name), {});
        (void)CreatePrefabAsset(destination, definition);
        m_AssetStatus = "Created prefab variant " + destination.generic_string() + ".";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Prefab variant creation failed: ") + error.what());
        return false;
    }
}

void EditorWorkspaceLayer::CreatePrefabFromObject(const Keire::AssetId object, const std::filesystem::path& folder)
{
    if (!m_AssetDatabase || !m_AssetOperations || !m_SceneDocument)
        throw std::logic_error("Prefab creation services are unavailable.");
    if (m_PrefabEditingStage)
        throw std::runtime_error("Create nested prefabs from a scene instance, not from Prefab Mode.");
    if (m_AssetOperations->Busy())
    {
        if (m_AssetOperations->PreemptBackgroundImports())
            m_MaterialDocument->ResetCatalogRefresh();
    }
    if (m_AssetOperations->Busy())
    {
        const auto duplicate = std::ranges::find_if(m_PendingPrefabCreations, [&](const PendingPrefabCreation& pending)
                                                    { return pending.Object == object && pending.Folder == folder; });
        if (duplicate == m_PendingPrefabCreations.end())
            m_PendingPrefabCreations.push_back({object, folder});
        m_AssetStatus = "Queued prefab creation until the active asset operation completes.";
        return;
    }
    const auto scene = m_SceneDocument->EditingScene();
    if (!scene)
        throw std::runtime_error("Open a scene or prefab stage before creating a prefab.");
    const auto snapshot = scene->Snapshot();
    const auto source = std::ranges::find(snapshot.Objects, object, &Keire::SceneObjectDefinition::Id);
    if (source == snapshot.Objects.end())
        throw std::invalid_argument("The dragged GameObject no longer exists.");

    std::string baseName = source->Name;
    for (char& character : baseName)
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-')
            character = '_';
    if (baseName.empty())
        baseName = "NewPrefab";
    std::filesystem::path destination;
    for (std::size_t suffix = 1;; ++suffix)
    {
        const auto name = suffix == 1 ? baseName : baseName + " " + std::to_string(suffix);
        destination = folder / (name + ".keireprefab");
        if (!m_AssetDatabase->Find(destination))
            break;
    }

    const std::array roots{object};
    const auto definition = KeireEditor::CreatePrefabFromSelection(snapshot, roots, destination.stem().string());
    auto replacement = snapshot;
    auto instance =
        KeireEditor::ConnectPrefabInstance(replacement, Keire::AssetId::Generate(), definition.Template, object);
    const auto created = CreatePrefabAsset(destination, definition);
    instance.Prefab = created;
    auto connected =
        std::ranges::find(replacement.PrefabInstances, instance.Root, &Keire::PrefabInstanceDefinition::Root);
    if (connected == replacement.PrefabInstances.end())
        throw std::logic_error("The newly connected prefab instance is unavailable.");
    connected->Prefab = created;
    Keire::SceneAsset::Validate(replacement);

    RecordSceneUndo("Connect Prefab Instance");
    auto rebuilt = Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
    rebuilt->MarkDirty();
    m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
    m_SceneDocument->Select(object);
    m_ActiveUndoContext = m_SceneDocument->History();
    m_PrefabOverrides.SetVisible(true);
    m_AssetStatus = "Created prefab " + destination.generic_string() + " and connected " + source->Name + ".";
}

Keire::AssetId EditorWorkspaceLayer::CreatePrefabAsset(const std::filesystem::path& destination,
                                                       const Keire::PrefabDefinition& definition)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        throw std::logic_error("Prefab creation services are unavailable.");
    if (m_AssetOperations->Busy())
        (void)m_AssetOperations->PreemptBackgroundImports();

    const auto source = Keire::PrefabAsset::Encode(definition);
    const auto created =
        m_AssetDatabase->CreateAsset(destination, Keire::CreatePrefabAssetImporter(), std::span(source));
    if (const auto assets = Owner().Assets())
        (void)assets->PublishDevelopmentAsset(created, Keire::CreateRef<Keire::PrefabAsset>(definition));
    RefreshAssetBrowserRecords();
    m_SelectedAsset = created;
    if (m_AssetBrowserPanel)
    {
        m_AssetBrowserPanel->InvalidateThumbnail(created);
        m_AssetBrowserPanel->RevealAsset(created);
        const auto undo = m_AssetBrowserPanel->UndoContext();
        if (undo && undo->IsOpen())
        {
            auto state = std::make_shared<KeireEditor::AssetMutationUndoState>();
            state->Reverse = {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashAsset, .Asset = created};
            state->Name = "Create Prefab";
            state->RecordCommand = false;
            undo->RecordApplied(Keire::CreateUndoCommand(
                state->Name, [this, state] { QueueAssetMutation(state, KeireEditor::AssetMutationPhase::Redo); },
                [this, state] { QueueAssetMutation(state, KeireEditor::AssetMutationPhase::Undo); }, sizeof(*state),
                [this] { return static_cast<bool>(m_AssetOperations); }));
            m_ActiveUndoContext = undo;
        }
    }
    return created;
}

void EditorWorkspaceLayer::ReplacePrefabSource(const Keire::AssetId asset, const Keire::PrefabDefinition& definition)
{
    if (!m_AssetDatabase)
        throw std::logic_error("The project asset database is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::PrefabAsset::StaticType())
        throw std::invalid_argument("The prefab source no longer exists in the project.");
    m_AssetDatabase->ReplaceAssetSource(asset, Keire::PrefabAsset::Encode(definition));
    RefreshAssetBrowserRecords();
    if (m_AssetBrowserPanel)
        m_AssetBrowserPanel->InvalidateThumbnail(asset);
}

void EditorWorkspaceLayer::OpenPrefabForEditing(const Keire::AssetId asset)
{
    if (!m_AssetDatabase || !Owner().GetProject())
        throw std::logic_error("Open a project before editing a prefab.");
    if (m_PrefabEditingStage)
    {
        if (m_PrefabEditingStage->Asset == asset)
            return;
        throw std::runtime_error("Save or discard the active prefab stage before opening another prefab.");
    }
    if (m_SceneDocument->PlaySession())
        throw std::runtime_error("Exit Play mode before opening a prefab stage.");

    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::PrefabAsset::StaticType())
        throw std::invalid_argument("The selected asset is not an available prefab source.");
    const auto projectRoot = Owner().GetProject()->Root();
    const auto source = Keire::PrefabAsset::Decode(ReadBytes(projectRoot / "Assets" / record->RelativePath));
    const auto resolver = [&](const Keire::AssetId requested)
    {
        const auto dependency = m_AssetDatabase->Find(requested);
        if (!dependency || dependency->Type != Keire::PrefabAsset::StaticType())
            return Keire::Ref<Keire::PrefabAsset>{};
        return Keire::PrefabAsset::Decode(ReadBytes(projectRoot / "Assets" / dependency->RelativePath));
    };
    const auto composed = Keire::ComposePrefab(asset, resolver);
    auto editingScene = Keire::CreateRef<Keire::Scene>(asset, composed, Owner().Scenes()->Components());
    editingScene->MarkSaved();
    Keire::Ref<Keire::UndoContext> history;
    if (const auto undo = Owner().Undo())
        history = undo->CreateContext({.Name = "Prefab: " + record->RelativePath.stem().string()});
    auto document = std::make_unique<KeireEditor::SceneDocument>();
    document->Open(std::move(editingScene), asset, {}, std::move(history));
    document->SetStatus("Prefab editing stage: " + record->RelativePath.generic_string());

    m_PrefabReturnDocument = m_DocumentCoordinator->ReplaceScene(std::move(document));
    m_SceneDocument = &m_DocumentCoordinator->Scene();
    m_PrefabEditingStage = PrefabEditingStage{asset, source->Definition(), composed, record->RelativePath};
    if (const auto root = std::ranges::find(composed.Objects, Keire::AssetId{}, &Keire::SceneObjectDefinition::Parent);
        root != composed.Objects.end())
        m_SceneDocument->Select(root->Id);
    m_PrefabOverrides.SetVisible(true);
    m_ActiveUndoContext = m_SceneDocument->History();
    m_SelectedAsset = {};
}

void EditorWorkspaceLayer::SavePrefabEditingStage()
{
    if (!m_PrefabEditingStage || !m_AssetDatabase || !Owner().GetProject())
        throw std::logic_error("No prefab editing stage is active.");
    if (m_AssetOperations && m_AssetOperations->Busy())
        throw std::runtime_error("Wait for the active asset operation before saving the prefab.");
    const auto scene = m_SceneDocument->EditingScene();
    if (!scene)
        throw std::logic_error("The prefab editing scene is unavailable.");

    const auto projectRoot = Owner().GetProject()->Root();
    const auto resolver = [&](const Keire::AssetId requested)
    {
        const auto dependency = m_AssetDatabase->Find(requested);
        if (!dependency || dependency->Type != Keire::PrefabAsset::StaticType())
            return Keire::Ref<Keire::PrefabAsset>{};
        return Keire::PrefabAsset::Decode(ReadBytes(projectRoot / "Assets" / dependency->RelativePath));
    };
    std::optional<Keire::SceneDefinition> composedBase;
    if (m_PrefabEditingStage->Source.BasePrefab)
        composedBase = Keire::ComposePrefab(m_PrefabEditingStage->Source.BasePrefab, resolver);
    const auto edited = scene->Snapshot();
    const auto updated = KeireEditor::UpdatePrefabFromEditingScene(m_PrefabEditingStage->Source, edited,
                                                                   composedBase ? &*composedBase : nullptr);
    ReplacePrefabSource(m_PrefabEditingStage->Asset, updated);
    m_PrefabEditingStage->Source = updated;
    m_PrefabEditingStage->Baseline = edited;
    scene->MarkSaved();
    m_SceneDocument->SetStatus("Saved prefab " + m_PrefabEditingStage->RelativePath.generic_string() + ".");
    ImportAssets();
}

void EditorWorkspaceLayer::ClosePrefabEditingStage()
{
    if (!m_PrefabEditingStage)
        return;
    m_SceneDocument->Close();
    auto closedStage = m_DocumentCoordinator->ReplaceScene(std::move(m_PrefabReturnDocument));
    m_SceneDocument = &m_DocumentCoordinator->Scene();
    closedStage.reset();
    m_PrefabEditingStage.reset();
    m_ActiveUndoContext = m_SceneDocument ? m_SceneDocument->History() : Keire::Ref<Keire::UndoContext>{};
    m_SelectedAsset = {};
}

void EditorWorkspaceLayer::ApplySelectedPrefabOverrides()
{
    if (m_PrefabEditingStage)
        throw std::runtime_error("Apply to Source is available from a scene instance, not inside prefab mode.");
    if (!m_AssetDatabase || !Owner().GetProject())
        throw std::logic_error("Open a project before applying prefab overrides.");
    if (m_AssetOperations && m_AssetOperations->Busy())
        throw std::runtime_error("Wait for the active asset operation before applying prefab overrides.");
    const auto scene = m_SceneDocument->EditingScene();
    if (!scene)
        throw std::runtime_error("Open a scene before applying prefab overrides.");
    const auto root = m_SceneDocument->Selection();
    const auto beforeScene = scene->Snapshot();
    const auto instance = std::ranges::find(beforeScene.PrefabInstances, root, &Keire::PrefabInstanceDefinition::Root);
    if (instance == beforeScene.PrefabInstances.end())
        throw std::runtime_error("Select a prefab instance root before applying overrides.");

    const auto record = m_AssetDatabase->Find(instance->Prefab);
    if (!record || record->Type != Keire::PrefabAsset::StaticType())
        throw std::runtime_error("The source prefab is unavailable.");
    const auto projectRoot = Owner().GetProject()->Root();
    const auto source =
        Keire::PrefabAsset::Decode(ReadBytes(projectRoot / "Assets" / record->RelativePath))->Definition();
    const auto resolver = [&](const Keire::AssetId requested)
    {
        const auto dependency = m_AssetDatabase->Find(requested);
        if (!dependency || dependency->Type != Keire::PrefabAsset::StaticType())
            return Keire::Ref<Keire::PrefabAsset>{};
        return Keire::PrefabAsset::Decode(ReadBytes(projectRoot / "Assets" / dependency->RelativePath));
    };
    const auto composed = Keire::ComposePrefab(instance->Prefab, resolver);
    std::optional<Keire::SceneDefinition> composedBase;
    if (source.BasePrefab)
        composedBase = Keire::ComposePrefab(source.BasePrefab, resolver);
    auto update = KeireEditor::ApplyPrefabInstanceToSource(beforeScene, root, source, composed,
                                                           composedBase ? &*composedBase : nullptr);
    const auto afterScene = update.Scene;
    const auto afterPrefab = update.Prefab;
    const auto prefab = instance->Prefab;
    const auto components = scene->Components();

    try
    {
        ReplacePrefabSource(prefab, afterPrefab);
        auto replacement = Keire::CreateRef<Keire::Scene>(scene->Asset(), afterScene, components);
        replacement->MarkDirty();
        m_SceneDocument->ReplaceEditingScene(std::move(replacement));
        m_SceneDocument->Select(root);
    }
    catch (...)
    {
        try
        {
            ReplacePrefabSource(prefab, source);
        }
        catch (...)
        {
        }
        throw;
    }

    if (const auto history = m_SceneDocument->History(); history && history->IsOpen())
    {
        const auto assign = [this, prefab, root, components](const Keire::PrefabDefinition& prefabSource,
                                                             const Keire::SceneDefinition& sceneSource)
        {
            ReplacePrefabSource(prefab, prefabSource);
            const auto current = m_SceneDocument->EditingScene();
            if (!current)
                throw std::runtime_error("The scene closed before prefab apply undo completed.");
            auto replacement = Keire::CreateRef<Keire::Scene>(current->Asset(), sceneSource, components);
            replacement->MarkDirty();
            m_SceneDocument->ReplaceEditingScene(std::move(replacement));
            m_SceneDocument->Select(root);
            ImportAssets();
        };
        history->RecordApplied(Keire::CreateUndoCommand(
            "Apply Prefab Overrides", [assign, afterPrefab, afterScene] { assign(afterPrefab, afterScene); },
            [assign, source, beforeScene] { assign(source, beforeScene); },
            Keire::PrefabAsset::Encode(source).size() + Keire::PrefabAsset::Encode(afterPrefab).size(),
            [this]
            {
                return !m_PrefabEditingStage && m_SceneDocument && m_SceneDocument->EditingScene() &&
                       (!m_AssetOperations || !m_AssetOperations->Busy());
            }));
    }
    m_SceneDocument->SetStatus("Applied prefab instance changes to " + record->RelativePath.generic_string() + ".");
    ImportAssets();
}

bool EditorWorkspaceLayer::CreateUnlitShader(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    std::filesystem::path manifest;
    std::filesystem::path hlsl;
    try
    {
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        if (!name.empty() && (name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos))
            throw std::invalid_argument("Shader name must be one non-empty path component.");
        std::string baseName = name.empty() ? "UnlitShader" : std::string(name);
        for (std::size_t copy = 2;; ++copy)
        {
            manifest = directory / (baseName + ".keireshader");
            hlsl = directory / (baseName + ".hlsl");
            if (!m_AssetDatabase->Find(manifest) &&
                !std::filesystem::exists(m_AssetDatabase->Specification().ProjectRoot / "Assets" / hlsl))
                break;
            if (!name.empty())
                throw std::runtime_error("A shader or HLSL source with that name already exists in this folder.");
            baseName = "UnlitShader " + std::to_string(copy);
        }

        const std::string shaderSource = R"(struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Color : TEXCOORD1;
};

struct VertexOutput
{
    float4 Color : TEXCOORD0;
    float4 Position : SV_Position;
};

cbuffer CameraObjectConstants : register(b0, space1)
{
    float4x4 ModelViewProjection;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.Color = float4(input.Color, 1.0F);
    output.Position = mul(ModelViewProjection, float4(input.Position, 1.0F));
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return input.Color;
}
)";
        const auto projectSource = (std::filesystem::path("Assets") / hlsl).generic_string();
        const auto includeRoot =
            (std::filesystem::path("Assets") / (directory.empty() ? std::filesystem::path{} : directory))
                .generic_string();
        const std::string manifestSource =
            "{\n  \"schemaVersion\": 1,\n  \"source\": \"" + projectSource +
            "\",\n  \"stages\": { \"vertex\": \"VSMain\", \"fragment\": \"PSMain\" },\n"
            "  \"defines\": {},\n  \"includeRoots\": [\"" +
            includeRoot +
            "\"],\n"
            "  \"renderState\": { \"topology\": \"TriangleList\", \"culling\": \"Back\", "
            "\"depthTest\": true, \"depthWrite\": true, \"blend\": false },\n"
            "  \"properties\": [{ \"name\": \"Tint\", \"type\": \"Color\", "
            "\"default\": [0.25, 0.55, 1.0, 1.0] }]\n}\n";
        const auto manifestBytes = std::as_bytes(std::span(manifestSource));
        const auto shaderBytes = std::as_bytes(std::span(shaderSource));
        std::vector<KeireEditor::AssetCreationAuxiliarySource> auxiliary;
        auxiliary.push_back({hlsl, std::vector<std::byte>(shaderBytes.begin(), shaderBytes.end())});
        m_AssetOperations->QueueCreateAssetWithAuxiliary(
            manifest, std::vector<std::byte>(manifestBytes.begin(), manifestBytes.end()), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal}, std::move(auxiliary));
        m_AssetStatus = "Creating and compiling " + manifest.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Shader creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateMaterial(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Material name must be one non-empty path component.");
        const auto destination = directory / (std::string(name) + std::string(Keire::MaterialAssetSourceExtension));
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A material with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(destination,
                                            Keire::MaterialGraphAsset::EncodeSource(Keire::CreateOpenPbrMaterial()), {},
                                            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenMaterialGraph,
                                             .UndoName = "Create Material",
                                             .Reason = "material-creation"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Material creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateAnimationGraph(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Animator Controller name must be one non-empty path component.");
        const auto destination = directory / (std::string(name) + ".keireanimgraph");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("An Animator Controller with that name already exists in this folder.");
        Keire::AnimationGraphDefinition definition;
        definition.SchemaVersion = 2;
        const auto source = Keire::AnimationGraphAsset::Encode(definition);
        m_AssetOperations->QueueCreateAsset(
            destination, source, {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Animator Controller"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Animator Controller creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateProceduralMotionProfile(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Procedural Motion Profile name must be one non-empty path component.");
        const auto destination = directory / (std::string(name) + ".keiremotionprofile");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A Procedural Motion Profile with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::ProceduralMotionProfileAsset::Encode(Keire::ProceduralMotionProfile::GroundedArmored()),
            {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Procedural Motion Profile"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Procedural Motion Profile creation failed: ") + error.what());
        return false;
    }
}

KeireEditor::AnimatorControllerDocument& EditorWorkspaceLayer::AnimatorControllerState() noexcept
{
    return *m_AnimatorControllerDocument;
}

const Keire::UiThemeDefinition& EditorWorkspaceLayer::AnimatorControllerTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::AnimatorControllerDatabase() const noexcept
{
    return m_AssetDatabase;
}

Keire::Ref<Keire::AssetSystem> EditorWorkspaceLayer::AnimatorControllerAssets() const noexcept
{
    return Owner().Assets();
}

KeireEditor::SceneDocument& EditorWorkspaceLayer::AnimatorControllerSceneDocument() noexcept
{
    return *m_SceneDocument;
}

void EditorWorkspaceLayer::ActivateAnimatorControllerHistory() noexcept
{
    m_ActiveUndoContext = m_AnimatorControllerDocument->UndoContext();
}

void EditorWorkspaceLayer::SaveAnimatorControllerDocument() { SaveAnimationGraph(); }

void EditorWorkspaceLayer::ReloadAnimatorControllerDocument(const Keire::AssetId asset) { OpenAnimationGraph(asset); }

void EditorWorkspaceLayer::UndoAnimatorControllerEdit() { (void)m_AnimatorControllerDocument->Undo(); }

void EditorWorkspaceLayer::RedoAnimatorControllerEdit() { (void)m_AnimatorControllerDocument->Redo(); }

void EditorWorkspaceLayer::ReportAnimatorControllerError(std::string message) noexcept
{
    SetAssetError(std::move(message));
}
