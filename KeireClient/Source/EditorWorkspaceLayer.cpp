#include "EditorWorkspaceLayer.h"

#include <algorithm>
#include <sstream>
#include <utility>
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

EditorWorkspaceLayer::EditorWorkspaceLayer(const bool smoke) : Layer("EditorWorkspaceLayer"), m_Smoke(smoke) {}

void EditorWorkspaceLayer::OnAttach()
{
    auto& workspace = Owner().GetUiWorkspace();
    m_Scene = workspace.RegisterPanel({"editor.scene", "Scene"});
    m_Game = workspace.RegisterPanel({"editor.game", "Game"});
    m_Hierarchy = workspace.RegisterPanel({"editor.hierarchy", "Hierarchy"});
    m_Inspector = workspace.RegisterPanel({"editor.inspector", "Inspector"});
    m_Project = workspace.RegisterPanel({"editor.project", "Project"});
    m_Console = workspace.RegisterPanel({"editor.console", "Console"});
    m_Diagnostics = workspace.RegisterPanel({"editor.diagnostics", "Diagnostics"});
    m_ThemeEditor = workspace.RegisterPanel({"editor.theme", "Theme Editor", false});
    LoadTheme(workspace, workspace.ActiveTheme());
}

void EditorWorkspaceLayer::OnUpdate(const Keire::Time&)
{
    if (m_Smoke && ++m_FrameCount >= 8)
        Owner().RequestExit();
}

void EditorWorkspaceLayer::OnUi(Keire::UiFrame& ui)
{
    auto& workspace = Owner().GetUiWorkspace();
    DrawMainMenu(ui, workspace);
    OpenPendingDialog(ui);
    DrawNotices(ui, workspace);
    DrawDialogs(ui, workspace);

    if (auto scene = ui.BeginPanel(m_Scene); scene)
        DrawEmptyState(ui, "SCENE", "No scene is loaded.", "Scene authoring belongs to a future engine milestone.");
    if (auto game = ui.BeginPanel(m_Game); game)
        DrawEmptyState(ui, "GAME", "No game preview is available.",
                       "The shell is ready for a future renderer-owned preview target.");
    if (auto hierarchy = ui.BeginPanel(m_Hierarchy); hierarchy)
        DrawEmptyState(ui, "HIERARCHY", "No scene objects", "Objects will appear when the scene model is introduced.");
    if (auto inspector = ui.BeginPanel(m_Inspector); inspector)
        DrawEmptyState(ui, "INSPECTOR", "Nothing selected", "Select an editor object to inspect its properties.");
    if (auto project = ui.BeginPanel(m_Project); project)
        DrawEmptyState(ui, "PROJECT", "No project assets to display.",
                       "Asset discovery and import remain outside this UI milestone.");
    if (auto console = ui.BeginPanel(m_Console); console)
    {
        ui.TextColored(m_Theme.MutedText, "CONSOLE");
        ui.Separator();
        ui.TextColored(m_Theme.Success, "Ready");
        ui.SameLine();
        ui.Text("No editor messages.");
    }
    DrawDiagnostics(ui);
    DrawThemeEditor(ui, workspace);
}

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
    if (auto menuBar = ui.BeginMainMenuBar(); menuBar)
    {
        if (auto file = ui.BeginMenu("File"); file)
        {
            if (ui.MenuItem("Exit"))
                Owner().RequestExit();
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
        if (auto window = ui.BeginMenu("Window"); window)
        {
            DrawPanelMenuItem(ui, m_Scene);
            DrawPanelMenuItem(ui, m_Game);
            DrawPanelMenuItem(ui, m_Hierarchy);
            DrawPanelMenuItem(ui, m_Inspector);
            DrawPanelMenuItem(ui, m_Project);
            DrawPanelMenuItem(ui, m_Console);
            DrawPanelMenuItem(ui, m_Diagnostics);
            DrawPanelMenuItem(ui, m_ThemeEditor);
        }
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
    DrawDeleteDialog(ui, workspace, "Delete Layout", false);
    DrawDeleteDialog(ui, workspace, "Delete Theme", true);
    DrawDirtyThemeDialog(ui, workspace);
}

void EditorWorkspaceLayer::DrawNameDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace,
                                          const std::string_view title, const Dialog dialog)
{
    if (auto popup = ui.BeginPopupModal(title); popup)
    {
        ui.Text("Profile name");
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
                    m_Dialog = Dialog::None;
                    ui.CloseCurrentPopup();
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_Dialog = Dialog::None;
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

void EditorWorkspaceLayer::DrawDiagnostics(Keire::UiFrame& ui)
{
    if (auto diagnostics = ui.BeginPanel(m_Diagnostics); diagnostics)
    {
        const auto& time = Owner().GetTime();
        std::ostringstream frame;
        frame << "Frame: " << time.FrameCount();
        ui.Text(frame.str());
        std::ostringstream delta;
        delta << "Delta: " << time.UnscaledDeltaTime().Milliseconds() << " ms";
        ui.Text(delta.str());
        const auto window = Owner().MainWindow();
        std::ostringstream extent;
        extent << "Window: " << window->LogicalSize().Width << 'x' << window->LogicalSize().Height << " logical pixels";
        ui.Text(extent.str());
        auto capture = Owner().UiCapture();
        (void)ui.Checkbox("Pointer capture", capture.Pointer);
        (void)ui.Checkbox("Keyboard capture", capture.Keyboard);
        ui.TextColored(m_Theme.Success, "Docking active; native viewports remain disabled.");
    }
}
