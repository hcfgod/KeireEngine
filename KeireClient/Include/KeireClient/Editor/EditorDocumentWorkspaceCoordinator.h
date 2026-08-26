#pragma once

#include "Keire/Assets/Asset.h"
#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace KeireEditor
{
    enum class EditorDocumentTransitionAction : std::uint8_t
    {
        None,
        Create,
        Open,
        Close,
        Exit
    };

    class AnimatorControllerDocument;
    class AudioMixerDocument;
    class InputActionsDocument;
    class MaterialDocument;
    class MaterialGraphDocument;
    class ProjectSettingsDocument;
    class SceneDocument;
    class ShaderGraphDocument;
    class VfxEffectDocument;

    struct EditorDocumentWorkspaceDocuments final
    {
        EditorDocumentWorkspaceDocuments();
        ~EditorDocumentWorkspaceDocuments() noexcept;

        EditorDocumentWorkspaceDocuments(const EditorDocumentWorkspaceDocuments&) = delete;
        EditorDocumentWorkspaceDocuments& operator=(const EditorDocumentWorkspaceDocuments&) = delete;
        EditorDocumentWorkspaceDocuments(EditorDocumentWorkspaceDocuments&&) noexcept;
        EditorDocumentWorkspaceDocuments& operator=(EditorDocumentWorkspaceDocuments&&) noexcept;

        std::unique_ptr<SceneDocument> Scene;
        std::unique_ptr<InputActionsDocument> InputActions;
        std::unique_ptr<AnimatorControllerDocument> AnimatorController;
        std::unique_ptr<AudioMixerDocument> AudioMixer;
        std::unique_ptr<VfxEffectDocument> VfxEffect;
        std::unique_ptr<ShaderGraphDocument> ShaderGraph;
        std::unique_ptr<MaterialGraphDocument> MaterialGraph;
        std::unique_ptr<MaterialDocument> Material;
        std::unique_ptr<ProjectSettingsDocument> ProjectSettings;
    };

    struct EditorDocumentWorkspaceDependencies final
    {
        std::function<void()> CommitMaterialDraft;
        std::function<void()> CancelMaterialCatalog;
        std::function<void(double)> UpdateMaterialGraphAutosave;
        std::function<void(double)> UpdateMaterialCatalog;
        std::function<void()> WriteSceneRecovery;
        std::function<void(std::string)> ReportSceneError;
        std::function<void()> CloseWorkspaceUndo;
        std::function<void()> CloseSupplementalDocuments;
        std::function<bool()> ProjectWritable;
        std::function<void(std::string_view, const std::exception_ptr&)> ReportShutdownFailure;
    };

    class EditorDocumentWorkspaceCoordinator final
    {
      public:
        EditorDocumentWorkspaceCoordinator(EditorDocumentWorkspaceDocuments documents,
                                           EditorDocumentWorkspaceDependencies dependencies);
        ~EditorDocumentWorkspaceCoordinator() noexcept;

        EditorDocumentWorkspaceCoordinator(const EditorDocumentWorkspaceCoordinator&) = delete;
        EditorDocumentWorkspaceCoordinator& operator=(const EditorDocumentWorkspaceCoordinator&) = delete;
        EditorDocumentWorkspaceCoordinator(EditorDocumentWorkspaceCoordinator&&) = delete;
        EditorDocumentWorkspaceCoordinator& operator=(EditorDocumentWorkspaceCoordinator&&) = delete;

        void UpdateMaintenance(Keire::AssetId selectedAsset, double unscaledDeltaSeconds);
        void UpdateSceneLoad();
        void UpdateRecovery(double unscaledDeltaSeconds);
        void CommitMaterialDraft();
        void CancelMaterialCatalog() noexcept;
        void CloseUndoContexts() noexcept;
        void PersistRecovery();
        void CloseProjectSettings() noexcept;
        void SetPendingTransition(EditorDocumentTransitionAction action, Keire::AssetId asset = {});
        void CancelPendingTransition();
        [[nodiscard]] std::pair<EditorDocumentTransitionAction, Keire::AssetId> TakePendingTransition();
        [[nodiscard]] EditorDocumentTransitionAction PendingTransition() const;
        [[nodiscard]] Keire::AssetId PendingTransitionAsset() const;
        void Shutdown() noexcept;

        [[nodiscard]] SceneDocument& Scene();
        [[nodiscard]] InputActionsDocument& InputActions();
        [[nodiscard]] AnimatorControllerDocument& AnimatorController();
        [[nodiscard]] AudioMixerDocument& AudioMixer();
        [[nodiscard]] VfxEffectDocument& VfxEffect();
        [[nodiscard]] ShaderGraphDocument& ShaderGraph();
        [[nodiscard]] MaterialGraphDocument& MaterialGraph();
        [[nodiscard]] MaterialDocument& Material();
        [[nodiscard]] ProjectSettingsDocument& ProjectSettings();
        [[nodiscard]] std::unique_ptr<SceneDocument> ReplaceScene(std::unique_ptr<SceneDocument> replacement);
        [[nodiscard]] EditorWorkspaceCallbackToken CaptureCallbackToken() const;
        [[nodiscard]] bool ShutdownComplete() const;

      private:
        void ReportShutdownFailure(std::string_view operation, const std::exception_ptr& failure) noexcept;

        EditorCoordinatorLifetime m_Lifetime{"Editor document/workspace coordinator"};
        EditorDocumentWorkspaceDocuments m_Documents;
        EditorDocumentWorkspaceDependencies m_Dependencies;
        bool m_ProjectSettingsClosed = false;
        EditorDocumentTransitionAction m_PendingTransition = EditorDocumentTransitionAction::None;
        Keire::AssetId m_PendingTransitionAsset;
    };
} // namespace KeireEditor
