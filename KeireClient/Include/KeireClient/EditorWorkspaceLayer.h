#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KeireEditor
{
    class AssetBrowserPanel;
    class AssetOperationService;
    struct AssetMutationUndoState;
    enum class AssetMutationPhase : std::uint8_t;
    class ConsolePanel;
    class DiagnosticsPanel;
    class EditorCommandRouter;
    class ExternalAssetImportController;
    class InputActionsDocument;
    class MaterialDocument;
    class InputActionsPanel;
    class InspectorPanel;
    class HierarchyPanel;
    class ProjectSettingsPanel;
    class ProjectSettingsDocument;
    class PropertyDrawerRegistry;
    class SceneDocument;
    class SceneViewportPanel;
    class SceneGizmoController;
    class SceneCameraController;
    class ScenePlayChangeSet;
    class ScenePlayChangeTracker;
    class ScenePlayChangesPanel;
    class SceneTransitionCoordinator;
    class ViewportAssetDropRouter;
} // namespace KeireEditor

class EditorWorkspaceLayer final : public Keire::Layer,
                                   private KeireEditor::ISceneViewportController,
                                   private KeireEditor::IHierarchyController,
                                   private KeireEditor::IInspectorController,
                                   private KeireEditor::IInputActionsController,
                                   private KeireEditor::IProjectSettingsController,
                                   private KeireEditor::IAssetBrowserController,
                                   private KeireEditor::IViewportAssetDropCommands
{
  public:
    explicit EditorWorkspaceLayer(bool smoke, bool initializeProject = false, std::filesystem::path executable = {});
    ~EditorWorkspaceLayer() override;

  protected:
    void OnAttach() override;
    void OnDetach() noexcept override;
    void OnFixedUpdate(const Keire::Time& time) override;
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

    enum class PendingPlayTransition : std::uint8_t
    {
        None,
        Apply,
        Discard
    };

    static void DrawEmptyState(Keire::UiFrame& ui, std::string_view heading, std::string_view primary,
                               std::string_view detail);
    static void DrawPanelMenuItem(Keire::UiFrame& ui, Keire::UiPanelRegistration& panel);
    void DrawMainMenu(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawMainToolbar(Keire::UiFrame& ui);
    void DrawMainStatusBar(Keire::UiFrame& ui);
    void DrawNotices(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawDialogs(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawNameDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace, std::string_view title, Dialog dialog);
    void DrawDeleteDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace, std::string_view title, bool theme);
    void DrawDirtyThemeDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    void DrawDirtySceneDialog(Keire::UiFrame& ui);
    void DrawThemeEditor(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    [[nodiscard]] KeireEditor::InputActionsDocument& InputActionsState() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& InputActionsTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> InputAssetDatabase() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::InputActionContext> InputActionContext() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::InputSystem> InputSystem() const noexcept override;
    void ActivateInputHistory() noexcept override;
    void SaveInputActionsDocument() override;
    void ReloadInputActionsDocument(Keire::AssetId asset) override;
    void RecordInputActionsUndo(std::string_view name) override;
    void UndoInputActions() override;
    void RedoInputActions() override;
    void ReportInputActionsError(std::string message) noexcept override;
    void DrawInputDebugger(Keire::UiFrame& ui);
    [[nodiscard]] KeireEditor::SceneDocument& SceneViewportDocument() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& SceneViewportTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> SceneViewportAssetDatabase() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetSystem> SceneViewportAssetSystem() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::RenderSystem> SceneViewportRenderer() const noexcept override;
    [[nodiscard]] const Keire::RenderEnvironmentSettings& SceneViewportSettings() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::WindowSystem> SceneViewportWindows() const noexcept override;
    [[nodiscard]] Keire::WindowId SceneViewportWindow() const noexcept override;
    [[nodiscard]] float SceneViewportDisplayScale() const noexcept override;
    [[nodiscard]] const Keire::Time& SceneViewportTime() const noexcept override;
    [[nodiscard]] bool SceneViewportPlayReviewActive() const noexcept override;
    void ActivateSceneViewportHistory() noexcept override;
    void RestoreSceneViewportRecovery() override;
    void DiscardSceneViewportRecovery() noexcept override;
    void ReportSceneViewportError(std::string message) noexcept override;
    void SetSceneViewportSelectedAsset(Keire::AssetId asset) noexcept override;
    void RequestSceneViewportNewScene() override;
    void RevealSceneViewportScenes() override;
    void RouteSceneViewportAsset(Keire::AssetTypeId type, Keire::AssetId asset, Keire::EntityId target) override;
    void RecordSceneViewportUndo(std::string_view name) override;
    void SelectSceneViewportEntity(Keire::AssetId entity, bool additive) override;
    void SetSceneViewportSelection(std::span<const Keire::EntityId> entities, bool additive) override;
    void DrawGame(Keire::UiFrame& ui);
    [[nodiscard]] Keire::Ref<Keire::Scene> ActiveHierarchyScene() const noexcept override;
    [[nodiscard]] KeireEditor::SceneDocument& HierarchyDocument() noexcept override;
    [[nodiscard]] Keire::UiColor HierarchyAccent() const noexcept override;
    void ActivateHierarchyHistory() noexcept override;
    void DeleteHierarchySelection() override;
    void RecordHierarchyUndo() override;
    void MarkHierarchyEntity(Keire::AssetId entity) override;
    void RequestHierarchyRename(Keire::AssetId entity, std::string name) override;
    void ReportHierarchyError(std::string message) noexcept override;
    void DrawConsole(Keire::UiFrame& ui);
    void DrawDiagnostics(Keire::UiFrame& ui);
    void DrawProject(Keire::UiFrame& ui);
    [[nodiscard]] const Keire::UiThemeDefinition& AssetBrowserTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> AssetBrowserDatabase() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetSystem> AssetBrowserAssets() const noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> AssetBrowserRecords() const noexcept override;
    [[nodiscard]] std::string_view AssetBrowserStatus() const noexcept override;
    [[nodiscard]] Keire::AssetId AssetBrowserSceneAsset() const noexcept override;
    [[nodiscard]] bool AssetBrowserSceneDirty() const noexcept override;
    [[nodiscard]] bool AssetBrowserImportPending() const noexcept override;
    void RefreshAssetBrowserRecords() override;
    void SetAssetBrowserSelected(Keire::AssetId asset) noexcept override;
    void ClearAssetBrowserSceneSelection() noexcept override;
    void SetAssetBrowserStatus(std::string status) noexcept override;
    void ReportAssetBrowserError(std::string message) noexcept override;
    void ImportAssetBrowserAssets() override;
    void RequestAssetBrowserCreateScene() override;
    bool CreateAssetBrowserMaterial(std::string_view name) override;
    void CreateAssetBrowserShader() override;
    void CreateAssetBrowserInputActions(Keire::InputActionAssetDefinition definition,
                                        std::string_view baseName) override;
    void MutateAssetBrowser(Keire::Detail::AssetWorkerMutation mutation, Keire::Detail::AssetWorkerMutation reverse,
                            std::string name, bool revealResult) override;
    void OpenAssetBrowserInputActions(Keire::AssetId asset) override;
    void OpenAssetBrowserScene(Keire::AssetId asset) override;
    void CopyAssetBrowserText(std::string_view value) override;
    [[nodiscard]] KeireEditor::SceneDocument& InspectorSceneDocument() noexcept override;
    [[nodiscard]] KeireEditor::InputActionsDocument& InspectorInputDocument() noexcept override;
    [[nodiscard]] KeireEditor::MaterialDocument& InspectorMaterialDocument() noexcept override;
    [[nodiscard]] KeireEditor::PropertyDrawerRegistry& InspectorPropertyDrawers() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& InspectorTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> InspectorAssetDatabase() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetSystem> InspectorAssetSystem() const noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> InspectorAssetRecords() const noexcept override;
    [[nodiscard]] Keire::AssetId InspectorSelectedAsset() const noexcept override;
    [[nodiscard]] std::string_view InspectorAssetStatus() const noexcept override;
    void SetInspectorSelectedAsset(Keire::AssetId asset) noexcept override;
    void ActivateInspectorHistory() noexcept override;
    void RecordInspectorUndo(std::string_view name, std::string mergeKey = {}) override;
    void CommitInspectorMaterial() override;
    void OpenInspectorInputActions(Keire::AssetId asset) override;
    void ImportInspectorAssets() override;
    void RenameInspectorAsset(Keire::AssetId asset, std::string_view name) override;
    void DuplicateInspectorAsset(Keire::AssetId asset, const std::filesystem::path& destination) override;
    void TrashInspectorAsset(Keire::AssetId asset) override;
    void SetInspectorAssetStatus(std::string status) noexcept override;
    void ReportInspectorAssetError(std::string message) noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> ProjectSettingsAssetRecords() const noexcept override;
    void RevealProjectSettingsAsset(Keire::AssetId asset) override;
    void OpenDroppedScene(Keire::AssetId asset) override;
    void OpenDroppedInputActions(Keire::AssetId asset) override;
    void CreateDroppedMeshEntity(Keire::AssetId asset) override;
    void AssignDroppedMaterial(Keire::EntityId entity, Keire::AssetId asset) override;
    void ImportAssets();
    void UpdateAssetOperations();
    void QueueAssetMutation(std::shared_ptr<KeireEditor::AssetMutationUndoState> state,
                            KeireEditor::AssetMutationPhase phase);
    void ApplyAssetImportResult(const Keire::AssetImportResult& result, bool reloadLoadedAssets,
                                Keire::AssetId reloadAsset = {});
    void QueueMaterialCatalogRefresh(Keire::AssetId reloadAsset = {});
    void UpdateMaterialCatalogRefresh(const Keire::Time& time);
    void FlushMaterialCatalogRefresh() noexcept;
    void CancelMaterialCatalogRefresh() noexcept;
    void CommitMaterialDraft();
    void HandleExternalAssetDrop(const Keire::WindowFileDropEvent& event);
    void DrawExternalAssetImport(Keire::UiFrame& ui);
    void CookAssets();
    void CreateInputActions(Keire::InputActionAssetDefinition definition, std::string_view baseName);
    void CreateUnlitShader();
    [[nodiscard]] bool CreateMaterial(std::string_view name = "Material");
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
    void QueueSceneTransition(PendingSceneAction action, Keire::AssetId asset = {});
    void ProcessSceneTransition();
    void WriteSceneRecovery();
    void RestoreSceneRecovery();
    void DiscardSceneRecovery() noexcept;
    void RecordSceneUndo(std::string_view name = "Edit Scene", std::string mergeKey = {});
    void MarkPlayEditorEntity(Keire::AssetId entity);
    void SelectSceneEntity(Keire::AssetId entity, bool additive = false);
    void SetSceneSelection(std::span<const Keire::EntityId> entities, bool additive);
    [[nodiscard]] Keire::Ref<Keire::Scene> ActiveScene() const noexcept;
    void BeginPlayMode();
    void RequestStopPlayMode();
    void FinishPlayMode(bool apply);
    void DrawPlayChanges(Keire::UiFrame& ui);
    void FinalizePendingPlayEditorMutation();
    void UndoSceneEdit();
    void RedoSceneEdit();
    void ApplyActiveUndo(bool redo);
    void OpenDialog(Dialog dialog);
    void OpenPendingDialog(Keire::UiFrame& ui);
    void RequestTheme(Keire::UiWorkspace& workspace, Keire::UiThemeId id);
    void LoadTheme(Keire::UiWorkspace& workspace, Keire::UiThemeId id);

    Keire::UiPanelRegistration m_Game;
    Keire::UiPanelRegistration m_ThemeEditor;
    Keire::UiPanelRegistration m_InputDebugger;
    std::unique_ptr<KeireEditor::AssetBrowserPanel> m_AssetBrowserPanel;
    std::unique_ptr<KeireEditor::ConsolePanel> m_ConsolePanel;
    std::unique_ptr<KeireEditor::DiagnosticsPanel> m_DiagnosticsPanel;
    std::unique_ptr<KeireEditor::SceneDocument> m_SceneDocument;
    std::unique_ptr<KeireEditor::InputActionsDocument> m_InputActionsDocument;
    std::unique_ptr<KeireEditor::ProjectSettingsDocument> m_ProjectSettingsDocument;
    std::unique_ptr<KeireEditor::MaterialDocument> m_MaterialDocument;
    std::unique_ptr<KeireEditor::EditorCommandRouter> m_CommandRouter;
    std::unique_ptr<KeireEditor::SceneViewportPanel> m_SceneViewportPanel;
    std::unique_ptr<KeireEditor::HierarchyPanel> m_HierarchyPanel;
    std::unique_ptr<KeireEditor::InspectorPanel> m_InspectorPanel;
    std::unique_ptr<KeireEditor::InputActionsPanel> m_InputActionsPanel;
    std::unique_ptr<KeireEditor::ProjectSettingsPanel> m_ProjectSettingsPanel;
    std::unique_ptr<KeireEditor::PropertyDrawerRegistry> m_PropertyDrawers;
    std::unique_ptr<KeireEditor::ViewportAssetDropRouter> m_ViewportAssetDropRouter;
    std::unique_ptr<KeireEditor::ScenePlayChangesPanel> m_PlayChangesPanel;
    std::unique_ptr<KeireEditor::ScenePlayChangeSet> m_PlayChanges;
    std::unique_ptr<KeireEditor::ScenePlayChangeTracker> m_PlayChangeTracker;
    std::unique_ptr<KeireEditor::SceneTransitionCoordinator> m_SceneTransitions;
    std::optional<Keire::SceneDefinition> m_PendingPlayEditorBefore;
    std::unique_ptr<KeireEditor::ExternalAssetImportController> m_ExternalAssetImport;
    std::unique_ptr<KeireEditor::AssetOperationService> m_AssetOperations;
    Keire::UiThemeDefinition m_Theme;
    Keire::UiThemeId m_PendingTheme;
    Dialog m_Dialog = Dialog::None;
    std::string m_ProfileName;
    std::string m_Error;
    std::string m_Notice;
    std::string m_AssetStatus;
    Keire::Ref<Keire::AssetDatabase> m_AssetDatabase;
    std::vector<Keire::AssetSourceRecord> m_AssetRecords;
    Keire::AssetId m_SelectedAsset;
    std::filesystem::path m_ExecutablePath;
    Keire::AssetId m_PendingStartupScene;
    Keire::Ref<Keire::InputActionContext> m_InputContext;
    std::vector<Keire::InputActionSubscription> m_InputSubscriptions;
    std::vector<Keire::InputCaptureOverride> m_InputCaptureOverrides;
    Keire::InputUserId m_EditorInputUser;
    std::string m_InputDebuggerMessage;
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
    Keire::Ref<Keire::RenderView> m_GameRenderView;
    Keire::Ref<Keire::UndoContext> m_ThemeUndoContext;
    Keire::Ref<Keire::UndoContext> m_ActiveUndoContext;
    PendingSceneAction m_PendingSceneAction = PendingSceneAction::None;
    PendingPlayTransition m_PendingPlayTransition = PendingPlayTransition::None;
    Keire::AssetId m_PendingSceneAsset;
    Keire::UiColor m_NoticeColor;
    std::uint32_t m_FrameCount = 0;
    double m_AssetPollSeconds = 0.0;
    bool m_ThemeDirty = false;
    bool m_InputTesting = false;
    bool m_InputForwardToConsole = false;
    bool m_InputRecordReleases = false;
    bool m_PlayFaultReported = false;
    Keire::ScenePlayState m_PlayResumeState = Keire::ScenePlayState::Stopped;
    std::unordered_set<Keire::AssetId> m_PlayEditorTouchedEntities;
    bool m_CloseThemeAfterDecision = false;
    bool m_OpenDialog = false;
    bool m_Smoke = false;
    bool m_InitializeProject = false;
    int m_GameAspect = 0;
};
