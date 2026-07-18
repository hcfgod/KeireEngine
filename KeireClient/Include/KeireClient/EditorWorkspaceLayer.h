#pragma once

#include "Keire/Core.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class EditorWorkspaceLayer final : public Keire::Layer
{
  public:
    explicit EditorWorkspaceLayer(bool smoke, bool initializeProject = false);

  protected:
    void OnAttach() override;
    void OnDetach() noexcept override;
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
        DirtyTheme,
        DirtyScene
    };

    enum class PendingSceneAction : std::uint8_t
    {
        None,
        Create,
        Open,
        Close,
        Exit
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
    void DrawDirtySceneDialog(Keire::UiFrame& ui);
    void DrawThemeEditor(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawInputActionsEditor(Keire::UiFrame& ui);
    void DrawInputDebugger(Keire::UiFrame& ui);
    void DrawScene(Keire::UiFrame& ui);
    void DrawHierarchy(Keire::UiFrame& ui);
    void DrawConsole(Keire::UiFrame& ui);
    void DrawDiagnostics(Keire::UiFrame& ui);
    void DrawProject(Keire::UiFrame& ui);
    void DrawInspector(Keire::UiFrame& ui);
    void ImportAssets();
    void CookAssets();
    void CreateInputActions(Keire::InputActionAssetDefinition definition, std::string_view baseName);
    void OpenInputActions(Keire::AssetId asset);
    void SaveInputActions();
    void RecordInputUndo();
    void UndoInputEdit();
    void RedoInputEdit();
    void BeginInputTest();
    void EndInputTest() noexcept;
    void AddConsoleMessage(std::string category, std::string message, Keire::UiColor color);
    void CreateScene();
    void RequestCreateScene();
    void OpenScene(Keire::AssetId asset);
    void RequestOpenScene(Keire::AssetId asset);
    void SaveScene();
    void RequestCloseScene();
    void CloseScene();
    void ExecutePendingSceneAction();
    void WriteSceneRecovery();
    void RestoreSceneRecovery();
    void DiscardSceneRecovery() noexcept;
    void RecordSceneUndo();
    void UndoSceneEdit();
    void RedoSceneEdit();
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
    Keire::UiPanelRegistration m_InputActionsEditor;
    Keire::UiPanelRegistration m_InputDebugger;
    Keire::UiThemeDefinition m_Theme;
    Keire::UiThemeId m_PendingTheme;
    Dialog m_Dialog = Dialog::None;
    std::string m_ProfileName;
    std::string m_Error;
    std::string m_Notice;
    std::string m_AssetStatus;
    std::string m_AssetName;
    std::string m_NewAssetFolder;
    Keire::Ref<Keire::AssetDatabase> m_AssetDatabase;
    std::vector<Keire::AssetSourceRecord> m_AssetRecords;
    Keire::AssetId m_SelectedAsset;
    Keire::AssetId m_EditingAsset;
    Keire::AssetId m_InputAsset;
    Keire::AssetId m_SelectedInputMap;
    Keire::AssetId m_SelectedInputAction;
    Keire::AssetId m_SelectedInputBinding;
    Keire::InputActionAssetDefinition m_InputDocument;
    std::vector<Keire::InputActionAssetDefinition> m_InputUndo;
    std::vector<Keire::InputActionAssetDefinition> m_InputRedo;
    Keire::Ref<Keire::InputActionContext> m_InputContext;
    Keire::Ref<Keire::InteractiveRebindOperation> m_Rebind;
    std::vector<Keire::InputActionSubscription> m_InputSubscriptions;
    std::vector<Keire::InputCaptureOverride> m_InputCaptureOverrides;
    Keire::InputUserId m_EditorInputUser;
    std::string m_InputSearch;
    std::string m_InputMessage;
    struct ConsoleMessage
    {
        std::string Category;
        std::string Message;
        Keire::UiColor Color;
        std::uint64_t Frame = 0;
    };
    std::deque<ConsoleMessage> m_ConsoleMessages;
    std::vector<ConsoleMessage> m_PausedConsoleSnapshot;
    std::string m_ConsoleSearch;
    Keire::Ref<Keire::Scene> m_EditingScene;
    Keire::Ref<Keire::SceneLoadOperation> m_SceneLoad;
    Keire::AssetId m_SceneAsset;
    Keire::AssetId m_SelectedSceneObject;
    std::vector<Keire::SceneDefinition> m_SceneUndo;
    std::vector<Keire::SceneDefinition> m_SceneRedo;
    std::filesystem::path m_SceneSource;
    std::filesystem::path m_SceneRecovery;
    std::string m_SceneStatus;
    PendingSceneAction m_PendingSceneAction = PendingSceneAction::None;
    Keire::AssetId m_PendingSceneAsset;
    Keire::UiColor m_NoticeColor;
    std::uint32_t m_FrameCount = 0;
    double m_AssetPollSeconds = 0.0;
    double m_SceneRecoverySeconds = 0.0;
    bool m_ThemeDirty = false;
    bool m_InputDirty = false;
    bool m_InputLiveMonitor = false;
    bool m_InputTesting = false;
    bool m_ConsolePaused = false;
    bool m_SceneRecoveryAvailable = false;
    bool m_CloseThemeAfterDecision = false;
    bool m_OpenDialog = false;
    bool m_Smoke = false;
    bool m_InitializeProject = false;
};
