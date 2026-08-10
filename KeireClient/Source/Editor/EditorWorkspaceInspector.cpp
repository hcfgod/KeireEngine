#include "KeireClient/EditorWorkspaceLayer.h"

#include <cctype>

#include "Keire/Audio/AudioAssets.h"
#include "Keire/Scripting/ManagedDataAsset.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/ConsolePanel.h"
#include "KeireClient/Editor/DiagnosticsPanel.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ScenePlayChanges.h"
#include "KeireClient/Editor/ScenePlayChangesPanel.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
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
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
void EditorWorkspaceLayer::DrawProject(Keire::UiFrame& ui)
{
    if (m_AssetBrowserPanel)
        m_AssetBrowserPanel->Draw(ui);
}

KeireEditor::SceneDocument& EditorWorkspaceLayer::InspectorSceneDocument() noexcept { return *m_SceneDocument; }

KeireEditor::InputActionsDocument& EditorWorkspaceLayer::InspectorInputDocument() noexcept
{
    return *m_InputActionsDocument;
}

KeireEditor::MaterialDocument& EditorWorkspaceLayer::InspectorMaterialDocument() noexcept
{
    return *m_MaterialDocument;
}

KeireEditor::PropertyDrawerRegistry& EditorWorkspaceLayer::InspectorPropertyDrawers() noexcept
{
    return *m_PropertyDrawers;
}

const Keire::UiThemeDefinition& EditorWorkspaceLayer::InspectorTheme() const noexcept { return m_Theme; }

std::span<const std::string> EditorWorkspaceLayer::InspectorLayerNames() const noexcept
{
    return m_ProjectSettingsDocument->AuthoringSettings().PhysicsLayerNames;
}

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::InspectorAssetDatabase() const noexcept
{
    return m_AssetDatabase;
}

Keire::Ref<Keire::AssetSystem> EditorWorkspaceLayer::InspectorAssetSystem() const noexcept { return Owner().Assets(); }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::InspectorAssetRecords() const noexcept
{
    return m_AssetRecords;
}

Keire::AssetId EditorWorkspaceLayer::InspectorSelectedAsset() const noexcept { return m_SelectedAsset; }

std::string_view EditorWorkspaceLayer::InspectorAssetStatus() const noexcept { return m_AssetStatus; }

std::vector<Keire::ManagedAssetTypeDescriptor> EditorWorkspaceLayer::InspectorManagedAssetTypes() const
{
    const auto scripts = Owner().Scripts();
    return scripts && scripts->RuntimeHostAvailable() ? scripts->ManagedAssetTypes()
                                                      : std::vector<Keire::ManagedAssetTypeDescriptor>{};
}

Keire::Ref<Keire::UndoContext> EditorWorkspaceLayer::InspectorManagedDataHistory() const noexcept
{
    return m_ManagedDataUndoContext;
}

bool EditorWorkspaceLayer::InspectorPlayModeActive() const noexcept
{
    const auto play = m_SceneDocument ? m_SceneDocument->PlaySession() : Keire::Ref<Keire::SceneRuntimeSession>{};
    return play && play->State() != Keire::ScenePlayState::Stopped;
}

void EditorWorkspaceLayer::SetInspectorSelectedAsset(const Keire::AssetId asset) noexcept
{
    if (asset != m_SelectedAsset)
        StopInspectorAudioPreview();
    m_SelectedAsset = asset;
}

void EditorWorkspaceLayer::PreviewInspectorAudio(const Keire::AssetId asset)
{
    const auto audio = Owner().Audio();
    const auto assets = Owner().Assets();
    if (!audio || !audio->IsOpen() || !assets || !asset)
        throw std::logic_error("Audio preview services are unavailable.");
    StopInspectorAudioPreview();
    const auto handle = assets->Load<Keire::AudioClipAsset>(asset, Keire::AssetPriority::High);
    const auto clip = handle.Get();
    if (!clip)
        throw std::runtime_error("The selected audio clip could not be loaded.");
    Keire::AudioPlaybackRequest request;
    request.Clip = clip->Clip();
    request.Bus = "EditorPreview";
    request.Spatial = false;
    request.Priority = 255;
    m_InspectorAudioPreviewVoice = audio->Play(std::move(request));
}

void EditorWorkspaceLayer::StopInspectorAudioPreview() noexcept
{
    try
    {
        if (m_InspectorAudioPreviewVoice)
        {
            if (const auto audio = Owner().Audio(); audio && audio->IsOpen())
                (void)audio->Stop(m_InspectorAudioPreviewVoice);
        }
    }
    catch (...)
    {
    }
    m_InspectorAudioPreviewVoice = {};
}

void EditorWorkspaceLayer::ActivateInspectorHistory() noexcept { m_ActiveUndoContext = m_SceneDocument->History(); }

void EditorWorkspaceLayer::ActivateInspectorManagedDataHistory() noexcept
{
    m_ActiveUndoContext = m_ManagedDataUndoContext;
}

void EditorWorkspaceLayer::RecordInspectorUndo(const std::string_view name, std::string mergeKey)
{
    RecordSceneUndo(name, std::move(mergeKey));
}

void EditorWorkspaceLayer::NotifyInspectorMaterialAssigned(const Keire::AssetId material)
{
    if (!material || !m_AssetDatabase || !m_ShaderGraphDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_ShaderGraphDocument->Asset());
    if (record && record->Type == Keire::ShaderGraphAsset::StaticType() &&
        std::ranges::find(record->SubAssets, material) != record->SubAssets.end())
    {
        m_ShaderGraphDocument->ApplyLiveRevision();
    }
}

void EditorWorkspaceLayer::AddScriptToEntity(const Keire::EntityId entity, const Keire::AssetId script)
{
    if (!m_AssetDatabase)
        throw std::logic_error("The asset database is unavailable.");
    const auto record = m_AssetDatabase->Find(script);
    if (!record)
        throw std::invalid_argument("Only C# script assets can be attached to GameObjects.");
    auto sourcePath = record->RelativePath;
    auto extension = sourcePath.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension == ".keiremeta")
    {
        sourcePath = sourcePath.stem();
        extension = sourcePath.extension().string();
        std::ranges::transform(extension, extension.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
    }
    if (extension != ".cs")
        return;
    const auto scripts = Owner().Scripts();
    if (!scripts || !scripts->RuntimeHostAvailable())
        throw std::logic_error("The managed scripting runtime is unavailable.");

    const auto scriptName = sourcePath.stem().string();
    const auto types = scripts->BehaviourTypes();
    const auto matches = std::ranges::count(types, scriptName, &Keire::ManagedBehaviourTypeDescriptor::DisplayName);
    if (matches == 0)
    {
        const auto build = scripts->BuildStatus();
        if (!m_ResolvingPendingScriptAttachments)
        {
            const auto attachment = std::pair{entity, script};
            if (std::ranges::find(m_PendingScriptAttachments, attachment) == m_PendingScriptAttachments.end())
                m_PendingScriptAttachments.push_back(attachment);
            const bool building = build.State == Keire::ManagedBuildState::Generating ||
                                  build.State == Keire::ManagedBuildState::Compiling ||
                                  build.State == Keire::ManagedBuildState::Publishing;
            if (!building)
                m_ManagedBuildDebounceSeconds = 0.0;
            m_SceneDocument->SetStatus("Queued " + scriptName + " for attachment after managed compilation.");
            return;
        }
        throw std::invalid_argument("The script must contain one public Behaviour whose type name matches the file "
                                    "name and declares StableComponentId.");
    }
    if (matches != 1)
        throw std::invalid_argument("More than one loaded Behaviour matches this script file name.");
    const auto type = std::ranges::find(types, scriptName, &Keire::ManagedBehaviourTypeDescriptor::DisplayName);

    RecordSceneUndo("Add " + scriptName);
    const auto component = m_SceneDocument->AddComponent(entity, type->ComponentType);
    if (!component)
        throw std::logic_error("The script is already attached or its component requirements are not satisfied.");
    m_SceneDocument->SetStatus("Attached " + scriptName + " to the selected GameObject.");
}

void EditorWorkspaceLayer::CommitInspectorMaterial() { CommitMaterialDraft(); }

void EditorWorkspaceLayer::OpenInspectorInputActions(const Keire::AssetId asset) { OpenInputActions(asset); }

void EditorWorkspaceLayer::ImportInspectorAssets() { ImportAssets(); }

void EditorWorkspaceLayer::PreviewInspectorManagedData(const Keire::AssetId asset,
                                                       const Keire::ManagedDataDefinition& definition)
{
    Keire::ManagedDataAsset::Validate(definition);
    const auto assets = Owner().Assets();
    if (!assets || !assets->PublishDevelopmentAsset(asset, Keire::CreateRef<Keire::ManagedDataAsset>(definition)))
        throw std::runtime_error("The managed data preview could not be published.");
    SetInspectorAssetStatus("Previewing managed data changes live.");
}

void EditorWorkspaceLayer::PersistInspectorManagedData(const Keire::AssetId asset,
                                                       const std::span<const std::byte> bytes)
{
    if (!m_AssetDatabase)
        throw std::logic_error("The asset database is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::ManagedDataAsset::StaticType())
        throw std::invalid_argument("Cannot save an unknown managed data asset.");
    const auto decoded = Keire::ManagedDataAsset::Decode(bytes);
    const auto sourceRoot =
        m_AssetDatabase->Specification().ProjectRoot / m_AssetDatabase->Specification().SourceDirectory;
    const std::string contents =
        bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Keire::Detail::WriteTextFileAtomically(sourceRoot / record->RelativePath, contents);
    const auto assets = Owner().Assets();
    const bool published = assets && assets->PublishDevelopmentAsset(
                                         asset, Keire::CreateRef<Keire::ManagedDataAsset>(decoded->Definition()));
    ImportAssets(KeireEditor::AssetOperationPriority::AutomaticRefresh);
    SetInspectorAssetStatus(published ? "Saved managed data and hot-applied the new revision."
                                      : "Saved managed data; the imported revision will apply after refresh.");
}

void EditorWorkspaceLayer::RenameInspectorAsset(const Keire::AssetId asset, const std::string_view name)
{
    if (!m_AssetOperations)
        throw std::logic_error("The isolated asset worker is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record)
        throw std::invalid_argument("Cannot rename an unknown asset.");
    m_AssetOperations->QueueMutation({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                      .Asset = asset,
                                      .Destination = record->RelativePath.parent_path() / std::string(name)});
}

void EditorWorkspaceLayer::DuplicateInspectorAsset(const Keire::AssetId asset, const std::filesystem::path& destination)
{
    if (!m_AssetOperations)
        throw std::logic_error("The isolated asset worker is unavailable.");
    m_AssetOperations->QueueMutation(
        {.Kind = Keire::Detail::AssetWorkerMutationKind::DuplicateAsset, .Asset = asset, .Destination = destination},
        {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Duplicate Asset"});
}

void EditorWorkspaceLayer::TrashInspectorAsset(const Keire::AssetId asset)
{
    if (!m_AssetOperations)
        throw std::logic_error("The isolated asset worker is unavailable.");
    m_AssetOperations->QueueMutation({.Kind = Keire::Detail::AssetWorkerMutationKind::TrashAsset, .Asset = asset},
                                     {.UndoName = "Trash Asset"});
}

void EditorWorkspaceLayer::SetInspectorAssetStatus(std::string status) noexcept { m_AssetStatus = std::move(status); }

void EditorWorkspaceLayer::ReportInspectorAssetError(std::string message) noexcept
{
    SetAssetError(std::move(message));
}

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::ProjectSettingsAssetRecords() const noexcept
{
    return m_AssetRecords;
}

void EditorWorkspaceLayer::RevealProjectSettingsAsset(const Keire::AssetId asset)
{
    if (!asset || !m_AssetBrowserPanel)
        return;
    m_SelectedAsset = asset;
    m_AssetBrowserPanel->RevealAsset(asset);
    m_AssetBrowserPanel->Registration().SetVisible(true);
}

KeireEditor::ManagedSdkPreference EditorWorkspaceLayer::ProjectManagedSdk() const
{
    const auto scripts = Owner().Scripts();
    if (!scripts)
        return {};
    const auto configuration = scripts->SdkConfiguration();
    return {configuration.Selection, configuration.CustomExecutable};
}

void EditorWorkspaceLayer::SetProjectManagedSdk(KeireEditor::ManagedSdkPreference preference)
{
    if (const auto scripts = Owner().Scripts())
        scripts->ConfigureManagedSdk(preference.Selection, std::move(preference.CustomExecutable));
}

void EditorWorkspaceLayer::ApplyProjectAuthoringSettings(const Keire::ProjectAuthoringSettings& settings)
{
    Keire::ValidateProjectAuthoringSettings(settings);
    if (const auto physics = Owner().Physics())
        physics->ConfigureCollisionMatrix(settings.PhysicsCollisionMatrix);
}
