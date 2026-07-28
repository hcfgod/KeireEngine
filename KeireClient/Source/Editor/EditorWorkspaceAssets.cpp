#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AnimatorControllerPanel.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/AudioMixerPanel.h"
#include "KeireClient/Editor/ConsolePanel.h"
#include "KeireClient/Editor/DiagnosticsPanel.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
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
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
namespace
{
    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open input action asset: " + path.string());
        const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        std::vector<std::byte> bytes(characters.size());
        std::ranges::transform(characters, bytes.begin(), [](const char value) { return std::byte(value); });
        return bytes;
    }

    [[nodiscard]] std::string FormatAssetDiagnostic(const Keire::AssetImportDiagnostic& diagnostic)
    {
        auto result = diagnostic.RelativePath.generic_string();
        if (diagnostic.Line != 0)
        {
            result += ':' + std::to_string(diagnostic.Line);
            if (diagnostic.Column != 0)
                result += ':' + std::to_string(diagnostic.Column);
        }
        if (!result.empty())
            result += ": ";
        result += diagnostic.Message;
        return result;
    }

    void WriteBytesAtomically(const std::filesystem::path& path, const std::span<const std::byte> bytes)
    {
        const std::string text =
            bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::Detail::WriteTextFileAtomically(path, text);
    }

    [[nodiscard]] bool IsCSharpIdentifier(const std::string_view value)
    {
        return !value.empty() && (std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_') &&
               std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                   { return std::isalnum(character) || character == '_'; });
    }

    [[nodiscard]] std::vector<std::byte> TextBytes(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text));
        return {bytes.begin(), bytes.end()};
    }

    [[nodiscard]] bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate)
    {
        const auto relative = candidate.lexically_normal().lexically_relative(parent.lexically_normal());
        return relative.empty() || (!relative.is_absolute() && !relative.generic_string().starts_with(".."));
    }

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

const Keire::UiThemeDefinition& EditorWorkspaceLayer::AssetBrowserTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::AssetBrowserDatabase() const noexcept { return m_AssetDatabase; }

Keire::Ref<Keire::AssetSystem> EditorWorkspaceLayer::AssetBrowserAssets() const noexcept { return Owner().Assets(); }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::AssetBrowserRecords() const noexcept
{
    return m_AssetRecords;
}

std::string_view EditorWorkspaceLayer::AssetBrowserStatus() const noexcept { return m_AssetStatus; }

Keire::AssetId EditorWorkspaceLayer::AssetBrowserSceneAsset() const noexcept { return m_SceneDocument->Asset(); }

bool EditorWorkspaceLayer::AssetBrowserSceneDirty() const noexcept { return m_SceneDocument->Dirty(); }

bool EditorWorkspaceLayer::AssetBrowserImportPending() const noexcept
{
    return m_ExternalAssetImport && m_ExternalAssetImport->Pending();
}

std::vector<Keire::ManagedAssetTypeDescriptor> EditorWorkspaceLayer::AssetBrowserManagedAssetTypes() const
{
    const auto scripts = Owner().Scripts();
    return scripts && scripts->RuntimeHostAvailable() ? scripts->ManagedAssetTypes()
                                                      : std::vector<Keire::ManagedAssetTypeDescriptor>{};
}

void EditorWorkspaceLayer::RefreshAssetBrowserRecords()
{
    if (m_AssetDatabase)
        m_AssetRecords = m_AssetDatabase->Records();
}

void EditorWorkspaceLayer::SetAssetBrowserSelected(const Keire::AssetId asset) noexcept { m_SelectedAsset = asset; }

void EditorWorkspaceLayer::ClearAssetBrowserSceneSelection() noexcept { m_SceneDocument->ClearSelection(); }

void EditorWorkspaceLayer::SetAssetBrowserStatus(std::string status) noexcept { m_AssetStatus = std::move(status); }

void EditorWorkspaceLayer::ReportAssetBrowserError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::ImportAssetBrowserAssets() { ImportAssets(); }

void EditorWorkspaceLayer::RequestAssetBrowserCreateScene() { RequestCreateScene(); }

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
            throw std::runtime_error("Wait for the active asset operation before creating managed data.");
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

void EditorWorkspaceLayer::CreateAssetBrowserShader() { CreateUnlitShader(); }

void EditorWorkspaceLayer::CreateAssetBrowserInputActions(Keire::InputActionAssetDefinition definition,
                                                          const std::string_view baseName)
{
    CreateInputActions(std::move(definition), baseName);
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
        m_AssetRecords = m_AssetDatabase->Records();
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
                    [this] { return m_AssetOperations && !m_AssetOperations->Busy(); }));
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
        target =
            KeireEditor::PickSceneEntity(ActiveScene(), viewportRect, position, m_SceneViewportPanel->LastCamera());

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
                    catch (const std::invalid_argument& error)
                    {
                        if (record->Type == Keire::MeshAsset::StaticType() ||
                            record->Type == Keire::MaterialAsset::StaticType())
                        {
                            m_AssetStatus = "Create or open a scene before dropping meshes or materials.";
                            m_Notice = error.what();
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
                    record->Type == Keire::MaterialAsset::StaticType())
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
            m_AssetRecords = m_AssetDatabase->Records();
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
                                    sizeof(*state),
                                    [this] { return m_AssetOperations && !m_AssetOperations->Busy(); }));
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
                                sizeof(*state), [this] { return m_AssetOperations && !m_AssetOperations->Busy(); }));
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
            if (m_PendingStartupScene)
            {
                const auto startup = std::exchange(m_PendingStartupScene, {});
                RequestOpenScene(startup);
            }
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
    m_AssetRecords = m_AssetDatabase->Records();
    if (!result.CatalogPath.empty())
    {
        if (const auto assets = Owner().Assets())
        {
            (void)assets->Unmount(result.CatalogPath);
            assets->Mount({result.CatalogPath, 0, true});
            if (reloadLoadedAssets)
            {
                if (reloadAsset)
                    (void)assets->Reload(reloadAsset, Keire::AssetPriority::Background);
                else
                    for (const auto& record : m_AssetRecords)
                        (void)assets->Reload(record.Id, Keire::AssetPriority::Background);
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
            profile.Roots.push_back(project->Descriptor().StartupScene);
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

void EditorWorkspaceLayer::CreateInputActions(Keire::InputActionAssetDefinition definition,
                                              const std::string_view baseName)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return;
    try
    {
        if (m_AssetOperations->Busy())
            throw std::runtime_error("Wait for the active asset operation before creating input actions.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        auto destination = directory / (std::string(baseName) + ".keireinput");
        for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
            destination = directory / (std::string(baseName) + " " + std::to_string(copy) + ".keireinput");
        definition.Name = destination.stem().string();
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::InputActionAsset::Encode(definition), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenInputActions, .UndoName = "Create Input Actions"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Input asset creation failed: ") + error.what());
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
            throw std::runtime_error("Wait for the active asset operation before creating a script.");
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
            throw std::runtime_error("Wait for the active asset operation before creating an assembly.");
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
            throw std::runtime_error("Wait for the active asset operation before creating an Audio Mixer.");
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
            throw std::runtime_error("Wait for the active asset operation before creating a Physics Material.");
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
            throw std::runtime_error("Wait for the active asset operation before creating a VFX Effect.");
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

bool EditorWorkspaceLayer::CreatePrefabFromSelection(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations || !m_SceneDocument)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            throw std::runtime_error("Wait for the active asset operation before creating a prefab.");
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
            throw std::runtime_error("Wait for the active asset operation before creating a prefab variant.");
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
        throw std::runtime_error("Wait for the active asset operation before creating a prefab.");

    const auto source = Keire::PrefabAsset::Encode(definition);
    const auto created =
        m_AssetDatabase->CreateAsset(destination, Keire::CreatePrefabAssetImporter(), std::span(source));
    if (const auto assets = Owner().Assets())
        (void)assets->PublishDevelopmentAsset(created, Keire::CreateRef<Keire::PrefabAsset>(definition));
    m_AssetRecords = m_AssetDatabase->Records();
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
                [this] { return m_AssetOperations && !m_AssetOperations->Busy(); }));
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
    m_AssetRecords = m_AssetDatabase->Records();
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

void EditorWorkspaceLayer::CreateUnlitShader()
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return;
    std::filesystem::path manifest;
    std::filesystem::path hlsl;
    try
    {
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        std::string baseName = "UnlitShader";
        for (std::size_t copy = 2;; ++copy)
        {
            manifest = directory / (baseName + ".keireshader");
            hlsl = directory / (baseName + ".hlsl");
            if (!m_AssetDatabase->Find(manifest) &&
                !std::filesystem::exists(m_AssetDatabase->Specification().ProjectRoot / "Assets" / hlsl))
                break;
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
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Shader creation failed: ") + error.what());
    }
}

bool EditorWorkspaceLayer::CreateMaterial(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            throw std::runtime_error("Wait for the active asset operation before creating a material.");
        Keire::AssetId shader;
        if (const auto selected = m_AssetDatabase->Find(m_SelectedAsset);
            selected && selected->Type == Keire::ShaderAsset::StaticType())
            shader = selected->Id;
        if (!shader)
        {
            const auto records = m_AssetDatabase->Records();
            const auto found =
                std::ranges::find(records, Keire::ShaderAsset::StaticType(), &Keire::AssetSourceRecord::Type);
            if (found != records.end())
                shader = found->Id;
        }

        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Material name must be one non-empty path component.");
        const auto destination = directory / (std::string(name) + ".keirematerial");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A material with that name already exists in this folder.");
        const std::string source =
            "{\n  \"schemaVersion\": 1,\n  \"shader\": " + (shader ? "\"" + shader.ToString() + "\"" : "null") +
            ",\n  \"properties\": { \"Tint\": [1.0, 1.0, 1.0, 1.0] }\n}\n";
        const auto sourceBytes = std::as_bytes(std::span(source));
        m_AssetOperations->QueueCreateAsset(
            destination, std::vector<std::byte>(sourceBytes.begin(), sourceBytes.end()), {},
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
            throw std::runtime_error("Wait for the active asset operation before creating an Animator Controller.");
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

KeireEditor::AudioMixerDocument& EditorWorkspaceLayer::AudioMixerState() noexcept { return *m_AudioMixerDocument; }

const Keire::UiThemeDefinition& EditorWorkspaceLayer::AudioMixerTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::AudioMixerDatabase() const noexcept { return m_AssetDatabase; }

std::string_view EditorWorkspaceLayer::AudioMixerPreviewDiagnostic() const noexcept
{
    return m_AudioMixerPreviewDiagnostic;
}

void EditorWorkspaceLayer::ActivateAudioMixerHistory() noexcept
{
    m_ActiveUndoContext = m_AudioMixerDocument->UndoContext();
}

void EditorWorkspaceLayer::SaveAudioMixerDocument() { SaveAudioMixer(); }

void EditorWorkspaceLayer::DiscardAudioMixerDocument()
{
    m_AudioMixerDocument->Discard();
    m_AudioMixerPanel->SetMessage("Discarded unsaved Audio Mixer changes.");
}

void EditorWorkspaceLayer::ReloadAudioMixerDocument(const Keire::AssetId asset)
{
    if (!m_AssetDatabase || asset != m_AudioMixerDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::AudioMixerAsset::StaticType() ||
        record->RelativePath.extension() != ".keiremixer")
        throw std::invalid_argument("Only .keiremixer assets can be reloaded in the Audio Mixer editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    if (++m_AudioMixerDocumentRevision == 0)
        ++m_AudioMixerDocumentRevision;
    const auto result = m_AudioMixerDocument->Reload(ReadBytes(source), m_AudioMixerDocumentRevision);
    switch (result)
    {
    case KeireEditor::AssetDocumentReloadResult::Applied:
        m_AudioMixerPanel->SetMessage("Reloaded " + record->RelativePath.generic_string() + ".");
        break;
    case KeireEditor::AssetDocumentReloadResult::Unchanged:
        m_AudioMixerPanel->SetMessage("Audio Mixer source is unchanged.");
        break;
    case KeireEditor::AssetDocumentReloadResult::LocalChanges:
        m_AudioMixerPanel->SetMessage("Reload skipped because the Audio Mixer has unsaved local changes.");
        break;
    }
}

void EditorWorkspaceLayer::UndoAudioMixerEdit() { (void)m_AudioMixerDocument->Undo(); }

void EditorWorkspaceLayer::RedoAudioMixerEdit() { (void)m_AudioMixerDocument->Redo(); }

void EditorWorkspaceLayer::StopAudioMixerPreview() noexcept { m_AudioMixerPreviewAsset = {}; }

void EditorWorkspaceLayer::ReportAudioMixerError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::PreviewAudioMixer(const Keire::AssetId asset, const Keire::AudioMixerDefinition& definition)
{
    (void)definition;
    m_AudioMixerPreviewAsset = asset;
    m_AudioMixerPreviewDiagnostic =
        "Live DSP audition is unavailable: device voices are not yet routed through authored mixer buses. "
        "Document edits remain transient and never dirty scene state.";
}

void EditorWorkspaceLayer::PersistAudioMixer(const Keire::AssetId asset, const std::span<const std::byte> bytes)
{
    if (!m_AssetDatabase)
        throw std::runtime_error("The Asset Database is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::AudioMixerAsset::StaticType() ||
        record->RelativePath.extension() != ".keiremixer")
        throw std::runtime_error("The edited Audio Mixer source is unavailable.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    WriteBytesAtomically(source, bytes);
}

void EditorWorkspaceLayer::OpenAudioMixer(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::AudioMixerAsset::StaticType() ||
        record->RelativePath.extension() != ".keiremixer")
        throw std::invalid_argument("Only .keiremixer assets can be opened in the Audio Mixer editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    const auto bytes = ReadBytes(source);
    if (const auto context = m_AudioMixerDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "Audio Mixer: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    if (++m_AudioMixerDocumentRevision == 0)
        ++m_AudioMixerDocumentRevision;
    m_AudioMixerDocument->Open(asset, bytes, m_AudioMixerDocumentRevision, std::move(context));
    m_ActiveUndoContext = m_AudioMixerDocument->UndoContext();
    m_AudioMixerPanel->ResetTransientState();
    m_AudioMixerPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_AudioMixerPanel->Registration().SetVisible(true);
    m_AudioMixerPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::SaveAudioMixer()
{
    if (!m_AssetDatabase || !m_AudioMixerDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_AudioMixerDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited Audio Mixer no longer exists.");
    m_AudioMixerDocument->Save();
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_AudioMixerDocument->Asset());
    m_AudioMixerPanel->SetMessage("Saved and queued import for " + record->RelativePath.generic_string() + ".");
}

KeireEditor::VfxEffectDocument& EditorWorkspaceLayer::VfxEffectState() noexcept { return *m_VfxEffectDocument; }

const Keire::UiThemeDefinition& EditorWorkspaceLayer::VfxEffectTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::VfxEffectDatabase() const noexcept { return m_AssetDatabase; }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::VfxEffectAssetRecords() const noexcept
{
    return m_AssetRecords;
}

std::string_view EditorWorkspaceLayer::VfxEffectPreviewDiagnostic() const noexcept
{
    return m_VfxEffectPreviewDiagnostic;
}

void EditorWorkspaceLayer::ActivateVfxEffectHistory() noexcept
{
    m_ActiveUndoContext = m_VfxEffectDocument->UndoContext();
}

void EditorWorkspaceLayer::SaveVfxEffectDocument() { SaveVfxEffect(); }

void EditorWorkspaceLayer::DiscardVfxEffectDocument()
{
    m_VfxEffectDocument->Discard();
    m_VfxEffectPanel->SetMessage("Discarded unsaved VFX Effect changes.");
}

void EditorWorkspaceLayer::ReloadVfxEffectDocument(const Keire::AssetId asset)
{
    if (!m_AssetDatabase || asset != m_VfxEffectDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::VfxEffectAsset::StaticType() ||
        record->RelativePath.extension() != ".keirevfx")
        throw std::invalid_argument("Only .keirevfx assets can be reloaded in the VFX Effect editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    if (++m_VfxEffectDocumentRevision == 0)
        ++m_VfxEffectDocumentRevision;
    const auto result = m_VfxEffectDocument->Reload(ReadBytes(source), m_VfxEffectDocumentRevision);
    switch (result)
    {
    case KeireEditor::AssetDocumentReloadResult::Applied:
        m_VfxEffectPanel->SetMessage("Reloaded " + record->RelativePath.generic_string() + ".");
        break;
    case KeireEditor::AssetDocumentReloadResult::Unchanged:
        m_VfxEffectPanel->SetMessage("VFX Effect source is unchanged.");
        break;
    case KeireEditor::AssetDocumentReloadResult::LocalChanges:
        m_VfxEffectPanel->SetMessage("Reload skipped because the VFX Effect has unsaved local changes.");
        break;
    }
}

void EditorWorkspaceLayer::UndoVfxEffectEdit() { (void)m_VfxEffectDocument->Undo(); }

void EditorWorkspaceLayer::RedoVfxEffectEdit() { (void)m_VfxEffectDocument->Redo(); }

void EditorWorkspaceLayer::RevealVfxEffectAsset(const Keire::AssetId asset)
{
    if (!asset || !m_AssetBrowserPanel)
        return;
    m_SelectedAsset = asset;
    m_AssetBrowserPanel->RevealAsset(asset);
    m_AssetBrowserPanel->Registration().SetVisible(true);
    m_AssetBrowserPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::StopVfxEffectPreview() noexcept { m_VfxEffectPreviewAsset = {}; }

void EditorWorkspaceLayer::ReportVfxEffectError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::PreviewVfxEffect(const Keire::AssetId asset, const Keire::VfxEffectDefinition& definition)
{
    (void)definition;
    m_VfxEffectPreviewAsset = asset;
    m_VfxEffectPreviewDiagnostic =
        "Live VFX preview is unavailable because no transient editor VFX world is connected. "
        "Document edits remain isolated and never spawn or dirty scene effects.";
}

void EditorWorkspaceLayer::PersistVfxEffect(const Keire::AssetId asset, const std::span<const std::byte> bytes)
{
    if (!m_AssetDatabase)
        throw std::runtime_error("The Asset Database is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::VfxEffectAsset::StaticType() ||
        record->RelativePath.extension() != ".keirevfx")
        throw std::runtime_error("The edited VFX Effect source is unavailable.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    WriteBytesAtomically(source, bytes);
}

void EditorWorkspaceLayer::OpenVfxEffect(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::VfxEffectAsset::StaticType() ||
        record->RelativePath.extension() != ".keirevfx")
        throw std::invalid_argument("Only .keirevfx assets can be opened in the VFX Effect editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    const auto bytes = ReadBytes(source);
    if (const auto context = m_VfxEffectDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "VFX Effect: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    if (++m_VfxEffectDocumentRevision == 0)
        ++m_VfxEffectDocumentRevision;
    m_VfxEffectDocument->Open(asset, bytes, m_VfxEffectDocumentRevision, std::move(context));
    m_ActiveUndoContext = m_VfxEffectDocument->UndoContext();
    m_VfxEffectPanel->ResetTransientState();
    m_VfxEffectPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_VfxEffectPanel->Registration().SetVisible(true);
    m_VfxEffectPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::SaveVfxEffect()
{
    if (!m_AssetDatabase || !m_VfxEffectDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_VfxEffectDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited VFX Effect no longer exists.");
    m_VfxEffectDocument->Save();
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_VfxEffectDocument->Asset());
    m_VfxEffectPanel->SetMessage("Saved and queued import for " + record->RelativePath.generic_string() + ".");
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
        m_InputContext = input->CreateActionContext(asset, m_EditorInputUser);
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
