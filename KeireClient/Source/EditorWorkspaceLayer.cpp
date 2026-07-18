#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    constexpr float Pi = 3.14159265358979323846F;

    [[nodiscard]] Keire::SceneVector3 QuaternionToEuler(const Keire::SceneQuaternion& quaternion) noexcept
    {
        const auto sinRoll = 2.0F * (quaternion.W * quaternion.X + quaternion.Y * quaternion.Z);
        const auto cosRoll = 1.0F - 2.0F * (quaternion.X * quaternion.X + quaternion.Y * quaternion.Y);
        const auto sinPitch =
            std::clamp(2.0F * (quaternion.W * quaternion.Y - quaternion.Z * quaternion.X), -1.0F, 1.0F);
        const auto sinYaw = 2.0F * (quaternion.W * quaternion.Z + quaternion.X * quaternion.Y);
        const auto cosYaw = 1.0F - 2.0F * (quaternion.Y * quaternion.Y + quaternion.Z * quaternion.Z);
        constexpr float radiansToDegrees = 180.0F / Pi;
        return {std::atan2(sinRoll, cosRoll) * radiansToDegrees, std::asin(sinPitch) * radiansToDegrees,
                std::atan2(sinYaw, cosYaw) * radiansToDegrees};
    }

    [[nodiscard]] Keire::SceneQuaternion EulerToQuaternion(const Keire::SceneVector3& euler) noexcept
    {
        constexpr float halfDegreesToRadians = Pi / 360.0F;
        const auto roll = euler.X * halfDegreesToRadians;
        const auto pitch = euler.Y * halfDegreesToRadians;
        const auto yaw = euler.Z * halfDegreesToRadians;
        const auto cr = std::cos(roll);
        const auto sr = std::sin(roll);
        const auto cp = std::cos(pitch);
        const auto sp = std::sin(pitch);
        const auto cy = std::cos(yaw);
        const auto sy = std::sin(yaw);
        return {sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy,
                cr * cp * cy + sr * sp * sy};
    }

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

    void WriteBytesAtomically(const std::filesystem::path& path, const std::span<const std::byte> bytes)
    {
        const std::string text =
            bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::Detail::WriteTextFileAtomically(path, text);
    }
} // namespace

EditorWorkspaceLayer::EditorWorkspaceLayer(const bool smoke, const bool initializeProject)
    : Layer("EditorWorkspaceLayer"), m_Smoke(smoke), m_InitializeProject(initializeProject)
{
}

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
    m_InputActionsEditor = workspace.RegisterPanel({"editor.input-actions", "Input Actions", false});
    m_InputDebugger = workspace.RegisterPanel({"editor.input-debugger", "Input Debugger", false});
    LoadTheme(workspace, workspace.ActiveTheme());
    if (!m_Smoke || m_InitializeProject)
    {
        try
        {
            Keire::AssetDatabaseSpecification databaseSpecification;
            const auto project = Owner().GetProject();
            if (!project)
                throw std::runtime_error("Editor workspace requires an active project.");
            databaseSpecification.ProjectRoot = project->Root();
            databaseSpecification.Importers = {Keire::CreateInputActionAssetImporter(),
                                               Keire::CreateSceneAssetImporter()};
            m_AssetDatabase = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
            ImportAssets();
            if (const auto input = Owner().Input())
            {
                m_EditorInputUser = input->CreateUser("Editor");
                (void)input->PairDevice(m_EditorInputUser, Keire::InputDeviceId(1));
                (void)input->PairDevice(m_EditorInputUser, Keire::InputDeviceId(2));
            }
            Listen<Keire::InputDeviceConnectedEvent>(
                [this](const auto& event)
                {
                    AddConsoleMessage("Input", "Connected " + event.Device.Name, m_Theme.Success);
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::InputDeviceDisconnectedEvent>(
                [this](const auto& event)
                {
                    AddConsoleMessage("Input", "Disconnected device " + std::to_string(event.Device.Value()),
                                      m_Theme.Warning);
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::WindowCloseRequestedEvent>(
                [this](const auto& event)
                {
                    if (event.Header.Window == Owner().MainWindow()->Id() && m_EditingScene && m_EditingScene->Dirty())
                    {
                        m_PendingSceneAction = PendingSceneAction::Exit;
                        OpenDialog(Dialog::DirtyScene);
                        return Keire::EventFlow::Handled;
                    }
                    return Keire::EventFlow::Continue;
                });
            if (project->Descriptor().StartupScene)
                OpenScene(project->Descriptor().StartupScene);
            if (project->Descriptor().DefaultInput)
            {
                OpenInputActions(project->Descriptor().DefaultInput);
                m_InputActionsEditor.SetVisible(false);
            }
        }
        catch (const std::exception& error)
        {
            m_AssetStatus = std::string("Asset database initialization failed: ") + error.what();
        }
    }
}

void EditorWorkspaceLayer::OnDetach() noexcept
{
    EndInputTest();
    m_Rebind.Reset();
    m_InputContext.Reset();
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        try
        {
            WriteSceneRecovery();
        }
        catch (...)
        {
        }
    }
    if (m_EditingScene)
        m_EditingScene->Close();
    m_EditingScene.Reset();
    m_AssetDatabase.Reset();
}

void EditorWorkspaceLayer::OnUpdate(const Keire::Time& time)
{
    if (m_Smoke && ++m_FrameCount >= 8)
        Owner().RequestExit();
    if (!m_AssetDatabase)
        return;
    if (m_SceneLoad && m_SceneLoad->State() == Keire::SceneLoadState::Failed)
    {
        m_SceneStatus = "Scene runtime load failed: " + m_SceneLoad->Diagnostic().Message;
        m_SceneLoad.Reset();
    }
    else if (m_SceneLoad && m_SceneLoad->State() == Keire::SceneLoadState::Ready)
    {
        m_SceneStatus = "Scene loaded and activated.";
        m_SceneLoad.Reset();
    }
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        m_SceneRecoverySeconds += time.UnscaledDeltaTime().Seconds();
        if (m_SceneRecoverySeconds >= 30.0)
        {
            m_SceneRecoverySeconds = 0.0;
            try
            {
                WriteSceneRecovery();
            }
            catch (const std::exception& error)
            {
                m_SceneStatus = std::string("Scene recovery save failed: ") + error.what();
            }
        }
    }
    else
        m_SceneRecoverySeconds = 0.0;
    m_AssetPollSeconds += time.UnscaledDeltaTime().Seconds();
    if (m_AssetPollSeconds < 0.25)
        return;
    m_AssetPollSeconds = 0.0;
    try
    {
        const auto changed = m_AssetDatabase->PollChangedAssets();
        if (!changed.empty())
        {
            ImportAssets();
            if (const auto assets = Owner().Assets())
            {
                for (const auto id : changed)
                    (void)assets->Reload(id);
            }
        }
    }
    catch (const std::exception& error)
    {
        m_AssetStatus = std::string("Asset hot reload failed: ") + error.what();
    }
}

void EditorWorkspaceLayer::OnUi(Keire::UiFrame& ui)
{
    auto& workspace = Owner().GetUiWorkspace();
    DrawMainMenu(ui, workspace);
    OpenPendingDialog(ui);
    DrawNotices(ui, workspace);
    DrawDialogs(ui, workspace);

    DrawScene(ui);
    if (auto game = ui.BeginPanel(m_Game); game)
        DrawEmptyState(ui, "GAME", "No game preview is available.",
                       "The shell is ready for a future renderer-owned preview target.");
    DrawHierarchy(ui);
    DrawInspector(ui);
    DrawProject(ui);
    DrawConsole(ui);
    DrawDiagnostics(ui);
    DrawThemeEditor(ui, workspace);
    DrawInputActionsEditor(ui);
    DrawInputDebugger(ui);
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
            if (ui.MenuItem("New Scene", false, static_cast<bool>(m_AssetDatabase)))
                RequestCreateScene();
            if (ui.MenuItem("Save Scene", false, m_EditingScene && m_EditingScene->Dirty()))
                SaveScene();
            if (ui.MenuItem("Close Scene", false, static_cast<bool>(m_EditingScene)))
                RequestCloseScene();
            ui.Separator();
            if (ui.MenuItem("Exit"))
            {
                if (m_EditingScene && m_EditingScene->Dirty())
                {
                    m_PendingSceneAction = PendingSceneAction::Exit;
                    OpenDialog(Dialog::DirtyScene);
                }
                else
                    Owner().RequestExit();
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
                    m_AssetStatus = std::string("Asset import failed: ") + error.what();
                }
            }
            if (ui.MenuItem("Cook Dist Build", false, static_cast<bool>(m_AssetDatabase)))
                CookAssets();
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
            DrawPanelMenuItem(ui, m_InputActionsEditor);
            DrawPanelMenuItem(ui, m_InputDebugger);
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
    case Dialog::DirtyScene:
        ui.OpenPopup("Unsaved Scene Changes");
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
    DrawDirtySceneDialog(ui);
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

void EditorWorkspaceLayer::DrawDirtySceneDialog(Keire::UiFrame& ui)
{
    if (auto popup = ui.BeginPopupModal("Unsaved Scene Changes"); popup)
    {
        ui.Text("Save the active scene before continuing?");
        if (ui.Button("Save"))
        {
            SaveScene();
            if (!m_EditingScene || !m_EditingScene->Dirty())
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

void EditorWorkspaceLayer::ImportAssets()
{
    if (!m_AssetDatabase)
        return;
    const auto result = m_AssetDatabase->ImportAll();
    m_AssetRecords = m_AssetDatabase->Records();
    if (const auto assets = Owner().Assets())
    {
        (void)assets->Unmount(result.CatalogPath);
        assets->Mount({result.CatalogPath, 0, true});
    }
    std::ostringstream status;
    status << "Imported " << result.Imported << " asset(s); " << result.CacheHits << " cache hit(s).";
    m_AssetStatus = status.str();
}

void EditorWorkspaceLayer::CookAssets()
{
    if (!m_AssetDatabase)
        return;
    try
    {
        (void)m_AssetDatabase->Refresh();
        Keire::AssetBuildProfile profile;
        profile.Name = "Dist";
        profile.Strict = true;
        const auto project = Owner().GetProject();
        const auto output =
            project ? project->Root() / "Build/CookedAssets/Dist" : std::filesystem::path("Build/CookedAssets/Dist");
        const auto result = Keire::AssetCooker::Cook(*m_AssetDatabase, profile, output);
        Keire::AssetCooker::Validate(result.CatalogPath);
        m_AssetStatus = "Cooked and validated " + std::to_string(result.AssetCount) + " asset(s) into " +
                        std::to_string(result.PackCount) + " pack(s).";
    }
    catch (const std::exception& error)
    {
        m_AssetStatus = std::string("Asset cook failed: ") + error.what();
    }
}

void EditorWorkspaceLayer::CreateInputActions(Keire::InputActionAssetDefinition definition,
                                              const std::string_view baseName)
{
    if (!m_AssetDatabase)
        return;
    try
    {
        auto destination = std::filesystem::path(std::string(baseName) + ".keireinput");
        for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
            destination = std::filesystem::path(std::string(baseName) + " " + std::to_string(copy) + ".keireinput");
        definition.Name = destination.stem().string();
        const auto bytes = Keire::InputActionAsset::Encode(definition);
        const auto id = m_AssetDatabase->CreateAsset(destination, Keire::CreateInputActionAssetImporter(), bytes);
        ImportAssets();
        m_SelectedAsset = id;
        OpenInputActions(id);
        m_AssetStatus = "Created " + destination.generic_string() + ".";
    }
    catch (const std::exception& error)
    {
        m_AssetStatus = std::string("Input asset creation failed: ") + error.what();
    }
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
    m_InputDocument = Keire::InputActionAsset::Decode(ReadBytes(source))->Definition();
    m_InputAsset = asset;
    m_SelectedInputMap = m_InputDocument.ActionMaps.empty() ? Keire::AssetId{} : m_InputDocument.ActionMaps.front().Id;
    m_SelectedInputAction = {};
    m_SelectedInputBinding = {};
    m_InputUndo.clear();
    m_InputRedo.clear();
    m_InputDirty = false;
    m_InputMessage = "Loaded " + record->RelativePath.generic_string() + ".";
    m_Rebind.Reset();
    m_InputContext.Reset();
    if (const auto input = Owner().Input(); input && m_EditorInputUser)
        m_InputContext = input->CreateActionContext(asset, m_EditorInputUser);
    m_InputActionsEditor.SetVisible(true);
}

void EditorWorkspaceLayer::SaveInputActions()
{
    if (!m_AssetDatabase || !m_InputAsset)
        return;
    const auto record = m_AssetDatabase->Find(m_InputAsset);
    if (!record)
        throw std::runtime_error("The edited input asset no longer exists.");
    const auto bytes = Keire::InputActionAsset::Encode(m_InputDocument);
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    WriteBytesAtomically(source, bytes);
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_InputAsset);
    m_InputDirty = false;
    m_InputMessage = "Saved and imported " + record->RelativePath.generic_string() + ".";
}

void EditorWorkspaceLayer::RecordInputUndo()
{
    constexpr std::size_t maximumUndoEntries = 128;
    if (m_InputUndo.size() == maximumUndoEntries)
        m_InputUndo.erase(m_InputUndo.begin());
    m_InputUndo.push_back(m_InputDocument);
    m_InputRedo.clear();
    m_InputDirty = true;
}

void EditorWorkspaceLayer::UndoInputEdit()
{
    if (m_InputUndo.empty())
        return;
    m_InputRedo.push_back(m_InputDocument);
    m_InputDocument = std::move(m_InputUndo.back());
    m_InputUndo.pop_back();
    m_InputDirty = true;
}

void EditorWorkspaceLayer::RedoInputEdit()
{
    if (m_InputRedo.empty())
        return;
    m_InputUndo.push_back(m_InputDocument);
    m_InputDocument = std::move(m_InputRedo.back());
    m_InputRedo.pop_back();
    m_InputDirty = true;
}

void EditorWorkspaceLayer::CreateScene()
{
    if (!m_AssetDatabase)
        return;
    try
    {
        auto destination = std::filesystem::path("Scenes/Untitled.keirescene");
        for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
            destination = std::filesystem::path("Scenes/Untitled " + std::to_string(copy) + ".keirescene");
        auto definition = Keire::SceneAsset::EmptyDefinition(destination.stem().string());
        const auto bytes = Keire::SceneAsset::Encode(definition);
        const auto id = m_AssetDatabase->CreateAsset(destination, Keire::CreateSceneAssetImporter(), bytes);
        ImportAssets();
        OpenScene(id);
        m_AssetStatus = "Created " + destination.generic_string() + ".";
    }
    catch (const std::exception& error)
    {
        m_AssetStatus = std::string("Scene creation failed: ") + error.what();
    }
}

void EditorWorkspaceLayer::RequestCreateScene()
{
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Create;
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    CreateScene();
}

void EditorWorkspaceLayer::OpenScene(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        throw std::logic_error("Asset database is unavailable.");
    if (m_EditingScene && m_EditingScene->Dirty())
        throw std::runtime_error("Save or revert the current scene before opening another scene.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->RelativePath.extension() != ".keirescene")
        throw std::invalid_argument("Only .keirescene assets can be opened as scenes.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    const auto definition = Keire::SceneAsset::Decode(ReadBytes(source))->Definition();
    m_EditingScene = Keire::CreateRef<Keire::Scene>(asset, definition);
    m_EditingScene->MarkSaved();
    m_SceneAsset = asset;
    m_SceneSource = source;
    if (const auto project = Owner().GetProject())
        m_SceneRecovery = project->SceneRecoveryDirectory() / (asset.ToString() + ".keirescene.recovery");
    m_SceneRecoveryAvailable = !m_SceneRecovery.empty() && std::filesystem::is_regular_file(m_SceneRecovery);
    m_SelectedAsset = asset;
    m_SelectedSceneObject = {};
    m_SceneUndo.clear();
    m_SceneRedo.clear();
    if (const auto scenes = Owner().Scenes())
        m_SceneLoad = scenes->Load(asset, Keire::SceneLoadMode::Single);
    m_SceneStatus = "Opening " + record->RelativePath.generic_string() + ".";
}

void EditorWorkspaceLayer::RequestOpenScene(const Keire::AssetId asset)
{
    if (asset == m_SceneAsset)
        return;
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Open;
        m_PendingSceneAsset = asset;
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    OpenScene(asset);
}

void EditorWorkspaceLayer::SaveScene()
{
    if (!m_EditingScene || !m_AssetDatabase || !m_SceneAsset)
        return;
    try
    {
        const auto bytes = Keire::SceneAsset::Encode(m_EditingScene->Snapshot());
        WriteBytesAtomically(m_SceneSource, bytes);
        ImportAssets();
        if (const auto assets = Owner().Assets())
            (void)assets->Reload(m_SceneAsset);
        if (const auto scenes = Owner().Scenes())
            m_SceneLoad = scenes->Load(m_SceneAsset, Keire::SceneLoadMode::Single);
        m_EditingScene->MarkSaved();
        DiscardSceneRecovery();
        m_SceneStatus = "Scene saved atomically.";
        AddConsoleMessage("Scene", "Saved " + m_SceneSource.filename().string(), m_Theme.Success);
    }
    catch (const std::exception& error)
    {
        m_SceneStatus = std::string("Scene save failed: ") + error.what();
        AddConsoleMessage("Scene", m_SceneStatus, m_Theme.Error);
    }
}

void EditorWorkspaceLayer::RequestCloseScene()
{
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Close;
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    CloseScene();
}

void EditorWorkspaceLayer::CloseScene()
{
    if (const auto scenes = Owner().Scenes(); scenes && m_SceneAsset)
        (void)scenes->Unload(m_SceneAsset);
    if (m_EditingScene)
        m_EditingScene->Close();
    m_EditingScene.Reset();
    m_SceneLoad.Reset();
    m_SceneAsset = {};
    m_SelectedSceneObject = {};
    m_SceneSource.clear();
    DiscardSceneRecovery();
    m_SceneRecovery.clear();
    m_SceneUndo.clear();
    m_SceneRedo.clear();
    m_SceneStatus = "No scene is open.";
}

void EditorWorkspaceLayer::WriteSceneRecovery()
{
    if (!m_EditingScene || !m_EditingScene->Dirty() || m_SceneRecovery.empty())
        return;
    std::filesystem::create_directories(m_SceneRecovery.parent_path());
    const auto bytes = Keire::SceneAsset::Encode(m_EditingScene->Snapshot());
    WriteBytesAtomically(m_SceneRecovery, bytes);
    m_SceneRecoveryAvailable = true;
    m_SceneStatus = "Scene recovery snapshot updated.";
}

void EditorWorkspaceLayer::RestoreSceneRecovery()
{
    if (!m_SceneRecoveryAvailable || !m_SceneAsset)
        return;
    const auto definition = Keire::SceneAsset::Decode(ReadBytes(m_SceneRecovery))->Definition();
    m_EditingScene = Keire::CreateRef<Keire::Scene>(m_SceneAsset, definition);
    m_EditingScene->MarkDirty();
    m_SelectedSceneObject = {};
    m_SceneUndo.clear();
    m_SceneRedo.clear();
    m_SceneRecoveryAvailable = false;
    m_SceneStatus = "Recovered unsaved scene changes. Save to commit them to the project.";
}

void EditorWorkspaceLayer::DiscardSceneRecovery() noexcept
{
    if (!m_SceneRecovery.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(m_SceneRecovery, ignored);
    }
    m_SceneRecoveryAvailable = false;
}

void EditorWorkspaceLayer::ExecutePendingSceneAction()
{
    const auto action = std::exchange(m_PendingSceneAction, PendingSceneAction::None);
    const auto asset = std::exchange(m_PendingSceneAsset, Keire::AssetId{});
    m_Dialog = Dialog::None;
    if (action == PendingSceneAction::Exit)
    {
        CloseScene();
        Owner().RequestExit();
        return;
    }
    CloseScene();
    try
    {
        if (action == PendingSceneAction::Create)
            CreateScene();
        else if (action == PendingSceneAction::Open)
            OpenScene(asset);
    }
    catch (const std::exception& error)
    {
        m_SceneStatus = std::string("Scene operation failed: ") + error.what();
        AddConsoleMessage("Scene", m_SceneStatus, m_Theme.Error);
    }
}

void EditorWorkspaceLayer::RecordSceneUndo()
{
    if (!m_EditingScene)
        return;
    constexpr std::size_t maximumUndoEntries = 256;
    if (m_SceneUndo.size() == maximumUndoEntries)
        m_SceneUndo.erase(m_SceneUndo.begin());
    m_SceneUndo.push_back(m_EditingScene->Snapshot());
    m_SceneRedo.clear();
}

void EditorWorkspaceLayer::UndoSceneEdit()
{
    if (!m_EditingScene || m_SceneUndo.empty())
        return;
    m_SceneRedo.push_back(m_EditingScene->Snapshot());
    auto definition = std::move(m_SceneUndo.back());
    m_SceneUndo.pop_back();
    m_EditingScene = Keire::CreateRef<Keire::Scene>(m_SceneAsset, std::move(definition));
    m_EditingScene->MarkDirty();
    m_SelectedSceneObject = {};
}

void EditorWorkspaceLayer::RedoSceneEdit()
{
    if (!m_EditingScene || m_SceneRedo.empty())
        return;
    m_SceneUndo.push_back(m_EditingScene->Snapshot());
    auto definition = std::move(m_SceneRedo.back());
    m_SceneRedo.pop_back();
    m_EditingScene = Keire::CreateRef<Keire::Scene>(m_SceneAsset, std::move(definition));
    m_EditingScene->MarkDirty();
    m_SelectedSceneObject = {};
}

void EditorWorkspaceLayer::DrawScene(Keire::UiFrame& ui)
{
    if (auto scenePanel = ui.BeginPanel(m_Scene); scenePanel)
    {
        ui.TextColored(m_Theme.Accent, "SCENE");
        ui.Separator();
        if (!m_EditingScene)
        {
            DrawEmptyState(ui, "SCENE", "No scene is loaded.",
                           "Create or double-click a .keirescene asset in the Project panel.");
            return;
        }
        ui.Text(m_EditingScene->Name() + (m_EditingScene->Dirty() ? " *" : ""));
        ui.SameLine();
        if (ui.Button("Save"))
            SaveScene();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_SceneUndo.empty()); disabled)
        {
            if (ui.Button("Undo"))
                UndoSceneEdit();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_SceneRedo.empty()); disabled)
        {
            if (ui.Button("Redo"))
                RedoSceneEdit();
        }
        ui.TextColored(m_Theme.MutedText, std::to_string(m_EditingScene->ObjectCount()) + " object(s)");
        if (m_SceneRecoveryAvailable)
        {
            ui.TextColored(m_Theme.Warning, "A recovery snapshot is available for this scene.");
            if (ui.Button("Restore Recovery"))
            {
                try
                {
                    RestoreSceneRecovery();
                }
                catch (const std::exception& error)
                {
                    m_SceneStatus = std::string("Scene recovery failed: ") + error.what();
                }
            }
            ui.SameLine();
            if (ui.Button("Discard Recovery"))
                DiscardSceneRecovery();
        }
        if (!m_SceneStatus.empty())
            ui.TextColored(m_Theme.MutedText, m_SceneStatus);
        ui.Spacing();
        ui.TextColored(m_Theme.MutedText,
                       "Renderer preview and transform gizmos will appear when the renderer owns a scene target.");
    }
}

void EditorWorkspaceLayer::DrawHierarchy(Keire::UiFrame& ui)
{
    if (auto hierarchy = ui.BeginPanel(m_Hierarchy); hierarchy)
    {
        ui.TextColored(m_Theme.Accent, "HIERARCHY");
        ui.Separator();
        if (!m_EditingScene)
        {
            ui.Text("No scene objects.");
            return;
        }
        if (ui.Button("Create Empty"))
        {
            RecordSceneUndo();
            m_SelectedSceneObject = m_EditingScene->CreateObject().Id();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!m_SelectedSceneObject); disabled)
        {
            if (ui.Button("Duplicate"))
            {
                RecordSceneUndo();
                m_SelectedSceneObject = m_EditingScene->DuplicateObject(m_SelectedSceneObject).Id();
            }
            ui.SameLine();
            if (ui.Button("Delete"))
            {
                RecordSceneUndo();
                (void)m_EditingScene->DestroyObject(m_SelectedSceneObject);
                m_SelectedSceneObject = {};
            }
        }
        ui.Separator();
        const auto objects = m_EditingScene->Objects();
        for (const auto& object : objects)
        {
            std::size_t depth = 0;
            auto parent = object.Parent;
            while (parent && depth < 512)
            {
                ++depth;
                const auto found = std::ranges::find(objects, parent, &Keire::SceneObjectDefinition::Id);
                parent = found == objects.end() ? Keire::AssetId{} : found->Parent;
            }
            auto id = ui.PushId(object.Id.ToString());
            const auto label = std::string(depth * 2, ' ') + (object.Active ? "" : "[inactive] ") + object.Name;
            if (ui.Selectable(label, object.Id == m_SelectedSceneObject))
                m_SelectedSceneObject = object.Id;
            if (auto source = ui.BeginDragSource(); source)
            {
                const auto value = object.Id.ToString();
                ui.SetDragPayload("KEIRE_SCENE_OBJECT", std::as_bytes(std::span(value.data(), value.size())));
                ui.Text(object.Name);
            }
            if (auto target = ui.BeginDragTarget(); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
                {
                    const std::string value(reinterpret_cast<const char*>(payload.data()), payload.size());
                    const auto child = Keire::AssetId::Parse(value);
                    if (child != object.Id)
                    {
                        RecordSceneUndo();
                        (void)m_EditingScene->ReparentObject(child, object.Id);
                    }
                }
            }
        }
    }
}

void EditorWorkspaceLayer::AddConsoleMessage(std::string category, std::string message, const Keire::UiColor color)
{
    constexpr std::size_t maximumMessages = 10'000;
    if (m_ConsoleMessages.size() == maximumMessages)
        m_ConsoleMessages.pop_front();
    m_ConsoleMessages.push_back({std::move(category), std::move(message), color, Owner().GetTime().FrameCount()});
}

void EditorWorkspaceLayer::DrawConsole(Keire::UiFrame& ui)
{
    if (auto console = ui.BeginPanel(m_Console); console)
    {
        ui.TextColored(m_Theme.Accent, "CONSOLE");
        ui.Separator();
        (void)ui.InputText("Search", m_ConsoleSearch);
        ui.SameLine();
        if (ui.Checkbox("Pause", m_ConsolePaused))
        {
            if (m_ConsolePaused)
                m_PausedConsoleSnapshot.assign(m_ConsoleMessages.begin(), m_ConsoleMessages.end());
            else
                m_PausedConsoleSnapshot.clear();
        }
        ui.SameLine();
        if (ui.Button("Clear"))
        {
            m_ConsoleMessages.clear();
            m_PausedConsoleSnapshot.clear();
        }
        if (m_ConsoleMessages.empty())
        {
            ui.TextColored(m_Theme.Success, "Ready");
            return;
        }
        const auto drawEntries = [&](const auto& entries)
        {
            for (const auto& entry : entries)
            {
                if (!m_ConsoleSearch.empty() && entry.Category.find(m_ConsoleSearch) == std::string::npos &&
                    entry.Message.find(m_ConsoleSearch) == std::string::npos)
                    continue;
                ui.TextColored(entry.Color,
                               "[" + std::to_string(entry.Frame) + "] [" + entry.Category + "] " + entry.Message);
            }
        };
        if (m_ConsolePaused)
            drawEntries(m_PausedConsoleSnapshot);
        else
            drawEntries(m_ConsoleMessages);
    }
}

void EditorWorkspaceLayer::BeginInputTest()
{
    if (!m_InputContext || m_InputDocument.ActionMaps.empty())
        throw std::logic_error("Open an imported input action asset before starting Input Test Mode.");
    EndInputTest();
    std::vector<Keire::InputActionSubscription> subscriptions;
    std::vector<Keire::InputCaptureOverride> captureOverrides;
    try
    {
        for (const auto& map : m_InputDocument.ActionMaps)
        {
            if (!m_InputContext->EnableMap(map.Id))
                throw std::runtime_error("Input action context is still loading; try again next frame.");
            captureOverrides.push_back(m_InputContext->OverrideUiCapture(map.Id));
            for (const auto& action : map.Actions)
            {
                subscriptions.push_back(m_InputContext->Subscribe(
                    action.Id,
                    [this, mapName = map.Name, actionName = action.Name](const Keire::InputActionEvent& event)
                    {
                        try
                        {
                            const auto phase = event.Phase == Keire::InputActionPhase::Started     ? "Started"
                                               : event.Phase == Keire::InputActionPhase::Performed ? "Performed"
                                               : event.Phase == Keire::InputActionPhase::Canceled  ? "Canceled"
                                                                                                   : "Waiting";
                            std::string scheme = "Automatic";
                            if (const auto input = Owner().Input())
                            {
                                const auto users = input->Users();
                                const auto user = std::ranges::find(users, event.User, &Keire::InputUserDescriptor::Id);
                                if (user != users.end() && !user->ControlScheme.empty())
                                    scheme = user->ControlScheme;
                            }
                            std::ostringstream message;
                            message << mapName << '/' << actionName << ' ' << phase << " value=[" << event.Value.X
                                    << ", " << event.Value.Y << "] user=" << event.User.Value()
                                    << " device=" << event.Device.Value() << " scheme=" << scheme
                                    << " duration=" << event.DurationSeconds
                                    << "s timestamp=" << event.TimestampNanoseconds << "ns";
                            AddConsoleMessage("Input", message.str(), m_Theme.Accent);
                        }
                        catch (...)
                        {
                        }
                    }));
            }
        }
    }
    catch (...)
    {
        m_InputContext->DisableAll();
        throw;
    }
    m_InputSubscriptions = std::move(subscriptions);
    m_InputCaptureOverrides = std::move(captureOverrides);
    m_InputTesting = true;
    AddConsoleMessage("Input", "Input Test Mode started; UI capture is bypassed for test maps.", m_Theme.Success);
}

void EditorWorkspaceLayer::EndInputTest() noexcept
{
    m_InputSubscriptions.clear();
    m_InputCaptureOverrides.clear();
    if (m_InputContext)
        m_InputContext->DisableAll();
    m_InputTesting = false;
}

void EditorWorkspaceLayer::DrawInputDebugger(Keire::UiFrame& ui)
{
    if (auto debugger = ui.BeginPanel(m_InputDebugger); debugger)
    {
        ui.TextColored(m_Theme.Accent, "INPUT DEBUGGER");
        ui.Separator();
        if (!m_InputAsset)
        {
            ui.Text("No input action asset is attached.");
            if (const auto project = Owner().GetProject(); project && project->Descriptor().DefaultInput)
            {
                if (ui.Button("Attach Project Default Input"))
                {
                    try
                    {
                        OpenInputActions(project->Descriptor().DefaultInput);
                    }
                    catch (const std::exception& error)
                    {
                        m_InputMessage = error.what();
                    }
                }
            }
            return;
        }
        ui.Text(m_InputDocument.Name);
        if (!m_InputTesting)
        {
            if (ui.Button("Start Input Test"))
            {
                try
                {
                    BeginInputTest();
                }
                catch (const std::exception& error)
                {
                    m_InputMessage = error.what();
                }
            }
        }
        else if (ui.Button("Stop Input Test"))
            EndInputTest();
        if (!m_InputMessage.empty())
            ui.TextColored(m_Theme.MutedText, m_InputMessage);
        if (const auto input = Owner().Input())
        {
            ui.Separator();
            ui.Text("DEVICES");
            for (const auto& device : input->Devices())
            {
                ui.Text(device.Name + "  id=" + std::to_string(device.Id.Value()) +
                        (device.Connected ? "  connected" : "  disconnected") +
                        (device.Paired ? "  paired" : "  unpaired"));
            }
            ui.Text("USERS");
            for (const auto& user : input->Users())
                ui.Text(user.Name + "  scheme=" + (user.ControlScheme.empty() ? "Automatic" : user.ControlScheme));
        }
        ui.Separator();
        ui.TextColored(m_Theme.MutedText,
                       "Action events are recorded in Console with processed values, user IDs, and device IDs.");
    }
}

void EditorWorkspaceLayer::DrawInputActionsEditor(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_InputActionsEditor); panel)
    {
        if (!m_InputAsset)
        {
            DrawEmptyState(ui, "INPUT ACTIONS", "No input action asset is open.",
                           "Select a .keireinput asset and choose Edit Input Actions in the Inspector.");
            return;
        }

        const auto record = m_AssetDatabase ? m_AssetDatabase->Find(m_InputAsset) : std::nullopt;
        ui.TextColored(m_Theme.Accent, "INPUT ACTIONS");
        ui.SameLine();
        ui.Text(record ? record->RelativePath.generic_string() + (m_InputDirty ? " *" : "") : "Missing asset");
        ui.Separator();
        if (ui.Shortcut({Keire::UiKey::S, true}) && m_InputDirty)
        {
            try
            {
                SaveInputActions();
            }
            catch (const std::exception& error)
            {
                m_InputMessage = error.what();
            }
        }
        if (ui.Shortcut({Keire::UiKey::Z, true}) && !m_InputUndo.empty())
            UndoInputEdit();
        if (ui.Shortcut({Keire::UiKey::Y, true}) && !m_InputRedo.empty())
            RedoInputEdit();
        if (ui.Button("Save"))
        {
            try
            {
                SaveInputActions();
            }
            catch (const std::exception& error)
            {
                m_InputMessage = error.what();
            }
        }
        ui.SameLine();
        if (ui.Button("Revert"))
        {
            try
            {
                OpenInputActions(m_InputAsset);
            }
            catch (const std::exception& error)
            {
                m_InputMessage = error.what();
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_InputUndo.empty()); disabled)
        {
            if (ui.Button("Undo"))
                UndoInputEdit();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_InputRedo.empty()); disabled)
        {
            if (ui.Button("Redo"))
                RedoInputEdit();
        }
        ui.SameLine();
        if (ui.Button("Validate"))
        {
            try
            {
                Keire::InputActionAsset::Validate(m_InputDocument);
                m_InputMessage = "Validation passed.";
            }
            catch (const std::exception& error)
            {
                m_InputMessage = error.what();
            }
        }
        ui.SameLine();
        (void)ui.Checkbox("Live Monitor", m_InputLiveMonitor);
        (void)ui.InputText("Search", m_InputSearch);
        if (!m_InputMessage.empty())
            ui.TextColored(m_Theme.MutedText, m_InputMessage);
        ui.Separator();

        auto findMap = [&]() -> Keire::InputActionMapDefinition*
        {
            const auto found =
                std::ranges::find(m_InputDocument.ActionMaps, m_SelectedInputMap, &Keire::InputActionMapDefinition::Id);
            return found == m_InputDocument.ActionMaps.end() ? nullptr : &*found;
        };
        auto map = findMap();
        if (auto maps = ui.BeginChild("InputMaps", {230.0F, 0.0F}, true); maps)
        {
            ui.TextColored(m_Theme.Accent, "ACTION MAPS");
            for (const auto& candidate : m_InputDocument.ActionMaps)
            {
                if (!m_InputSearch.empty() && candidate.Name.find(m_InputSearch) == std::string::npos)
                    continue;
                if (ui.Selectable(candidate.Name, candidate.Id == m_SelectedInputMap))
                {
                    m_SelectedInputMap = candidate.Id;
                    m_SelectedInputAction = {};
                    m_SelectedInputBinding = {};
                }
            }
            ui.Separator();
            ui.TextColored(m_Theme.MutedText, "CONTROL SCHEMES");
            for (const auto& scheme : m_InputDocument.ControlSchemes)
                ui.Text(scheme.Name + "  [" + scheme.BindingGroup + "]");
            if (ui.Button("+ Map"))
            {
                RecordInputUndo();
                Keire::InputActionMapDefinition added;
                added.Id = Keire::AssetId::Generate();
                added.Name = "New Map";
                m_SelectedInputMap = added.Id;
                m_InputDocument.ActionMaps.push_back(std::move(added));
            }
        }
        ui.SameLine();
        map = findMap();
        if (auto actions = ui.BeginChild("InputActions", {430.0F, 0.0F}, true); actions)
        {
            ui.TextColored(m_Theme.Accent, map ? map->Name : "ACTIONS");
            if (!map)
                ui.TextColored(m_Theme.MutedText, "Select or create an action map.");
            else
            {
                if (m_InputContext)
                    (void)m_InputContext->EnableMap(map->Id);
                for (const auto& action : map->Actions)
                {
                    auto actionId = ui.PushId(action.Id.ToString());
                    if (ui.Selectable(action.Name, action.Id == m_SelectedInputAction))
                    {
                        m_SelectedInputAction = action.Id;
                        m_SelectedInputBinding = {};
                    }
                    for (const auto& binding : map->Bindings)
                    {
                        if (binding.Action != action.Id)
                            continue;
                        auto bindingId = ui.PushId(binding.Id.ToString());
                        const auto label =
                            "   " + (binding.Name.empty() ? binding.Path : binding.Name + ": " + binding.Path);
                        if (ui.Selectable(label, binding.Id == m_SelectedInputBinding))
                        {
                            m_SelectedInputAction = action.Id;
                            m_SelectedInputBinding = binding.Id;
                        }
                    }
                }
                if (ui.Button("+ Action"))
                {
                    RecordInputUndo();
                    Keire::InputActionDefinition action;
                    action.Id = Keire::AssetId::Generate();
                    action.Name = "New Action";
                    m_SelectedInputAction = action.Id;
                    map->Actions.push_back(std::move(action));
                }
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(!m_SelectedInputAction); disabled)
                {
                    if (ui.Button("+ Binding"))
                    {
                        RecordInputUndo();
                        Keire::InputBindingDefinition binding;
                        binding.Id = Keire::AssetId::Generate();
                        binding.Action = m_SelectedInputAction;
                        binding.Path = "<Keyboard>/space";
                        m_SelectedInputBinding = binding.Id;
                        map->Bindings.push_back(std::move(binding));
                    }
                }
            }
        }
        ui.SameLine();
        map = findMap();
        if (auto properties = ui.BeginChild("InputProperties", {}, true); properties)
        {
            ui.TextColored(m_Theme.Accent, "PROPERTIES");
            if (!map)
                return;
            if (!m_SelectedInputAction)
            {
                auto name = map->Name;
                if (ui.InputText("Map Name", name))
                {
                    RecordInputUndo();
                    map->Name = std::move(name);
                }
                bool alwaysReceive = map->CapturePolicy == Keire::InputCapturePolicy::AlwaysReceive;
                if (ui.Checkbox("Always Receive", alwaysReceive))
                {
                    RecordInputUndo();
                    map->CapturePolicy = alwaysReceive ? Keire::InputCapturePolicy::AlwaysReceive
                                                       : Keire::InputCapturePolicy::RespectUiCapture;
                }
            }
            else
            {
                auto action = std::ranges::find(map->Actions, m_SelectedInputAction, &Keire::InputActionDefinition::Id);
                if (action != map->Actions.end())
                {
                    auto name = action->Name;
                    if (ui.InputText("Action Name", name))
                    {
                        RecordInputUndo();
                        action->Name = std::move(name);
                    }
                    ui.Text("Type: " + std::to_string(static_cast<int>(action->Type)));
                    ui.Text("Value: " + std::to_string(static_cast<int>(action->ValueType)));
                }
                if (m_SelectedInputBinding)
                {
                    auto binding =
                        std::ranges::find(map->Bindings, m_SelectedInputBinding, &Keire::InputBindingDefinition::Id);
                    if (binding != map->Bindings.end())
                    {
                        auto name = binding->Name;
                        if (ui.InputText("Binding Name", name))
                        {
                            RecordInputUndo();
                            binding->Name = std::move(name);
                        }
                        auto path = binding->Path;
                        if (ui.InputText("Control Path", path))
                        {
                            RecordInputUndo();
                            binding->Path = std::move(path);
                        }
                        if (ui.Button("Listen"))
                        {
                            try
                            {
                                if (!m_InputContext)
                                    throw std::runtime_error("The runtime input context is not ready.");
                                m_Rebind = Owner().Input()->BeginInteractiveRebind(m_InputContext, binding->Id);
                                m_InputMessage = "Listening for a control...";
                            }
                            catch (const std::exception& error)
                            {
                                m_InputMessage = error.what();
                            }
                        }
                        if (m_Rebind)
                        {
                            const auto status = m_Rebind->Status();
                            if (status == Keire::RebindStatus::Listening)
                            {
                                ui.Text("Listening... " + std::to_string(m_Rebind->RemainingSeconds()) + "s");
                                ui.ProgressBar(static_cast<float>(1.0 - m_Rebind->RemainingSeconds() / 5.0),
                                               {0.0F, 4.0F}, {});
                            }
                            else if (status == Keire::RebindStatus::Candidate)
                            {
                                ui.TextColored(m_Theme.Success, "Candidate: " + m_Rebind->CandidatePath());
                                const auto conflicts = m_Rebind->Conflicts();
                                if (!conflicts.empty())
                                    ui.TextColored(m_Theme.Warning,
                                                   std::to_string(conflicts.size()) + " binding conflict(s).");
                                if (ui.Button(conflicts.empty() ? "Accept" : "Replace"))
                                {
                                    const auto targetBinding = m_Rebind->TargetBinding();
                                    const auto candidatePath = m_Rebind->CandidatePath();
                                    RecordInputUndo();
                                    if (!conflicts.empty())
                                    {
                                        std::erase_if(map->Bindings,
                                                      [&](const auto& candidate)
                                                      {
                                                          return std::ranges::any_of(
                                                              conflicts, [&](const auto& conflict)
                                                              { return conflict.Binding == candidate.Id; });
                                                      });
                                    }
                                    binding = std::ranges::find(map->Bindings, targetBinding,
                                                                &Keire::InputBindingDefinition::Id);
                                    if (binding != map->Bindings.end())
                                        binding->Path = candidatePath;
                                    m_Rebind->Apply(conflicts.empty() ? Keire::RebindConflictResolution::KeepBoth
                                                                      : Keire::RebindConflictResolution::Replace);
                                    if (m_Rebind->Status() == Keire::RebindStatus::Completed)
                                        m_Rebind.Reset();
                                }
                                ui.SameLine();
                                if (!conflicts.empty() && ui.Button("Keep Both"))
                                {
                                    const auto targetBinding = m_Rebind->TargetBinding();
                                    const auto candidatePath = m_Rebind->CandidatePath();
                                    RecordInputUndo();
                                    const auto target = std::ranges::find(map->Bindings, targetBinding,
                                                                          &Keire::InputBindingDefinition::Id);
                                    if (target != map->Bindings.end())
                                        target->Path = candidatePath;
                                    m_Rebind->Apply(Keire::RebindConflictResolution::KeepBoth);
                                    if (m_Rebind->Status() == Keire::RebindStatus::Completed)
                                        m_Rebind.Reset();
                                }
                                ui.SameLine();
                                if (ui.Button("Cancel"))
                                {
                                    m_Rebind->Cancel();
                                    m_Rebind.Reset();
                                }
                            }
                            else
                                m_Rebind.Reset();
                        }
                    }
                }
                if (m_InputLiveMonitor && m_InputContext)
                {
                    ui.Separator();
                    ui.TextColored(m_Theme.Accent, "LIVE VALUE");
                    const auto handle = m_InputContext->FindAction(m_SelectedInputAction);
                    if (handle)
                    {
                        const auto value = handle.Value();
                        ui.Text("Phase " + std::to_string(static_cast<int>(handle.Phase())) + "  [" +
                                std::to_string(value.X) + ", " + std::to_string(value.Y) + "]");
                    }
                    if (const auto input = Owner().Input())
                    {
                        ui.TextColored(m_Theme.MutedText, std::to_string(input->Devices().size()) + " device(s), " +
                                                              std::to_string(input->Users().size()) + " user(s)");
                    }
                }
            }
        }
    }
}

void EditorWorkspaceLayer::DrawProject(Keire::UiFrame& ui)
{
    if (auto project = ui.BeginPanel(m_Project); project)
    {
        ui.TextColored(m_Theme.Accent, "PROJECT");
        ui.Separator();
        if (!m_AssetDatabase)
        {
            ui.TextColored(m_Theme.Error, m_AssetStatus.empty() ? "Asset database is unavailable." : m_AssetStatus);
            return;
        }
        if (ui.Button("Refresh + Import"))
        {
            try
            {
                ImportAssets();
            }
            catch (const std::exception& error)
            {
                m_AssetStatus = std::string("Asset import failed: ") + error.what();
            }
        }
        ui.SameLine();
        if (ui.Button("Cook Dist"))
            CookAssets();
        (void)ui.InputText("New Folder", m_NewAssetFolder);
        ui.SameLine();
        if (ui.Button("Create") && !m_NewAssetFolder.empty())
        {
            try
            {
                m_AssetDatabase->CreateFolder(m_NewAssetFolder);
                m_NewAssetFolder.clear();
                m_AssetStatus = "Created asset folder.";
            }
            catch (const std::exception& error)
            {
                m_AssetStatus = std::string("Folder creation failed: ") + error.what();
            }
        }
        if (!m_AssetStatus.empty())
            ui.TextColored(m_Theme.MutedText, m_AssetStatus);
        ui.Separator();
        if (m_AssetRecords.empty())
        {
            ui.Text("No assets found under Assets/.");
            return;
        }
        for (const auto& record : m_AssetRecords)
        {
            auto id = ui.PushId(record.Id.ToString());
            if (ui.Selectable(record.RelativePath.generic_string(), record.Id == m_SelectedAsset))
                m_SelectedAsset = record.Id;
            const auto itemState = ui.LastItemState();
            if (itemState.DoubleClicked && record.RelativePath.extension() == ".keireinput")
            {
                try
                {
                    OpenInputActions(record.Id);
                }
                catch (const std::exception& error)
                {
                    m_AssetStatus = std::string("Input editor failed to open: ") + error.what();
                }
            }
            else if (itemState.DoubleClicked && record.RelativePath.extension() == ".keirescene")
            {
                try
                {
                    RequestOpenScene(record.Id);
                }
                catch (const std::exception& error)
                {
                    m_AssetStatus = std::string("Scene failed to open: ") + error.what();
                }
            }
            if (auto context = ui.BeginItemContextMenu(); context)
            {
                if (ui.MenuItem("Edit", false, record.RelativePath.extension() == ".keireinput"))
                {
                    try
                    {
                        OpenInputActions(record.Id);
                    }
                    catch (const std::exception& error)
                    {
                        m_AssetStatus = std::string("Input editor failed to open: ") + error.what();
                    }
                }
                if (ui.MenuItem("Open Scene", false, record.RelativePath.extension() == ".keirescene"))
                {
                    try
                    {
                        RequestOpenScene(record.Id);
                    }
                    catch (const std::exception& error)
                    {
                        m_AssetStatus = std::string("Scene failed to open: ") + error.what();
                    }
                }
            }
            if (auto source = ui.BeginDragSource(); source)
            {
                const auto value = record.Id.ToString();
                ui.SetDragPayload("KEIRE_ASSET", std::as_bytes(std::span(value.data(), value.size())));
                ui.Text(record.RelativePath.generic_string());
            }
        }
    }
}

void EditorWorkspaceLayer::DrawInspector(Keire::UiFrame& ui)
{
    if (auto inspector = ui.BeginPanel(m_Inspector); inspector)
    {
        ui.TextColored(m_Theme.Accent, "INSPECTOR");
        ui.Separator();
        if (m_EditingScene && m_SelectedSceneObject)
        {
            const auto object = m_EditingScene->Find(m_SelectedSceneObject).Snapshot();
            if (object)
            {
                auto name = object->Name;
                if (ui.InputText("Object Name", name))
                {
                    RecordSceneUndo();
                    (void)m_EditingScene->RenameObject(object->Id, std::move(name));
                }
                auto active = object->Active;
                if (ui.Checkbox("Active", active))
                {
                    RecordSceneUndo();
                    (void)m_EditingScene->SetObjectActive(object->Id, active);
                }
                auto transform = object->Transform;
                bool changed = false;
                changed |= ui.SliderFloat("Position X", transform.Position.X, -10000.0F, 10000.0F);
                changed |= ui.SliderFloat("Position Y", transform.Position.Y, -10000.0F, 10000.0F);
                changed |= ui.SliderFloat("Position Z", transform.Position.Z, -10000.0F, 10000.0F);
                auto rotation = QuaternionToEuler(transform.Rotation);
                bool rotationChanged = false;
                rotationChanged |= ui.SliderFloat("Rotation X", rotation.X, -180.0F, 180.0F);
                rotationChanged |= ui.SliderFloat("Rotation Y", rotation.Y, -180.0F, 180.0F);
                rotationChanged |= ui.SliderFloat("Rotation Z", rotation.Z, -180.0F, 180.0F);
                if (rotationChanged)
                    transform.Rotation = EulerToQuaternion(rotation);
                changed |= rotationChanged;
                changed |= ui.SliderFloat("Scale X", transform.Scale.X, -100.0F, 100.0F);
                changed |= ui.SliderFloat("Scale Y", transform.Scale.Y, -100.0F, 100.0F);
                changed |= ui.SliderFloat("Scale Z", transform.Scale.Z, -100.0F, 100.0F);
                if (changed)
                {
                    RecordSceneUndo();
                    (void)m_EditingScene->SetObjectTransform(object->Id, transform);
                }
                ui.TextColored(m_Theme.MutedText, "Object ID");
                ui.Text(object->Id.ToString());
                return;
            }
            m_SelectedSceneObject = {};
        }
        if (!m_SelectedAsset || !m_AssetDatabase)
        {
            ui.Text("Nothing selected");
            ui.TextColored(m_Theme.MutedText, "Select an asset in the Project panel.");
            return;
        }
        const auto record = m_AssetDatabase->Find(m_SelectedAsset);
        if (!record)
        {
            m_SelectedAsset = {};
            ui.TextColored(m_Theme.Warning, "The selected asset no longer exists.");
            return;
        }
        if (m_EditingAsset != record->Id)
        {
            m_EditingAsset = record->Id;
            m_AssetName = record->RelativePath.filename().string();
        }
        ui.Text(record->RelativePath.generic_string());
        ui.TextColored(m_Theme.MutedText, "Asset ID");
        ui.Text(record->Id.ToString());
        ui.TextColored(m_Theme.MutedText, "Importer");
        ui.Text(record->Importer + " v" + std::to_string(record->ImporterVersion));
        ui.TextColored(m_Theme.MutedText, "Content SHA-256");
        ui.Text(record->SourceDigest);
        if (record->RelativePath.extension() == ".keireinput")
        {
            ui.Separator();
            ui.TextColored(m_Theme.Accent, "INPUT ACTION ASSET");
            ui.Text("Action maps, bindings, control schemes, and runtime overrides.");
            if (ui.Button("Edit Input Actions"))
            {
                try
                {
                    if (m_InputDirty && m_InputAsset != record->Id)
                        throw std::runtime_error("Save or Revert the currently edited input asset before switching.");
                    OpenInputActions(record->Id);
                }
                catch (const std::exception& error)
                {
                    m_AssetStatus = std::string("Input editor failed to open: ") + error.what();
                }
            }
        }
        ui.Separator();
        (void)ui.InputText("Name", m_AssetName);
        if (ui.Button("Rename") && !m_AssetName.empty())
        {
            try
            {
                m_AssetDatabase->Rename(record->Id, m_AssetName);
                m_AssetRecords = m_AssetDatabase->Records();
                m_AssetStatus = "Renamed asset and preserved its metadata identity.";
            }
            catch (const std::exception& error)
            {
                m_AssetStatus = std::string("Asset rename failed: ") + error.what();
            }
        }
        ui.SameLine();
        if (ui.Button("Duplicate"))
        {
            try
            {
                const auto stem = record->RelativePath.stem().string();
                const auto extension = record->RelativePath.extension().string();
                auto destination = record->RelativePath.parent_path() / (stem + " Copy" + extension);
                for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
                    destination =
                        record->RelativePath.parent_path() / (stem + " Copy " + std::to_string(copy) + extension);
                m_SelectedAsset = m_AssetDatabase->Duplicate(record->Id, destination);
                m_AssetRecords = m_AssetDatabase->Records();
                m_AssetStatus = "Duplicated asset with a new stable identity.";
            }
            catch (const std::exception& error)
            {
                m_AssetStatus = std::string("Asset duplication failed: ") + error.what();
            }
        }
        ui.SameLine();
        if (ui.Button("Move to Trash"))
        {
            try
            {
                const auto trash = m_AssetDatabase->MoveToTrash(record->Id);
                m_SelectedAsset = {};
                m_EditingAsset = {};
                m_AssetRecords = m_AssetDatabase->Records();
                m_AssetStatus = "Moved asset to recoverable trash: " + trash.string();
            }
            catch (const std::exception& error)
            {
                m_AssetStatus = std::string("Asset trash operation failed: ") + error.what();
            }
        }
    }
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
