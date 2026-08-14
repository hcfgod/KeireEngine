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

    [[nodiscard]] std::string AssetPackageFileName(const std::string_view displayName)
    {
        std::string result(displayName);
        for (char& character : result)
            if (static_cast<unsigned char>(character) < 32U ||
                std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos)
                character = '-';
        while (!result.empty() && (result.back() == ' ' || result.back() == '.'))
            result.pop_back();
        if (result.empty())
            result = "Asset Package";
        return result + ".keireassetpackage";
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

bool EditorWorkspaceLayer::AssetSourcesAreNewerThanCatalog(const std::filesystem::path& assetsRoot,
                                                           const std::filesystem::path& catalog) noexcept
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(catalog, error) || error)
        return true;
    const auto catalogTime = std::filesystem::last_write_time(catalog, error);
    if (error)
        return true;
    for (std::filesystem::recursive_directory_iterator
             iterator(assetsRoot, std::filesystem::directory_options::skip_permission_denied, error),
         end;
         iterator != end; iterator.increment(error))
    {
        if (error)
            return true;
        if (iterator->is_regular_file(error) && !error && FileIsNewerThan(iterator->path(), catalogTime))
            return true;
        error.clear();
    }
    return false;
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
        ++m_AssetRecordRevision;
    }
}

void EditorWorkspaceLayer::SetAssetBrowserSelected(const Keire::AssetId asset) noexcept { m_SelectedAsset = asset; }

void EditorWorkspaceLayer::ClearAssetBrowserSceneSelection() noexcept { m_SceneDocument->ClearSelection(); }

void EditorWorkspaceLayer::SetAssetBrowserStatus(std::string status) noexcept { m_AssetStatus = std::move(status); }

void EditorWorkspaceLayer::ReportAssetBrowserError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::ImportAssetBrowserAssets() { ImportAssets(); }

bool EditorWorkspaceLayer::CreateAssetBrowserScene(const std::string_view name) { return CreateSceneAsset(name); }

bool EditorWorkspaceLayer::CreateAssetBrowserMaterial(const std::string_view name) { return CreateMaterial(name); }

bool EditorWorkspaceLayer::CreateAssetBrowserAnimationGraph(const std::string_view name)
{
    return CreateAnimationGraph(name);
}

bool EditorWorkspaceLayer::CreateAssetBrowserScript(const std::string_view name) { return CreateCSharpScript(name); }

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

void EditorWorkspaceLayer::ExtractAssetBrowserMaterials(const Keire::AssetId model)
{
    if (!m_AssetDatabase)
        return;
    try
    {
        const auto record = m_AssetDatabase->Find(model);
        if (!record)
            throw std::runtime_error("The selected model no longer exists in the project database.");
        const auto directory =
            record->RelativePath.parent_path() / (record->RelativePath.stem().string() + " Materials");
        if (!m_AssetOperations)
            throw std::logic_error("The isolated asset worker is unavailable.");
        m_AssetOperations->QueueExtractMaterials(model, directory,
                                                 {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal});
        m_AssetStatus = "Extracting editable materials in the isolated worker.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Material extraction failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::CreateAssetBrowserPackage(KeireEditor::AssetPackageSelection selection,
                                                     KeireEditor::AssetPackageDraft draft)
{
    try
    {
        if (!m_AssetDatabase)
            throw std::logic_error("No project asset database is available.");
        if (m_PendingAssetPackageDialog || m_AssetPackageExport.valid())
            throw std::logic_error("An asset-package export is already active.");
        static_cast<void>(KeireEditor::ResolveAssetPackageRecords(m_AssetRecords, selection));
        Keire::SaveFileDialogSpecification dialog;
        dialog.Title = "Create Kéire Asset Package";
        dialog.DefaultLocation = m_AssetDatabase->Specification().ProjectRoot;
        dialog.DefaultName = AssetPackageFileName(draft.DisplayName);
        dialog.FilterName = "Kéire Asset Package";
        dialog.Extension = "keireassetpackage";
        auto operation = Owner().Windows()->ShowSaveFileDialog(Owner().MainWindow()->Id(), dialog);
        m_PendingAssetPackageDialog = {
            .Selection = std::move(selection), .Draft = std::move(draft), .Dialog = std::move(operation)};
        m_AssetStatus = "Choose a destination for the asset package.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Asset-package export failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::CompleteAssetBrowserPackage()
{
    if (m_AssetPackageExport.valid())
    {
        if (m_AssetPackageExport.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try
        {
            const auto result = m_AssetPackageExport.get();
            m_AssetStatus = "Created " + m_AssetPackageOutput.filename().string() + " with " +
                            std::to_string(result.Manifest.Assets.size()) + " asset(s).";
            std::string diagnostic;
            if (!Keire::Detail::RevealInFileManager(m_AssetPackageOutput, diagnostic))
                m_AssetStatus += " Reveal failed: " + diagnostic;
        }
        catch (const std::exception& error)
        {
            SetAssetError(std::string("Asset-package export failed: ") + error.what());
        }
        m_AssetPackageOutput.clear();
    }
    if (!m_PendingAssetPackageDialog ||
        m_PendingAssetPackageDialog->Dialog->Status() == Keire::SaveFileDialogStatus::Pending)
        return;

    auto pending = std::move(*m_PendingAssetPackageDialog);
    m_PendingAssetPackageDialog.reset();
    if (pending.Dialog->Status() == Keire::SaveFileDialogStatus::Cancelled)
    {
        m_AssetStatus = "Asset-package export cancelled.";
        return;
    }
    if (pending.Dialog->Status() == Keire::SaveFileDialogStatus::Failed)
    {
        SetAssetError("Asset-package save dialog failed: " + pending.Dialog->Diagnostic());
        return;
    }
    try
    {
        auto output = pending.Dialog->SelectedPath();
        if (output.extension() != ".keireassetpackage")
            output += ".keireassetpackage";
        if (std::filesystem::exists(output))
            throw std::invalid_argument("Asset-package export will not overwrite an existing file.");
        if (!m_AssetDatabase)
            throw std::logic_error("The project closed before asset-package export began.");
        const auto& specification = m_AssetDatabase->Specification();
        KeireEditor::AssetPackageAuthoringRequest request{.ProjectRoot = specification.ProjectRoot,
                                                          .SourceDirectory = specification.SourceDirectory,
                                                          .StagingParent =
                                                              specification.ProjectRoot / "Library/AssetPackageExports",
                                                          .Output = output,
                                                          .Selection = std::move(pending.Selection),
                                                          .Draft = std::move(pending.Draft),
                                                          .Records = m_AssetRecords};
        m_AssetPackageOutput = std::move(output);
        m_AssetPackageExport = std::async(std::launch::async, [request = std::move(request)]
                                          { return KeireEditor::CreateAssetPackageArchive(request); });
        m_AssetStatus = "Creating the asset package in the background...";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Asset-package export failed: ") + error.what());
    }
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
            m_AssetStatus = "Queued asset trash update until the active asset operation completes.";
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
        m_AssetStatus = "Asset trash updated.";
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

void EditorWorkspaceLayer::PrepareAssetBrowserExternalOpen(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (record && (record->RelativePath.extension() == ".cs" || record->RelativePath.extension() == ".keireasm"))
        GenerateManagedIdeWorkspace();
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
            if (completion->Result.Cancelled && completion->Kind == Keire::Detail::AssetWorkerOperationKind::ImportAll)
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
            RefreshAssetBrowserRecords();
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
            ApplyAssetImportResult(completion->Result.Import, true, completion->Context.ReloadAsset);
            CompletePendingMaterialAssignment(completion->Context.ReloadAsset);
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
                (void)assets->Reload(asset, Keire::AssetPriority::Background);
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

void EditorWorkspaceLayer::UpdateMaterialCatalogRefresh(const Keire::Time& time)
{
    if (!m_AssetOperations)
        return;
    m_MaterialDocument->AdvanceCatalogRefresh(time.UnscaledDeltaTime().Seconds());
    const auto pending = m_MaterialDocument->PendingCatalogRefresh();
    if (!pending)
        return;
    try
    {
        m_AssetOperations->QueueImport(KeireEditor::AssetOperationPriority::MaterialRefresh,
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
                m_AssetOperations->QueueImport(KeireEditor::AssetOperationPriority::MaterialRefresh,
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

bool EditorWorkspaceLayer::CreateCSharpScript(const std::string_view name)
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
        auto destinationDirectory = directory;
        const auto projectRelativeParent = std::filesystem::path("Assets") / directory;
        std::string rootNamespace;
        std::size_t matchedLength = 0;
        std::filesystem::path fallbackRoot;
        std::string fallbackNamespace;
        bool fallbackIsDescendant = false;
        const auto projectRoot = Owner().GetProject()->Root();
        for (const auto& record : m_AssetDatabase->Records())
        {
            if (record.Type != Keire::ManagedAssemblyAsset::StaticType())
                continue;
            const auto assembly =
                Keire::ManagedAssemblyAsset::Decode(ReadBytes(projectRoot / "Assets" / record.RelativePath));
            for (const auto& root : assembly->Definition().SourceRoots)
            {
                const auto assetsRelativeRoot = root.lexically_normal().lexically_relative("Assets");
                if (assetsRelativeRoot.empty() || assetsRelativeRoot.is_absolute() ||
                    assetsRelativeRoot.generic_string().starts_with(".."))
                    continue;
                if (SameOrChild(root, projectRelativeParent) && root.generic_string().size() >= matchedLength)
                {
                    rootNamespace = assembly->Definition().RootNamespace;
                    matchedLength = root.generic_string().size();
                }
                const bool descendant = SameOrChild(projectRelativeParent, root);
                const bool runtime =
                    assembly->Definition().Classification == Keire::ManagedAssemblyClassification::Runtime;
                if ((descendant || runtime) &&
                    (fallbackNamespace.empty() || (descendant && !fallbackIsDescendant) ||
                     (descendant == fallbackIsDescendant && root.generic_string() < fallbackRoot.generic_string())))
                {
                    fallbackRoot = assetsRelativeRoot;
                    fallbackNamespace = assembly->Definition().RootNamespace;
                    fallbackIsDescendant = descendant;
                }
            }
        }
        if (rootNamespace.empty())
        {
            if (fallbackNamespace.empty())
                throw std::runtime_error("Create a runtime .keireasm asset before creating a C# script.");
            destinationDirectory = fallbackRoot;
            rootNamespace = fallbackNamespace;
        }

        const auto destination = destinationDirectory / (std::string(name) + ".cs");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A script with that name already exists in " +
                                     destinationDirectory.generic_string() + ".");

        const std::string source = "using Keire;\n\nnamespace " + rootNamespace + ";\n\n[StableComponentId(\"" +
                                   Keire::AssetId::Generate().ToString() + "\")]\npublic sealed class " +
                                   std::string(name) +
                                   " : Behaviour\n{\n    protected override void Start()\n    {\n    }\n\n"
                                   "    protected override void Update()\n    {\n    }\n}\n";
        m_AssetOperations->QueueCreateAsset(
            destination, TextBytes(source), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenExternal, .UndoName = "Create C# Script"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Script creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateManagedAssembly(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (!IsCSharpIdentifier(name))
            throw std::invalid_argument("Managed assembly names must be valid C# identifiers.");
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keireasm");
        const auto script = directory / std::string(name) / (std::string(name) + "Root.cs");
        if (m_AssetDatabase->Find(destination) ||
            std::filesystem::exists(m_AssetDatabase->Specification().ProjectRoot / "Assets" / script))
            throw std::runtime_error("The assembly or its source folder already exists.");

        Keire::ManagedAssemblyDefinition definition;
        definition.Name = name;
        definition.RootNamespace = name;
        definition.SourceRoots = {std::filesystem::path("Assets") / directory / std::string(name)};
        const std::string source = "using Keire;\n\nnamespace " + std::string(name) + ";\n\npublic sealed class " +
                                   std::string(name) +
                                   "Root : Behaviour\n{\n    protected override void Start() => "
                                   "Log.Info(\"Managed assembly loaded.\");\n}\n";
        m_AssetOperations->QueueCreateAssetWithAuxiliary(
            destination, Keire::ManagedAssemblyAsset::Encode(definition), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Managed Assembly"},
            {{script, TextBytes(source)}});
        m_AssetStatus = "Creating managed assembly " + destination.generic_string() + ".";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Managed assembly creation failed: ") + error.what());
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
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Material Graph name must be one non-empty path component.");
        const auto shader = m_AssetDatabase->Find(shaderAsset);
        if (!shader ||
            (shader->Type != Keire::ShaderAsset::StaticType() && shader->Type != Keire::ShaderGraphAsset::StaticType()))
            throw std::runtime_error("Choose a compatible Shader Graph or raw Shader for the Material Graph.");

        Keire::MaterialShaderReference shaderReference;
        shaderReference.Asset = shader->Id;
        shaderReference.Kind = shader->Type == Keire::ShaderGraphAsset::StaticType()
                                   ? Keire::MaterialShaderSourceKind::ShaderGraph
                                   : Keire::MaterialShaderSourceKind::ShaderAsset;
        const auto shaderInterface = ResolveMaterialGraphInterface(shaderReference);
        if (!shaderInterface)
            throw std::runtime_error("The selected shader does not expose a compatible material interface.");
        auto definition = Keire::CreateMaterialGraph(std::move(shaderReference), *shaderInterface);
        if (definition.Shader.Kind == Keire::MaterialShaderSourceKind::ShaderGraph)
        {
            const auto shaderTemplate = ResolveMaterialGraphTemplate(definition.Shader);
            if (!shaderTemplate)
                throw std::runtime_error("The selected Shader Graph template could not be loaded.");
            definition.SurfaceGraph = Keire::CreateMaterialSurfaceGraph(*shaderTemplate);
        }
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keirematerialgraph");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A Material Graph with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::MaterialGraphAsset::EncodeSource(definition), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Material Graph"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
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
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Shader Graph"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
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

        std::string_view extension;
        std::vector<std::byte> bytes;
        switch (purpose)
        {
        case Keire::ShaderGraphPurpose::MaterialFunction:
            extension = Keire::MaterialFunctionAssetSourceExtension;
            bytes = Keire::MaterialFunctionAsset::Encode(Keire::CreateDefaultGraphFunction(purpose));
            break;
        case Keire::ShaderGraphPurpose::ShaderFunction:
            extension = Keire::ShaderFunctionAssetSourceExtension;
            bytes = Keire::ShaderFunctionAsset::Encode(Keire::CreateDefaultGraphFunction(purpose));
            break;
        case Keire::ShaderGraphPurpose::MaterialLayer:
            extension = Keire::MaterialLayerAssetSourceExtension;
            bytes = Keire::MaterialLayerAsset::Encode(Keire::CreateDefaultGraphFunction(purpose));
            break;
        case Keire::ShaderGraphPurpose::MaterialLayerBlend:
            extension = Keire::MaterialLayerBlendAssetSourceExtension;
            bytes = Keire::MaterialLayerBlendAsset::Encode(Keire::CreateDefaultGraphFunction(purpose));
            break;
        case Keire::ShaderGraphPurpose::Shader:
            throw std::invalid_argument("Use Shader Graph creation for shader assets.");
        }

        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + std::string(extension));
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

    m_PrefabReturnDocument = std::move(m_SceneDocument);
    m_SceneDocument = std::move(document);
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
    m_SceneDocument = std::move(m_PrefabReturnDocument);
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
        Keire::MaterialShaderReference shader;
        if (const auto selected = m_AssetDatabase->Find(m_SelectedAsset);
            selected && (selected->Type == Keire::ShaderAsset::StaticType() ||
                         selected->Type == Keire::ShaderGraphAsset::StaticType()))
        {
            shader.Asset = selected->Id;
            shader.Kind = selected->Type == Keire::ShaderGraphAsset::StaticType()
                              ? Keire::MaterialShaderSourceKind::ShaderGraph
                              : Keire::MaterialShaderSourceKind::ShaderAsset;
        }
        if (!shader.Asset)
        {
            const auto records = m_AssetDatabase->Records();
            const auto found =
                std::ranges::find(records, Keire::ShaderAsset::StaticType(), &Keire::AssetSourceRecord::Type);
            if (found != records.end())
                shader.Asset = found->Id;
        }

        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Material name must be one non-empty path component.");
        const auto destination = directory / (std::string(name) + ".keirematerial");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A material with that name already exists in this folder.");
        Keire::MaterialAuthoringDefinition definition;
        definition.Shader = std::move(shader);
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::MaterialAsset::EncodeAuthoringSource(definition), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Material"});
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

KeireEditor::ShaderGraphDocument& EditorWorkspaceLayer::ShaderGraphState() noexcept { return *m_ShaderGraphDocument; }

const Keire::UiThemeDefinition& EditorWorkspaceLayer::ShaderGraphTheme() const noexcept { return m_Theme; }

void EditorWorkspaceLayer::SaveShaderGraphDocument() { SaveShaderGraph(); }

void EditorWorkspaceLayer::UndoShaderGraphEdit() { (void)m_ShaderGraphDocument->Undo(); }

void EditorWorkspaceLayer::RedoShaderGraphEdit() { (void)m_ShaderGraphDocument->Redo(); }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::ShaderGraphAssetRecords() const noexcept
{
    return m_AssetRecords;
}

Keire::Ref<const Keire::MeshAsset> EditorWorkspaceLayer::ResolveShaderGraphPreviewMesh(const Keire::AssetId asset)
{
    if (auto builtin = Keire::MeshAsset::ResolveBuiltin(asset))
        return builtin;
    const auto assets = Owner().Assets();
    if (!asset || !assets)
        return {};
    return assets->Load<Keire::MeshAsset>(asset, Keire::AssetPriority::High).TryGetLoaded();
}

std::optional<Keire::ShaderGraphDefinition>
EditorWorkspaceLayer::ResolveShaderGraphFunction(const Keire::AssetId asset) const
{
    return ResolveReusableGraph(asset);
}

void EditorWorkspaceLayer::ApplyShaderGraphDevelopmentRevision(
    const Keire::AssetId asset, const Keire::ShaderGraphDefinition& definition,
    const Keire::ShaderGraphCompilation& compilation,
    const std::span<const Keire::Ref<Keire::ShaderAsset>> developmentShaders) noexcept
{
    try
    {
        const auto assets = Owner().Assets();
        const auto record = m_AssetDatabase ? m_AssetDatabase->Find(asset) : std::nullopt;
        if (!assets || !record || !compilation.Succeeded() || compilation.Variants.empty())
            return;

        std::vector<Keire::AssetId> shaderAssets;
        Keire::AssetId materialAsset;
        for (const auto subAsset : record->SubAssets)
        {
            const auto type = assets->TryGetType(subAsset);
            if (type && *type == Keire::ShaderAsset::StaticType())
                shaderAssets.push_back(subAsset);
            else if (type && *type == Keire::MaterialAsset::StaticType() && !materialAsset)
                materialAsset = subAsset;
        }
        if (!materialAsset || shaderAssets.size() != compilation.Variants.size())
            return;
        if (!developmentShaders.empty() && developmentShaders.size() != shaderAssets.size())
            throw std::logic_error("A live Shader Graph shader revision does not match its catalog variants.");

        for (std::size_t index = 0; index < developmentShaders.size(); ++index)
        {
            if (!developmentShaders[index] ||
                !assets->PublishDevelopmentAsset(shaderAssets[index], developmentShaders[index]))
            {
                throw std::runtime_error("A live Shader Graph shader variant could not be published.");
            }
        }

        Keire::ShaderGraphInstanceDefinition defaults;
        defaults.Parent = asset;
        const std::array ancestry{defaults};
        const auto resolved = Keire::ResolveShaderGraphInstance(definition, ancestry);
        const auto material = Keire::BakeShaderGraphInstance(
            definition, resolved,
            [&compilation, &shaderAssets](const std::span<const std::string> keywords)
            {
                for (std::size_t index = 0; index < compilation.Variants.size(); ++index)
                    if (std::ranges::equal(compilation.Variants[index].Keywords, keywords))
                        return shaderAssets[index];
                return Keire::AssetId{};
            });
        if (!assets->PublishDevelopmentAsset(materialAsset, Keire::CreateRef<Keire::MaterialAsset>(material)))
            throw std::runtime_error("The live Shader Graph material revision could not be published.");
    }
    catch (const std::exception& error)
    {
        KEIRE_CLIENT_ERROR("[Shader Graph] Live scene apply failed for {}: {}", asset.ToString(), error.what());
    }
    catch (...)
    {
        KEIRE_CLIENT_ERROR("[Shader Graph] Live scene apply failed for {}.", asset.ToString());
    }
}

void EditorWorkspaceLayer::RevealShaderGraphAsset(const Keire::AssetId asset)
{
    if (!asset || !m_AssetBrowserPanel)
        return;
    m_SelectedAsset = asset;
    m_AssetBrowserPanel->RevealAsset(asset);
    m_AssetBrowserPanel->Registration().SetVisible(true);
    m_AssetBrowserPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::ReportShaderGraphError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::PersistShaderGraph(const Keire::AssetId asset, const std::span<const std::byte> bytes)
{
    if (!m_AssetDatabase)
        throw std::runtime_error("The Asset Database is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record)
        throw std::runtime_error("The edited graph source is unavailable.");
    if (record->Type != Keire::ShaderGraphAsset::StaticType())
    {
        const bool reusable = record->Type == Keire::MaterialFunctionAsset::StaticType() ||
                              record->Type == Keire::ShaderFunctionAsset::StaticType() ||
                              record->Type == Keire::MaterialLayerAsset::StaticType() ||
                              record->Type == Keire::MaterialLayerBlendAsset::StaticType();
        if (!reusable || m_ShaderGraphDocument->Asset() != asset || !m_ShaderGraphDocument->Publishable())
            throw std::runtime_error("The edited reusable graph is unavailable or invalid.");
        const auto& specification = m_AssetDatabase->Specification();
        WriteBytesAtomically(specification.ProjectRoot / specification.SourceDirectory / record->RelativePath, bytes);
        return;
    }
    if (record->RelativePath.extension() != ".keireshadergraph" || m_ShaderGraphDocument->Asset() != asset ||
        !m_ShaderGraphDocument->Compilation().Succeeded() || m_ShaderGraphDocument->Compilation().Variants.empty())
        throw std::runtime_error("The Shader Graph has no publishable generated shader variants.");

    const auto& specification = m_AssetDatabase->Specification();
    KeireEditor::PublishShaderGraph({.ProjectRoot = specification.ProjectRoot,
                                     .SourceDirectory = specification.SourceDirectory,
                                     .GraphRelativePath = record->RelativePath,
                                     .Asset = asset,
                                     .Variants = m_ShaderGraphDocument->Compilation().Variants,
                                     .GraphBytes = bytes});
}

void EditorWorkspaceLayer::OpenShaderGraph(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    const bool reusable = record && (record->Type == Keire::MaterialFunctionAsset::StaticType() ||
                                     record->Type == Keire::ShaderFunctionAsset::StaticType() ||
                                     record->Type == Keire::MaterialLayerAsset::StaticType() ||
                                     record->Type == Keire::MaterialLayerBlendAsset::StaticType());
    if (!record || (record->Type != Keire::ShaderGraphAsset::StaticType() && !reusable))
        throw std::invalid_argument("Only Shader Graph, function, and material-layer assets can be opened here.");

    m_SelectedAsset = asset;
    const auto& specification = m_AssetDatabase->Specification();
    const auto source = specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
    const auto bytes = ReadBytes(source);
    if (const auto context = m_ShaderGraphDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "Shader Graph: " + record->RelativePath.stem().string(), .MaximumCommands = 128});

    Keire::ShaderGraphCompileOptions options;
    options.GeneratedSource =
        specification.SourceDirectory / "Generated" / "ShaderGraphs" / asset.ToString() / "ShaderGraph.hlsl";
    options.ResolveFunction = [this](const Keire::AssetId dependency) { return ResolveReusableGraph(dependency); };
    const auto allowedRoot = specification.SourceDirectory.lexically_normal();
    const auto projectRoot = specification.ProjectRoot;
    options.ReadInclude = [allowedRoot,
                           projectRoot](const std::filesystem::path& requested) -> std::optional<std::string>
    {
        const auto relative = requested.lexically_normal();
        if (relative.empty() || relative.is_absolute() || !SameOrChild(allowedRoot, relative))
            return std::nullopt;
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(projectRoot / allowedRoot, error);
        if (error)
            return std::nullopt;
        const auto path = std::filesystem::weakly_canonical(projectRoot / relative, error);
        if (error || !SameOrChild(root, path) || !std::filesystem::is_regular_file(path, error) || error)
            return std::nullopt;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > std::uintmax_t{1024} * 1024U)
            return std::nullopt;
        try
        {
            const auto include = ReadBytes(path);
            return std::string(reinterpret_cast<const char*>(include.data()), include.size());
        }
        catch (...)
        {
            return std::nullopt;
        }
    };
    m_ShaderGraphDocument->SetCompileOptions(std::move(options));
    if (++m_ShaderGraphDocumentRevision == 0)
        ++m_ShaderGraphDocumentRevision;
    if (record->Type == Keire::ShaderGraphAsset::StaticType())
        m_ShaderGraphDocument->Open(asset, bytes, m_ShaderGraphDocumentRevision, std::move(context));
    else
    {
        Keire::GraphFunctionDefinition definition;
        if (record->Type == Keire::MaterialFunctionAsset::StaticType())
            definition = Keire::MaterialFunctionAsset::DecodeSource(bytes);
        else if (record->Type == Keire::ShaderFunctionAsset::StaticType())
            definition = Keire::ShaderFunctionAsset::DecodeSource(bytes);
        else if (record->Type == Keire::MaterialLayerAsset::StaticType())
            definition = Keire::MaterialLayerAsset::DecodeSource(bytes);
        else
            definition = Keire::MaterialLayerBlendAsset::DecodeSource(bytes);
        m_ShaderGraphDocument->Open(asset, std::move(definition), m_ShaderGraphDocumentRevision, std::move(context));
    }
    m_ActiveUndoContext = m_ShaderGraphDocument->UndoContext();
    m_ShaderGraphPanel->ResetTransientState();
    m_ShaderGraphPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_ShaderGraphPanel->Registration().SetVisible(true);
    m_ShaderGraphPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::SaveShaderGraph()
{
    if (!m_AssetDatabase || !m_ShaderGraphDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_ShaderGraphDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited Shader Graph no longer exists.");
    m_ShaderGraphDocument->Save();
    if (!m_AssetOperations)
        throw std::runtime_error("The asset worker is unavailable for Shader Graph compilation.");
    m_AssetOperations->QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction,
                                   {.ReloadAsset = m_ShaderGraphDocument->Asset()});
    m_ShaderGraphPanel->SetMessage(m_ShaderGraphDocument->ReusableGraph()
                                       ? "Saved " + record->RelativePath.generic_string() +
                                             "; recompiling dependent graphs..."
                                       : "Saved " + record->RelativePath.generic_string() +
                                             "; compiling and hot-reloading its runtime shader variants...");
}

void EditorWorkspaceLayer::OpenAnimationGraph(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::AnimationGraphAsset::StaticType() ||
        record->RelativePath.extension() != ".keireanimgraph")
    {
        throw std::invalid_argument("Only .keireanimgraph assets can be opened in the Animator Controller editor.");
    }
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    auto definition = Keire::AnimationGraphAsset::Decode(ReadBytes(source))->Definition();
    if (const auto context = m_AnimatorControllerDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
    {
        context = undo->CreateContext(
            {.Name = "Animator Controller: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    }
    m_AnimatorControllerDocument->Open(asset, std::move(definition), std::move(context), source);
    m_ActiveUndoContext = m_AnimatorControllerDocument->UndoContext();
    m_AnimatorControllerPanel->ResetTransientState();
    m_AnimatorControllerPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_AnimatorControllerPanel->Registration().SetVisible(true);
    m_AnimatorControllerPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::SaveAnimationGraph()
{
    if (!m_AssetDatabase || !m_AnimatorControllerDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_AnimatorControllerDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited Animator Controller no longer exists.");
    m_AnimatorControllerDocument->Save();
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_AnimatorControllerDocument->Asset());
    m_AnimatorControllerPanel->SetMessage("Saved and imported " + record->RelativePath.generic_string() + ".");
}

void EditorWorkspaceLayer::OpenInputActions(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->RelativePath.extension() != ".keireinput")
        throw std::invalid_argument("Only .keireinput assets can be opened in the Input Actions editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    auto definition = Keire::InputActionAsset::Decode(ReadBytes(source))->Definition();
    if (const auto context = m_InputActionsDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "Input Actions: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    m_InputActionsDocument->Open(asset, std::move(definition), std::move(context), source);
    if (!m_InputActionsDocument->Definition().ActionMaps.empty())
        m_InputActionsDocument->SelectMap(m_InputActionsDocument->Definition().ActionMaps.front().Id);
    m_ActiveUndoContext = m_InputActionsDocument->UndoContext();
    m_InputActionsPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_InputActionsPanel->ResetTransientState();
    m_InputContext.Reset();
    if (const auto input = Owner().Input(); input && m_EditorInputUser)
        m_InputContext = input->CreateActionContext(asset, m_EditorInputUser, Keire::InputContextRole::EditorControl);
    m_InputActionsPanel->Registration().SetVisible(true);
}

void EditorWorkspaceLayer::SaveInputActions()
{
    if (!m_AssetDatabase || !m_InputActionsDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_InputActionsDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited input asset no longer exists.");
    m_InputActionsDocument->Save();
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_InputActionsDocument->Asset());
    m_InputActionsPanel->SetMessage("Saved and imported " + record->RelativePath.generic_string() + ".");
}

void EditorWorkspaceLayer::RecordInputUndo(const std::string_view name)
{
    m_InputActionsDocument->RecordApplied(name, m_InputActionsDocument->Definition());
}

void EditorWorkspaceLayer::UndoInputEdit() { (void)m_InputActionsDocument->Undo(); }

void EditorWorkspaceLayer::RedoInputEdit() { (void)m_InputActionsDocument->Redo(); }
