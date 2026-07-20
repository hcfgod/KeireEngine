#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/EditorPanels.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace KeireEditor
{
    class AssetBrowserPanel;
    class ConsolePanel;
    class DiagnosticsPanel;
    class EditorCommandRouter;
    class InputActionsDocument;
    class InputActionsPanel;
    class InspectorPanel;
    class HierarchyPanel;
    class ProjectSettingsPanel;
    class SceneDocument;
    class SceneViewportPanel;
    class SceneGizmoController;
} // namespace KeireEditor

namespace Keire::Detail
{
    class EditorCameraController;
} // namespace Keire::Detail

class EditorWorkspaceLayer final : public Keire::Layer,
                                   private KeireEditor::ISceneViewportController,
                                   private KeireEditor::IHierarchyController,
                                   private KeireEditor::IInspectorController,
                                   private KeireEditor::IInputActionsController,
                                   private KeireEditor::IProjectSettingsController
{
  public:
    explicit EditorWorkspaceLayer(bool smoke, bool initializeProject = false);
    ~EditorWorkspaceLayer() override;

  protected:
    void OnAttach() override;
    void OnDetach() noexcept override;
    void OnFixedUpdate(const Keire::Time& time) override;
    void OnUpdate(const Keire::Time& time) override;
    void OnUi(Keire::UiFrame& ui) override;

  private:
    friend class KeireEditor::AssetBrowserPanel;
    friend class KeireEditor::ConsolePanel;
    friend class KeireEditor::DiagnosticsPanel;
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
        DirtyScene,
        RenameEntity
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
    void DrawInputActionsEditor(Keire::UiFrame& ui) override;
    void DrawInputDebugger(Keire::UiFrame& ui);
    void DrawProjectSettings(Keire::UiFrame& ui) override;
    void DrawScene(Keire::UiFrame& ui) override;
    void DrawGame(Keire::UiFrame& ui);
    void DrawHierarchy(Keire::UiFrame& ui) override;
    void DrawConsole(Keire::UiFrame& ui);
    void DrawDiagnostics(Keire::UiFrame& ui);
    void DrawProject(Keire::UiFrame& ui);
    void DrawInspector(Keire::UiFrame& ui) override;
    void UpdateSceneCamera(Keire::UiFrame& ui, const Keire::UiItemState& imageState);
    void LoadSceneCamera();
    void SaveSceneCamera() noexcept;
    void ImportAssets();
    void CookAssets();
    void CreateInputActions(Keire::InputActionAssetDefinition definition, std::string_view baseName);
    void CreateUnlitShader();
    void CreateMaterial();
    void OpenInputActions(Keire::AssetId asset);
    void SaveInputActions();
    void RecordInputUndo(std::string_view name = "Edit Input Actions");
    void UndoInputEdit();
    void RedoInputEdit();
    void BeginInputTest();
    void EndInputTest() noexcept;
    void AddConsoleMessage(std::string category, std::string message, Keire::UiColor color,
                           Keire::LogLevel level = Keire::LogLevel::Info) noexcept;
    void ReportError(std::string category, std::string message) noexcept;
    void SetAssetError(std::string message) noexcept;
    void CreateScene();
    void RequestCreateScene();
    void OpenScene(Keire::AssetId asset);
    void RequestOpenScene(Keire::AssetId asset);
    void SaveScene();
    void SaveSceneAs();
    void CompleteSaveSceneAs();
    void RequestCloseScene();
    void CloseScene();
    void ExecutePendingSceneAction();
    void WriteSceneRecovery();
    void RestoreSceneRecovery();
    void DiscardSceneRecovery() noexcept;
    void RecordSceneUndo(std::string_view name = "Edit Scene", std::string mergeKey = {});
    void UndoSceneEdit();
    void RedoSceneEdit();
    void ApplyActiveUndo(bool redo);
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
    Keire::UiPanelRegistration m_ProjectSettings;
    std::unique_ptr<KeireEditor::AssetBrowserPanel> m_AssetBrowserPanel;
    std::unique_ptr<KeireEditor::ConsolePanel> m_ConsolePanel;
    std::unique_ptr<KeireEditor::DiagnosticsPanel> m_DiagnosticsPanel;
    std::unique_ptr<KeireEditor::SceneGizmoController> m_SceneGizmos;
    std::unique_ptr<KeireEditor::SceneDocument> m_SceneDocument;
    std::unique_ptr<KeireEditor::InputActionsDocument> m_InputActionsDocument;
    std::unique_ptr<KeireEditor::EditorCommandRouter> m_CommandRouter;
    std::unique_ptr<KeireEditor::SceneViewportPanel> m_SceneViewportPanel;
    std::unique_ptr<KeireEditor::HierarchyPanel> m_HierarchyPanel;
    std::unique_ptr<KeireEditor::InspectorPanel> m_InspectorPanel;
    std::unique_ptr<KeireEditor::InputActionsPanel> m_InputActionsPanel;
    std::unique_ptr<KeireEditor::ProjectSettingsPanel> m_ProjectSettingsPanel;
    Keire::UiThemeDefinition m_Theme;
    Keire::RenderEnvironmentSettings m_RenderEnvironment;
    bool m_RenderEnvironmentDirty = false;
    Keire::UiThemeId m_PendingTheme;
    Dialog m_Dialog = Dialog::None;
    std::string m_ProfileName;
    std::string m_Error;
    std::string m_Notice;
    std::string m_AssetStatus;
    std::string m_AssetName;
    Keire::Ref<Keire::AssetDatabase> m_AssetDatabase;
    std::vector<Keire::AssetSourceRecord> m_AssetRecords;
    Keire::AssetId m_SelectedAsset;
    Keire::AssetId m_EditingAsset;
    Keire::AssetId& m_InputAsset;
    Keire::AssetId& m_SelectedInputMap;
    Keire::AssetId& m_SelectedInputScheme;
    Keire::AssetId& m_SelectedInputAction;
    Keire::AssetId& m_SelectedInputBinding;
    Keire::InputActionAssetDefinition& m_InputDocument;
    Keire::Ref<Keire::UndoContext>& m_InputUndoContext;
    Keire::Ref<Keire::InputActionContext> m_InputContext;
    Keire::Ref<Keire::InteractiveRebindOperation> m_Rebind;
    std::vector<Keire::InputActionSubscription> m_InputSubscriptions;
    std::vector<Keire::InputCaptureOverride> m_InputCaptureOverrides;
    Keire::InputUserId m_EditorInputUser;
    std::string m_InputSearch;
    std::string m_InputMessage;
    struct InputHistoryEntry
    {
        Keire::AssetId Action;
        std::string Map;
        std::string Name;
        std::string Phase;
        Keire::InputValue Value;
        Keire::InputUserId User;
        Keire::InputDeviceId Device;
        std::uint64_t TimestampNanoseconds = 0;
        std::uint32_t Repetitions = 1;
    };
    std::deque<InputHistoryEntry> m_InputHistory;
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
    Keire::Ref<Keire::Scene>& m_EditingScene;
    Keire::Ref<Keire::SceneRuntimeSession>& m_PlaySession;
    Keire::Ref<Keire::RenderView> m_SceneRenderView;
    Keire::Ref<Keire::RenderView> m_GameRenderView;
    Keire::Ref<Keire::SceneLoadOperation>& m_SceneLoad;
    Keire::Ref<Keire::SaveFileDialogOperation>& m_SaveSceneDialog;
    Keire::AssetId& m_SceneAsset;
    Keire::AssetId& m_SelectedSceneObject;
    Keire::Ref<Keire::UndoContext>& m_SceneUndoContext;
    Keire::Ref<Keire::UndoContext> m_ThemeUndoContext;
    Keire::Ref<Keire::UndoContext> m_ActiveUndoContext;
    std::filesystem::path& m_SceneSource;
    std::filesystem::path& m_SceneRecovery;
    std::string& m_SceneStatus;
    std::unique_ptr<Keire::Detail::EditorCameraController> m_EditorCamera;
    Keire::EntityId m_EditorCameraLockedEntity;
    PendingSceneAction m_PendingSceneAction = PendingSceneAction::None;
    Keire::AssetId m_PendingSceneAsset;
    Keire::UiColor m_NoticeColor;
    std::uint32_t m_FrameCount = 0;
    std::uint64_t m_ContinuousEditSerial = 0;
    std::unordered_map<std::string, bool> m_ComponentExpansion;
    double m_AssetPollSeconds = 0.0;
    double& m_SceneRecoverySeconds;
    bool m_ThemeDirty = false;
    bool& m_InputDirty;
    bool m_InputLiveMonitor = false;
    bool m_InputTesting = false;
    bool m_InputForwardToConsole = false;
    bool m_InputRecordReleases = false;
    bool m_ConsolePaused = false;
    bool& m_SceneRecoveryAvailable;
    bool m_UniformScale = false;
    bool m_PlayFaultReported = false;
    bool m_SceneCameraCapturing = false;
    bool m_SceneCameraDirty = false;
    bool m_CloseThemeAfterDecision = false;
    bool m_OpenDialog = false;
    bool m_Smoke = false;
    bool m_InitializeProject = false;
};
