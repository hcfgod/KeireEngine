#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/SceneTransitionCoordinator.h"

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
namespace
{
    const Keire::UiLayoutInfo* ActiveLayout(const std::vector<Keire::UiLayoutInfo>& layouts)
    {
        const auto found = std::ranges::find(layouts, true, &Keire::UiLayoutInfo::Active);
        return found == layouts.end() ? nullptr : &*found;
    }

    const Keire::UiThemeInfo* ActiveTheme(const std::vector<Keire::UiThemeInfo>& themes)
    {
        const auto found = std::ranges::find(themes, true, &Keire::UiThemeInfo::Active);
        return found == themes.end() ? nullptr : &*found;
    }

} // namespace

void EditorWorkspaceLayer::DrawEmptyState(Keire::UiFrame& ui, const std::string_view heading,
                                          const std::string_view primary, const std::string_view detail)
{
    ui.TextColored({0.30F, 0.55F, 1.0F, 1.0F}, heading);
    ui.Separator();
    ui.Spacing();
    ui.Text(primary);
    ui.Spacing();
    ui.TextColored({0.61F, 0.65F, 0.72F, 1.0F}, detail);
}

void EditorWorkspaceLayer::DrawPanelMenuItem(Keire::UiFrame& ui, Keire::UiPanelRegistration& panel)
{
    if (ui.MenuItem(panel.Title(), panel.Visible()))
        panel.SetVisible(!panel.Visible());
}

void EditorWorkspaceLayer::DrawMainMenu(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    const auto scene = ActiveScene();
    if (auto menuBar = ui.BeginMainMenuBar(); menuBar)
    {
        if (auto file = ui.BeginMenu("File"); file)
        {
            if (ui.MenuItem("New Scene", false, m_CommandRouter->Available(KeireEditor::EditorCommand::NewScene)))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::NewScene);
            if (ui.MenuItem("Save Scene", false, m_CommandRouter->Available(KeireEditor::EditorCommand::SaveScene)))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::SaveScene);
            if (ui.MenuItem("Save Scene As...", false,
                            m_CommandRouter->Available(KeireEditor::EditorCommand::SaveSceneAs)))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::SaveSceneAs);
            if (ui.MenuItem("Close Scene", false, m_CommandRouter->Available(KeireEditor::EditorCommand::CloseScene)))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::CloseScene);
            ui.Separator();
            if (ui.MenuItem("Exit"))
            {
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Exit);
            }
        }
        if (auto edit = ui.BeginMenu("Edit"); edit)
        {
            const bool canUndo = m_ActiveUndoContext && m_ActiveUndoContext->CanUndo();
            const bool canRedo = m_ActiveUndoContext && m_ActiveUndoContext->CanRedo();
            const auto undoLabel = canUndo ? "Undo " + m_ActiveUndoContext->UndoName() : "Undo";
            const auto redoLabel = canRedo ? "Redo " + m_ActiveUndoContext->RedoName() : "Redo";
            if (ui.MenuItem(undoLabel, false, canUndo))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Undo);
            if (ui.MenuItem(redoLabel, false, canRedo))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Redo);
            ui.Separator();
            ui.TextColored(m_Theme.MutedText,
                           m_ActiveUndoContext ? std::string(m_ActiveUndoContext->Name()) : "No active history");
            ui.Separator();
            if (ui.MenuItem("Project Settings..."))
                m_ProjectSettingsPanel->Registration().SetVisible(true);
        }
        if (auto build = ui.BeginMenu("Build"); build)
        {
            if (ui.MenuItem("Build Scripts    Ctrl+Shift+B", false,
                            m_CommandRouter->Available(KeireEditor::EditorCommand::BuildScripts)))
            {
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::BuildScripts);
            }
            if (ui.MenuItem("Build Settings..."))
                m_BuildSettings.SetVisible(true);
        }
        if (auto entity = ui.BeginMenu("Entity", static_cast<bool>(scene)); entity)
        {
            if (ui.MenuItem("Create Empty"))
            {
                RecordSceneUndo();
                m_SceneDocument->Select(m_SceneDocument->CreateEntity().Value());
                MarkPlayEditorEntity(m_SceneDocument->Selection());
            }
            if (ui.MenuItem("Create Child", false, static_cast<bool>(m_SceneDocument->Selection())))
            {
                RecordSceneUndo();
                m_SceneDocument->Select(
                    m_SceneDocument->CreateEntity("GameObject", Keire::EntityId(m_SceneDocument->Selection())).Value());
                MarkPlayEditorEntity(m_SceneDocument->Selection());
            }
            if (ui.MenuItem("Directional Light"))
            {
                RecordSceneUndo();
                const auto created = m_SceneDocument->CreateEntity("Directional Light", {},
                                                                   Keire::DirectionalLightComponent::StaticType());
                m_SceneDocument->Select(created.Value());
                MarkPlayEditorEntity(m_SceneDocument->Selection());
            }
            if (ui.MenuItem("Point Light"))
            {
                RecordSceneUndo();
                const auto created =
                    m_SceneDocument->CreateEntity("Point Light", {}, Keire::PointLightComponent::StaticType());
                m_SceneDocument->Select(created.Value());
                MarkPlayEditorEntity(m_SceneDocument->Selection());
            }
            if (ui.MenuItem("Spot Light"))
            {
                RecordSceneUndo();
                const auto created =
                    m_SceneDocument->CreateEntity("Spot Light", {}, Keire::SpotLightComponent::StaticType());
                m_SceneDocument->Select(created.Value());
                MarkPlayEditorEntity(m_SceneDocument->Selection());
            }
            if (ui.MenuItem("Reflection Probe"))
            {
                RecordSceneUndo();
                const auto created = m_SceneDocument->CreateEntity("Reflection Probe", {},
                                                                   Keire::ReflectionProbeComponent::StaticType());
                m_SceneDocument->Select(created.Value());
                MarkPlayEditorEntity(m_SceneDocument->Selection());
            }
            if (ui.MenuItem("Light Probe Volume"))
            {
                RecordSceneUndo();
                const auto created = m_SceneDocument->CreateEntity("Light Probe Volume", {},
                                                                   Keire::LightProbeVolumeComponent::StaticType());
                m_SceneDocument->Select(created.Value());
                MarkPlayEditorEntity(m_SceneDocument->Selection());
            }
            if (ui.MenuItem("Main Camera"))
            {
                RecordSceneUndo();
                const auto created =
                    m_SceneDocument->CreateEntity("Main Camera", {}, Keire::CameraComponent::StaticType());
                m_SceneDocument->SetTransform(created, {.Position = Keire::Vector3{0.0F, 1.0F, -10.0F}});
                m_SceneDocument->Select(created.Value());
                MarkPlayEditorEntity(m_SceneDocument->Selection());
            }
            if (ui.MenuItem("3D Object/Cube"))
            {
                RecordSceneUndo();
                const auto created =
                    m_SceneDocument->CreateEntity("Cube", {}, Keire::MeshRendererComponent::StaticType());
                m_SceneDocument->SetComponentProperty(created, Keire::MeshRendererComponent::StaticType(), "mesh",
                                                      Keire::MeshAsset::CubeId());
                m_SceneDocument->Select(created.Value());
                MarkPlayEditorEntity(m_SceneDocument->Selection());
            }
            ui.Separator();
            if (ui.MenuItem("Duplicate", false, static_cast<bool>(m_SceneDocument->Selection())))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::DuplicateSelection);
            if (ui.MenuItem("Delete", false, static_cast<bool>(m_SceneDocument->Selection())))
            {
                RecordSceneUndo();
                const auto selected = m_SceneDocument->Selections();
                const std::vector selection(selected.begin(), selected.end());
                for (const auto selectedEntity : selection)
                {
                    MarkPlayEditorEntity(selectedEntity);
                    m_SceneDocument->DeleteEntity(Keire::EntityId(selectedEntity));
                }
                m_SceneDocument->ClearSelection();
            }
        }
        if (auto layoutsMenu = ui.BeginMenu("Layout"); layoutsMenu)
        {
            const auto layouts = workspace.Layouts();
            for (const auto& layout : layouts)
            {
                std::string label = layout.Name + (layout.Modified ? " *" : "");
                if (ui.MenuItem(label, layout.Active))
                    workspace.LoadLayout(layout.Id);
            }
            ui.Separator();
            if (ui.MenuItem("Save As..."))
                OpenDialog(Dialog::SaveLayout);
            const auto* active = ActiveLayout(layouts);
            const bool editable = active && !active->BuiltIn;
            if (ui.MenuItem("Rename...", false, editable))
            {
                m_ProfileName = active->Name;
                OpenDialog(Dialog::RenameLayout);
            }
            if (ui.MenuItem("Delete...", false, editable))
                OpenDialog(Dialog::DeleteLayout);
            if (ui.MenuItem("Reset to Default"))
                workspace.ResetFactoryLayout();
            ui.Separator();
            if (ui.MenuItem("Import..."))
                workspace.ShowImportLayoutDialog();
            if (ui.MenuItem("Export..."))
                workspace.ShowExportLayoutDialog(workspace.ActiveLayout());
        }
        if (auto themesMenu = ui.BeginMenu("Theme"); themesMenu)
        {
            const auto themes = workspace.Themes();
            for (const auto& theme : themes)
            {
                if (ui.MenuItem(theme.Name, theme.Active))
                    RequestTheme(workspace, theme.Id);
            }
            ui.Separator();
            if (ui.MenuItem("Theme Editor", m_ThemeEditor.Visible()))
                m_ThemeEditor.SetVisible(!m_ThemeEditor.Visible());
            if (ui.MenuItem("Import..."))
                workspace.ShowImportThemeDialog();
            if (ui.MenuItem("Export..."))
                workspace.ShowExportThemeDialog(workspace.ActiveTheme());
        }
        if (auto assetsMenu = ui.BeginMenu("Assets"); assetsMenu)
        {
            if (ui.MenuItem("Create/Scene", false, static_cast<bool>(m_AssetDatabase)))
                RequestCreateScene();
            if (ui.MenuItem("Create/Unlit Shader", false, static_cast<bool>(m_AssetDatabase)))
                CreateUnlitShader();
            if (ui.MenuItem("Create/Material", false, static_cast<bool>(m_AssetDatabase)))
            {
                if (m_AssetBrowserPanel)
                    m_AssetBrowserPanel->RequestCreateMaterial();
                else
                    (void)CreateMaterial();
            }
            if (ui.MenuItem("Create/Input Actions/Empty", false, static_cast<bool>(m_AssetDatabase)))
                CreateInputActions({.SchemaVersion = 1, .Name = "InputActions"}, "InputActions");
            if (ui.MenuItem("Create/Input Actions/Default", false, static_cast<bool>(m_AssetDatabase)))
                CreateInputActions(Keire::InputActionAsset::DefaultDefinition(), "DefaultInput");
            if (ui.MenuItem("Create/Input Actions/3D Gameplay", false, static_cast<bool>(m_AssetDatabase)))
                CreateInputActions(Keire::InputActionAsset::GameplayDefinition(), "GameplayInput");
            if (ui.MenuItem("Create/Input Actions/UI Navigation", false, static_cast<bool>(m_AssetDatabase)))
                CreateInputActions(Keire::InputActionAsset::UiDefinition(), "UiInput");
            ui.Separator();
            if (ui.MenuItem("Refresh and Import", false, static_cast<bool>(m_AssetDatabase)))
            {
                try
                {
                    ImportAssets();
                }
                catch (const std::exception& error)
                {
                    SetAssetError(std::string("Asset import failed: ") + error.what());
                }
            }
            if (ui.MenuItem("Cook Dist Build", false, static_cast<bool>(m_AssetDatabase)))
                CookAssets();
        }
        if (auto window = ui.BeginMenu("Window"); window)
        {
            DrawPanelMenuItem(ui, m_SceneViewportPanel->Registration());
            DrawPanelMenuItem(ui, m_Game);
            DrawPanelMenuItem(ui, m_HierarchyPanel->Registration());
            DrawPanelMenuItem(ui, m_InspectorPanel->Registration());
            DrawPanelMenuItem(ui, m_AssetBrowserPanel->Registration());
            DrawPanelMenuItem(ui, m_ConsolePanel->Registration());
            DrawPanelMenuItem(ui, m_DiagnosticsPanel->Registration());
            DrawPanelMenuItem(ui, m_ThemeEditor);
            DrawPanelMenuItem(ui, m_InputActionsPanel->Registration());
            DrawPanelMenuItem(ui, m_AnimatorControllerPanel->Registration());
            DrawPanelMenuItem(ui, m_RiggingStudioPanel->Registration());
            DrawPanelMenuItem(ui, m_AudioMixerPanel->Registration());
            DrawPanelMenuItem(ui, m_VfxEffectPanel->Registration());
            DrawPanelMenuItem(ui, m_MaterialGraphPanel->Registration());
            DrawPanelMenuItem(ui, m_InputDebugger);
            DrawPanelMenuItem(ui, m_ProjectSettingsPanel->Registration());
            DrawPanelMenuItem(ui, m_LightingPanel->Registration());
            DrawPanelMenuItem(ui, m_PrefabOverrides);
            DrawPanelMenuItem(ui, m_BuildSettings);
            DrawPanelMenuItem(ui, m_Profiler);
            if (ui.MenuItem("Viewport Performance Overlay", m_ShowPerformanceOverlay))
                m_ShowPerformanceOverlay = !m_ShowPerformanceOverlay;
        }
    }
}

void EditorWorkspaceLayer::DrawMainToolbar(Keire::UiFrame& ui)
{
    if (auto toolbar = ui.BeginMainToolbar(); toolbar)
    {
        if (ui.IconButton("ToolbarNewScene", Keire::UiIcon::Create, false, {28.0F, 24.0F}))
            (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::NewScene);
        if (ui.LastItemState().Hovered)
            ui.SetTooltip("New Scene", {.Delayed = true});
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!m_CommandRouter->Available(KeireEditor::EditorCommand::SaveScene));
            disabled)
        {
            if (ui.IconButton("ToolbarSaveScene", Keire::UiIcon::Folder, false, {28.0F, 24.0F}))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::SaveScene);
        }
        if (ui.LastItemState().Hovered)
            ui.SetTooltip("Save Scene (Ctrl+S)", {.Delayed = true});

        ui.SameLine();
        ui.AlignNextItemGroup(0.5F, 98.0F);
        const auto playState =
            m_SceneDocument->PlaySession() ? m_SceneDocument->PlaySession()->State() : Keire::ScenePlayState::Stopped;
        const bool playActive = playState != Keire::ScenePlayState::Stopped || m_PlayStartPending;
        if (ui.IconButton("ToolbarPlay", playActive ? Keire::UiIcon::Stop : Keire::UiIcon::Play, playActive,
                          {28.0F, 24.0F}))
        {
            (void)m_CommandRouter->Execute(playActive ? KeireEditor::EditorCommand::Stop
                                                      : KeireEditor::EditorCommand::Play);
        }
        if (m_PlayStartPending && ui.LastItemState().Hovered)
            ui.SetTooltip("Waiting for gameplay scripts (click to cancel)", {.Delayed = true});
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(playState == Keire::ScenePlayState::Stopped ||
                                             playState == Keire::ScenePlayState::Faulted);
            disabled)
        {
            if (ui.IconButton("ToolbarPause", Keire::UiIcon::Pause, playState == Keire::ScenePlayState::Paused,
                              {28.0F, 24.0F}))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Pause);
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(playState != Keire::ScenePlayState::Paused); disabled)
        {
            if (ui.IconButton("ToolbarStep", Keire::UiIcon::Step, false, {28.0F, 24.0F}))
                (void)m_SceneDocument->PlaySession()->Step(
                    static_cast<float>(Owner().GetTime().FixedDeltaTime().Seconds()));
        }

        ui.SameLine();
        ui.AlignNextItemGroup(1.0F, 106.0F);
        if (m_AssetOperations && m_AssetOperations->Busy())
        {
            ui.TextColored(m_Theme.AccentHovered, "Assets...");
            ui.SameLine();
        }
        if (ui.IconButton("ToolbarTheme", Keire::UiIcon::Settings, m_ThemeEditor.Visible(), {28.0F, 24.0F}))
            m_ThemeEditor.SetVisible(!m_ThemeEditor.Visible());
        if (ui.LastItemState().Hovered)
            ui.SetTooltip("Theme and editor appearance", {.Delayed = true});
        if (!m_Notice.empty())
        {
            ui.SameLine();
            if (ui.IconButton("ToolbarNotice", Keire::UiIcon::Information, false, {28.0F, 24.0F}))
                m_Notice.clear();
            if (ui.LastItemState().Hovered)
                ui.SetTooltip(m_Notice, {.Delayed = true});
        }
    }
}

void EditorWorkspaceLayer::DrawMainStatusBar(Keire::UiFrame& ui)
{
    if (auto status = ui.BeginMainStatusBar(); status)
    {
        const auto scene = m_SceneDocument->EditingScene();
        const std::string sceneStatus = scene ? scene->Name() + (scene->Dirty() ? "  *" : "") : "No scene";
        ui.TextColored(scene && scene->Dirty() ? m_Theme.Warning : m_Theme.MutedText, sceneStatus);
        ui.SameLine();
        ui.TextColored(m_Theme.MutedText, std::to_string(m_SceneDocument->Selections().size()) + " selected");
        if (m_AssetOperations && m_AssetOperations->Busy())
        {
            ui.SameLine();
            const auto progress = m_AssetOperations->Progress();
            const std::string progressText =
                progress && progress->Total > 0
                    ? "Assets " + std::to_string(progress->Completed) + "/" + std::to_string(progress->Total)
                    : "Assets working";
            ui.TextColored(m_Theme.AccentHovered, progressText);
        }
        ui.SameLine();
        const auto renderer = Owner().Renderer();
        const auto statistics = renderer ? renderer->Statistics() : Keire::RenderStatistics{};
        ui.TextColored(m_Theme.MutedText,
                       std::string(renderer && renderer->IsOpen() ? "Renderer online  |  " : "Renderer offline  |  ") +
                           "Frame " + std::to_string(Owner().GetTime().FrameCount()) + "  " +
                           std::to_string(Owner().GetTime().UnscaledDeltaTime().Milliseconds()) + " ms  " +
                           std::to_string(statistics.DrawCalls) + " draws");
    }
}

void EditorWorkspaceLayer::DrawNotices(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    if (const auto notice = workspace.ConsumeNotice())
    {
        m_Notice = notice->Message;
        m_NoticeColor = notice->Severity == Keire::UiWorkspaceNoticeSeverity::Error     ? m_Theme.Error
                        : notice->Severity == Keire::UiWorkspaceNoticeSeverity::Warning ? m_Theme.Warning
                                                                                        : m_Theme.Success;
        ui.OpenPopup("Workspace Notice");
    }
    if (auto popup = ui.BeginPopupModal("Workspace Notice"); popup)
    {
        ui.TextColored(m_NoticeColor, m_Notice);
        if (ui.Button("OK"))
            ui.CloseCurrentPopup();
    }
}

void EditorWorkspaceLayer::OpenDialog(const Dialog dialog)
{
    m_Dialog = dialog;
    if (dialog == Dialog::SaveLayout || dialog == Dialog::SaveTheme)
        m_ProfileName.clear();
    m_Error.clear();
    m_OpenDialog = true;
}

void EditorWorkspaceLayer::OpenPendingDialog(Keire::UiFrame& ui)
{
    if (!m_OpenDialog)
        return;
    m_OpenDialog = false;
    switch (m_Dialog)
    {
    case Dialog::SaveLayout:
        ui.OpenPopup("Save Layout As");
        break;
    case Dialog::RenameLayout:
        ui.OpenPopup("Rename Layout");
        break;
    case Dialog::DeleteLayout:
        ui.OpenPopup("Delete Layout");
        break;
    case Dialog::SaveTheme:
        ui.OpenPopup("Save Theme As");
        break;
    case Dialog::RenameTheme:
        ui.OpenPopup("Rename Theme");
        break;
    case Dialog::DeleteTheme:
        ui.OpenPopup("Delete Theme");
        break;
    case Dialog::DirtyTheme:
        ui.OpenPopup("Unsaved Theme Changes");
        break;
    case Dialog::DirtyScene:
        ui.OpenPopup("Unsaved Scene Changes");
        break;
    case Dialog::RenameEntity:
        ui.OpenPopup("Rename Entity");
        break;
    case Dialog::None:
    default:
        break;
    }
}

void EditorWorkspaceLayer::DrawDialogs(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    DrawNameDialog(ui, workspace, "Save Layout As", Dialog::SaveLayout);
    DrawNameDialog(ui, workspace, "Rename Layout", Dialog::RenameLayout);
    DrawNameDialog(ui, workspace, "Save Theme As", Dialog::SaveTheme);
    DrawNameDialog(ui, workspace, "Rename Theme", Dialog::RenameTheme);
    DrawNameDialog(ui, workspace, "Rename Entity", Dialog::RenameEntity);
    DrawDeleteDialog(ui, workspace, "Delete Layout", false);
    DrawDeleteDialog(ui, workspace, "Delete Theme", true);
    DrawDirtyThemeDialog(ui, workspace);
    DrawDirtySceneDialog(ui);
}

void EditorWorkspaceLayer::DrawNameDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace,
                                          const std::string_view title, const Dialog dialog)
{
    if (auto popup = ui.BeginPopupModal(title); popup)
    {
        ui.Text(dialog == Dialog::RenameEntity ? "Entity name" : "Profile name");
        (void)ui.InputText("##ProfileName", m_ProfileName);
        if (auto disabled = ui.BeginDisabled(m_ProfileName.empty()); disabled)
        {
            if (ui.Button(dialog == Dialog::SaveTheme ? "Save" : "Confirm"))
            {
                try
                {
                    if (dialog == Dialog::SaveLayout)
                        workspace.SaveLayoutAs(m_ProfileName);
                    else if (dialog == Dialog::RenameLayout)
                        workspace.RenameLayout(workspace.ActiveLayout(), m_ProfileName);
                    else if (dialog == Dialog::SaveTheme)
                    {
                        (void)workspace.SaveThemeAs(m_ProfileName, m_Theme);
                        m_ThemeDirty = false;
                        if (m_PendingTheme)
                        {
                            workspace.ApplyTheme(m_PendingTheme);
                            LoadTheme(workspace, m_PendingTheme);
                        }
                        if (m_CloseThemeAfterDecision)
                            m_ThemeEditor.SetVisible(false);
                        m_PendingTheme = {};
                        m_CloseThemeAfterDecision = false;
                    }
                    else if (dialog == Dialog::RenameTheme)
                        workspace.RenameTheme(workspace.ActiveTheme(), m_ProfileName);
                    else if (dialog == Dialog::RenameEntity && ActiveScene() && m_SceneDocument->Selection())
                    {
                        RecordSceneUndo("Rename Entity");
                        m_SceneDocument->RenameEntity(Keire::EntityId(m_SceneDocument->Selection()), m_ProfileName);
                        MarkPlayEditorEntity(m_SceneDocument->Selection());
                    }
                    m_Dialog = Dialog::None;
                    ui.CloseCurrentPopup();
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                    ReportError("Editor", m_Error);
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_Dialog = Dialog::None;
            if (m_SceneTransitions)
                m_SceneTransitions->Cancel();
            ui.CloseCurrentPopup();
        }
        if (!m_Error.empty())
            ui.TextColored(m_Theme.Error, m_Error);
    }
}

void EditorWorkspaceLayer::DrawDeleteDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace,
                                            const std::string_view title, const bool theme)
{
    if (auto popup = ui.BeginPopupModal(title); popup)
    {
        ui.Text(theme ? "Delete the active custom theme?" : "Delete the active custom layout?");
        ui.TextColored(m_Theme.Warning, "This cannot be undone.");
        if (ui.Button("Delete"))
        {
            if (theme)
            {
                workspace.DeleteTheme(workspace.ActiveTheme());
                LoadTheme(workspace, workspace.ActiveTheme());
            }
            else
                workspace.DeleteLayout(workspace.ActiveLayout());
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
    }
}

void EditorWorkspaceLayer::RequestTheme(Keire::UiWorkspace& workspace, const Keire::UiThemeId id)
{
    if (id == workspace.ActiveTheme())
        return;
    if (!m_ThemeDirty)
    {
        workspace.ApplyTheme(id);
        LoadTheme(workspace, id);
        return;
    }
    m_PendingTheme = id;
    m_CloseThemeAfterDecision = false;
    m_Dialog = Dialog::DirtyTheme;
    m_OpenDialog = true;
}

void EditorWorkspaceLayer::DrawDirtyThemeDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    if (auto popup = ui.BeginPopupModal("Unsaved Theme Changes"); popup)
    {
        ui.Text("Save changes before switching or closing the editor?");
        const auto themes = workspace.Themes();
        const auto* active = ActiveTheme(themes);
        const bool canOverwrite = active && !active->BuiltIn;
        if (ui.Button(canOverwrite ? "Save" : "Save As..."))
        {
            if (canOverwrite)
            {
                workspace.UpdateTheme(workspace.ActiveTheme(), m_Theme);
                m_ThemeDirty = false;
                if (m_PendingTheme)
                {
                    workspace.ApplyTheme(m_PendingTheme);
                    LoadTheme(workspace, m_PendingTheme);
                }
                if (m_CloseThemeAfterDecision)
                    m_ThemeEditor.SetVisible(false);
                m_PendingTheme = {};
                m_CloseThemeAfterDecision = false;
                m_Dialog = Dialog::None;
                ui.CloseCurrentPopup();
            }
            else
            {
                m_Dialog = Dialog::SaveTheme;
                m_ProfileName.clear();
                ui.CloseCurrentPopup();
                m_OpenDialog = true;
            }
        }
        ui.SameLine();
        if (ui.Button("Discard"))
        {
            workspace.CancelThemePreview();
            m_ThemeDirty = false;
            if (m_PendingTheme)
            {
                workspace.ApplyTheme(m_PendingTheme);
                LoadTheme(workspace, m_PendingTheme);
            }
            else
                LoadTheme(workspace, workspace.ActiveTheme());
            if (m_CloseThemeAfterDecision)
                m_ThemeEditor.SetVisible(false);
            m_PendingTheme = {};
            m_CloseThemeAfterDecision = false;
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_PendingTheme = {};
            m_CloseThemeAfterDecision = false;
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
        if (!canOverwrite)
            ui.TextColored(m_Theme.MutedText, "Built-in themes must be preserved with Save As.");
    }
}

void EditorWorkspaceLayer::DrawDirtySceneDialog(Keire::UiFrame& ui)
{
    if (auto popup = ui.BeginPopupModal("Unsaved Scene Changes"); popup)
    {
        ui.Text("Save the active scene before continuing?");
        if (ui.Button("Save"))
        {
            SaveScene();
            if (!m_SceneDocument->EditingScene() || !m_SceneDocument->EditingScene()->Dirty())
            {
                ui.CloseCurrentPopup();
                ExecutePendingSceneAction();
            }
        }
        ui.SameLine();
        if (ui.Button("Discard"))
        {
            ui.CloseCurrentPopup();
            ExecutePendingSceneAction();
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_PendingSceneAction = PendingSceneAction::None;
            m_PendingSceneAsset = {};
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
        ui.TextColored(m_Theme.MutedText, "Save is atomic; Cancel leaves the scene and selection unchanged.");
    }
}

void EditorWorkspaceLayer::LoadTheme(Keire::UiWorkspace& workspace, const Keire::UiThemeId id)
{
    m_Theme = workspace.ThemeDefinition(id);
    m_ThemeDirty = false;
}

void EditorWorkspaceLayer::DrawThemeEditor(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    const bool wasVisible = m_ThemeEditor.Visible();
    auto panel = ui.BeginPanel(m_ThemeEditor);
    if (wasVisible && !m_ThemeEditor.Visible() && m_ThemeDirty)
    {
        m_ThemeEditor.SetVisible(true);
        m_PendingTheme = {};
        m_CloseThemeAfterDecision = true;
        m_Dialog = Dialog::DirtyTheme;
        m_OpenDialog = true;
    }
    if (!panel)
        return;
    if (ui.WindowFocused())
        m_ActiveUndoContext = m_ThemeUndoContext;

    const auto themes = workspace.Themes();
    const auto* active = ActiveTheme(themes);
    if (auto combo = ui.BeginCombo("Theme", active ? active->Name : "Unknown"); combo)
    {
        for (const auto& theme : themes)
        {
            if (ui.Selectable(theme.Name, theme.Active))
                RequestTheme(workspace, theme.Id);
        }
    }
    ui.Separator();
    const auto themeBeforeEdit = m_Theme;
    bool changed = false;
    changed |= ui.ColorEdit("Canvas", m_Theme.Canvas);
    changed |= ui.ColorEdit("Panel", m_Theme.Panel);
    changed |= ui.ColorEdit("Raised panel", m_Theme.RaisedPanel);
    changed |= ui.ColorEdit("Border", m_Theme.Border);
    changed |= ui.ColorEdit("Text", m_Theme.Text);
    changed |= ui.ColorEdit("Muted text", m_Theme.MutedText);
    changed |= ui.ColorEdit("Accent", m_Theme.Accent);
    changed |= ui.ColorEdit("Accent hovered", m_Theme.AccentHovered);
    changed |= ui.ColorEdit("Accent active", m_Theme.AccentActive);
    changed |= ui.ColorEdit("Selection", m_Theme.Selection);
    changed |= ui.ColorEdit("Success", m_Theme.Success);
    changed |= ui.ColorEdit("Warning", m_Theme.Warning);
    changed |= ui.ColorEdit("Error", m_Theme.Error);
    ui.Separator();
    changed |= ui.SliderFloat("Window padding X", m_Theme.WindowPadding.Width, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Window padding Y", m_Theme.WindowPadding.Height, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Frame padding X", m_Theme.FramePadding.Width, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Frame padding Y", m_Theme.FramePadding.Height, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Item spacing X", m_Theme.ItemSpacing.Width, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Item spacing Y", m_Theme.ItemSpacing.Height, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Window rounding", m_Theme.WindowRounding, 0.0F, 24.0F);
    changed |= ui.SliderFloat("Frame rounding", m_Theme.FrameRounding, 0.0F, 24.0F);
    changed |= ui.SliderFloat("Tab rounding", m_Theme.TabRounding, 0.0F, 24.0F);
    changed |= ui.SliderFloat("Scrollbar rounding", m_Theme.ScrollbarRounding, 0.0F, 24.0F);
    changed |= ui.SliderFloat("Window border", m_Theme.WindowBorderSize, 0.0F, 4.0F);
    changed |= ui.SliderFloat("Frame border", m_Theme.FrameBorderSize, 0.0F, 4.0F);
    if (changed)
    {
        m_ThemeDirty = true;
        workspace.PreviewTheme(m_Theme);
        if (m_ThemeUndoContext)
        {
            const auto themeAfterEdit = m_Theme;
            m_ThemeUndoContext->RecordApplied(Keire::CreateUndoCommand(
                "Edit Theme",
                [this, &workspace, themeAfterEdit]
                {
                    m_Theme = themeAfterEdit;
                    m_ThemeDirty = true;
                    workspace.PreviewTheme(m_Theme);
                },
                [this, &workspace, themeBeforeEdit]
                {
                    m_Theme = themeBeforeEdit;
                    m_ThemeDirty = true;
                    workspace.PreviewTheme(m_Theme);
                },
                sizeof(Keire::UiThemeDefinition), [this] { return m_ThemeEditor.Visible(); }));
        }
    }

    const bool canOverwrite = active && !active->BuiltIn;
    if (auto disabled = ui.BeginDisabled(!m_ThemeDirty || !canOverwrite); disabled)
    {
        if (ui.Button("Save"))
        {
            workspace.UpdateTheme(workspace.ActiveTheme(), m_Theme);
            m_ThemeDirty = false;
        }
    }
    ui.SameLine();
    if (ui.Button("Save As..."))
        OpenDialog(Dialog::SaveTheme);
    ui.SameLine();
    if (auto disabled = ui.BeginDisabled(!m_ThemeDirty); disabled)
    {
        if (ui.Button("Revert"))
        {
            workspace.CancelThemePreview();
            LoadTheme(workspace, workspace.ActiveTheme());
        }
    }
    if (active && !active->BuiltIn)
    {
        if (ui.Button("Rename..."))
        {
            m_ProfileName = active->Name;
            OpenDialog(Dialog::RenameTheme);
        }
        ui.SameLine();
        if (ui.Button("Delete..."))
            OpenDialog(Dialog::DeleteTheme);
    }
    else
        ui.TextColored(m_Theme.MutedText, "Built-in themes are immutable. Save As creates an editable copy.");
}
