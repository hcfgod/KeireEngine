#pragma once

#include <stdexcept>

#include "Keire/Core.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace KeireEditor
{
    class ProjectSettingsDocument;
    class AssetPicker;
    class SceneDocument;
    class InputActionsDocument;
    class ManagedDataInspectorPanel;
    class MaterialDocument;
    class PropertyDrawerRegistry;
    class SceneCameraController;
    class SceneGizmoController;
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
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> InspectorAssetDatabase() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetSystem> InspectorAssetSystem() const noexcept = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> InspectorAssetRecords() const noexcept = 0;
        [[nodiscard]] virtual Keire::AssetId InspectorSelectedAsset() const noexcept = 0;
        [[nodiscard]] virtual std::string_view InspectorAssetStatus() const noexcept = 0;
        [[nodiscard]] virtual std::vector<Keire::ManagedAssetTypeDescriptor> InspectorManagedAssetTypes() const = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::UndoContext> InspectorManagedDataHistory() const noexcept = 0;
        [[nodiscard]] virtual bool InspectorPlayModeActive() const noexcept = 0;
        virtual void SetInspectorSelectedAsset(Keire::AssetId asset) noexcept = 0;
        virtual void PreviewInspectorAudio(Keire::AssetId asset) = 0;
        virtual void StopInspectorAudioPreview() noexcept = 0;
        virtual void ActivateInspectorHistory() noexcept = 0;
        virtual void ActivateInspectorManagedDataHistory() noexcept = 0;
        virtual void RecordInspectorUndo(std::string_view name = "Edit Scene", std::string mergeKey = {}) = 0;
        virtual void AddScriptToEntity(Keire::EntityId, Keire::AssetId)
        {
            throw std::logic_error("This inspector does not support managed script attachment.");
        }
        virtual void CommitInspectorMaterial() = 0;
        virtual void OpenInspectorInputActions(Keire::AssetId asset) = 0;
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
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        void UpdateCamera(Keire::UiFrame& ui, const Keire::UiItemState& imageState);

        ISceneViewportController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        std::unique_ptr<SceneGizmoController> m_Gizmos;
        std::unique_ptr<SceneCameraController> m_Camera;
        Keire::Ref<Keire::RenderView> m_RenderView;
        Keire::Ref<Keire::RenderView> m_CameraPreviewView;
        Keire::Ref<Keire::ScenePresentationRuntime> m_EditPresentation;
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
        std::string m_Search;
    };

    class AssetInspectorPanel final
    {
      public:
        explicit AssetInspectorPanel(IInspectorController& controller);
        ~AssetInspectorPanel();
        void Draw(Keire::UiFrame& ui);
        void ClearState() noexcept;

      private:
        IInspectorController& m_Controller;
        std::unique_ptr<AssetPicker> m_AssetPicker;
        std::unique_ptr<ManagedDataInspectorPanel> m_ManagedDataInspector;
        Keire::AssetId m_EditingAsset;
        std::string m_AssetName;
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
            m_AssetInspector->ClearState();
        }

      private:
        IInspectorController& m_Controller;
        std::unique_ptr<AssetInspectorPanel> m_AssetInspector;
        std::unique_ptr<AssetPicker> m_AssetPicker;
        Keire::UiPanelRegistration m_Registration;
        std::unordered_map<std::string, bool> m_ComponentExpansion;
        std::string m_ComponentSearch;
        std::uint64_t m_EditSerial = 0;
        Keire::AssetId m_LockedEntity;
        bool m_Locked = false;
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
        IInputActionsController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        Keire::Ref<Keire::InputActionContext> m_RebindContext;
        Keire::Ref<Keire::InteractiveRebindOperation> m_Rebind;
        std::string m_Search;
        std::string m_Message;
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
        std::string m_CustomSdkPath;
        bool m_SdkInitialized = false;
    };
} // namespace KeireEditor
