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
#include "KeireClient/Editor/EditorManagedRuntimeCoordinator.h"
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
    using KeireEditor::Detail::FormatAssetDiagnostic;
    using KeireEditor::Detail::IsCSharpIdentifier;
    using KeireEditor::Detail::ReadBytes;
    using KeireEditor::Detail::RequireCompiledVfxSystems;
    using KeireEditor::Detail::SameOrChild;
    using KeireEditor::Detail::TextBytes;
    using KeireEditor::Detail::WriteBytesAtomically;

    [[nodiscard]] bool IsImmediateAssetMutation(const Keire::Detail::AssetWorkerMutationKind kind) noexcept
    {
        return kind == Keire::Detail::AssetWorkerMutationKind::CreateFolder ||
               kind == Keire::Detail::AssetWorkerMutationKind::MoveAsset ||
               kind == Keire::Detail::AssetWorkerMutationKind::MoveFolder ||
               kind == Keire::Detail::AssetWorkerMutationKind::TrashAsset ||
               kind == Keire::Detail::AssetWorkerMutationKind::TrashFolder ||
               kind == Keire::Detail::AssetWorkerMutationKind::RestoreTrash ||
               kind == Keire::Detail::AssetWorkerMutationKind::PermanentlyDeleteTrash;
    }

} // namespace

void EditorWorkspaceLayer::ConfigureAssetImporters(Keire::AssetDatabaseSpecification& specification) const
{
    specification.Importers = Keire::CreateBuiltinAssetImporters();
    if (const auto modules = Owner().Modules())
        for (auto& importer : modules->Importers())
            specification.Importers.push_back(std::move(importer));
}

bool EditorWorkspaceLayer::FileIsNewerThan(const std::filesystem::path& path,
                                           const std::filesystem::file_time_type reference) noexcept
{
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    return error || modified > reference;
}

const Keire::UiThemeDefinition& EditorWorkspaceLayer::AssetBrowserTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::AssetBrowserDatabase() const noexcept { return m_AssetDatabase; }

Keire::Ref<Keire::AssetSystem> EditorWorkspaceLayer::AssetBrowserAssets() const noexcept { return Owner().Assets(); }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::AssetBrowserRecords() const noexcept
{
    return m_AssetRecords;
}

std::uint64_t EditorWorkspaceLayer::AssetBrowserRecordRevision() const noexcept { return m_AssetRecordRevision; }

std::string_view EditorWorkspaceLayer::AssetBrowserStatus() const noexcept { return m_AssetStatus; }

Keire::AssetId EditorWorkspaceLayer::AssetBrowserSceneAsset() const noexcept { return m_SceneDocument->Asset(); }

bool EditorWorkspaceLayer::AssetBrowserSceneDirty() const noexcept { return m_SceneDocument->Dirty(); }

std::vector<Keire::ManagedAssetTypeDescriptor> EditorWorkspaceLayer::AssetBrowserManagedAssetTypes() const
{
    const auto scripts = Owner().Scripts();
    return scripts && scripts->RuntimeHostAvailable() ? scripts->ManagedAssetTypes()
                                                      : std::vector<Keire::ManagedAssetTypeDescriptor>{};
}

std::vector<Keire::ManagedAssetTypeDiagnostic> EditorWorkspaceLayer::AssetBrowserManagedAssetTypeDiagnostics() const
{
    const auto scripts = Owner().Scripts();
    return scripts && scripts->RuntimeHostAvailable() ? scripts->ManagedAssetTypeDiagnostics()
                                                      : std::vector<Keire::ManagedAssetTypeDiagnostic>{};
}

std::filesystem::path EditorWorkspaceLayer::AssetBrowserExternalEditor() const
{
    if (!m_ProjectSettingsDocument)
        return {};
    const auto& settings = m_ProjectSettingsDocument->AuthoringSettings();
    if (settings.ExternalEditorId == "custom")
        return settings.ExternalEditorExecutable;
    if (settings.ExternalEditorId == "system")
        return {};
    const auto profiles = KeireEditor::DiscoverExternalEditorProfiles();
    const auto selected =
        std::ranges::find(profiles, settings.ExternalEditorId, &KeireEditor::ExternalEditorProfile::Id);
    return selected != profiles.end() && selected->Installed ? selected->Executable : std::filesystem::path{};
}

void EditorWorkspaceLayer::ConfigureAssetBrowserExternalEditor()
{
    if (m_ProjectSettingsPanel)
        m_ProjectSettingsPanel->Registration().SetVisible(true);
}

void EditorWorkspaceLayer::RefreshAssetBrowserRecords()
{
    if (m_AssetDatabase)
    {
        m_AssetRecords = m_AssetDatabase->Records();
        const auto& specification = m_AssetDatabase->Specification();
        const auto sourceRoot =
            Keire::Detail::ResolveConfinedPath(specification.ProjectRoot, specification.SourceDirectory);
        const auto managedData =
            m_ManagedDataTypeCache.Refresh(m_AssetRecords, sourceRoot, specification.MaximumSourceBytes);
        for (const auto& diagnostic : managedData.Diagnostics)
            AddConsoleMessage("Assets", diagnostic, m_Theme.Warning, Keire::LogLevel::Warn);
        ++m_AssetRecordRevision;
        if (m_SceneDocument && m_SceneDocument->Asset())
        {
            const auto scene = m_SceneDocument->Asset();
            if (const auto record = m_AssetDatabase->Find(scene))
            {
                const auto source = Keire::Detail::ResolveConfinedPath(sourceRoot, record->RelativePath);
                if (m_SceneDocument->Source() != source)
                    m_SceneDocument->SetIdentity(scene, source);
            }
        }
    }
}

void EditorWorkspaceLayer::SetAssetBrowserSelected(const Keire::AssetId asset) noexcept { m_SelectedAsset = asset; }

void EditorWorkspaceLayer::ClearAssetBrowserSceneSelection() noexcept { m_SceneDocument->ClearSelection(); }

void EditorWorkspaceLayer::SetAssetBrowserStatus(std::string status) noexcept { m_AssetStatus = std::move(status); }

void EditorWorkspaceLayer::ReportAssetBrowserError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::ImportAssetBrowserAssets(const std::span<const Keire::AssetId> assets)
{
    if (assets.empty())
    {
        ImportAssets();
        return;
    }

    for (const auto asset : assets)
    {
        m_AssetOperations->QueueAssetImport(asset, KeireEditor::AssetOperationPriority::ExplicitAction,
                                            {.ReloadAsset = assets.size() == 1 ? asset : Keire::AssetId{}});
    }
    m_AssetStatus = assets.size() == 1 ? "Reimporting 1 asset..."
                                       : "Reimporting " + std::to_string(assets.size()) + " selected assets...";
}

bool EditorWorkspaceLayer::CreateAssetBrowserScene(const std::string_view name) { return CreateSceneAsset(name); }

bool EditorWorkspaceLayer::CreateAssetBrowserMaterial(const std::string_view name) { return CreateMaterial(name); }

bool EditorWorkspaceLayer::CreateAssetBrowserAnimationGraph(const std::string_view name)
{
    return CreateAnimationGraph(name);
}

bool EditorWorkspaceLayer::CreateAssetBrowserProceduralMotionProfile(const std::string_view name)
{
    return CreateProceduralMotionProfile(name);
}

bool EditorWorkspaceLayer::CreateAssetBrowserScript(const std::string_view name) { return CreateCSharpScript(name); }

bool EditorWorkspaceLayer::CreateAssetBrowserScriptableObjectScript(const std::string_view name)
{
    return CreateCSharpScript(name, true);
}

bool EditorWorkspaceLayer::CreateAssetBrowserManagedAssembly(const std::string_view name)
{
    return CreateManagedAssembly(name);
}

bool EditorWorkspaceLayer::CreateAssetBrowserManagedData(const Keire::ManagedTypeId type, const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations || !type)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Managed data asset name must be one non-empty path component.");
        const auto scripts = Owner().Scripts();
        if (!scripts || !scripts->RuntimeHostAvailable())
            throw std::runtime_error("Build scripts before creating a managed data asset.");
        const auto descriptors = scripts->ManagedAssetTypes();
        const auto descriptor = std::ranges::find(descriptors, type, &Keire::ManagedAssetTypeDescriptor::StableTypeId);
        if (descriptor == descriptors.end() || descriptor->MenuPath.empty())
            throw std::runtime_error("The selected managed data type is no longer authorable.");

        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keiredata");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A managed data asset with that name already exists in this folder.");
        Keire::ManagedDataDefinition definition;
        definition.ManagedType = descriptor->StableTypeId;
        definition.ManagedTypeName = descriptor->FullName;
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::ManagedDataAsset::Encode(definition), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Managed Data"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Managed data creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateAssetBrowserAudioMixer(const std::string_view name) { return CreateAudioMixer(name); }

bool EditorWorkspaceLayer::CreateAssetBrowserPhysicsMaterial(const std::string_view name)
{
    return CreatePhysicsMaterial(name);
}

bool EditorWorkspaceLayer::CreateAssetBrowserVfxEffect(const std::string_view name) { return CreateVfxEffect(name); }

bool EditorWorkspaceLayer::CreateAssetBrowserMaterialGraph(const std::string_view name, const Keire::AssetId shader)
{
    return CreateMaterialGraph(name, shader);
}

bool EditorWorkspaceLayer::CreateAssetBrowserShaderGraph(const std::string_view name,
                                                         const Keire::ShaderGraphTemplate graphTemplate)
{
    return CreateShaderGraph(name, graphTemplate);
}

bool EditorWorkspaceLayer::CreateAssetBrowserReusableGraph(const std::string_view name,
                                                           const Keire::ShaderGraphPurpose purpose)
{
    return CreateReusableGraph(name, purpose);
}

bool EditorWorkspaceLayer::CreateAssetBrowserMaterialParameterCollection(const std::string_view name)
{
    return CreateMaterialParameterCollection(name);
}

bool EditorWorkspaceLayer::CreateAssetBrowserMaterialInstance(const std::string_view name)
{
    return CreateMaterialInstance(name);
}

bool EditorWorkspaceLayer::CreateAssetBrowserPrefab(const std::string_view name)
{
    return CreatePrefabFromSelection(name);
}

bool EditorWorkspaceLayer::CreateAssetBrowserPrefabVariant(const Keire::AssetId basePrefab, const std::string_view name)
{
    return CreatePrefabVariant(basePrefab, name);
}

void EditorWorkspaceLayer::CreateAssetBrowserPrefabFromObject(const Keire::AssetId object,
                                                              const std::filesystem::path& folder)
{
    CreatePrefabFromObject(object, folder);
}

bool EditorWorkspaceLayer::CreateAssetBrowserShader(const std::string_view name) { return CreateUnlitShader(name); }

bool EditorWorkspaceLayer::CreateAssetBrowserInputActions(Keire::InputActionAssetDefinition definition,
                                                          const std::string_view baseName)
{
    return CreateInputActions(std::move(definition), baseName, true);
}

void EditorWorkspaceLayer::MutateAssetBrowser(Keire::Detail::AssetWorkerMutation mutation,
                                              Keire::Detail::AssetWorkerMutation reverse, std::string name,
                                              const bool revealResult)
{
    auto state = std::make_shared<KeireEditor::AssetMutationUndoState>();
    state->Forward = std::move(mutation);
    state->Reverse = std::move(reverse);
    state->Name = std::move(name);
    state->RecordCommand = !state->Name.empty();
    state->RevealResult = revealResult;
    QueueAssetMutation(std::move(state), KeireEditor::AssetMutationPhase::Initial);
}

void EditorWorkspaceLayer::QueueAssetMutation(std::shared_ptr<KeireEditor::AssetMutationUndoState> state,
                                              const KeireEditor::AssetMutationPhase phase)
{
    if (!m_AssetOperations || !m_AssetDatabase)
        throw std::logic_error("The isolated asset worker is unavailable.");
    const auto& mutation = phase == KeireEditor::AssetMutationPhase::Undo ? state->Reverse : state->Forward;
    if (IsImmediateAssetMutation(mutation.Kind))
    {
        if (!m_ExecutingQueuedAssetMutation)
        {
            m_PendingAssetMutations.push_back({std::move(state), phase});
            m_AssetStatus = "Asset mutation queued for the next editor safe boundary.";
            return;
        }
        const auto activeScene = m_SceneDocument ? m_SceneDocument->Asset() : Keire::AssetId{};
        const bool renamesActiveScene = mutation.Kind == Keire::Detail::AssetWorkerMutationKind::MoveAsset &&
                                        activeScene && mutation.Asset == activeScene;
        const bool activeSceneWasDirty = renamesActiveScene && m_SceneDocument->Dirty();
        if (m_AssetOperations->Busy())
        {
            if (m_AssetOperations->PreemptBackgroundImports())
                m_MaterialDocument->ResetCatalogRefresh();
        }
        if (m_AssetOperations->Busy())
        {
            const auto duplicate =
                std::ranges::find_if(m_PendingAssetMutations, [&](const PendingAssetMutation& pending)
                                     { return pending.State == state && pending.Phase == phase; });
            if (duplicate == m_PendingAssetMutations.end())
                m_PendingAssetMutations.push_back({std::move(state), phase});
            m_AssetStatus = "Queued asset mutation until the active asset operation completes.";
            return;
        }

        std::optional<Keire::AssetTrashRecord> trashed;
        switch (mutation.Kind)
        {
        case Keire::Detail::AssetWorkerMutationKind::CreateFolder:
            m_AssetDatabase->CreateFolder(mutation.Destination);
            break;
        case Keire::Detail::AssetWorkerMutationKind::MoveAsset:
            m_AssetDatabase->MoveAsset(mutation.Asset, mutation.Destination);
            break;
        case Keire::Detail::AssetWorkerMutationKind::MoveFolder:
            m_AssetDatabase->MoveFolder(mutation.Source, mutation.Destination);
            break;
        case Keire::Detail::AssetWorkerMutationKind::TrashAsset:
            trashed = m_AssetDatabase->TrashAsset(mutation.Asset);
            break;
        case Keire::Detail::AssetWorkerMutationKind::TrashFolder:
            trashed = m_AssetDatabase->TrashFolder(mutation.Source);
            break;
        case Keire::Detail::AssetWorkerMutationKind::RestoreTrash:
            m_AssetDatabase->RestoreTrash(mutation.Trash);
            break;
        case Keire::Detail::AssetWorkerMutationKind::PermanentlyDeleteTrash:
            m_AssetDatabase->PermanentlyDeleteTrash(mutation.Trash);
            break;
        default:
            throw std::logic_error("Immediate asset mutation routing received an unsupported operation.");
        }

        if (trashed)
        {
            Keire::Detail::AssetWorkerMutation restore{.Kind = Keire::Detail::AssetWorkerMutationKind::RestoreTrash,
                                                       .Trash = trashed->Id};
            if (phase == KeireEditor::AssetMutationPhase::Undo)
                state->Forward = std::move(restore);
            else
                state->Reverse = std::move(restore);
        }
        RefreshAssetBrowserRecords();
        if (renamesActiveScene)
        {
            const auto record = m_AssetDatabase->Find(activeScene);
            const auto editingScene = m_SceneDocument->EditingScene();
            if (record && editingScene && record->Type == Keire::SceneAsset::StaticType())
            {
                const auto expectedName = record->RelativePath.stem().string();
                if (!expectedName.empty() && editingScene->Name() != expectedName)
                {
                    editingScene->SetName(expectedName);
                    if (!activeSceneWasDirty)
                    {
                        m_AssetDatabase->ReplaceAssetSource(activeScene,
                                                            Keire::SceneAsset::Encode(editingScene->Snapshot()));
                        editingScene->MarkSaved();
                    }
                }
            }
        }
        if (m_SelectedAsset && !m_AssetDatabase->Find(m_SelectedAsset))
            m_SelectedAsset = {};

        if (state->RecordCommand && phase == KeireEditor::AssetMutationPhase::Initial)
        {
            const auto undo = m_AssetBrowserPanel ? m_AssetBrowserPanel->UndoContext() : nullptr;
            if (undo && undo->IsOpen())
            {
                undo->RecordApplied(Keire::CreateUndoCommand(
                    state->Name, [this, state] { QueueAssetMutation(state, KeireEditor::AssetMutationPhase::Redo); },
                    [this, state] { QueueAssetMutation(state, KeireEditor::AssetMutationPhase::Undo); }, sizeof(*state),
                    [this] { return static_cast<bool>(m_AssetOperations); }));
                m_ActiveUndoContext = undo;
            }
            state->RecordCommand = false;
        }
        m_AssetStatus = "Asset mutation completed.";
        return;
    }

    KeireEditor::AssetOperationContext context;
    context.MutationUndo = std::move(state);
    context.MutationPhase = phase;
    if (context.MutationUndo->RevealResult && phase != KeireEditor::AssetMutationPhase::Undo)
        context.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal;
    m_AssetOperations->QueueMutation(mutation, std::move(context));
}

void EditorWorkspaceLayer::OpenAssetBrowserInputActions(const Keire::AssetId asset) { OpenInputActions(asset); }

void EditorWorkspaceLayer::OpenAssetBrowserAnimationGraph(const Keire::AssetId asset) { OpenAnimationGraph(asset); }

void EditorWorkspaceLayer::OpenAssetBrowserAudioMixer(const Keire::AssetId asset) { OpenAudioMixer(asset); }

void EditorWorkspaceLayer::OpenAssetBrowserVfxEffect(const Keire::AssetId asset) { OpenVfxEffect(asset); }

void EditorWorkspaceLayer::OpenAssetBrowserMaterial(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::MaterialAsset::StaticType() ||
        record->RelativePath.extension() != ".keirematerial")
        throw std::invalid_argument("Only .keirematerial assets can be opened in the Material Inspector.");
    m_SelectedAsset = asset;
    if (m_InspectorPanel)
    {
        m_InspectorPanel->Registration().SetVisible(true);
        m_InspectorPanel->Registration().RequestFocus();
    }
}

void EditorWorkspaceLayer::OpenAssetBrowserMaterialGraph(const Keire::AssetId asset) { OpenMaterialGraph(asset); }

void EditorWorkspaceLayer::OpenAssetBrowserMaterialInstance(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::MaterialInstanceAsset::StaticType() ||
        record->RelativePath.extension() != ".keirematerialinstance")
        throw std::invalid_argument("Only .keirematerialinstance assets can be opened as Material Instances.");
    m_SelectedAsset = asset;
    if (m_InspectorPanel)
    {
        m_InspectorPanel->Registration().SetVisible(true);
        m_InspectorPanel->Registration().RequestFocus();
    }
}

void EditorWorkspaceLayer::OpenAssetBrowserShaderGraph(const Keire::AssetId asset) { OpenShaderGraph(asset); }

void EditorWorkspaceLayer::OpenAssetBrowserMaterialParameterCollection(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::MaterialParameterCollectionAsset::StaticType())
        throw std::invalid_argument("Only Material Parameter Collections can be opened here.");
    m_SelectedAsset = asset;
    if (m_InspectorPanel)
    {
        m_InspectorPanel->Registration().SetVisible(true);
        m_InspectorPanel->Registration().RequestFocus();
    }
}

void EditorWorkspaceLayer::OpenAssetBrowserPrefab(const Keire::AssetId asset) { OpenPrefabForEditing(asset); }

void EditorWorkspaceLayer::OpenAssetBrowserScene(const Keire::AssetId asset) { RequestOpenScene(asset); }

bool EditorWorkspaceLayer::PrepareAssetBrowserExternalOpen(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return false;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || (record->RelativePath.extension() != ".cs" && record->RelativePath.extension() != ".keireasm"))
        return false;
    const bool reuseManagedSession = m_ManagedIdeWorkspaceOpened;
    if (!reuseManagedSession)
        GenerateManagedIdeWorkspace();
    m_ManagedIdeWorkspaceOpened = true;
    return reuseManagedSession;
}

void EditorWorkspaceLayer::CopyAssetBrowserText(const std::string_view value)
{
    Owner().Windows()->SetClipboardText(value);
}

void EditorWorkspaceLayer::HandleExternalAssetDrop(const Keire::WindowFileDropEvent& event)
{
    if (!m_AssetDatabase || event.Paths.empty())
        return;
    const Keire::UiPosition position{static_cast<float>(event.Position.X), static_cast<float>(event.Position.Y)};
    const auto& viewportRect = m_SceneViewportPanel->ViewportRect();
    const bool viewport = position.X >= viewportRect.Minimum.X && position.X <= viewportRect.Maximum.X &&
                          position.Y >= viewportRect.Minimum.Y && position.Y <= viewportRect.Maximum.Y;
    Keire::EntityId target;
    if (viewport && ActiveScene())
    {
        const auto assets = Owner().Assets();
        const KeireEditor::MeshBoundsResolver resolveMeshBounds =
            [assets](const Keire::AssetId mesh) -> std::optional<Keire::MeshBounds>
        {
            if (!assets)
                return std::nullopt;
            const auto metadata = assets->TryGetMetadata(mesh);
            if (!metadata || !metadata->LocalBounds)
                return std::nullopt;
            const auto& bounds = *metadata->LocalBounds;
            return Keire::MeshBounds{{bounds.Minimum[0], bounds.Minimum[1], bounds.Minimum[2]},
                                     {bounds.Maximum[0], bounds.Maximum[1], bounds.Maximum[2]}};
        };
        target = KeireEditor::PickSceneEntity(ActiveScene(), viewportRect, position, m_SceneViewportPanel->LastCamera(),
                                              resolveMeshBounds);
    }

    const auto sourceRoot = std::filesystem::absolute(m_AssetDatabase->Specification().ProjectRoot /
                                                      m_AssetDatabase->Specification().SourceDirectory)
                                .lexically_normal();
    std::vector<std::filesystem::path> external;
    for (const auto& dropped : event.Paths)
    {
        const auto absolute = std::filesystem::absolute(dropped).lexically_normal();
        std::error_code error;
        const auto relative = std::filesystem::relative(absolute, sourceRoot, error);
        if (!error && !relative.empty() && !relative.generic_string().starts_with(".."))
        {
            const auto record = m_AssetDatabase->Find(relative);
            if (record)
            {
                m_SelectedAsset = record->Id;
                if (viewport)
                {
                    try
                    {
                        m_ViewportAssetDropRouter->Route(record->Type, record->Id, target, *this);
                    }
                    catch (const std::invalid_argument& exception)
                    {
                        if (record->Type == Keire::MeshAsset::StaticType() ||
                            record->Type == Keire::MaterialAsset::StaticType() ||
                            record->Type == Keire::MaterialGraphAsset::StaticType() ||
                            record->Type == Keire::MaterialInstanceAsset::StaticType() ||
                            record->Type == Keire::ShaderGraphInstanceAsset::StaticType())
                        {
                            m_AssetStatus = "Create or open a scene before dropping meshes or materials.";
                            m_Notice = exception.what();
                            m_NoticeColor = m_Theme.Warning;
                        }
                    }
                }
            }
            continue;
        }
        external.push_back(absolute);
    }
    if (external.empty())
        return;
    const auto destination =
        m_AssetBrowserPanel ? m_AssetBrowserPanel->ResolveExternalDropFolder(position) : std::filesystem::path{};
    if (!m_AssetOperations)
        throw std::logic_error("Asset operation service is unavailable.");
    m_ExternalAssetImport->Queue(external, destination, viewport, target, m_AssetDatabase, *m_AssetOperations);
}

void EditorWorkspaceLayer::DrawExternalAssetImport(Keire::UiFrame& ui)
{
    if (!m_ExternalAssetImport || !m_AssetDatabase || !m_AssetOperations)
        return;
    m_ExternalAssetImport->Draw(ui, m_AssetDatabase, *m_AssetOperations);
    auto completion = m_ExternalAssetImport->TakeCompletion();
    if (!completion)
        return;
    ApplyAssetImportResult(completion->Result.Import, false);
    if (m_AssetBrowserPanel && m_AssetBrowserPanel->UndoContext() && completion->Result.Receipt)
    {
        const auto receipt = completion->Result.Receipt;
        m_AssetBrowserPanel->UndoContext()->RecordApplied(Keire::CreateUndoCommand(
            "Import Assets", [this, receipt] { m_AssetOperations->QueueReceipt(receipt, true); },
            [this, receipt] { m_AssetOperations->QueueReceipt(receipt, false); }));
    }
    for (const auto& entry : completion->Result.Entries)
    {
        if (m_AssetBrowserPanel)
            m_AssetBrowserPanel->InvalidateThumbnail(entry.Id);
        if (const auto assets = Owner().Assets())
            (void)assets->Reload(entry.Id);
        const auto record = m_AssetDatabase->Find(entry.Id);
        if (!record)
            continue;
        m_SelectedAsset = entry.Id;
        if (m_AssetBrowserPanel)
        {
            m_AssetBrowserPanel->RevealAsset(entry.Id);
        }
        if (completion->Viewport)
        {
            try
            {
                m_ViewportAssetDropRouter->Route(record->Type, entry.Id, completion->ViewportTarget, *this);
            }
            catch (const std::invalid_argument& error)
            {
                // Textures and shaders are imported and revealed because they have no unambiguous viewport action.
                if (record->Type == Keire::MeshAsset::StaticType() ||
                    record->Type == Keire::MaterialAsset::StaticType() ||
                    record->Type == Keire::MaterialGraphAsset::StaticType() ||
                    record->Type == Keire::MaterialInstanceAsset::StaticType() ||
                    record->Type == Keire::ShaderGraphInstanceAsset::StaticType())
                {
                    m_AssetStatus = "Create or open a scene before dropping meshes or materials.";
                    m_Notice = error.what();
                    m_NoticeColor = m_Theme.Warning;
                }
            }
        }
    }
    m_AssetStatus = m_ExternalAssetImport->Diagnostic();
}

void EditorWorkspaceLayer::ImportAssets(const KeireEditor::AssetOperationPriority priority)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return;
    try
    {
        m_AssetOperations->QueueImport(priority);
        m_AssetStatus = "Asset import is running in the isolated worker.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Could not queue asset import: ") + error.what());
    }
}

void EditorWorkspaceLayer::DrainQueuedAssetMutation()
{
    if (m_PendingAssetMutations.empty() || !m_AssetOperations || m_AssetOperations->Busy())
        return;
    auto pending = std::move(m_PendingAssetMutations.front());
    m_PendingAssetMutations.pop_front();
    try
    {
        m_ExecutingQueuedAssetMutation = true;
        QueueAssetMutation(std::move(pending.State), pending.Phase);
        m_ExecutingQueuedAssetMutation = false;
    }
    catch (const std::exception& error)
    {
        m_ExecutingQueuedAssetMutation = false;
        SetAssetError(std::string("Queued asset mutation failed: ") + error.what());
    }
    catch (...)
    {
        m_ExecutingQueuedAssetMutation = false;
        SetAssetError("Queued asset mutation failed with an unknown error.");
    }
}

void EditorWorkspaceLayer::DrainQueuedPrefabCreation()
{
    if (m_PendingPrefabCreations.empty() || !m_AssetOperations || m_AssetOperations->Busy())
        return;
    auto pending = std::move(m_PendingPrefabCreations.front());
    m_PendingPrefabCreations.erase(m_PendingPrefabCreations.begin());
    try
    {
        CreatePrefabFromObject(pending.Object, pending.Folder);
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Queued prefab creation failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::PollAssetHotReload()
{
    try
    {
        const auto changed = m_AssetDatabase->PollChangedAssets();
        if (changed.empty())
            return;

        bool requiresFullAssetImport = false;
        std::vector<Keire::AssetId> changedAssetSources;
        for (const auto id : changed)
        {
            const auto record = m_AssetDatabase->Find(id);
            const auto previous = std::ranges::find(m_AssetRecords, id, &Keire::AssetSourceRecord::Id);
            const auto path = record                             ? record->RelativePath
                              : previous != m_AssetRecords.end() ? previous->RelativePath
                                                                 : std::filesystem::path{};
            KEIRE_CLIENT_INFO("[Asset Hot Reload] Change detected: asset={} path='{}' indexed={}.", id.ToString(),
                              Keire::Detail::PathToUtf8(path), record.has_value());
            if (path.extension() == ".cs" || path.extension() == ".keireasm")
                m_ManagedRuntimeCoordinator->ScheduleBuild(0.1);
            if (path.extension() != ".cs")
            {
                if (record)
                    changedAssetSources.push_back(id);
                else
                    requiresFullAssetImport = true;
            }
        }
        RefreshAssetBrowserRecords();
        if (requiresFullAssetImport && m_AssetOperations)
        {
            KEIRE_CLIENT_WARN("[Asset Hot Reload] Scheduling full import because a removed or unindexed non-script "
                              "source cannot be targeted safely.");
            m_AssetOperations->QueueImport(KeireEditor::AssetOperationPriority::AutomaticRefresh,
                                           {.Reason = "automatic-refresh: removed or unindexed non-script source"});
        }
        else if (!changedAssetSources.empty() && m_AssetOperations)
        {
            KEIRE_CLIENT_INFO("[Asset Hot Reload] Scheduling targeted import for {} changed source(s).",
                              changedAssetSources.size());
            m_AssetOperations->QueueAssetImport(std::move(changedAssetSources),
                                                KeireEditor::AssetOperationPriority::AutomaticRefresh,
                                                {.Reason = "automatic-refresh: changed indexed sources"});
        }
        if (const auto assets = Owner().Assets())
        {
            for (const auto id : changed)
                (void)assets->Reload(id);
        }
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Asset hot reload failed: ") + error.what());
    }
}

KeireEditor::SceneDocument& EditorWorkspaceLayer::LightingSceneDocument() noexcept { return *m_SceneDocument; }

bool EditorWorkspaceLayer::LightingBakeBusy() const noexcept { return m_AssetOperations && m_AssetOperations->Busy(); }

std::optional<Keire::AssetOperationProgress> EditorWorkspaceLayer::LightingBakeProgress() const noexcept
{
    return m_AssetOperations ? m_AssetOperations->Progress() : std::nullopt;
}

void EditorWorkspaceLayer::QueueLightingBake(const bool force)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        throw std::logic_error("The isolated asset worker is unavailable.");
    if (!m_SceneDocument->EditingScene() || !m_SceneDocument->Asset())
        throw std::logic_error("Save the open scene as a project asset before baking lighting.");
    if (m_SceneDocument->PlaySession())
        throw std::logic_error("Stop Play Mode before baking lighting.");
    if (m_AssetOperations->Busy())
        throw std::logic_error("Wait for the active asset operation to finish before baking lighting.");
    if (m_SceneDocument->Dirty())
        m_SceneDocument->Save();
    m_AssetOperations->QueueLightingBake(m_SceneDocument->Asset(), force,
                                         {.SourceSceneAsset = m_SceneDocument->Asset()});
    m_AssetStatus = force ? "Forced lighting rebuild queued." : "Lighting bake queued.";
}

void EditorWorkspaceLayer::UpdateAssetOperations()
{
    if (!m_AssetOperations || !m_AssetDatabase)
        return;
    m_AssetOperations->Update();
    while (auto completion = m_AssetOperations->TakeCompletion())
    {
        if (!completion->Result.Success)
        {
            const auto generation = completion->Context.Generation;
            if (completion->Result.Cancelled &&
                (completion->Kind == Keire::Detail::AssetWorkerOperationKind::ImportAll ||
                 completion->Kind == Keire::Detail::AssetWorkerOperationKind::ImportAssets))
            {
                m_AssetStatus = "Background asset refresh yielded to an interactive editor action.";
                if (generation > 0)
                    m_MaterialDocument->MarkCatalogRefreshApplied(generation);
                continue;
            }
            if (m_PendingMaterialAssignment && completion->Context.ReloadAsset == m_PendingMaterialAssignment->Source)
            {
                m_PendingMaterialAssignment.reset();
                m_SceneDocument->SetStatus("Material source compilation failed; the previous material was kept.");
            }
            if (completion->Kind == Keire::Detail::AssetWorkerOperationKind::ExternalImport)
                m_ExternalAssetImport->Complete(std::move(*completion));
            else
                SetAssetError(std::string("Asset worker failed: ") + completion->Result.Diagnostic);
            if (generation > 0)
                m_MaterialDocument->MarkCatalogRefreshApplied(generation);
            continue;
        }
        try
        {
            (void)Keire::Detail::AssetDatabaseWorkerAccess::ReloadSourceIndex(*m_AssetDatabase,
                                                                              completion->SourceIndexPath);
            if (completion->Kind == Keire::Detail::AssetWorkerOperationKind::ExternalImport)
            {
                m_ExternalAssetImport->Complete(std::move(*completion));
                continue;
            }
            if (completion->Kind == Keire::Detail::AssetWorkerOperationKind::Cook)
            {
                const auto& cooked = *completion->Result.Cook;
                m_AssetStatus = "Cooked and validated " + std::to_string(cooked.AssetCount) + " asset(s) into " +
                                std::to_string(cooked.PackCount) + " pack(s).";
                continue;
            }
            ApplyAssetImportResult(completion->Result.Import,
                                   completion->Kind == Keire::Detail::AssetWorkerOperationKind::ImportAll,
                                   completion->Context.ReloadAsset);
            CompletePendingMaterialAssignment(completion->Context.ReloadAsset);
            const auto activeSceneAsset = m_SceneDocument->Asset();
            if (completion->Kind == Keire::Detail::AssetWorkerOperationKind::Mutate && activeSceneAsset &&
                std::ranges::find(completion->Result.MutatedAssets, activeSceneAsset) !=
                    completion->Result.MutatedAssets.end())
            {
                const auto sceneRecord = m_AssetDatabase->Find(activeSceneAsset);
                const auto editingScene = m_SceneDocument->EditingScene();
                if (sceneRecord && editingScene && sceneRecord->Type == Keire::SceneAsset::StaticType())
                {
                    const bool wasDirty = editingScene->Dirty();
                    const auto expectedName = sceneRecord->RelativePath.stem().string();
                    if (!expectedName.empty() && editingScene->Name() != expectedName)
                    {
                        editingScene->SetName(expectedName);
                        if (!wasDirty)
                        {
                            m_AssetDatabase->ReplaceAssetSource(activeSceneAsset,
                                                                Keire::SceneAsset::Encode(editingScene->Snapshot()));
                            editingScene->MarkSaved();
                        }
                    }
                }
            }
            if (completion->Kind == Keire::Detail::AssetWorkerOperationKind::BakeLighting)
            {
                if (!completion->Result.CreatedAsset)
                    throw std::runtime_error("Lighting bake completed without a generated lighting-set identity.");
                if (completion->Context.SourceSceneAsset == m_SceneDocument->Asset())
                {
                    if (const auto editing = m_SceneDocument->EditingScene())
                    {
                        editing->SetBakedLighting(completion->Result.CreatedAsset);
                        editing->MarkSaved();
                    }
                    m_SceneDocument->SetStatus(completion->Result.LightingCacheHit
                                                   ? "Baked lighting restored from the deterministic cache."
                                                   : "Baked lighting published and linked to the scene.");
                }
                m_SelectedAsset = completion->Result.CreatedAsset;
                m_AssetStatus = completion->Result.LightingCacheHit ? "Lighting bake completed from cache."
                                                                    : "Lighting bake and asset publication completed.";
                continue;
            }
            if (completion->Kind == Keire::Detail::AssetWorkerOperationKind::ExtractMaterials)
            {
                if (completion->Result.MutatedAssets.empty())
                    throw std::runtime_error("Material extraction completed without creating any assets.");
                m_SelectedAsset = completion->Result.MutatedAssets.back();
                if (m_AssetBrowserPanel)
                    m_AssetBrowserPanel->RevealAsset(m_SelectedAsset);
                m_AssetStatus = "Extracted " + std::to_string(completion->Result.MutatedAssets.size()) +
                                " editable material" + (completion->Result.MutatedAssets.size() == 1 ? "." : "s.");
                continue;
            }
            if (completion->Kind == Keire::Detail::AssetWorkerOperationKind::Mutate)
            {
                if (m_SceneDocument && m_SceneDocument->Asset())
                {
                    const auto scene = m_SceneDocument->Asset();
                    if (std::ranges::find(completion->Result.MutatedAssets, scene) !=
                        completion->Result.MutatedAssets.end())
                    {
                        if (const auto record = m_AssetDatabase->Find(scene))
                        {
                            const auto& specification = m_AssetDatabase->Specification();
                            m_SceneDocument->SetIdentity(scene, (specification.ProjectRoot /
                                                                 specification.SourceDirectory / record->RelativePath)
                                                                    .lexically_normal());
                        }
                    }
                }
                if (const auto& state = completion->Context.MutationUndo)
                {
                    const auto phase = completion->Context.MutationPhase;
                    const auto mutationKind =
                        phase == KeireEditor::AssetMutationPhase::Undo ? state->Reverse.Kind : state->Forward.Kind;
                    if (phase == KeireEditor::AssetMutationPhase::Initial)
                    {
                        if (mutationKind == Keire::Detail::AssetWorkerMutationKind::DuplicateAsset)
                        {
                            if (completion->Result.MutatedAssets.size() != 1)
                                throw std::runtime_error("Asset duplication returned an invalid identity set.");
                            state->Reverse = {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashAsset,
                                              .Asset = completion->Result.MutatedAssets.front()};
                        }
                        else if (mutationKind == Keire::Detail::AssetWorkerMutationKind::DuplicateFolder ||
                                 mutationKind == Keire::Detail::AssetWorkerMutationKind::CreateFolder)
                        {
                            state->Reverse = {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashFolder,
                                              .Source = state->Forward.Destination};
                        }
                        else if (mutationKind == Keire::Detail::AssetWorkerMutationKind::TrashAsset ||
                                 mutationKind == Keire::Detail::AssetWorkerMutationKind::TrashFolder)
                        {
                            state->Reverse = {.Kind = Keire::Detail::AssetWorkerMutationKind::RestoreTrash,
                                              .Trash = completion->Result.Trash};
                        }
                        if (state->RecordCommand)
                        {
                            const auto undo = m_AssetBrowserPanel ? m_AssetBrowserPanel->UndoContext() : nullptr;
                            if (undo && undo->IsOpen())
                            {
                                undo->RecordApplied(Keire::CreateUndoCommand(
                                    state->Name,
                                    [this, state] { QueueAssetMutation(state, KeireEditor::AssetMutationPhase::Redo); },
                                    [this, state] { QueueAssetMutation(state, KeireEditor::AssetMutationPhase::Undo); },
                                    sizeof(*state), [this] { return static_cast<bool>(m_AssetOperations); }));
                                m_ActiveUndoContext = undo;
                            }
                            state->RecordCommand = false;
                        }
                    }
                    else if ((phase == KeireEditor::AssetMutationPhase::Undo &&
                              (mutationKind == Keire::Detail::AssetWorkerMutationKind::TrashAsset ||
                               mutationKind == Keire::Detail::AssetWorkerMutationKind::TrashFolder)) ||
                             (phase == KeireEditor::AssetMutationPhase::Redo &&
                              (mutationKind == Keire::Detail::AssetWorkerMutationKind::TrashAsset ||
                               mutationKind == Keire::Detail::AssetWorkerMutationKind::TrashFolder)))
                    {
                        auto restore = Keire::Detail::AssetWorkerMutation{
                            .Kind = Keire::Detail::AssetWorkerMutationKind::RestoreTrash,
                            .Trash = completion->Result.Trash};
                        if (phase == KeireEditor::AssetMutationPhase::Undo)
                            state->Forward = std::move(restore);
                        else
                            state->Reverse = std::move(restore);
                    }
                }
                if (completion->Context.FollowUp == KeireEditor::AssetOperationFollowUp::Reveal &&
                    !completion->Result.MutatedAssets.empty())
                {
                    m_SelectedAsset = completion->Result.MutatedAssets.front();
                    if (m_AssetBrowserPanel)
                        m_AssetBrowserPanel->RevealAsset(m_SelectedAsset);
                }
                m_AssetStatus = "Asset mutation and catalog publication completed.";
            }
            if (completion->Kind == Keire::Detail::AssetWorkerOperationKind::CreateAsset)
            {
                const auto created = completion->Result.CreatedAsset;
                if (!created)
                    throw std::runtime_error("Asset worker completed creation without a stable asset identity.");
                if (completion->Context.ManagedAssembly)
                    ExtendManagedAssemblySourceRoot(completion->Context.ManagedAssembly,
                                                    completion->Context.ManagedSourceRoot);
                if (const auto createdRecord = m_AssetDatabase->Find(created);
                    createdRecord && (createdRecord->RelativePath.extension() == ".cs" ||
                                      createdRecord->RelativePath.extension() == ".keireasm"))
                {
                    m_ManagedRuntimeCoordinator->ScheduleBuild(0.1);
                }
                if (completion->Context.GraphFunctionExtraction)
                    CompleteGraphFunctionExtraction(*completion->Context.GraphFunctionExtraction, created);
                m_SelectedAsset = created;
                if (m_AssetBrowserPanel)
                {
                    m_AssetBrowserPanel->RevealAsset(created);
                    if (!completion->Context.UndoName.empty())
                    {
                        auto state = std::make_shared<KeireEditor::AssetMutationUndoState>();
                        state->Reverse = {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashAsset, .Asset = created};
                        state->Name = completion->Context.UndoName;
                        state->RecordCommand = false;
                        const auto undo = m_AssetBrowserPanel->UndoContext();
                        if (undo && undo->IsOpen())
                        {
                            undo->RecordApplied(Keire::CreateUndoCommand(
                                state->Name,
                                [this, state] { QueueAssetMutation(state, KeireEditor::AssetMutationPhase::Redo); },
                                [this, state] { QueueAssetMutation(state, KeireEditor::AssetMutationPhase::Undo); },
                                sizeof(*state), [this] { return static_cast<bool>(m_AssetOperations); }));
                            m_ActiveUndoContext = undo;
                        }
                    }
                }
                if (completion->Context.FollowUp == KeireEditor::AssetOperationFollowUp::OpenScene)
                    RequestOpenScene(created);
                else if (completion->Context.FollowUp == KeireEditor::AssetOperationFollowUp::OpenInputActions)
                    OpenInputActions(created);
                else if (completion->Context.FollowUp == KeireEditor::AssetOperationFollowUp::OpenExternal &&
                         m_AssetBrowserPanel)
                    m_AssetBrowserPanel->OpenAsset(created);
                else if (completion->Context.FollowUp == KeireEditor::AssetOperationFollowUp::OpenMaterialGraph)
                    OpenMaterialGraph(created);
                else if (completion->Context.FollowUp == KeireEditor::AssetOperationFollowUp::OpenShaderGraph)
                    OpenShaderGraph(created);
                else if (completion->Context.FollowUp == KeireEditor::AssetOperationFollowUp::AdoptSceneCopy)
                {
                    if (!completion->Context.SceneSnapshot)
                        throw std::runtime_error("Scene copy completion omitted its captured scene definition.");
                    const auto editing = m_SceneDocument->EditingScene();
                    const bool sameDocument =
                        editing && m_SceneDocument->Asset() == completion->Context.SourceSceneAsset;
                    const bool unchanged =
                        sameDocument && Keire::SceneAsset::Encode(editing->Snapshot()) ==
                                            Keire::SceneAsset::Encode(*completion->Context.SceneSnapshot);
                    if (!unchanged)
                    {
                        m_SceneDocument->SetStatus(
                            "The scene copy was created, but the current document changed while saving and was kept.");
                    }
                    else
                    {
                        auto scene = Keire::CreateRef<Keire::Scene>(created, *completion->Context.SceneSnapshot,
                                                                    editing->Components());
                        scene->MarkSaved();
                        m_SceneDocument->ReplaceEditingScene(std::move(scene), false);
                        m_SceneDocument->SetIdentity(created, completion->Context.SceneSource);
                        if (const auto project = Owner().GetProject())
                        {
                            m_SceneDocument->SetRecoveryPath(project->SceneRecoveryDirectory() /
                                                             (created.ToString() + ".keirescene.recovery"));
                        }
                        if (m_SceneDocument->UndoContext())
                            m_SceneDocument->UndoContext()->Close();
                        if (const auto undo = Owner().Undo())
                        {
                            m_SceneDocument->SetUndoContext(undo->CreateContext(
                                {.Name = "Scene: " + completion->Context.SceneSource.stem().string()}));
                        }
                        m_ActiveUndoContext = m_SceneDocument->UndoContext();
                        if (const auto scenes = Owner().Scenes())
                            m_SceneDocument->SetLoadOperation(scenes->Load(created, Keire::SceneLoadMode::Single));
                        m_SceneDocument->SetStatus("Saved a new scene asset with a new stable identity.");
                        PersistEditorSessionScene(created);
                        AddConsoleMessage("Scene",
                                          "Saved As " + Keire::Detail::PathToUtf8(completion->Context.SceneSource),
                                          m_Theme.Success);
                    }
                }
                const auto record = m_AssetDatabase->Find(created);
                if (!record)
                    throw std::runtime_error("Created asset is absent from the published source index.");
                m_AssetStatus = "Created and published " + record->RelativePath.generic_string() + ".";
            }
            if (completion->Context.Generation > 0)
                m_MaterialDocument->MarkCatalogRefreshApplied(completion->Context.Generation);
            OpenPendingStartupScene();
        }
        catch (const std::exception& error)
        {
            SetAssetError(std::string("Asset worker result could not be applied: ") + error.what());
        }
    }
}

void EditorWorkspaceLayer::ApplyAssetImportResult(const Keire::AssetImportResult& result, const bool reloadLoadedAssets,
                                                  const Keire::AssetId reloadAsset)
{
    RefreshAssetBrowserRecords();
    if (!result.CatalogPath.empty())
    {
        if (const auto assets = Owner().Assets())
        {
            (void)assets->Unmount(result.CatalogPath);
            assets->Mount({result.CatalogPath, 0, true});

            std::set<Keire::AssetId> affected;
            const auto includeRecord = [&affected](const Keire::AssetSourceRecord& record)
            {
                affected.insert(record.Id);
                affected.insert(record.SubAssets.begin(), record.SubAssets.end());
            };
            for (const auto& status : result.Statuses)
            {
                if (status.State != Keire::AssetImportState::Imported)
                    continue;
                affected.insert(status.Id);
                if (const auto record = m_AssetDatabase->Find(status.Id))
                    includeRecord(*record);
            }
            if (reloadAsset)
            {
                affected.insert(reloadAsset);
                if (const auto record = m_AssetDatabase->Find(reloadAsset))
                    includeRecord(*record);
            }
            if (reloadLoadedAssets && !reloadAsset)
            {
                for (const auto& record : m_AssetRecords)
                    includeRecord(record);
            }

            bool expanded = true;
            while (expanded)
            {
                expanded = false;
                for (const auto& record : m_AssetRecords)
                {
                    if (affected.contains(record.Id) ||
                        !std::ranges::any_of(record.Dependencies, [&affected](const Keire::AssetId dependency)
                                             { return affected.contains(dependency); }))
                    {
                        continue;
                    }
                    includeRecord(record);
                    expanded = true;
                }
            }
            for (const auto asset : affected)
            {
                if (m_AssetBrowserPanel)
                    m_AssetBrowserPanel->InvalidateThumbnail(asset);
                (void)assets->Reload(asset, Keire::AssetPriority::Background);
            }
        }
    }
    for (const auto& importStatus : result.Statuses)
    {
        for (const auto& diagnostic : importStatus.Diagnostics)
        {
            const auto message = FormatAssetDiagnostic(diagnostic);
            switch (diagnostic.Severity)
            {
            case Keire::AssetDiagnosticSeverity::Information:
                AddConsoleMessage("Asset Import", message, m_Theme.MutedText);
                break;
            case Keire::AssetDiagnosticSeverity::Warning:
                AddConsoleMessage("Asset Import", message, m_Theme.Warning, Keire::LogLevel::Warn);
                break;
            case Keire::AssetDiagnosticSeverity::Error:
                ReportError("Asset Import", message);
                break;
            }
        }
    }
    const auto failures =
        std::ranges::count(result.Statuses, Keire::AssetImportState::Failed, &Keire::AssetImportStatus::State);
    std::ostringstream status;
    status << "Imported " << result.Imported << " asset(s); " << result.CacheHits << " cache hit(s).";
    if (failures > 0)
        status << ' ' << failures << " asset(s) kept their last-good revision; select an asset for full diagnostics.";
    m_AssetStatus = status.str();
}

void EditorWorkspaceLayer::QueueMaterialCatalogRefresh(const Keire::AssetId reloadAsset)
{
    m_MaterialDocument->RequestCatalogRefresh(reloadAsset);
}

void EditorWorkspaceLayer::UpdateMaterialCatalogRefresh(const double unscaledDeltaSeconds)
{
    if (!m_AssetOperations)
        return;
    m_MaterialDocument->AdvanceCatalogRefresh(unscaledDeltaSeconds);
    const auto pending = m_MaterialDocument->PendingCatalogRefresh();
    if (!pending)
        return;
    try
    {
        m_AssetOperations->QueueAssetImport(pending->Asset, KeireEditor::AssetOperationPriority::MaterialRefresh,
                                            {.ReloadAsset = pending->Asset, .Generation = pending->Generation});
        m_MaterialDocument->MarkCatalogRefreshQueued(pending->Generation);
    }
    catch (const std::exception& error)
    {
        ReportError("Asset Import", std::string("Could not queue material refresh: ") + error.what());
    }
}

void EditorWorkspaceLayer::FlushMaterialCatalogRefresh() noexcept
{
    if (m_AssetOperations)
    {
        try
        {
            if (const auto pending = m_MaterialDocument->PendingCatalogRefresh(true))
            {
                m_AssetOperations->QueueAssetImport(pending->Asset,
                                                    KeireEditor::AssetOperationPriority::MaterialRefresh,
                                                    {.ReloadAsset = pending->Asset, .Generation = pending->Generation});
                m_MaterialDocument->MarkCatalogRefreshQueued(pending->Generation);
            }
        }
        catch (const std::exception& error)
        {
            KEIRE_CLIENT_ERROR("[Asset Import] Could not queue material refresh during flush: {}", error.what());
        }
        catch (...)
        {
            KEIRE_CLIENT_ERROR("[Asset Import] Could not queue material refresh during flush.");
        }
    }
}

void EditorWorkspaceLayer::CancelMaterialCatalogRefresh() noexcept
{
    if (m_AssetOperations)
        m_AssetOperations->Shutdown();
    m_MaterialDocument->ResetCatalogRefresh();
}
