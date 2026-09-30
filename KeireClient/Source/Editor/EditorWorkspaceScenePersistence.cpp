#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/EditorWorkspaceLayer.h"
#include "KeireInternal/FileSystem.h"

#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

void EditorWorkspaceLayer::SaveScene()
{
    if (m_PrefabEditingStage)
    {
        SavePrefabEditingStage();
        return;
    }
    if (!m_SceneDocument->EditingScene() || !m_AssetDatabase || !m_SceneDocument->Asset())
        return;
    try
    {
        m_SceneDocument->Save();
        QueueMaterialCatalogRefresh(m_SceneDocument->Asset());
        m_SceneDocument->SetStatus("Scene saved atomically; refreshing runtime content in the background.");
        AddConsoleMessage("Scene", "Saved " + Keire::Detail::PathToUtf8(m_SceneDocument->Source().filename()),
                          m_Theme.Success);
    }
    catch (const std::exception& error)
    {
        m_SceneDocument->SetStatus(std::string("Scene save failed: ") + error.what());
        ReportError("Scene", m_SceneDocument->Status());
    }
}

void EditorWorkspaceLayer::SaveSceneAs()
{
    if (!m_SceneDocument->EditingScene() || !m_AssetDatabase || m_SceneDocument->SaveDialog())
        return;
    const auto assets = m_AssetDatabase->Specification().ProjectRoot / m_AssetDatabase->Specification().SourceDirectory;
    Keire::SaveFileDialogSpecification dialog;
    dialog.Title = "Save Scene As";
    dialog.DefaultLocation = assets / "Scenes";
    dialog.DefaultName = m_SceneDocument->EditingScene()->Name() + " Copy.keirescene";
    dialog.FilterName = "Kéire Scene";
    dialog.Extension = "keirescene";
    m_SceneDocument->SetSaveDialog(Owner().Windows()->ShowSaveFileDialog(Owner().MainWindow()->Id(), dialog));
    m_SceneDocument->SetStatus("Choose a new scene path under this project's Assets directory.");
}

void EditorWorkspaceLayer::CompleteSaveSceneAs()
{
    if (!m_SceneDocument->SaveDialog() ||
        m_SceneDocument->SaveDialog()->Status() == Keire::SaveFileDialogStatus::Pending)
        return;
    const auto operation = m_SceneDocument->TakeSaveDialog();
    if (operation->Status() == Keire::SaveFileDialogStatus::Cancelled)
        return;
    if (operation->Status() == Keire::SaveFileDialogStatus::Failed)
    {
        m_SceneDocument->SetStatus("Save As dialog failed: " + operation->Diagnostic());
        return;
    }
    try
    {
        auto destination = operation->SelectedPath();
        if (destination.extension() != ".keirescene")
            destination += ".keirescene";
        const auto assets = std::filesystem::weakly_canonical(m_AssetDatabase->Specification().ProjectRoot /
                                                              m_AssetDatabase->Specification().SourceDirectory);
        const auto parent = std::filesystem::weakly_canonical(destination.parent_path());
        const auto relativeParent = std::filesystem::relative(parent, assets);
        if (relativeParent.empty() || relativeParent.native().starts_with(std::filesystem::path("..").native()) ||
            destination.filename().empty())
            throw std::invalid_argument("Scene Save As must remain inside the project's Assets directory.");
        if (std::filesystem::exists(destination))
            throw std::invalid_argument("Scene Save As requires a new path and will not overwrite an existing asset.");
        const auto sourceDefinition = m_SceneDocument->EditingScene()->Snapshot();
        auto definition = sourceDefinition;
        definition.Name = Keire::Detail::PathToUtf8(destination.stem());
        const auto bytes = Keire::SceneAsset::Encode(definition);
        const auto relative = relativeParent / destination.filename();
        if (!m_AssetOperations)
            throw std::logic_error("The isolated asset worker is unavailable.");
        m_AssetOperations->QueueCreateAsset(relative, bytes, {},
                                            {.FollowUp = KeireEditor::AssetOperationFollowUp::AdoptSceneCopy,
                                             .UndoName = "Save Scene As",
                                             .SceneSnapshot = definition,
                                             .SourceSceneSnapshot = sourceDefinition,
                                             .SourceSceneAsset = m_SceneDocument->Asset(),
                                             .SceneSource = destination});
        m_SceneDocument->SetStatus("Saving the scene copy in the isolated asset worker.");
    }
    catch (const std::exception& error)
    {
        m_SceneDocument->SetStatus(std::string("Scene Save As failed: ") + error.what());
        ReportError("Scene", m_SceneDocument->Status());
    }
}

void EditorWorkspaceLayer::DrawSceneSourceConflictDialog(Keire::UiFrame& ui)
{
    if (m_SceneDocument->SourceConflict())
        ui.OpenPopup("Scene Source Conflict");
    if (auto popup = ui.BeginPopupModal("Scene Source Conflict"); popup)
    {
        ui.Text("The scene file changed or was deleted outside this document.");
        ui.Text("Reload discards local edits. Save Copy preserves them in a new asset.");
        ui.Text("Overwrite replaces the external version with your current scene.");
        try
        {
            if (ui.Button("Reload"))
            {
                m_SceneDocument->ReloadSource();
                m_InspectorPanel->ClearSceneState();
                QueueMaterialCatalogRefresh(m_SceneDocument->Asset());
                m_SceneDocument->SetStatus("Reloaded the current scene source.");
                ui.CloseCurrentPopup();
            }
            ui.SameLine();
            if (ui.Button("Save Copy"))
            {
                SaveSceneAs();
                m_SceneDocument->DismissSourceConflict();
                ui.CloseCurrentPopup();
            }
            ui.SameLine();
            if (ui.Button("Overwrite"))
            {
                m_SceneDocument->Save(true);
                QueueMaterialCatalogRefresh(m_SceneDocument->Asset());
                m_SceneDocument->SetStatus("Scene source explicitly overwritten; refreshing runtime content.");
                ui.CloseCurrentPopup();
            }
        }
        catch (const std::exception& error)
        {
            m_SceneDocument->SetStatus(std::string("Scene conflict resolution failed: ") + error.what());
            ReportError("Scene", m_SceneDocument->Status());
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_SceneDocument->DismissSourceConflict();
            ui.CloseCurrentPopup();
        }
        ui.TextColored(m_Theme.MutedText, m_SceneDocument->Status());
    }
}
