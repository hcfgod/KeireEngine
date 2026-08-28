#pragma once

#include <stdexcept>

#include "Keire/Core.h"
#include "KeireClient/Editor/ExternalEditorProfiles.h"
#include "KeireClient/Editor/InspectorTransformUndo.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace KeireEditor
{
    enum class EditorViewportTarget : std::uint8_t
    {
        Scene,
        Game
    };

    [[nodiscard]] constexpr bool CompositesRuntimeGameUi(const EditorViewportTarget target) noexcept
    {
        return target == EditorViewportTarget::Scene || target == EditorViewportTarget::Game;
    }

    class ProjectSettingsDocument;
    class AssetPicker;
    class SceneDocument;
    class InputActionsDocument;
    class ManagedDataInspectorPanel;
    class MaterialDocument;
    class PropertyDrawerRegistry;
    class SceneCameraController;
    class SceneGizmoController;
    class ThumbnailService;
    class ISceneViewportController
    {
      public:
        virtual ~ISceneViewportController() = default;
        [[nodiscard]] virtual SceneDocument& SceneViewportDocument() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& SceneViewportTheme() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> SceneViewportAssetDatabase() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetSystem> SceneViewportAssetSystem() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::RenderSystem> SceneViewportRenderer() const noexcept = 0;
        [[nodiscard]] virtual const Keire::RenderEnvironmentSettings& SceneViewportSettings() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::WindowSystem> SceneViewportWindows() const noexcept = 0;
        [[nodiscard]] virtual Keire::WindowId SceneViewportWindow() const noexcept = 0;
        [[nodiscard]] virtual float SceneViewportDisplayScale() const noexcept = 0;
        [[nodiscard]] virtual const Keire::Time& SceneViewportTime() const noexcept = 0;
        [[nodiscard]] virtual bool SceneViewportPlayReviewActive() const noexcept = 0;
        [[nodiscard]] virtual Keire::VfxRenderSnapshot SceneViewportEditVfx() const = 0;
        virtual void ActivateSceneViewportHistory() noexcept = 0;
        virtual void RestoreSceneViewportRecovery() = 0;
        virtual void DiscardSceneViewportRecovery() noexcept = 0;
        virtual void ReportSceneViewportError(std::string message) noexcept = 0;
        virtual void SetSceneViewportSelectedAsset(Keire::AssetId asset) noexcept = 0;
        virtual void RequestSceneViewportNewScene() = 0;
        virtual void RevealSceneViewportScenes() = 0;
        virtual void RouteSceneViewportAsset(Keire::AssetTypeId type, Keire::AssetId asset, Keire::EntityId target) = 0;
        virtual void RecordSceneViewportUndo(std::string_view name) = 0;
        virtual void SelectSceneViewportEntity(Keire::AssetId entity, bool additive) = 0;
        virtual void SetSceneViewportSelection(std::span<const Keire::EntityId> entities, bool additive) = 0;
        [[nodiscard]] virtual std::optional<Keire::UiItemRect>
        DrawSceneViewportPerformanceOverlay(Keire::UiFrame& ui, Keire::UiItemRect viewport,
                                            std::optional<Keire::GpuOcclusionSurfaceDiagnostics> occlusionSurface,
                                            std::optional<Keire::UiItemRect> occupied = std::nullopt) = 0;
    };

    class IHierarchyController
    {
      public:
        virtual ~IHierarchyController() = default;
        [[nodiscard]] virtual Keire::Ref<Keire::Scene> ActiveHierarchyScene() const noexcept = 0;
        [[nodiscard]] virtual SceneDocument& HierarchyDocument() noexcept = 0;
        [[nodiscard]] virtual Keire::UiColor HierarchyAccent() const noexcept = 0;
        virtual void ActivateHierarchyHistory() noexcept = 0;
        virtual void DeleteHierarchySelection() = 0;
        virtual void RecordHierarchyUndo() = 0;
        virtual void MarkHierarchyEntity(Keire::AssetId entity) = 0;
        virtual void RequestHierarchyRename(Keire::AssetId entity, std::string name) = 0;
        virtual void UnpackHierarchyPrefab(Keire::AssetId entity, bool completely) = 0;
        virtual void AddScriptToEntity(Keire::EntityId, Keire::AssetId)
        {
            throw std::logic_error("This hierarchy does not support managed script attachment.");
        }
        virtual void ReportHierarchyError(std::string message) noexcept = 0;
    };

    class IInspectorController
    {
      public:
        virtual ~IInspectorController() = default;
        [[nodiscard]] virtual SceneDocument& InspectorSceneDocument() noexcept = 0;
        [[nodiscard]] virtual InputActionsDocument& InspectorInputDocument() noexcept = 0;
        [[nodiscard]] virtual MaterialDocument& InspectorMaterialDocument() noexcept = 0;
        [[nodiscard]] virtual PropertyDrawerRegistry& InspectorPropertyDrawers() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& InspectorTheme() const noexcept = 0;
        [[nodiscard]] virtual std::span<const std::string> InspectorLayerNames() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> InspectorAssetDatabase() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetSystem> InspectorAssetSystem() const noexcept = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> InspectorAssetRecords() const noexcept = 0;
        [[nodiscard]] virtual Keire::AssetId InspectorDefaultAudioMixer() const noexcept = 0;
        [[nodiscard]] virtual Keire::AssetId InspectorSelectedAsset() const noexcept = 0;
        [[nodiscard]] virtual std::string_view InspectorAssetStatus() const noexcept = 0;
        [[nodiscard]] virtual std::vector<Keire::ManagedAssetTypeDescriptor> InspectorManagedAssetTypes() const = 0;
        [[nodiscard]] virtual std::optional<Keire::ManagedTypeId>
        InspectorManagedDataType(Keire::AssetId asset) const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::UndoContext> InspectorManagedDataHistory() const noexcept = 0;
        [[nodiscard]] virtual bool InspectorPlayModeActive() const noexcept = 0;
        virtual void SetInspectorSelectedAsset(Keire::AssetId asset) noexcept = 0;
        virtual void PreviewInspectorAudio(Keire::AssetId asset) = 0;
        virtual void StopInspectorAudioPreview() noexcept = 0;
        virtual void ActivateInspectorHistory() noexcept = 0;
        virtual void ActivateInspectorManagedDataHistory() noexcept = 0;
        virtual void RecordInspectorUndo(std::string_view name = "Edit Scene", std::string mergeKey = {}) = 0;
        virtual void ApplyInspectorTransformEdit(InspectorTransformEdit edit) = 0;
        virtual void NotifyInspectorMaterialAssigned(Keire::AssetId) {}
        virtual void AddScriptToEntity(Keire::EntityId, Keire::AssetId)
        {
            throw std::logic_error("This inspector does not support managed script attachment.");
        }
        virtual void CommitInspectorMaterial() = 0;
        virtual void OpenInspectorInputActions(Keire::AssetId asset) = 0;
        virtual void OpenInspectorMaterialGraph(Keire::AssetId asset) = 0;
        virtual void PersistInspectorMaterialInstance(Keire::AssetId asset, std::span<const std::byte> bytes) = 0;
        virtual void PersistInspectorMaterialParameterCollection(Keire::AssetId asset,
                                                                 std::span<const std::byte> bytes) = 0;
        virtual void PersistInspectorProceduralMotionProfile(Keire::AssetId asset,
                                                             std::span<const std::byte> bytes) = 0;
        virtual void ApplyInspectorImportSettings(Keire::AssetId asset, const Keire::AssetImportSettings& settings) = 0;
        virtual void ImportInspectorAssets() = 0;
        virtual void PreviewInspectorManagedData(Keire::AssetId asset,
                                                 const Keire::ManagedDataDefinition& definition) = 0;
        virtual void PersistInspectorManagedData(Keire::AssetId asset, std::span<const std::byte> bytes) = 0;
        virtual void RenameInspectorAsset(Keire::AssetId asset, std::string_view name) = 0;
        virtual void DuplicateInspectorAsset(Keire::AssetId asset, const std::filesystem::path& destination) = 0;
        virtual void TrashInspectorAsset(Keire::AssetId asset) = 0;
        virtual void SetInspectorAssetStatus(std::string status) noexcept = 0;
        virtual void ReportInspectorAssetError(std::string message) noexcept = 0;
    };

    class IInputActionsController
    {
      public:
        virtual ~IInputActionsController() = default;
        [[nodiscard]] virtual InputActionsDocument& InputActionsState() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& InputActionsTheme() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> InputAssetDatabase() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::InputActionContext> InputActionContext() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::InputSystem> InputSystem() const noexcept = 0;
        virtual void ActivateInputHistory() noexcept = 0;
        virtual void SaveInputActionsDocument() = 0;
        virtual std::filesystem::path GenerateInputActionsWrapper(std::string_view className,
                                                                  std::string_view nameSpace) = 0;
        virtual void ReloadInputActionsDocument(Keire::AssetId asset) = 0;
        virtual void RecordInputActionsUndo(std::string_view name = "Edit Input Actions") = 0;
        virtual void UndoInputActions() = 0;
        virtual void RedoInputActions() = 0;
        virtual void ReportInputActionsError(std::string message) noexcept = 0;
    };

    struct ManagedSdkPreference
    {
        Keire::ManagedSdkSelection Selection = Keire::ManagedSdkSelection::Bundled;
        std::filesystem::path CustomExecutable;
    };

    class IProjectSettingsController
    {
      public:
        virtual ~IProjectSettingsController() = default;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord>
        ProjectSettingsAssetRecords() const noexcept = 0;
        virtual void RevealProjectSettingsAsset(Keire::AssetId asset) = 0;
        [[nodiscard]] virtual std::pair<Keire::AssetId, Keire::AssetId> ProjectDefaultInput() const noexcept = 0;
        [[nodiscard]] virtual std::vector<Keire::InputActionMapDefinition>
        ProjectInputActionMaps(Keire::AssetId asset) const = 0;
        virtual void SetProjectDefaultInput(Keire::AssetId asset, Keire::AssetId map) = 0;
        [[nodiscard]] virtual ManagedSdkPreference ProjectManagedSdk() const = 0;
        virtual void SetProjectManagedSdk(ManagedSdkPreference preference) = 0;
        virtual void ApplyProjectAuthoringSettings(const Keire::ProjectAuthoringSettings& settings) = 0;
    };

    class SceneViewportPanel final
    {
      public:
        explicit SceneViewportPanel(ISceneViewportController& controller);
        ~SceneViewportPanel();
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.scene", "Scene"});
        }
        void Initialize(const std::filesystem::path& projectRoot);
        void Shutdown(const std::filesystem::path& projectRoot) noexcept;
        void Draw(Keire::UiFrame& ui);
        [[nodiscard]] const Keire::UiItemRect& ViewportRect() const noexcept { return m_ViewportRect; }
        [[nodiscard]] const Keire::RenderCamera& LastCamera() const noexcept { return m_LastCamera; }
        [[nodiscard]] std::optional<Keire::GpuOcclusionSurfaceDiagnostics> OcclusionDiagnostics() const noexcept;
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        void UpdateCamera(Keire::UiFrame& ui, const Keire::UiItemState& imageState);

        ISceneViewportController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        std::unique_ptr<SceneGizmoController> m_Gizmos;
        std::unique_ptr<SceneCameraController> m_Camera;
        Keire::Ref<Keire::RenderView> m_RenderView;
        Keire::Ref<Keire::RenderView> m_CameraPreviewView;
        Keire::Ref<Keire::ScenePresentationRuntime> m_UiPresentation;
        Keire::UiItemRect m_ViewportRect;
        Keire::RenderCamera m_LastCamera;
        std::filesystem::path m_ProjectRoot;
        std::vector<Keire::AssetId> m_BoxSelectionBase;
        Keire::UiPosition m_BoxSelectionStart;
        bool m_BoxSelecting = false;
        bool m_BoxSelectionAdditive = false;
        bool m_CameraPreviewVisible = true;
        bool m_SuppressWarpPointerDelta = false;
    };

    class HierarchyPanel final
    {
      public:
        explicit HierarchyPanel(IHierarchyController& controller) noexcept : m_Controller(controller) {}
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.hierarchy", "Hierarchy"});
        }
        void Draw(Keire::UiFrame& ui);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        IHierarchyController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        Keire::AssetId m_SelectionAnchor;
        Keire::AssetId m_PendingSelectionCollapse;
        std::string m_Search;
    };

    class AssetInspectorPanel final
    {
      public:
        explicit AssetInspectorPanel(IInspectorController& controller);
        ~AssetInspectorPanel();
        void Draw(Keire::UiFrame& ui, Keire::AssetId selectedAsset, bool pinned);
        void ClearState() noexcept;

      private:
        IInspectorController& m_Controller;
        std::unique_ptr<AssetPicker> m_AssetPicker;
        std::unique_ptr<ManagedDataInspectorPanel> m_ManagedDataInspector;
        std::unique_ptr<ThumbnailService> m_Thumbnails;
        Keire::Ref<Keire::UiImage> m_PreviewImage;
        Keire::AssetId m_EditingAsset;
        Keire::AssetImportSettings m_OriginalImportSettings;
        Keire::AssetImportSettings m_ImportSettings;
        std::string m_PreviewDigest;
        std::filesystem::path m_PreviewProjectRoot;
        std::string m_AssetName;
        std::optional<Keire::MaterialParameterCollectionDefinition> m_MaterialParameterCollection;
        std::optional<Keire::ProceduralMotionProfile> m_ProceduralMotionProfile;
        bool m_MaterialParameterCollectionDirty = false;
        bool m_ProceduralMotionProfileDirty = false;
    };

    class InspectorPanel final
    {
      public:
        explicit InspectorPanel(IInspectorController& controller);
        ~InspectorPanel();
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.inspector", "Inspector"});
        }
        void Draw(Keire::UiFrame& ui);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }
        [[nodiscard]] bool UniformScale() const noexcept { return m_UniformScale; }
        [[nodiscard]] std::uint64_t EditSerial() const noexcept { return m_EditSerial; }
        void AdvanceEditSerial() noexcept { ++m_EditSerial; }
        void ClearSceneState() noexcept
        {
            m_ComponentExpansion.clear();
            m_ManagedGraphFocus.clear();
            m_AssetInspector->ClearState();
            m_EntityNameTarget = {};
            m_EntityNameDraft.clear();
            m_EntityNameEditing = false;
            m_EntityTagTarget = {};
            m_EntityTagDraft.clear();
            m_EntityTagEditing = false;
            m_RotationTarget = {};
            m_RotationEditing = false;
        }

      private:
        struct ComponentClipboard final
        {
            Keire::ComponentTypeId Type;
            Keire::ComponentPropertyBag Values;
            bool WholeComponent = false;
        };

        [[nodiscard]] bool DrawComponentMenu(Keire::UiFrame& ui, const Keire::Entity& entity,
                                             const Keire::Ref<Keire::Component>& component,
                                             const Keire::ComponentRegistration& registration,
                                             SceneDocument& sceneDocument, const Keire::Ref<Keire::Scene>& scene);
        void DrawRectTransformAnchorPreset(Keire::UiFrame& ui, const Keire::Entity& entity,
                                           const Keire::ComponentRegistration& registration,
                                           SceneDocument& sceneDocument, const Keire::UiThemeDefinition& theme);

        IInspectorController& m_Controller;
        std::unique_ptr<AssetInspectorPanel> m_AssetInspector;
        std::unique_ptr<AssetPicker> m_AssetPicker;
        Keire::UiPanelRegistration m_Registration;
        std::unordered_map<std::string, bool> m_ComponentExpansion;
        std::unordered_map<std::string, std::uint32_t> m_ManagedGraphFocus;
        std::optional<ComponentClipboard> m_ComponentClipboard;
        std::string m_ComponentSearch;
        std::uint64_t m_EditSerial = 0;
        Keire::AssetId m_LockedEntity;
        Keire::AssetId m_LockedAsset;
        Keire::AssetId m_EntityNameTarget;
        std::string m_EntityNameDraft;
        Keire::AssetId m_EntityTagTarget;
        std::string m_EntityTagDraft;
        Keire::AssetId m_RotationTarget;
        Keire::Vector3 m_RotationEuler;
        Keire::Quaternion m_RotationOrientation;
        bool m_EntityNameEditing = false;
        bool m_EntityTagEditing = false;
        bool m_RotationEditing = false;
        bool m_UniformScale = false;
    };

    class InputActionsPanel final
    {
      public:
        explicit InputActionsPanel(IInputActionsController& controller) noexcept : m_Controller(controller) {}
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.input-actions", "Input Actions", false});
        }
        void Draw(Keire::UiFrame& ui);
        void SetMessage(std::string message) { m_Message = std::move(message); }
        void ResetTransientState() noexcept;
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        struct TextDraft final
        {
            Keire::AssetId Target;
            std::string Value;
            bool Editing = false;
        };

        [[nodiscard]] static bool DrawTextDraft(Keire::UiFrame& ui, std::string_view label, Keire::AssetId target,
                                                std::string_view current, TextDraft& draft);

        IInputActionsController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        Keire::Ref<Keire::InputActionContext> m_RebindContext;
        Keire::Ref<Keire::InteractiveRebindOperation> m_Rebind;
        TextDraft m_SchemeNameDraft;
        TextDraft m_BindingGroupDraft;
        TextDraft m_MapNameDraft;
        TextDraft m_ActionNameDraft;
        TextDraft m_BindingNameDraft;
        TextDraft m_ControlPathDraft;
        std::string m_Search;
        std::string m_Message;
        std::string m_GeneratedClass;
        std::string m_GeneratedNamespace = "Game";
        bool m_LiveMonitor = false;
    };

    class ProjectSettingsPanel final
    {
      public:
        ProjectSettingsPanel(ProjectSettingsDocument& document, IProjectSettingsController& controller);
        ~ProjectSettingsPanel();
        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        ProjectSettingsDocument& m_Document;
        IProjectSettingsController& m_Controller;
        std::unique_ptr<AssetPicker> m_AssetPicker;
        Keire::UiPanelRegistration m_Registration;
        std::string m_Error;
        std::string m_AudioDeviceError;
        std::string m_CustomSdkPath;
        std::vector<ExternalEditorProfile> m_ExternalEditorProfiles;
        std::vector<Keire::AudioDeviceInfo> m_AudioPlaybackDevices;
        std::string m_SelectedExternalEditorId;
        std::string m_CustomEditorPath;
        bool m_SdkInitialized = false;
        bool m_EditorsInitialized = false;
        bool m_AudioDevicesInitialized = false;
    };
} // namespace KeireEditor
