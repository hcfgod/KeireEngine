#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AnimatorControllerPanel.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AudioMixerPanel.h"
#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/EditorRuntimeUiInput.h"
#include "KeireClient/Editor/LightingPanel.h"
#include "KeireClient/Editor/ManagedDataTypeCache.h"
#include "KeireClient/Editor/ManagedRuntimeDiagnostics.h"
#include "KeireClient/Editor/MaterialGraphPanel.h"
#include "KeireClient/Editor/RiggingStudioPanel.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/UiBuilderPanel.h"
#include "KeireClient/Editor/VfxEffectPanel.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"
#include "KeireInternal/Scripting/ManagedRuntimeInput.h"
#include "KeireInternal/Scripting/ManagedRuntimeRenderingServices.h"

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
    class EditorAssetOperationCoordinator;
    class EditorBuildCookCoordinator;
    class EditorDocumentWorkspaceCoordinator;
    struct EditorDocumentWorkspaceDocuments;
    enum class EditorDocumentTransitionAction : std::uint8_t;
    class EditorManagedRuntimeCoordinator;
    class EditorPackageCoordinator;
    class EditorPlayModeCoordinator;
    class EditorReplayProfilingCoordinator;
    class EditorSmokePlayValidation;
    class EditorWorkspaceLifecycleCoordinator;
    class ExternalAssetImportController;
    class InputActionsDocument;
    class MaterialDocument;
    class MaterialGraphDocument;
    class PackageManagerPanel;
    class ShaderGraphDocument;
    class InputActionsPanel;
    class AudioMixerPanel;
    class ShaderGraphPanel;
    class PlayerBuildService;
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
    class UiBuilderDocument;
    class UiBuilderLiveDraftSession;
    class UiBuilderPanel;
    class UiBuilderStyleSheetDocument;
    class ViewportAssetDropRouter;
} // namespace KeireEditor

namespace Keire::Internal
{
    class DiagnosticBundleDialogController;
}

class EditorWorkspaceLayer final : public Keire::Layer,
                                   private KeireEditor::ISceneViewportController,
                                   private KeireEditor::IHierarchyController,
                                   private KeireEditor::IInspectorController,
                                   private KeireEditor::IInputActionsController,
                                   private KeireEditor::IAnimatorControllerPanelController,
                                   private KeireEditor::IRiggingStudioController,
                                   private KeireEditor::IAudioMixerPanelController,
                                   private KeireEditor::IVfxEffectPanelController,
                                   private KeireEditor::IMaterialGraphPanelController,
                                   private KeireEditor::IShaderGraphPanelController,
                                   private KeireEditor::IUiBuilderController,
                                   private KeireEditor::IProjectSettingsController,
                                   private KeireEditor::ILightingPanelController,
                                   private KeireEditor::IAssetBrowserController,
                                   private KeireEditor::IViewportAssetDropCommands,
                                   private Keire::Detail::ManagedRuntimeSceneServices
{
  public:
    explicit EditorWorkspaceLayer(bool smoke, bool initializeProject = false, bool smokePlay = false,
                                  std::filesystem::path executable = {}, std::filesystem::path smokePlayOutput = {}
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                                  ,
                                  bool validateDeviceLoss = false
#endif
    );
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
        DirtyShaderGraph,
        DirtyMaterialGraph,
        DirtyPlayerBuild,
        RenameEntity
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
    void DrawDirtyShaderGraphDialog(Keire::UiFrame& ui);
    void DrawDirtyMaterialGraphDialog(Keire::UiFrame& ui);
    void DrawDirtyPlayerBuildDialog(Keire::UiFrame& ui);
    void DrawThemeEditor(Keire::UiFrame& ui, Keire::UiWorkspace& workspace);
    [[nodiscard]] KeireEditor::InputActionsDocument& InputActionsState() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& InputActionsTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> InputAssetDatabase() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::InputActionContext> InputActionContext() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::InputSystem> InputSystem() const noexcept override;
    void ActivateInputHistory() noexcept override;
    void SaveInputActionsDocument() override;
    std::filesystem::path GenerateInputActionsWrapper(std::string_view className, std::string_view nameSpace) override;
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
    void OpenSceneViewportUiDocument(Keire::AssetId asset) override;
    void SelectSceneViewportEntity(Keire::AssetId entity, bool additive) override;
    void SetSceneViewportSelection(std::span<const Keire::EntityId> entities, bool additive) override;
    [[nodiscard]] std::optional<Keire::UiItemRect>
    DrawSceneViewportPerformanceOverlay(Keire::UiFrame& ui, Keire::UiItemRect viewport,
                                        std::optional<Keire::GpuOcclusionSurfaceDiagnostics> occlusionSurface,
                                        std::optional<Keire::UiItemRect> occupied = std::nullopt) override;
    void DrawGame(Keire::UiFrame& ui);
    void UpdateSmokePlayValidation();
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
    void DrawRenderGraph(Keire::UiFrame& ui);
    void DrawArchitectureDashboard(Keire::UiFrame& ui);
    [[nodiscard]] std::optional<Keire::UiItemRect>
    DrawPerformanceOverlay(Keire::UiFrame& ui, Keire::UiItemRect viewport, std::string_view label,
                           std::optional<Keire::GpuOcclusionSurfaceDiagnostics> occlusionSurface = std::nullopt,
                           bool reserveToolbar = false, std::optional<Keire::UiItemRect> occupied = std::nullopt);
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
    [[nodiscard]] std::vector<Keire::ManagedAssetTypeDiagnostic>
    AssetBrowserManagedAssetTypeDiagnostics() const override;
    [[nodiscard]] std::filesystem::path AssetBrowserExternalEditor() const override;
    void ConfigureAssetBrowserExternalEditor() override;
    [[nodiscard]] static bool FileIsNewerThan(const std::filesystem::path& path,
                                              std::filesystem::file_time_type reference) noexcept;
    void RefreshAssetBrowserRecords();
    void SetAssetBrowserSelected(Keire::AssetId asset) noexcept override;
    void ClearAssetBrowserSceneSelection() noexcept override;
    void SetAssetBrowserStatus(std::string status) noexcept override;
    void ReportAssetBrowserError(std::string message) noexcept override;
    void ImportAssetBrowserAssets(std::span<const Keire::AssetId> assets = {}) override;
    bool CreateAssetBrowserScene(std::string_view name) override;
    bool CreateAssetBrowserMaterial(std::string_view name) override;
    bool CreateAssetBrowserAnimationGraph(std::string_view name) override;
    bool CreateAssetBrowserProceduralMotionProfile(std::string_view name) override;
    bool CreateAssetBrowserScript(std::string_view name) override;
    bool CreateAssetBrowserScriptableObjectScript(std::string_view name) override;
    bool CreateAssetBrowserManagedAssembly(std::string_view name) override;
    bool CreateAssetBrowserManagedData(Keire::ManagedTypeId type, std::string_view name) override;
    bool CreateAssetBrowserAudioMixer(std::string_view name) override;
    bool CreateAssetBrowserPhysicsMaterial(std::string_view name) override;
    bool CreateAssetBrowserVfxEffect(std::string_view name) override;
    bool CreateAssetBrowserUiDocument(std::string_view name) override;
    bool CreateAssetBrowserUiStyleSheet(std::string_view name) override;
    bool CreateAssetBrowserUiFontFamily(std::string_view name, Keire::AssetId face) override;
    bool CreateAssetBrowserMaterialGraph(std::string_view name, Keire::AssetId shader) override;
    bool CreateAssetBrowserShaderGraph(std::string_view name, Keire::ShaderGraphTemplate graphTemplate) override;
    bool CreateAssetBrowserReusableGraph(std::string_view name, Keire::ShaderGraphPurpose purpose) override;
    bool CreateAssetBrowserMaterialParameterCollection(std::string_view name) override;
    bool CreateAssetBrowserMaterialInstance(std::string_view name) override;
    bool CreateAssetBrowserPrefab(std::string_view name) override;
    bool CreateAssetBrowserPrefabVariant(Keire::AssetId basePrefab, std::string_view name) override;
    void CreateAssetBrowserPrefabFromObject(Keire::AssetId object, const std::filesystem::path& folder) override;
    bool CreateAssetBrowserShader(std::string_view name) override;
    bool CreateAssetBrowserInputActions(Keire::InputActionAssetDefinition definition,
                                        std::string_view baseName) override;
    void ExtractAssetBrowserMaterials(Keire::AssetId model) override;
    void CreateAssetBrowserPackage(KeireEditor::AssetPackageSelection selection,
                                   KeireEditor::AssetPackageDraft draft) override;
    void MutateAssetBrowser(Keire::Detail::AssetWorkerMutation mutation, Keire::Detail::AssetWorkerMutation reverse,
                            std::string name, bool revealResult) override;
    void OpenAssetBrowserInputActions(Keire::AssetId asset) override;
    void OpenAssetBrowserAnimationGraph(Keire::AssetId asset) override;
    void OpenAssetBrowserAudioMixer(Keire::AssetId asset) override;
    void OpenAssetBrowserVfxEffect(Keire::AssetId asset) override;
    void OpenAssetBrowserUiDocument(Keire::AssetId asset) override;
    void OpenAssetBrowserUiStyleSheet(Keire::AssetId asset) override;
    void OpenAssetBrowserMaterial(Keire::AssetId asset) override;
    void OpenAssetBrowserMaterialGraph(Keire::AssetId asset) override;
    void OpenAssetBrowserMaterialInstance(Keire::AssetId asset) override;
    void OpenAssetBrowserShaderGraph(Keire::AssetId asset) override;
    void OpenAssetBrowserMaterialParameterCollection(Keire::AssetId asset) override;
    void OpenAssetBrowserPrefab(Keire::AssetId asset) override;
    void OpenAssetBrowserScene(Keire::AssetId asset) override;
    [[nodiscard]] bool PrepareAssetBrowserExternalOpen(Keire::AssetId asset) override;
    void CopyAssetBrowserText(std::string_view value) override;
    [[nodiscard]] KeireEditor::UiBuilderDocument& UiBuilderState() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& UiBuilderTheme() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetSystem> UiBuilderAssets() const noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> UiBuilderAssetRecords() const noexcept override;
    void RevealUiBuilderAsset(Keire::AssetId asset) override;
    [[nodiscard]] std::optional<Keire::UiSize> UiBuilderGameViewSize() const noexcept override;
    [[nodiscard]] KeireEditor::UiBuilderStyleSheetDocument& UiBuilderStyleSheetState() noexcept override;
    void ActivateUiBuilderHistory() noexcept override;
    void ActivateUiBuilderStyleSheetHistory() noexcept override;
    void OpenUiBuilderStyleSheet(Keire::AssetId asset) override;
    void SaveUiBuilderDocument() override;
    void ReloadUiBuilderDocument() override;
    void SaveUiBuilderStyleSheet() override;
    void RequestSaveUiBuilderStyleSheetAs() override;
    void ReloadUiBuilderStyleSheet() override;
    [[nodiscard]] KeireEditor::UiStyleTokenRefactorPreview
    PreviewUiBuilderTokenRefactor(std::string_view currentName, std::string_view replacementName) override;
    void ApplyUiBuilderTokenRefactor(const KeireEditor::UiStyleTokenRefactorPreview& preview) override;
    void ReportUiBuilderError(std::string message) noexcept override;
    [[nodiscard]] KeireEditor::UiBuilderLiveDebugCapture CaptureUiBuilderLiveDebug(Keire::AssetId visualTree) override;
    void SetUiBuilderLivePicking(Keire::AssetId visualTree, bool enabled) noexcept override;
    [[nodiscard]] std::optional<Keire::AssetId> ConsumeUiBuilderLivePick(Keire::AssetId visualTree) noexcept override;
    void SynchronizeUiBuilderLiveDraft() noexcept;
    [[nodiscard]] KeireEditor::SceneDocument& InspectorSceneDocument() noexcept override;
    [[nodiscard]] KeireEditor::InputActionsDocument& InspectorInputDocument() noexcept override;
    [[nodiscard]] KeireEditor::MaterialDocument& InspectorMaterialDocument() noexcept override;
    [[nodiscard]] KeireEditor::PropertyDrawerRegistry& InspectorPropertyDrawers() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& InspectorTheme() const noexcept override;
    [[nodiscard]] std::span<const std::string> InspectorLayerNames() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> InspectorAssetDatabase() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetSystem> InspectorAssetSystem() const noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> InspectorAssetRecords() const noexcept override;
    [[nodiscard]] Keire::AssetId InspectorDefaultAudioMixer() const noexcept override;
    [[nodiscard]] Keire::AssetId InspectorSelectedAsset() const noexcept override;
    [[nodiscard]] std::string_view InspectorAssetStatus() const noexcept override;
    [[nodiscard]] std::vector<Keire::ManagedAssetTypeDescriptor> InspectorManagedAssetTypes() const override;
    [[nodiscard]] std::optional<Keire::ManagedTypeId>
    InspectorManagedDataType(Keire::AssetId asset) const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::UndoContext> InspectorManagedDataHistory() const noexcept override;
    [[nodiscard]] bool InspectorPlayModeActive() const noexcept override;
    void SetInspectorSelectedAsset(Keire::AssetId asset) noexcept override;
    void PreviewInspectorAudio(Keire::AssetId asset) override;
    void StopInspectorAudioPreview() noexcept override;
    void ActivateInspectorHistory() noexcept override;
    void ActivateInspectorManagedDataHistory() noexcept override;
    void RecordInspectorUndo(std::string_view name, std::string mergeKey = {}) override;
    void ApplyInspectorTransformEdit(KeireEditor::InspectorTransformEdit edit) override;
    void NotifyInspectorMaterialAssigned(Keire::AssetId material) override;
    void NotifyInspectorUiToolkitAssetAssigned(Keire::AssetId asset) noexcept override;
    void AddScriptToEntity(Keire::EntityId entity, Keire::AssetId script) override;
    void CommitInspectorMaterial() override;
    void OpenInspectorInputActions(Keire::AssetId asset) override;
    void OpenInspectorMaterialGraph(Keire::AssetId asset) override;
    void OpenInspectorUiDocument(Keire::AssetId asset) override;
    void PersistInspectorMaterialInstance(Keire::AssetId asset, std::span<const std::byte> bytes) override;
    void PersistInspectorMaterialParameterCollection(Keire::AssetId asset, std::span<const std::byte> bytes) override;
    void PersistInspectorProceduralMotionProfile(Keire::AssetId asset, std::span<const std::byte> bytes) override;
    void ApplyInspectorImportSettings(Keire::AssetId asset, const Keire::AssetImportSettings& settings) override;
    void ImportInspectorAssets() override;
    void PreviewInspectorManagedData(Keire::AssetId asset, const Keire::ManagedDataDefinition& definition) override;
    void PersistInspectorManagedData(Keire::AssetId asset, std::span<const std::byte> bytes) override;
    void RenameInspectorAsset(Keire::AssetId asset, std::string_view name) override;
    void DuplicateInspectorAsset(Keire::AssetId asset, const std::filesystem::path& destination) override;
    void TrashInspectorAsset(Keire::AssetId asset) override;
    void SetInspectorAssetStatus(std::string status) noexcept override;
    void ReportInspectorAssetError(std::string message) noexcept override;
    [[nodiscard]] KeireEditor::MaterialGraphDocument& MaterialGraphState() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& MaterialGraphTheme() const noexcept override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> MaterialGraphAssetRecords() const noexcept override;
    [[nodiscard]] std::optional<Keire::ShaderGraphDefinition>
    ResolveMaterialGraphFunction(Keire::AssetId asset) const override;
    [[nodiscard]] std::optional<Keire::ShaderGraphDefinition>
    ResolveMaterialGraphTemplate(const Keire::MaterialShaderReference& shader) const override;
    [[nodiscard]] Keire::Ref<const Keire::Texture2DAsset>
    ResolveMaterialGraphTexture(Keire::AssetId asset) const override;
    void OpenMaterialGraphFunction(Keire::AssetId asset) override;
    void SaveMaterialGraphDocument() override;
    void UndoMaterialGraphEdit() override;
    void RedoMaterialGraphEdit() override;
    void RevealMaterialGraphAsset(Keire::AssetId asset) override;
    void ReportMaterialGraphError(std::string message) noexcept override;
    void SetGraphClipboard(std::string_view text) override;
    [[nodiscard]] std::string GraphClipboard() const override;
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
    [[nodiscard]] std::pair<Keire::AssetId, Keire::AssetId> ProjectDefaultInput() const noexcept override;
    [[nodiscard]] std::vector<Keire::InputActionMapDefinition>
    ProjectInputActionMaps(Keire::AssetId asset) const override;
    void SetProjectDefaultInput(Keire::AssetId asset, Keire::AssetId map) override;
    [[nodiscard]] KeireEditor::ManagedSdkPreference ProjectManagedSdk() const override;
    void SetProjectManagedSdk(KeireEditor::ManagedSdkPreference preference) override;
    [[nodiscard]] Keire::RenderFeatureCapabilities ProjectRenderFeatureCapabilities() const noexcept override;
    void ApplyProjectAuthoringSettings(const Keire::ProjectAuthoringSettings& settings) override;
    [[nodiscard]] KeireEditor::SceneDocument& LightingSceneDocument() noexcept override;
    [[nodiscard]] bool LightingBakeBusy() const noexcept override;
    [[nodiscard]] std::optional<Keire::AssetOperationProgress> LightingBakeProgress() const noexcept override;
    void QueueLightingBake(bool force) override;
    void OpenDroppedScene(Keire::AssetId asset) override;
    void OpenDroppedInputActions(Keire::AssetId asset) override;
    void InstantiateDroppedPrefab(Keire::AssetId asset) override;
    void CreateDroppedMeshEntity(Keire::AssetId asset) override;
    void AssignDroppedMaterial(Keire::EntityId entity, Keire::AssetId asset) override;
    void ConfigureAssetImporters(Keire::AssetDatabaseSpecification& specification) const;
    void
    ImportAssets(KeireEditor::AssetOperationPriority priority = KeireEditor::AssetOperationPriority::ExplicitAction);
    void UpdateAssetOperations();
    void CompleteGraphFunctionExtraction(const KeireEditor::GraphFunctionExtractionState& state,
                                         Keire::AssetId created);
    void QueueAssetMutation(std::shared_ptr<KeireEditor::AssetMutationUndoState> state,
                            KeireEditor::AssetMutationPhase phase);
    void ApplyAssetImportResult(const Keire::AssetImportResult& result, bool reloadLoadedAssets,
                                Keire::AssetId reloadAsset = {});
    void CompletePendingMaterialAssignment(Keire::AssetId refreshedAsset);
    void QueueMaterialCatalogRefresh(Keire::AssetId reloadAsset = {});
    void UpdateMaterialCatalogRefresh(double unscaledDeltaSeconds);
    void FlushMaterialCatalogRefresh() noexcept;
    void CancelMaterialCatalogRefresh() noexcept;
    void CommitMaterialDraft();
    void HandleExternalAssetDrop(const Keire::WindowFileDropEvent& event);
    void DrawExternalAssetImport(Keire::UiFrame& ui);
    void CookAssets();
    void StartManagedBuild();
    void UpdateManagedBuild();
    void RequestPlayerBuild(bool runAfterBuild);
    void StartPlayerBuild(bool runAfterBuild);
    void UpdatePlayerBuild();
    void BindPlayerBuildCommands();
    void InitializePlayerBuild();
    void ShutdownPlayerBuild() noexcept;
    void SavePlayerBuildConfiguration();
    void OpenBuildSupportHub(const Keire::PlayerBuildProfile& profile);
    void RevealPlayerBuild();
    [[nodiscard]] bool CanBuildPlayer(bool runAfterBuild) const noexcept;
    [[nodiscard]] bool CanRevealPlayerBuild() const noexcept;
    bool CreateInputActions(Keire::InputActionAssetDefinition definition, std::string_view baseName,
                            bool requireExactName = false);
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
    [[nodiscard]] Keire::AudioMeterSnapshot AudioMixerMeters() const noexcept override;
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
    [[nodiscard]] KeireEditor::VfxEffectPreviewStatus VfxEffectPreviewState() const noexcept override;
    void ActivateVfxEffectHistory() noexcept override;
    void SaveVfxEffectDocument() override;
    void DiscardVfxEffectDocument() override;
    void ReloadVfxEffectDocument(Keire::AssetId asset) override;
    void UndoVfxEffectEdit() override;
    void RedoVfxEffectEdit() override;
    void RevealVfxEffectAsset(Keire::AssetId asset) override;
    void RestartVfxEffectPreview() override;
    void SetVfxEffectPreviewPaused(bool paused) noexcept override;
    void SetVfxEffectPreviewAutoRestart(bool enabled) noexcept override;
    void SetVfxEffectPreviewBackend(Keire::VfxBackend backend) override;
    void SetVfxEffectPreviewSpeed(float speed) override;
    void StopVfxEffectPreview() noexcept override;
    void ReportVfxEffectError(std::string message) noexcept override;
    [[nodiscard]] KeireEditor::ShaderGraphDocument& ShaderGraphState() noexcept override;
    [[nodiscard]] const Keire::UiThemeDefinition& ShaderGraphTheme() const noexcept override;
    void SaveShaderGraphDocument() override;
    void UndoShaderGraphEdit() override;
    void RedoShaderGraphEdit() override;
    [[nodiscard]] std::span<const Keire::AssetSourceRecord> ShaderGraphAssetRecords() const noexcept override;
    [[nodiscard]] Keire::Ref<const Keire::MeshAsset> ResolveShaderGraphPreviewMesh(Keire::AssetId asset) override;
    [[nodiscard]] std::optional<Keire::ShaderGraphDefinition>
    ResolveShaderGraphFunction(Keire::AssetId asset) const override;
    void RevealShaderGraphAsset(Keire::AssetId asset) override;
    [[nodiscard]] bool ExtractShaderGraphSelectionToFunction(std::span<const Keire::AssetId> selection,
                                                             std::string_view name) override;
    void ReportShaderGraphError(std::string message) noexcept override;
    [[nodiscard]] bool ExtractMaterialGraphSelectionToFunction(std::span<const Keire::AssetId> selection,
                                                               std::string_view name) override;
    [[nodiscard]] bool CreateCSharpScript(std::string_view name, bool scriptableObject = false);
    [[nodiscard]] bool CreateManagedAssembly(std::string_view name);
    [[nodiscard]] bool CreateAudioMixer(std::string_view name);
    [[nodiscard]] bool CreatePhysicsMaterial(std::string_view name);
    [[nodiscard]] bool CreateVfxEffect(std::string_view name);
    [[nodiscard]] bool CreateMaterialGraph(std::string_view name, Keire::AssetId shader);
    [[nodiscard]] bool CreateShaderGraph(std::string_view name, Keire::ShaderGraphTemplate graphTemplate);
    [[nodiscard]] bool CreateReusableGraph(std::string_view name, Keire::ShaderGraphPurpose purpose);
    [[nodiscard]] bool CreateMaterialParameterCollection(std::string_view name);
    [[nodiscard]] bool CreateMaterialInstance(std::string_view name);
    [[nodiscard]] bool CreatePrefabFromSelection(std::string_view name);
    [[nodiscard]] bool CreatePrefabVariant(Keire::AssetId basePrefab, std::string_view name);
    void CreatePrefabFromObject(Keire::AssetId object, const std::filesystem::path& folder);
    [[nodiscard]] Keire::AssetId CreatePrefabAsset(const std::filesystem::path& destination,
                                                   const Keire::PrefabDefinition& definition);
    void GenerateManagedIdeWorkspace();
    void ExtendManagedAssemblySourceRoot(Keire::AssetId assembly, const std::filesystem::path& sourceRoot);
    void OpenPrefabForEditing(Keire::AssetId asset);
    void SavePrefabEditingStage();
    void ClosePrefabEditingStage();
    void ApplySelectedPrefabOverrides();
    void ReplacePrefabSource(Keire::AssetId asset, const Keire::PrefabDefinition& definition);
    bool CreateUnlitShader(std::string_view name = {});
    [[nodiscard]] bool CreateMaterial(std::string_view name = "Material");
    [[nodiscard]] bool CreateAnimationGraph(std::string_view name = "NewAnimatorController");
    [[nodiscard]] bool CreateProceduralMotionProfile(std::string_view name = "NewProceduralMotionProfile");
    void OpenAnimationGraph(Keire::AssetId asset);
    void OpenUiBuilder(Keire::AssetId asset);
    [[nodiscard]] bool
    TryPickUiBuilderLiveElement(std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations,
                                const Keire::UiItemRect& viewport, const Keire::UiPointerState& pointer) noexcept;
    void SaveAnimationGraph();
    void OpenAudioMixer(Keire::AssetId asset);
    void SaveAudioMixer();
    void PersistAudioMixer(Keire::AssetId asset, std::span<const std::byte> bytes);
    void PreviewAudioMixer(Keire::AssetId asset, const Keire::AudioMixerDefinition& definition);
    void OpenVfxEffect(Keire::AssetId asset);
    void SaveVfxEffect();
    void PersistVfxEffect(Keire::AssetId asset, std::span<const std::byte> bytes);
    void PreviewVfxEffect(Keire::AssetId asset, const Keire::VfxEffectDefinition& definition);
    void OpenShaderGraph(Keire::AssetId asset);
    void SaveShaderGraph();
    void OpenMaterialGraph(Keire::AssetId asset);
    void SaveMaterialGraph();
    void UpdateMaterialGraphAutosave(double unscaledDeltaSeconds);
    [[nodiscard]] std::optional<Keire::ShaderInterfaceDefinition>
    ResolveMaterialGraphInterface(const Keire::MaterialShaderReference& shader) const;
    [[nodiscard]] std::optional<Keire::ShaderGraphDefinition> ResolveReusableGraph(Keire::AssetId asset) const;
    [[nodiscard]] Keire::AssetId ResolveMaterialGraphShader(const Keire::MaterialShaderReference& shader) const;
    void ApplyMaterialGraphDevelopmentRevision(Keire::AssetId asset,
                                               const Keire::MaterialAssetDefinition& material) noexcept;
    void PersistMaterialGraph(Keire::AssetId asset, std::span<const std::byte> bytes);
    void
    ApplyShaderGraphDevelopmentRevision(Keire::AssetId asset, const Keire::ShaderGraphDefinition& definition,
                                        const Keire::ShaderGraphCompilation& compilation,
                                        std::span<const Keire::Ref<Keire::ShaderAsset>> developmentShaders) noexcept;
    void PersistShaderGraph(Keire::AssetId asset, std::span<const std::byte> bytes);
    void EnsureEditorVfxPreviewWorld(std::uint32_t minimumParticleCapacity);
    void ResetEditorVfxPreviewWorld() noexcept;
    void SynchronizeEditModeVfxPreviews();
    void StopEditModeVfxPreviews() noexcept;
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
    [[nodiscard]] Keire::ManagedApplicationInfo ManagedApplication() const override;
    void RequestManagedExit(int exitCode) noexcept override;
    [[nodiscard]] double ManagedTimeScale() const noexcept override;
    [[nodiscard]] bool SetManagedTimeScale(double scale) noexcept override;
    [[nodiscard]] bool ManagedTimePaused() const noexcept override;
    [[nodiscard]] bool SetManagedTimePaused(bool paused) noexcept override;
    [[nodiscard]] Keire::ManagedScreenState ManagedScreen() const noexcept override;
    [[nodiscard]] bool SetManagedScreen(std::uint32_t width, std::uint32_t height,
                                        Keire::ManagedScreenMode mode) noexcept override;
    [[nodiscard]] std::uint64_t BeginManagedSceneLoad(Keire::AssetId scene,
                                                      Keire::SceneLoadMode mode) noexcept override;
    [[nodiscard]] std::optional<Keire::ManagedSceneLoadStatus>
    ManagedSceneLoad(std::uint64_t operation) const noexcept override;
    [[nodiscard]] bool CancelManagedSceneLoad(std::uint64_t operation) noexcept override;
    [[nodiscard]] bool UnloadManagedScene(Keire::SceneHandle scene) noexcept override;
    [[nodiscard]] bool SetActiveManagedScene(Keire::SceneHandle scene) noexcept override;
    [[nodiscard]] bool MakeManagedEntityPersistent(Keire::ManagedEntityHandle entity) noexcept override;
    [[nodiscard]] Keire::AssetId ActiveManagedScene() const noexcept override;
    [[nodiscard]] std::vector<Keire::AssetId> LoadedManagedScenes() const override;
    [[nodiscard]] Keire::ManagedSceneHandle ActiveManagedSceneHandle() const noexcept override;
    [[nodiscard]] std::vector<Keire::ManagedSceneHandle> LoadedManagedSceneHandles() const override;
    [[nodiscard]] std::vector<std::string> ManagedEntityTags(Keire::ManagedEntityHandle entity) const override;
    [[nodiscard]] bool AddManagedEntityTag(Keire::ManagedEntityHandle entity, std::string_view tag) noexcept override;
    [[nodiscard]] bool RemoveManagedEntityTag(Keire::ManagedEntityHandle entity,
                                              std::string_view tag) noexcept override;
    [[nodiscard]] bool ClearManagedEntityTags(Keire::ManagedEntityHandle entity) noexcept override;
    [[nodiscard]] std::vector<Keire::ManagedEntityHandle> QueryManagedEntityNames(std::string_view name,
                                                                                  std::size_t maximum) const override;
    [[nodiscard]] std::vector<Keire::ManagedEntityHandle> QueryManagedEntityTags(std::string_view tag,
                                                                                 std::size_t maximum) const override;
    [[nodiscard]] std::vector<Keire::ManagedEntityHandle>
    QueryManagedEntityComponents(Keire::ComponentTypeId component, std::size_t maximum) const override;
    [[nodiscard]] std::vector<Keire::ManagedEntityHandle>
    QueryManagedEntityNamesScoped(std::string_view name, Keire::ManagedSceneQuery query,
                                  std::size_t maximum) const override;
    [[nodiscard]] std::vector<Keire::ManagedEntityHandle>
    QueryManagedEntityTagsScoped(std::string_view tag, Keire::ManagedSceneQuery query,
                                 std::size_t maximum) const override;
    [[nodiscard]] std::vector<Keire::ManagedEntityHandle>
    QueryManagedEntityComponentsScoped(Keire::ComponentTypeId component, Keire::ManagedSceneQuery query,
                                       std::size_t maximum) const override;
    [[nodiscard]] std::optional<Keire::RenderEnvironmentSettings> ManagedRenderEnvironment() const noexcept override;
    [[nodiscard]] bool SetManagedRenderEnvironment(Keire::RenderEnvironmentSettings settings) noexcept override;
    [[nodiscard]] bool ManagedMaterialParameterCollectionReady(Keire::AssetId collection) noexcept override;
    [[nodiscard]] bool SetManagedMaterialParameter(Keire::AssetId collection, std::string_view name,
                                                   Keire::MaterialPropertyValue value) noexcept override;
    [[nodiscard]] bool ResetManagedMaterialParameter(Keire::AssetId collection,
                                                     std::string_view name) noexcept override;
    [[nodiscard]] bool ClearManagedMaterialParameters(Keire::AssetId collection) noexcept override;
    [[nodiscard]] Keire::Vector2 ReadManagedInput(std::string_view action) noexcept override;
    [[nodiscard]] Keire::ManagedInputState ReadManagedInputState(std::string_view action) noexcept override;
    [[nodiscard]] std::vector<Keire::ManagedInputDevice> ManagedInputDevices() const override;
    [[nodiscard]] std::string ManagedInputControlScheme() const override;
    [[nodiscard]] bool SetManagedInputControlScheme(std::string_view scheme, bool locked) noexcept override;
    [[nodiscard]] bool ClearManagedInputControlSchemeLock() noexcept override;
    [[nodiscard]] bool SetManagedGamepadRumble(std::uint32_t device, float lowFrequency, float highFrequency,
                                               float durationSeconds) noexcept override;
    [[nodiscard]] std::uint64_t BeginManagedInputRebind(Keire::AssetId binding,
                                                        Keire::ManagedInputRebindOptions options) noexcept override;
    [[nodiscard]] std::optional<Keire::ManagedInputRebindSnapshot>
    ManagedInputRebind(std::uint64_t operation) const noexcept override;
    [[nodiscard]] bool ResolveManagedInputRebind(std::uint64_t operation,
                                                 Keire::ManagedInputRebindResolution resolution) noexcept override;
    [[nodiscard]] bool CancelManagedInputRebind(std::uint64_t operation) noexcept override;
    [[nodiscard]] bool SaveManagedInputBindings(std::string_view profile) noexcept override;
    [[nodiscard]] int LoadManagedInputBindings(std::string_view profile) noexcept override;
    [[nodiscard]] bool ClearManagedInputBindings() noexcept override;
    [[nodiscard]] std::uint64_t CreateManagedInputContext(std::uint64_t generation,
                                                          Keire::AssetId asset) noexcept override;
    [[nodiscard]] bool ReleaseManagedInputContext(std::uint64_t handle) noexcept override;
    void ReleaseManagedInputContexts(std::uint64_t generation) noexcept override;
    [[nodiscard]] bool OperateManagedInputContext(std::uint64_t handle, Keire::ManagedInputContextOperation operation,
                                                  Keire::AssetId target) noexcept override;
    [[nodiscard]] std::uint64_t
    BeginManagedInputContextRebind(std::uint64_t handle, Keire::AssetId binding,
                                   Keire::ManagedInputRebindOptions options) noexcept override;
    [[nodiscard]] Keire::AssetId FindManagedInputMap(std::uint64_t handle, std::string_view name) noexcept override;
    [[nodiscard]] Keire::AssetId FindManagedInputAction(std::uint64_t handle, Keire::AssetId map,
                                                        std::string_view name) noexcept override;
    [[nodiscard]] std::optional<Keire::ManagedInputActionSnapshot>
    ManagedInputAction(std::uint64_t handle, Keire::AssetId action) noexcept override;
    [[nodiscard]] std::optional<Keire::InputDeviceId>
    CurrentManagedInputDevice(Keire::InputDeviceType type) noexcept override;
    [[nodiscard]] std::optional<Keire::InputControlSnapshot>
    ManagedInputControl(Keire::InputDeviceId device, std::string_view path) noexcept override;
    [[nodiscard]] std::optional<Keire::ManagedRaycastHit>
    RaycastManaged(const Keire::ManagedRaycastQuery& query) noexcept override;
    [[nodiscard]] std::optional<Keire::ManagedRaycastHit>
    CapsuleCastManaged(const Keire::ManagedCapsuleCastQuery& query) noexcept override;
    [[nodiscard]] std::vector<Keire::AssetId>
    OverlapSphereManaged(const Keire::ManagedSphereOverlapQuery& query) override;
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
    [[nodiscard]] bool SendManagedVfxEvent(Keire::AssetId entity, std::string_view eventName,
                                           std::uint32_t spawnCount) noexcept override;
    [[nodiscard]] bool SetManagedVfxParameter(Keire::AssetId entity,
                                              const Keire::VfxParameterOverride& value) noexcept override;
    [[nodiscard]] std::optional<Keire::ManagedUiDocumentElement>
    ManagedUiDocumentRoot(Keire::AssetId document) noexcept override;
    [[nodiscard]] std::optional<Keire::ManagedUiDocumentElement>
    FindManagedUiDocumentElement(Keire::AssetId document, Keire::AssetId stableId) noexcept override;
    [[nodiscard]] std::optional<Keire::ManagedUiDocumentElement>
    FindManagedUiDocumentElement(Keire::AssetId document, std::string_view name) noexcept override;
    [[nodiscard]] bool ManagedUiDocumentElementAlive(Keire::AssetId document, std::uint64_t documentGeneration,
                                                     std::uint64_t element) noexcept override;
    [[nodiscard]] std::optional<std::string> ReadManagedUiDocumentElementText(Keire::AssetId document,
                                                                              std::uint64_t documentGeneration,
                                                                              std::uint64_t element) noexcept override;
    [[nodiscard]] bool SetManagedUiDocumentElementText(Keire::AssetId document, std::uint64_t documentGeneration,
                                                       std::uint64_t element, std::string_view text) noexcept override;
    [[nodiscard]] std::optional<float> ReadManagedUiDocumentElementValue(Keire::AssetId document,
                                                                         std::uint64_t documentGeneration,
                                                                         std::uint64_t element) noexcept override;
    [[nodiscard]] bool SetManagedUiDocumentElementValue(Keire::AssetId document, std::uint64_t documentGeneration,
                                                        std::uint64_t element, float value) noexcept override;
    [[nodiscard]] std::optional<bool>
    ReadManagedUiDocumentElementFlag(Keire::AssetId document, std::uint64_t documentGeneration, std::uint64_t element,
                                     Keire::ManagedUiDocumentFlag property) noexcept override;
    [[nodiscard]] bool SetManagedUiDocumentElementFlag(Keire::AssetId document, std::uint64_t documentGeneration,
                                                       std::uint64_t element, Keire::ManagedUiDocumentFlag property,
                                                       bool value) noexcept override;
    [[nodiscard]] bool ConsumeManagedUiDocumentElementEvent(Keire::AssetId document, std::uint64_t documentGeneration,
                                                            std::uint64_t element,
                                                            Keire::RuntimeUiEventType type) noexcept override;
    [[nodiscard]] bool FocusManagedUiDocumentElement(Keire::AssetId document, std::uint64_t documentGeneration,
                                                     std::uint64_t element) noexcept override;
    [[nodiscard]] Keire::Ref<Keire::Scene> ManagedRuntimeScene(Keire::AssetId entity = {}) const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::AssetSystem> ManagedRuntimeAssets() const noexcept override;
    [[nodiscard]] Keire::Ref<Keire::SceneRuntimeSession>
    ManagedRuntimeSession(Keire::AssetId entity = {}) const noexcept;
    void AddConsoleMessage(std::string category, std::string message, Keire::UiColor color,
                           Keire::LogLevel level = Keire::LogLevel::Info) noexcept;
    void CaptureManagedRuntimeDiagnostics() noexcept;
    void ReportError(std::string category, std::string message) noexcept;
    void SetAssetError(std::string message) noexcept;
    void CreateScene();
    bool CreateSceneAsset(std::string_view name);
    void RequestCreateScene();
    void OpenScene(Keire::AssetId asset);
    void RequestOpenScene(Keire::AssetId asset);
    void RequestInitialScene(Keire::AssetId candidate);
    void OpenPendingStartupScene();
    void PersistEditorSessionScene(Keire::AssetId asset) noexcept;
    void PersistEditorSessionPreferences() noexcept;
    void SaveScene();
    void SaveSceneAs();
    void CompleteSaveSceneAs();
    void CompleteSaveUiBuilderStyleSheetAs();
    void CompleteAssetBrowserPackage();
    void RequestCloseScene();
    void CloseScene();
    void ExecutePendingSceneAction();
    void RequestEditorExit();
    void QueueSceneTransition(KeireEditor::EditorDocumentTransitionAction action, Keire::AssetId asset = {});
    void ProcessSceneTransition();
    void WriteSceneRecovery();
    void RestoreSceneRecovery();
    void DiscardSceneRecovery() noexcept;
    void RecordSceneUndo(std::string_view name = "Edit Scene", std::string mergeKey = {});
    void MarkPlayEditorEntity(Keire::AssetId entity);
    void SelectSceneEntity(Keire::AssetId entity, bool additive = false);
    void SetSceneSelection(std::span<const Keire::EntityId> entities, bool additive);
    [[nodiscard]] Keire::Ref<Keire::Scene> ActiveScene() const noexcept;
    [[nodiscard]] bool ProjectRequiresManagedRuntime() const noexcept;
    void BeginPlayMode();
    void ContinuePendingPlayMode();
    void CompletePendingPlayTransition();
    void UpdatePlayRuntime(double deltaSeconds, double interpolationAlpha);
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
    void OpenDiagnosticBundle();
    void DrawDiagnosticBundle(Keire::UiFrame& ui);
    [[nodiscard]] std::unique_ptr<KeireEditor::EditorDocumentWorkspaceDocuments> CreateDocumentWorkspaceDocuments();
    [[nodiscard]] std::unique_ptr<KeireEditor::EditorDocumentWorkspaceCoordinator> CreateDocumentCoordinator();
    [[nodiscard]] std::unique_ptr<KeireEditor::EditorManagedRuntimeCoordinator> CreateManagedRuntimeCoordinator();
    [[nodiscard]] std::unique_ptr<KeireEditor::EditorPackageCoordinator> CreatePackageCoordinator();
    [[nodiscard]] std::unique_ptr<KeireEditor::EditorPlayModeCoordinator> CreatePlayModeCoordinator();
    [[nodiscard]] std::unique_ptr<KeireEditor::EditorAssetOperationCoordinator> CreateAssetOperationCoordinator();
    [[nodiscard]] std::unique_ptr<KeireEditor::EditorBuildCookCoordinator> CreateBuildCookCoordinator();
    void DrainQueuedAssetMutation();
    void DrainQueuedPrefabCreation();
    void PollAssetHotReload();

    Keire::UiPanelRegistration m_Game;
    Keire::UiPanelRegistration m_ThemeEditor;
    Keire::UiPanelRegistration m_InputDebugger;
    Keire::UiPanelRegistration m_PrefabOverrides;
    Keire::UiPanelRegistration m_BuildSettings;
    Keire::UiPanelRegistration m_Profiler;
    Keire::UiPanelRegistration m_RenderGraph;
    Keire::UiPanelRegistration m_ArchitectureDashboard;
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
    std::unique_ptr<KeireEditor::EditorDocumentWorkspaceDocuments> m_ConstructedDocuments;
    KeireEditor::SceneDocument* m_SceneDocument = nullptr;
    std::unique_ptr<KeireEditor::SceneDocument> m_PrefabReturnDocument;
    std::optional<PrefabEditingStage> m_PrefabEditingStage;
    KeireEditor::InputActionsDocument* m_InputActionsDocument = nullptr;
    KeireEditor::AnimatorControllerDocument* m_AnimatorControllerDocument = nullptr;
    KeireEditor::AudioMixerDocument* m_AudioMixerDocument = nullptr;
    KeireEditor::VfxEffectDocument* m_VfxEffectDocument = nullptr;
    KeireEditor::ShaderGraphDocument* m_ShaderGraphDocument = nullptr;
    KeireEditor::MaterialGraphDocument* m_MaterialGraphDocument = nullptr;
    KeireEditor::ProjectSettingsDocument* m_ProjectSettingsDocument = nullptr;
    KeireEditor::MaterialDocument* m_MaterialDocument = nullptr;
    std::unique_ptr<KeireEditor::UiBuilderDocument> m_UiBuilderDocument;
    std::unique_ptr<KeireEditor::UiBuilderStyleSheetDocument> m_UiBuilderStyleSheetDocument;
    std::unique_ptr<KeireEditor::UiBuilderLiveDraftSession> m_UiBuilderLiveDraft;
    Keire::Ref<Keire::SaveFileDialogOperation> m_UiBuilderStyleSheetSaveDialog;
    std::unique_ptr<KeireEditor::EditorDocumentWorkspaceCoordinator> m_DocumentCoordinator;
    std::unique_ptr<KeireEditor::EditorCommandRouter> m_CommandRouter;
    std::unique_ptr<KeireEditor::EditorWorkspaceLifecycleCoordinator> m_LifecycleCoordinator;
    std::unique_ptr<KeireEditor::EditorReplayProfilingCoordinator> m_ReplayProfilingCoordinator;
    std::unique_ptr<Keire::Internal::DiagnosticBundleDialogController> m_DiagnosticBundle;
    std::unique_ptr<KeireEditor::SceneViewportPanel> m_SceneViewportPanel;
    std::unique_ptr<KeireEditor::HierarchyPanel> m_HierarchyPanel;
    std::unique_ptr<KeireEditor::InspectorPanel> m_InspectorPanel;
    std::unique_ptr<KeireEditor::InputActionsPanel> m_InputActionsPanel;
    std::unique_ptr<KeireEditor::AnimatorControllerPanel> m_AnimatorControllerPanel;
    std::unique_ptr<KeireEditor::RiggingStudioPanel> m_RiggingStudioPanel;
    std::unique_ptr<KeireEditor::AudioMixerPanel> m_AudioMixerPanel;
    std::unique_ptr<KeireEditor::VfxEffectPanel> m_VfxEffectPanel;
    std::unique_ptr<KeireEditor::ShaderGraphPanel> m_ShaderGraphPanel;
    std::unique_ptr<KeireEditor::MaterialGraphPanel> m_MaterialGraphPanel;
    std::unique_ptr<KeireEditor::UiBuilderPanel> m_UiBuilderPanel;
    std::unique_ptr<KeireEditor::ProjectSettingsPanel> m_ProjectSettingsPanel;
    std::unique_ptr<KeireEditor::LightingPanel> m_LightingPanel;
    std::unique_ptr<KeireEditor::PackageManagerPanel> m_PackageManagerPanel;
    std::unique_ptr<KeireEditor::EditorPackageCoordinator> m_PackageCoordinator;
    std::unique_ptr<KeireEditor::PropertyDrawerRegistry> m_PropertyDrawers;
    std::unique_ptr<KeireEditor::ViewportAssetDropRouter> m_ViewportAssetDropRouter;
    std::unique_ptr<KeireEditor::ScenePlayChangesPanel> m_PlayChangesPanel;
    std::unique_ptr<KeireEditor::ScenePlayChangeSet> m_PlayChanges;
    std::unique_ptr<KeireEditor::ScenePlayChangeTracker> m_PlayChangeTracker;
    std::unique_ptr<KeireEditor::SceneTransitionCoordinator> m_SceneTransitions;
    std::optional<Keire::SceneDefinition> m_PendingPlayEditorBefore;
    std::unique_ptr<KeireEditor::ExternalAssetImportController> m_ExternalAssetImport;
    std::unique_ptr<KeireEditor::AssetOperationService> m_AssetOperations;
    std::unique_ptr<KeireEditor::PlayerBuildService> m_PlayerBuildService;
    Keire::UiThemeDefinition m_Theme;
    Keire::UiThemeId m_PendingTheme;
    Dialog m_Dialog = Dialog::None;
    std::string m_ProfileName;
    std::string m_Error;
    std::string m_Notice;
    std::string m_AssetStatus;
    std::string m_RenderGraphStatus;
    std::string m_AudioMixerPreviewDiagnostic;
    std::string m_VfxEffectPreviewDiagnostic;
    Keire::Ref<Keire::AssetDatabase> m_AssetDatabase;
    std::vector<Keire::AssetSourceRecord> m_AssetRecords;
    KeireEditor::ManagedDataTypeCache m_ManagedDataTypeCache;
    KeireEditor::ManagedRuntimeDiagnosticsBridge m_ManagedRuntimeDiagnostics;
    Keire::WeakRef<Keire::ScriptSystem> m_ManagedRuntimeDiagnosticSource;
    std::uint64_t m_AssetRecordRevision = 0;
    std::uint64_t m_UiBuilderDocumentRevision = 0;
    std::uint64_t m_UiBuilderStyleSheetRevision = 0;
    std::string m_UiBuilderLiveDraftDiagnostic;
    bool m_ManagedIdeWorkspaceOpened = false;
    Keire::AssetId m_SelectedAsset;
    std::filesystem::path m_ExecutablePath;
    std::filesystem::path m_EditorSessionPath;
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
    std::deque<PendingAssetMutation> m_PendingAssetMutations;
    bool m_ExecutingQueuedAssetMutation = false;
    std::vector<PendingPrefabCreation> m_PendingPrefabCreations;
    struct PendingMaterialAssignment
    {
        Keire::EntityId Entity;
        Keire::AssetId Source;
    };
    std::optional<PendingMaterialAssignment> m_PendingMaterialAssignment;
    Keire::ManagedBuildOperationId m_LastManagedReload;
    Keire::ManagedBuildOperationId m_LastManagedBuildReport;
    Keire::PlayerSettings m_PlayerSettings;
    Keire::PlayerBuildProfiles m_PlayerBuildProfiles;
    Keire::PlayerBuildScenes m_PlayerBuildScenes;
    Keire::AssetId m_SelectedPlayerBuildScene;
    Keire::AssetId m_PlayerBuildSceneCandidate;
    Keire::AssetId m_PlayerSigningEditProfile;
    KeireEditor::AssetPicker m_PlayerBuildScenePicker;
    KeireEditor::AssetPicker m_WindowsPlayerIconPicker;
    KeireEditor::AssetPicker m_LinuxPlayerIconPicker;
    KeireEditor::AssetPicker m_MacOSPlayerIconPicker;
    std::string m_PlayerSigningArgumentsText;
    std::string m_PlayerSigningEnvironmentText;
    Keire::Ref<Keire::InputActionContext> m_InputContext;
    Keire::Ref<Keire::InputActionContext> m_GameplayInputContext;
    Keire::AssetId m_GameplayInputMap;
    Keire::Ref<Keire::SceneRuntimeWorld> m_PlayRuntimeWorld;
    struct ManagedSceneOperation final
    {
        std::uint64_t Id = 0;
        Keire::Ref<Keire::SceneRuntimeLoadOperation> Load;
    };
    std::vector<ManagedSceneOperation> m_ManagedSceneOperations;
    std::uint64_t m_NextManagedSceneOperation = 1;
    Keire::Detail::ManagedInputOperationStore m_ManagedInputOperations;
    Keire::Detail::ManagedInputContextStore m_ManagedInputContexts;
    std::optional<Keire::InputCaptureOverride> m_ManagedInputCaptureOverride;
    bool m_ManagedCursorVisible = true;
    bool m_ManagedCursorLocked = false;
    std::optional<Keire::RenderEnvironmentSettings> m_ManagedRenderEnvironmentOverride;
    Keire::Detail::ManagedMaterialParameterStore m_ManagedMaterialParameters;
    bool m_GameViewportInputActive = false;
    bool m_GameViewportCaptureSuspended = false;
    KeireEditor::RuntimeUiPointerRoutingState m_GameRuntimeUiPointer;
    Keire::AssetId m_UiBuilderLivePickVisualTree;
    std::optional<Keire::AssetId> m_UiBuilderLivePickedElement;
    std::uint64_t m_UiBuilderLiveDebugSequence = 0;
    bool m_UiBuilderLivePicking = false;
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
    Keire::Internal::DynamicResolutionController m_GameDynamicResolution;
    Keire::Ref<Keire::ScenePresentationRuntime> m_GameEditPresentation;
    Keire::UiItemRect m_GameViewportRect;
    Keire::AudioVoiceId m_InspectorAudioPreviewVoice;
    Keire::AssetId m_AudioMixerPreviewAsset;
    Keire::AssetId m_VfxEffectPreviewAsset;
    Keire::Ref<Keire::VfxWorld> m_VfxEffectPreviewWorld;
    Keire::VfxHandle m_VfxEffectPreviewHandle;
    Keire::Ref<const Keire::VfxEffectAsset> m_VfxEffectPreviewEffect;
    Keire::EntityId m_VfxEffectPreviewRoutedEntity;
    Keire::Vector3 m_VfxEffectPreviewPosition;
    Keire::Quaternion m_VfxEffectPreviewRotation;
    std::uint32_t m_VfxEffectPreviewSeedOffset = 0;
    std::vector<Keire::VfxParameterOverride> m_VfxEffectPreviewParameterOverrides;
    Keire::VfxHandle m_VfxEffectPreviewRestartHandle;
    Keire::Vector3 m_VfxEffectPreviewRestartPosition;
    Keire::Quaternion m_VfxEffectPreviewRestartRotation;
    bool m_VfxEffectPreviewRestartTransformInitialized = false;
    struct EditModeVfxPreviewState
    {
        Keire::AssetId Effect;
        Keire::AssetHandle<Keire::VfxEffectAsset> EffectHandle;
        Keire::VfxHandle Handle;
        std::uint64_t Revision = 0;
        std::uint32_t SeedOffset = 0;
        std::vector<Keire::VfxParameterOverride> ParameterOverrides;
        Keire::Vector3 RestartPosition;
        Keire::Quaternion RestartRotation;
        bool RestartTransformInitialized = false;
    };
    std::unordered_map<Keire::EntityId, EditModeVfxPreviewState> m_EditModeVfxPreviews;
    Keire::WeakRef<Keire::Scene> m_EditModeVfxPreviewScene;
    Keire::Ref<Keire::UndoContext> m_ThemeUndoContext;
    Keire::Ref<Keire::UndoContext> m_ManagedDataUndoContext;
    Keire::Ref<Keire::UndoContext> m_ActiveUndoContext;
    PendingPlayTransition m_PendingPlayTransition = PendingPlayTransition::None;
    Keire::UiColor m_NoticeColor;
    std::uint32_t m_FrameCount = 0;
    std::uint64_t m_AudioMixerDocumentRevision = 0;
    std::uint64_t m_VfxEffectDocumentRevision = 0;
    std::uint64_t m_MaterialGraphDocumentRevision = 0;
    std::uint64_t m_ShaderGraphDocumentRevision = 0;
    std::uint64_t m_VfxEffectPreviewRevision = 0;
    std::uint32_t m_VfxEffectPreviewCapacity = 0;
    float m_VfxEffectPreviewSpeed = 1.0F;
    Keire::VfxBackend m_VfxEffectPreviewBackend = Keire::VfxBackend::Cpu;
    std::vector<std::pair<Keire::EntityId, Keire::AssetId>> m_PendingScriptAttachments;
    bool m_ResolvingPendingScriptAttachments = false;
    bool m_ThemeDirty = false;
    bool m_InputTesting = false;
    bool m_InputForwardToConsole = false;
    bool m_InputRecordReleases = false;
    bool m_PlayFaultReported = false;
    bool m_VfxEffectPreviewPaused = false;
    bool m_VfxEffectPreviewAutoRestart = true;
    bool m_ShowPerformanceOverlay = false;
    bool m_ShowAdvancedPerformanceOverlay = false;
    bool m_MaximizeGameOnPlay = false;
    Keire::ScenePlayState m_PlayResumeState = Keire::ScenePlayState::Stopped;
    std::unordered_set<Keire::AssetId> m_PlayEditorTouchedEntities;
    bool m_PlayStartPending = false;
    bool m_PlayerBuildSettingsLoaded = false;
    bool m_PendingPlayerBuildRun = false;
    bool m_PlayerBuildReported = false;
    bool m_CloseThemeAfterDecision = false;
    bool m_OpenDialog = false;
    bool m_Smoke = false;
    bool m_InitializeProject = false;
    bool m_SmokePlay = false;
    bool m_SmokePlayRequested = false;
    int m_GameAspect = 0;
    std::unique_ptr<KeireEditor::EditorManagedRuntimeCoordinator> m_ManagedRuntimeCoordinator;
    std::unique_ptr<KeireEditor::EditorPlayModeCoordinator> m_PlayModeCoordinator;
    std::unique_ptr<KeireEditor::EditorAssetOperationCoordinator> m_AssetOperationCoordinator;
    std::unique_ptr<KeireEditor::EditorBuildCookCoordinator> m_BuildCookCoordinator;
    std::unique_ptr<KeireEditor::EditorSmokePlayValidation> m_SmokePlayValidation;
};
