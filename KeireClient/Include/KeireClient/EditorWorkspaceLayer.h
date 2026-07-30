#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AnimatorControllerPanel.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AudioMixerPanel.h"
#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/RiggingStudioPanel.h"
#include "KeireClient/Editor/VfxEffectPanel.h"
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
    class AnimatorControllerDocument;
    class AudioMixerDocument;
    class VfxEffectDocument;
    struct AssetMutationUndoState;
    enum class AssetMutationPhase : std::uint8_t;
    class ConsolePanel;
    class DiagnosticsPanel;
    class EditorCommandRouter;
    class ExternalAssetImportController;
    class InputActionsDocument;
    class MaterialDocument;
    class InputActionsPanel;
    class AudioMixerPanel;
    class VfxEffectPanel;
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
                                   private KeireEditor::IAnimatorControllerPanelController,
                                   private KeireEditor::IRiggingStudioController,
                                   private KeireEditor::IAudioMixerPanelController,
                                   private KeireEditor::IVfxEffectPanelController,
                                   private KeireEditor::IProjectSettingsController,
                                   private KeireEditor::IAssetBrowserController,
                                   private KeireEditor::IViewportAssetDropCommands,
                                   private Keire::IScriptRuntimeServices
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
    [[nodiscard]] Keire::VfxRenderSnapshot SceneViewportEditVfx() const override;
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
    void UnpackHierarchyPrefab(Keire::AssetId entity, bool completely) override;
    void ReportHierarchyError(std::string message) noexcept override;
    void DrawConsole(Keire::UiFrame& ui);
    void DrawDiagnostics(Keire::UiFrame& ui);
    void DrawPrefabOverrides(Keire::UiFrame& ui);
    void DrawBuildSettings(Keire::UiFrame& ui);
    void DrawProfiler(Keire::UiFrame& ui);
    void DrawPerformanceOverlay(Keire::UiFrame& ui, Keire::UiItemRect viewport, std::string_view label);
    void DrawProject(Keire::UiFrame& ui);
    [[nodiscard]] const Keire::UiThemeDefinition& AssetBrowserTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> AssetBrowserDatabase() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetSystem> AssetBrowserAssets() const noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> AssetBrowserRecords() const noexcept override;
    [[nodiscard]] std::uint64_t AssetBrowserRecordRevision() const noexcept override;
    [[nodiscard]] std::string_view AssetBrowserStatus() const noexcept override;
    [[nodiscard]] Keire::AssetId AssetBrowserSceneAsset() const noexcept override;
    [[nodiscard]] bool AssetBrowserSceneDirty() const noexcept override;
    [[nodiscard]] std::vector<Keire::ManagedAssetTypeDescriptor> AssetBrowserManagedAssetTypes() const override;
    void RefreshAssetBrowserRecords();
    void SetAssetBrowserSelected(Keire::AssetId asset) noexcept override;
    void ClearAssetBrowserSceneSelection() noexcept override;
    void SetAssetBrowserStatus(std::string status) noexcept override;
    void ReportAssetBrowserError(std::string message) noexcept override;
    void ImportAssetBrowserAssets() override;
    void RequestAssetBrowserCreateScene() override;
    bool CreateAssetBrowserMaterial(std::string_view name) override;
    bool CreateAssetBrowserAnimationGraph(std::string_view name) override;
    bool CreateAssetBrowserScript(std::string_view name) override;
    bool CreateAssetBrowserManagedAssembly(std::string_view name) override;
    bool CreateAssetBrowserManagedData(Keire::ManagedTypeId type, std::string_view name) override;
    bool CreateAssetBrowserAudioMixer(std::string_view name) override;
    bool CreateAssetBrowserPhysicsMaterial(std::string_view name) override;
    bool CreateAssetBrowserVfxEffect(std::string_view name) override;
    bool CreateAssetBrowserPrefab(std::string_view name) override;
    bool CreateAssetBrowserPrefabVariant(Keire::AssetId basePrefab, std::string_view name) override;
    void CreateAssetBrowserPrefabFromObject(Keire::AssetId object, const std::filesystem::path& folder) override;
    void CreateAssetBrowserShader() override;
    void CreateAssetBrowserInputActions(Keire::InputActionAssetDefinition definition,
                                        std::string_view baseName) override;
    void ExtractAssetBrowserMaterials(Keire::AssetId model) override;
    void MutateAssetBrowser(Keire::Detail::AssetWorkerMutation mutation, Keire::Detail::AssetWorkerMutation reverse,
                            std::string name, bool revealResult) override;
    void OpenAssetBrowserInputActions(Keire::AssetId asset) override;
    void OpenAssetBrowserAnimationGraph(Keire::AssetId asset) override;
    void OpenAssetBrowserAudioMixer(Keire::AssetId asset) override;
    void OpenAssetBrowserVfxEffect(Keire::AssetId asset) override;
    void OpenAssetBrowserPrefab(Keire::AssetId asset) override;
    void OpenAssetBrowserScene(Keire::AssetId asset) override;
    void PrepareAssetBrowserExternalOpen(Keire::AssetId asset) override;
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
    [[nodiscard]] std::vector<Keire::ManagedAssetTypeDescriptor> InspectorManagedAssetTypes() const override;
    [[nodiscard]] Keire::Ref<Keire::UndoContext> InspectorManagedDataHistory() const noexcept override;
    [[nodiscard]] bool InspectorPlayModeActive() const noexcept override;
    void SetInspectorSelectedAsset(Keire::AssetId asset) noexcept override;
    void PreviewInspectorAudio(Keire::AssetId asset) override;
    void StopInspectorAudioPreview() noexcept override;
    void ActivateInspectorHistory() noexcept override;
    void ActivateInspectorManagedDataHistory() noexcept override;
    void RecordInspectorUndo(std::string_view name, std::string mergeKey = {}) override;
    void AddScriptToEntity(Keire::EntityId entity, Keire::AssetId script) override;
    void CommitInspectorMaterial() override;
    void OpenInspectorInputActions(Keire::AssetId asset) override;
    void ImportInspectorAssets() override;
    void PreviewInspectorManagedData(Keire::AssetId asset, const Keire::ManagedDataDefinition& definition) override;
    void PersistInspectorManagedData(Keire::AssetId asset, std::span<const std::byte> bytes) override;
    void RenameInspectorAsset(Keire::AssetId asset, std::string_view name) override;
    void DuplicateInspectorAsset(Keire::AssetId asset, const std::filesystem::path& destination) override;
    void TrashInspectorAsset(Keire::AssetId asset) override;
    void SetInspectorAssetStatus(std::string status) noexcept override;
    void ReportInspectorAssetError(std::string message) noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& RiggingStudioTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> RiggingStudioDatabase() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetSystem> RiggingStudioAssets() const noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> RiggingStudioRecords() const noexcept override;
    [[nodiscard]] Keire::AssetId RiggingStudioSelectedAsset() const noexcept override;
    [[nodiscard]] std::string_view RiggingStudioStatus() const noexcept override;
    void ApplyRiggingStudioSettings(Keire::AssetId asset, const Keire::AssetImportSettings& settings) override;
    void CreateRiggingStudioRetarget(std::string_view name, std::vector<std::byte> bytes) override;
    void RevealRiggingStudioAsset(Keire::AssetId asset) override;
    void ReportRiggingStudioError(std::string message) noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> ProjectSettingsAssetRecords() const noexcept override;
    void RevealProjectSettingsAsset(Keire::AssetId asset) override;
    [[nodiscard]] KeireEditor::ManagedSdkPreference ProjectManagedSdk() const override;
    void SetProjectManagedSdk(KeireEditor::ManagedSdkPreference preference) override;
    void ApplyProjectAuthoringSettings(const Keire::ProjectAuthoringSettings& settings) override;
    void OpenDroppedScene(Keire::AssetId asset) override;
    void OpenDroppedInputActions(Keire::AssetId asset) override;
    void InstantiateDroppedPrefab(Keire::AssetId asset) override;
    void CreateDroppedMeshEntity(Keire::AssetId asset) override;
    void AssignDroppedMaterial(Keire::EntityId entity, Keire::AssetId asset) override;
    void
    ImportAssets(KeireEditor::AssetOperationPriority priority = KeireEditor::AssetOperationPriority::ExplicitAction);
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
    void StartManagedBuild();
    void UpdateManagedBuild(const Keire::Time& time);
    void CreateInputActions(Keire::InputActionAssetDefinition definition, std::string_view baseName);
    [[nodiscard]] KeireEditor::AnimatorControllerDocument& AnimatorControllerState() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& AnimatorControllerTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> AnimatorControllerDatabase() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetSystem> AnimatorControllerAssets() const noexcept override;
    [[nodiscard]] KeireEditor::SceneDocument& AnimatorControllerSceneDocument() noexcept override;
    void ActivateAnimatorControllerHistory() noexcept override;
    void SaveAnimatorControllerDocument() override;
    void ReloadAnimatorControllerDocument(Keire::AssetId asset) override;
    void UndoAnimatorControllerEdit() override;
    void RedoAnimatorControllerEdit() override;
    void ReportAnimatorControllerError(std::string message) noexcept override;
    [[nodiscard]] KeireEditor::AudioMixerDocument& AudioMixerState() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& AudioMixerTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> AudioMixerDatabase() const noexcept override;
    [[nodiscard]] std::string_view AudioMixerPreviewDiagnostic() const noexcept override;
    void ActivateAudioMixerHistory() noexcept override;
    void SaveAudioMixerDocument() override;
    void DiscardAudioMixerDocument() override;
    void ReloadAudioMixerDocument(Keire::AssetId asset) override;
    void UndoAudioMixerEdit() override;
    void RedoAudioMixerEdit() override;
    void StopAudioMixerPreview() noexcept override;
    void ReportAudioMixerError(std::string message) noexcept override;
    [[nodiscard]] KeireEditor::VfxEffectDocument& VfxEffectState() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& VfxEffectTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> VfxEffectDatabase() const noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> VfxEffectAssetRecords() const noexcept override;
    [[nodiscard]] std::string_view VfxEffectPreviewDiagnostic() const noexcept override;
    void ActivateVfxEffectHistory() noexcept override;
    void SaveVfxEffectDocument() override;
    void DiscardVfxEffectDocument() override;
    void ReloadVfxEffectDocument(Keire::AssetId asset) override;
    void UndoVfxEffectEdit() override;
    void RedoVfxEffectEdit() override;
    void RevealVfxEffectAsset(Keire::AssetId asset) override;
    void StopVfxEffectPreview() noexcept override;
    void ReportVfxEffectError(std::string message) noexcept override;
    [[nodiscard]] bool CreateCSharpScript(std::string_view name);
    [[nodiscard]] bool CreateManagedAssembly(std::string_view name);
    [[nodiscard]] bool CreateAudioMixer(std::string_view name);
    [[nodiscard]] bool CreatePhysicsMaterial(std::string_view name);
    [[nodiscard]] bool CreateVfxEffect(std::string_view name);
    [[nodiscard]] bool CreatePrefabFromSelection(std::string_view name);
    [[nodiscard]] bool CreatePrefabVariant(Keire::AssetId basePrefab, std::string_view name);
    void CreatePrefabFromObject(Keire::AssetId object, const std::filesystem::path& folder);
    [[nodiscard]] Keire::AssetId CreatePrefabAsset(const std::filesystem::path& destination,
                                                   const Keire::PrefabDefinition& definition);
    void GenerateManagedIdeWorkspace();
    void OpenPrefabForEditing(Keire::AssetId asset);
    void SavePrefabEditingStage();
    void ClosePrefabEditingStage();
    void ApplySelectedPrefabOverrides();
    void ReplacePrefabSource(Keire::AssetId asset, const Keire::PrefabDefinition& definition);
    void CreateUnlitShader();
    [[nodiscard]] bool CreateMaterial(std::string_view name = "Material");
    [[nodiscard]] bool CreateAnimationGraph(std::string_view name = "NewAnimatorController");
    void OpenAnimationGraph(Keire::AssetId asset);
    void SaveAnimationGraph();
    void OpenAudioMixer(Keire::AssetId asset);
    void SaveAudioMixer();
    void PersistAudioMixer(Keire::AssetId asset, std::span<const std::byte> bytes);
    void PreviewAudioMixer(Keire::AssetId asset, const Keire::AudioMixerDefinition& definition);
    void OpenVfxEffect(Keire::AssetId asset);
    void SaveVfxEffect();
    void PersistVfxEffect(Keire::AssetId asset, std::span<const std::byte> bytes);
    void PreviewVfxEffect(Keire::AssetId asset, const Keire::VfxEffectDefinition& definition);
    void OpenInputActions(Keire::AssetId asset);
    void SaveInputActions();
    void RecordInputUndo(std::string_view name = "Edit Input Actions");
    void UndoInputEdit();
    void RedoInputEdit();
    void BeginInputTest();
    void EndInputTest() noexcept;
    void WriteManagedLog(Keire::ManagedLogLevel level, std::string_view message) noexcept override;
    void RecordManagedProfileSpan(std::string_view name, double startMicroseconds,
                                  double durationMicroseconds) noexcept override;
    void SetManagedProfileCounter(std::string_view name, double value) noexcept override;
    [[nodiscard]] float ManagedDeltaTime() const noexcept override;
    [[nodiscard]] float ManagedFixedDeltaTime() const noexcept override;
    [[nodiscard]] float ManagedUnscaledDeltaTime() const noexcept override;
    [[nodiscard]] double ManagedElapsedTime() const noexcept override;
    [[nodiscard]] Keire::Vector2 ReadManagedInput(std::string_view action) noexcept override;
    [[nodiscard]] Keire::ManagedInputState ReadManagedInputState(std::string_view action) noexcept override;
    [[nodiscard]] std::optional<Keire::ManagedRaycastHit>
    RaycastManaged(const Keire::ManagedRaycastQuery& query) noexcept override;
    void SetManagedCursorVisible(bool visible) noexcept override;
    void SetManagedCursorLocked(bool locked) noexcept override;
    [[nodiscard]] bool IsManagedCursorVisible() const noexcept override;
    [[nodiscard]] bool IsManagedCursorLocked() const noexcept override;
    [[nodiscard]] bool PlayManagedAudio(const Keire::ManagedAudioPlayback& playback) noexcept override;
    [[nodiscard]] bool StopManagedAudio(Keire::AssetId entity) noexcept override;
    [[nodiscard]] bool PauseManagedAudio(Keire::AssetId entity, bool paused) noexcept override;
    [[nodiscard]] bool SeekManagedAudio(Keire::AssetId entity, float positionSeconds) noexcept override;
    [[nodiscard]] Keire::ManagedAudioSourceStatus ManagedAudioStatus(Keire::AssetId entity) const noexcept override;
    [[nodiscard]] bool PlayManagedVfx(Keire::AssetId entity, Keire::AssetId effect, bool restart) noexcept override;
    [[nodiscard]] bool StopManagedVfx(Keire::AssetId entity) noexcept override;
    [[nodiscard]] bool PauseManagedVfx(Keire::AssetId entity, bool paused) noexcept override;
    [[nodiscard]] bool IsManagedVfxAlive(Keire::AssetId entity) const noexcept override;
    [[nodiscard]] bool SetManagedUiText(Keire::AssetId entity, std::string_view text) noexcept override;
    [[nodiscard]] bool ConsumeManagedUiClick(Keire::AssetId entity) noexcept override;
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
    void ApplyManagedCursorMode() noexcept;
    void SetGameViewportInputActive(bool active) noexcept;
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
    Keire::UiPanelRegistration m_PrefabOverrides;
    Keire::UiPanelRegistration m_BuildSettings;
    Keire::UiPanelRegistration m_Profiler;
    struct PrefabEditingStage
    {
        Keire::AssetId Asset;
        Keire::PrefabDefinition Source;
        Keire::SceneDefinition Baseline;
        std::filesystem::path RelativePath;
    };
    std::unique_ptr<KeireEditor::AssetBrowserPanel> m_AssetBrowserPanel;
    std::unique_ptr<KeireEditor::ConsolePanel> m_ConsolePanel;
    std::unique_ptr<KeireEditor::DiagnosticsPanel> m_DiagnosticsPanel;
    std::unique_ptr<KeireEditor::SceneDocument> m_SceneDocument;
    std::unique_ptr<KeireEditor::SceneDocument> m_PrefabReturnDocument;
    std::optional<PrefabEditingStage> m_PrefabEditingStage;
    std::unique_ptr<KeireEditor::InputActionsDocument> m_InputActionsDocument;
    std::unique_ptr<KeireEditor::AnimatorControllerDocument> m_AnimatorControllerDocument;
    std::unique_ptr<KeireEditor::AudioMixerDocument> m_AudioMixerDocument;
    std::unique_ptr<KeireEditor::VfxEffectDocument> m_VfxEffectDocument;
    std::unique_ptr<KeireEditor::ProjectSettingsDocument> m_ProjectSettingsDocument;
    std::unique_ptr<KeireEditor::MaterialDocument> m_MaterialDocument;
    std::unique_ptr<KeireEditor::EditorCommandRouter> m_CommandRouter;
    std::unique_ptr<KeireEditor::SceneViewportPanel> m_SceneViewportPanel;
    std::unique_ptr<KeireEditor::HierarchyPanel> m_HierarchyPanel;
    std::unique_ptr<KeireEditor::InspectorPanel> m_InspectorPanel;
    std::unique_ptr<KeireEditor::InputActionsPanel> m_InputActionsPanel;
    std::unique_ptr<KeireEditor::AnimatorControllerPanel> m_AnimatorControllerPanel;
    std::unique_ptr<KeireEditor::RiggingStudioPanel> m_RiggingStudioPanel;
    std::unique_ptr<KeireEditor::AudioMixerPanel> m_AudioMixerPanel;
    std::unique_ptr<KeireEditor::VfxEffectPanel> m_VfxEffectPanel;
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
    std::string m_AudioMixerPreviewDiagnostic;
    std::string m_VfxEffectPreviewDiagnostic;
    Keire::Ref<Keire::AssetDatabase> m_AssetDatabase;
    std::vector<Keire::AssetSourceRecord> m_AssetRecords;
    std::uint64_t m_AssetRecordRevision = 0;
    Keire::AssetId m_SelectedAsset;
    std::filesystem::path m_ExecutablePath;
    Keire::AssetId m_PendingStartupScene;
    struct PendingPrefabCreation
    {
        Keire::AssetId Object;
        std::filesystem::path Folder;
    };
    struct PendingAssetMutation
    {
        std::shared_ptr<KeireEditor::AssetMutationUndoState> State;
        KeireEditor::AssetMutationPhase Phase;
    };
    std::vector<PendingAssetMutation> m_PendingAssetMutations;
    std::vector<PendingPrefabCreation> m_PendingPrefabCreations;
    Keire::ManagedBuildOperationId m_LastManagedReload;
    Keire::ManagedBuildOperationId m_LastManagedBuildReport;
    Keire::Ref<Keire::InputActionContext> m_InputContext;
    Keire::Ref<Keire::InputActionContext> m_GameplayInputContext;
    std::optional<Keire::InputCaptureOverride> m_ManagedInputCaptureOverride;
    bool m_ManagedCursorVisible = true;
    bool m_ManagedCursorLocked = false;
    bool m_GameViewportInputActive = false;
    std::uint32_t m_SuppressManagedLookFrames = 0;
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
    Keire::Ref<Keire::ScenePresentationRuntime> m_GameEditPresentation;
    Keire::UiItemRect m_GameViewportRect;
    Keire::AudioVoiceId m_InspectorAudioPreviewVoice;
    Keire::AssetId m_AudioMixerPreviewAsset;
    Keire::AssetId m_VfxEffectPreviewAsset;
    Keire::Ref<Keire::VfxWorld> m_VfxEffectPreviewWorld;
    Keire::VfxHandle m_VfxEffectPreviewHandle;
    Keire::Ref<Keire::UndoContext> m_ThemeUndoContext;
    Keire::Ref<Keire::UndoContext> m_ManagedDataUndoContext;
    Keire::Ref<Keire::UndoContext> m_ActiveUndoContext;
    PendingSceneAction m_PendingSceneAction = PendingSceneAction::None;
    PendingPlayTransition m_PendingPlayTransition = PendingPlayTransition::None;
    Keire::AssetId m_PendingSceneAsset;
    Keire::UiColor m_NoticeColor;
    std::uint32_t m_FrameCount = 0;
    std::uint64_t m_AudioMixerDocumentRevision = 0;
    std::uint64_t m_VfxEffectDocumentRevision = 0;
    std::uint64_t m_VfxEffectPreviewRevision = 0;
    std::uint32_t m_VfxEffectPreviewCapacity = 0;
    double m_AssetPollSeconds = 0.0;
    double m_ManagedBuildDebounceSeconds = -1.0;
    std::vector<std::pair<Keire::EntityId, Keire::AssetId>> m_PendingScriptAttachments;
    bool m_ResolvingPendingScriptAttachments = false;
    bool m_ThemeDirty = false;
    bool m_InputTesting = false;
    bool m_InputForwardToConsole = false;
    bool m_InputRecordReleases = false;
    bool m_PlayFaultReported = false;
    bool m_ShowPerformanceOverlay = false;
    bool m_ProfilerPaused = false;
    bool m_ProfilerShowAllManagedCallbacks = false;
    bool m_ProfilerShowAllHotspots = false;
    bool m_ProfilerShowAllCounters = false;
    struct ProfilerPresentationCache
    {
        std::uint64_t FrameSequence = 0;
        double FramesPerSecond = 0.0;
        double AverageFrameMicroseconds = 0.0;
        double AverageFramesPerSecond = 0.0;
        double P95FrameMicroseconds = 0.0;
        double P99FrameMicroseconds = 0.0;
        double MaximumFrameMicroseconds = 0.0;
        double OnePercentLow = 0.0;
        std::size_t StutterCount = 0;
        std::string FrameLine;
        std::string HistoryLine;
        std::string TailLine;
        std::vector<Keire::ProfileSpan> OrderedSpans;
        std::vector<Keire::ProfileSpan> TimelineSpans;
        std::vector<std::string> SpanLines;
        std::vector<std::string> TimelineLines;
        std::vector<std::string> ThreadLines;
        std::vector<std::string> CounterLines;
        std::vector<std::string> ManagedCallbackLines;
        bool ManagedCallbacksTruncated = false;
    };
    Keire::ProfileFrame m_CachedProfileFrame;
    std::vector<Keire::ProfileFrameSummary> m_CachedProfileHistory;
    Keire::ProfileFrame m_FrozenProfileFrame;
    std::vector<Keire::ProfileFrameSummary> m_FrozenProfileHistory;
    ProfilerPresentationCache m_ProfilerPresentation;
    Keire::ScenePlayState m_PlayResumeState = Keire::ScenePlayState::Stopped;
    std::unordered_set<Keire::AssetId> m_PlayEditorTouchedEntities;
    bool m_CloseThemeAfterDecision = false;
    bool m_OpenDialog = false;
    bool m_Smoke = false;
    bool m_InitializeProject = false;
    int m_GameAspect = 0;
    int m_BuildConfiguration = 0;
    int m_BuildPlatform = 0;
    bool m_BuildSymbols = true;
};
