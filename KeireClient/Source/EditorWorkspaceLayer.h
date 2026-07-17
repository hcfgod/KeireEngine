#pragma once

#include "Keire/Core.h"

#include <cstdint>
#include <string>
#include <string_view>

class EditorWorkspaceLayer final : public Keire::Layer
{
  public:
    explicit EditorWorkspaceLayer(bool smoke);

  protected:
    void OnAttach() override;
    void OnUpdate(const Keire::Time& time) override;
    void OnUi(Keire::UiFrame& ui) override;

  private:
    enum class Dialog : std::uint8_t
    {
        None,
        SaveLayout,
        RenameLayout,
        DeleteLayout,
        SaveTheme,
        RenameTheme,
        DeleteTheme,
        DirtyTheme
    };

    static void DrawEmptyState(Keire::UiFrame& ui, std::string_view heading, std::string_view primary,
                               std::string_view detail);
    static void DrawPanelMenuItem(Keire::UiFrame& ui, Keire::UiPanelRegistration& panel);
    void DrawMainMenu(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawNotices(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawDialogs(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawNameDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace, std::string_view title, Dialog dialog);
    void DrawDeleteDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace, std::string_view title, bool theme);
    void DrawDirtyThemeDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawThemeEditor(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawDiagnostics(Keire::UiFrame& ui);
    void OpenDialog(Dialog dialog);
    void OpenPendingDialog(Keire::UiFrame& ui);
    void RequestTheme(Keire::UiWorkspace& workspace, Keire::UiThemeId id);
    void LoadTheme(Keire::UiWorkspace& workspace, Keire::UiThemeId id);

    Keire::UiPanelRegistration m_Scene;
    Keire::UiPanelRegistration m_Game;
    Keire::UiPanelRegistration m_Hierarchy;
    Keire::UiPanelRegistration m_Inspector;
    Keire::UiPanelRegistration m_Project;
    Keire::UiPanelRegistration m_Console;
    Keire::UiPanelRegistration m_Diagnostics;
    Keire::UiPanelRegistration m_ThemeEditor;
    Keire::UiThemeDefinition m_Theme;
    Keire::UiThemeId m_PendingTheme;
    Dialog m_Dialog = Dialog::None;
    std::string m_ProfileName;
    std::string m_Error;
    std::string m_Notice;
    Keire::UiColor m_NoticeColor;
    std::uint32_t m_FrameCount = 0;
    bool m_ThemeDirty = false;
    bool m_CloseThemeAfterDecision = false;
    bool m_OpenDialog = false;
    bool m_Smoke = false;
};
