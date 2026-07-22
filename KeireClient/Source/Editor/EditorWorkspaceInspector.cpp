#include "KeireClient/EditorWorkspaceLayer.h"

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

void EditorWorkspaceLayer::SetInspectorSelectedAsset(const Keire::AssetId asset) noexcept { m_SelectedAsset = asset; }

void EditorWorkspaceLayer::ActivateInspectorHistory() noexcept { m_ActiveUndoContext = m_SceneDocument->History(); }

void EditorWorkspaceLayer::RecordInspectorUndo(const std::string_view name, std::string mergeKey)
{
    RecordSceneUndo(name, std::move(mergeKey));
}

void EditorWorkspaceLayer::CommitInspectorMaterial() { CommitMaterialDraft(); }

void EditorWorkspaceLayer::OpenInspectorInputActions(const Keire::AssetId asset) { OpenInputActions(asset); }

void EditorWorkspaceLayer::ImportInspectorAssets() { ImportAssets(); }

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
