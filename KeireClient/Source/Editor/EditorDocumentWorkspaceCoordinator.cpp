#include "KeireClient/Editor/EditorDocumentWorkspaceCoordinator.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/VfxEffectDocument.h"

#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        void RequireDocuments(const EditorDocumentWorkspaceDocuments& documents)
        {
            if (!documents.Scene || !documents.InputActions || !documents.AnimatorController || !documents.AudioMixer ||
                !documents.VfxEffect || !documents.ShaderGraph || !documents.MaterialGraph || !documents.Material ||
                !documents.ProjectSettings)
            {
                throw std::invalid_argument("Editor document/workspace ownership is incomplete.");
            }
        }

        void RequireDependencies(const EditorDocumentWorkspaceDependencies& dependencies)
        {
            if (!dependencies.CommitMaterialDraft || !dependencies.CancelMaterialCatalog ||
                !dependencies.UpdateMaterialGraphAutosave || !dependencies.UpdateMaterialCatalog ||
                !dependencies.WriteSceneRecovery || !dependencies.ReportSceneError ||
                !dependencies.CloseWorkspaceUndo || !dependencies.CloseSupplementalDocuments ||
                !dependencies.ProjectWritable)
            {
                throw std::invalid_argument("Editor document/workspace coordinator dependencies are incomplete.");
            }
        }
    } // namespace

    EditorDocumentWorkspaceDocuments::EditorDocumentWorkspaceDocuments() = default;

    EditorDocumentWorkspaceDocuments::~EditorDocumentWorkspaceDocuments() noexcept = default;

    EditorDocumentWorkspaceDocuments::EditorDocumentWorkspaceDocuments(EditorDocumentWorkspaceDocuments&&) noexcept =
        default;

    EditorDocumentWorkspaceDocuments&
    EditorDocumentWorkspaceDocuments::operator=(EditorDocumentWorkspaceDocuments&&) noexcept = default;

    EditorDocumentWorkspaceCoordinator::EditorDocumentWorkspaceCoordinator(
        EditorDocumentWorkspaceDocuments documents, EditorDocumentWorkspaceDependencies dependencies)
        : m_Documents(std::move(documents)), m_Dependencies(std::move(dependencies))
    {
        RequireDocuments(m_Documents);
        RequireDependencies(m_Dependencies);
    }

    EditorDocumentWorkspaceCoordinator::~EditorDocumentWorkspaceCoordinator() noexcept
    {
        // The composition root performs the named Shutdown sequence while its callback dependencies are alive. The
        // destructor only closes callback admission before releasing the uniquely owned documents.
        static_cast<void>(m_Lifetime.BeginShutdown());
    }

    void EditorDocumentWorkspaceCoordinator::UpdateMaintenance(const Keire::AssetId selectedAsset,
                                                               const double unscaledDeltaSeconds)
    {
        m_Lifetime.RequireOwnerThread("update maintenance");
        if (unscaledDeltaSeconds < 0.0)
            throw std::invalid_argument("Editor document update delta must not be negative.");
        if (m_Documents.Material->Dirty() && selectedAsset != m_Documents.Material->Asset())
            m_Dependencies.CommitMaterialDraft();
        m_Dependencies.UpdateMaterialGraphAutosave(unscaledDeltaSeconds);
        m_Documents.ShaderGraph->AdvanceCompilation(unscaledDeltaSeconds);
        m_Dependencies.UpdateMaterialCatalog(unscaledDeltaSeconds);
    }

    void EditorDocumentWorkspaceCoordinator::UpdateSceneLoad()
    {
        m_Lifetime.RequireOwnerThread("update scene load");
        const auto operation = m_Documents.Scene->LoadOperation();
        if (!operation)
            return;
        if (operation->State() == Keire::SceneLoadState::Failed)
        {
            m_Documents.Scene->SetStatus("Scene runtime load failed: " + operation->Diagnostic().Message);
            m_Documents.Scene->SetLoadOperation({});
        }
        else if (operation->State() == Keire::SceneLoadState::Ready)
        {
            m_Documents.Scene->SetStatus("Scene loaded and activated.");
            m_Documents.Scene->SetLoadOperation({});
        }
    }

    void EditorDocumentWorkspaceCoordinator::UpdateRecovery(const double unscaledDeltaSeconds)
    {
        m_Lifetime.RequireOwnerThread("update recovery");
        if (unscaledDeltaSeconds < 0.0)
            throw std::invalid_argument("Editor recovery update delta must not be negative.");
        const auto scene = m_Documents.Scene->EditingScene();
        if (!scene || !scene->Dirty())
        {
            m_Documents.Scene->ResetRecoveryTimer();
            return;
        }

        m_Documents.Scene->AdvanceRecovery(unscaledDeltaSeconds);
        if (m_Documents.Scene->RecoverySeconds() < 30.0)
            return;
        m_Documents.Scene->ResetRecoveryTimer();
        try
        {
            m_Dependencies.WriteSceneRecovery();
        }
        catch (const std::exception& error)
        {
            m_Documents.Scene->SetStatus(std::string("Scene recovery save failed: ") + error.what());
            m_Dependencies.ReportSceneError(std::string(m_Documents.Scene->Status()));
        }
    }

    void EditorDocumentWorkspaceCoordinator::CommitMaterialDraft()
    {
        m_Lifetime.RequireOwnerThread("commit material draft");
        m_Dependencies.CommitMaterialDraft();
    }

    void EditorDocumentWorkspaceCoordinator::CancelMaterialCatalog() noexcept
    {
        try
        {
            m_Lifetime.RequireOwnerThread("cancel material catalog");
            m_Dependencies.CancelMaterialCatalog();
        }
        catch (...)
        {
            ReportShutdownFailure("cancel-material-catalog", std::current_exception());
        }
    }

    void EditorDocumentWorkspaceCoordinator::CloseUndoContexts() noexcept
    {
        try
        {
            m_Lifetime.RequireOwnerThread("close undo contexts");
            for (const auto& context :
                 {m_Documents.InputActions->UndoContext(), m_Documents.AnimatorController->UndoContext(),
                  m_Documents.AudioMixer->UndoContext(), m_Documents.VfxEffect->UndoContext(),
                  m_Documents.ShaderGraph->UndoContext(), m_Documents.MaterialGraph->UndoContext(),
                  m_Documents.Scene->UndoContext()})
            {
                if (context)
                    context->Close();
            }
            m_Dependencies.CloseWorkspaceUndo();
        }
        catch (...)
        {
            ReportShutdownFailure("close-undo-contexts", std::current_exception());
        }
    }

    void EditorDocumentWorkspaceCoordinator::PersistRecovery()
    {
        m_Lifetime.RequireOwnerThread("persist recovery");
        const auto scene = m_Documents.Scene->EditingScene();
        if (scene && scene->Dirty())
            m_Dependencies.WriteSceneRecovery();
    }

    void EditorDocumentWorkspaceCoordinator::CloseProjectSettings() noexcept
    {
        if (m_ProjectSettingsClosed)
            return;
        try
        {
            m_Lifetime.RequireOwnerThread("close project settings");
        }
        catch (...)
        {
            ReportShutdownFailure("close-project-settings", std::current_exception());
            return;
        }
        try
        {
            if (m_Documents.ProjectSettings->Dirty() && m_Dependencies.ProjectWritable())
                m_Documents.ProjectSettings->Save();
        }
        catch (...)
        {
            ReportShutdownFailure("save-project-settings", std::current_exception());
        }
        try
        {
            m_Documents.ProjectSettings->Close();
            m_ProjectSettingsClosed = true;
        }
        catch (...)
        {
            ReportShutdownFailure("close-project-settings", std::current_exception());
        }
    }

    void EditorDocumentWorkspaceCoordinator::SetPendingTransition(const EditorDocumentTransitionAction action,
                                                                  const Keire::AssetId asset)
    {
        m_Lifetime.RequireOwnerThread("set pending transition");
        if (action == EditorDocumentTransitionAction::None)
            throw std::invalid_argument("A pending document transition requires a concrete action.");
        if (action == EditorDocumentTransitionAction::Open && !asset)
            throw std::invalid_argument("An open transition requires a scene asset.");
        m_PendingTransition = action;
        m_PendingTransitionAsset = action == EditorDocumentTransitionAction::Open ? asset : Keire::AssetId{};
    }

    void EditorDocumentWorkspaceCoordinator::CancelPendingTransition()
    {
        m_Lifetime.RequireOwnerThread("cancel pending transition");
        m_PendingTransition = EditorDocumentTransitionAction::None;
        m_PendingTransitionAsset = {};
    }

    std::pair<EditorDocumentTransitionAction, Keire::AssetId>
    EditorDocumentWorkspaceCoordinator::TakePendingTransition()
    {
        m_Lifetime.RequireOwnerThread("take pending transition");
        const auto result = std::pair{m_PendingTransition, m_PendingTransitionAsset};
        m_PendingTransition = EditorDocumentTransitionAction::None;
        m_PendingTransitionAsset = {};
        return result;
    }

    EditorDocumentTransitionAction EditorDocumentWorkspaceCoordinator::PendingTransition() const
    {
        m_Lifetime.RequireOwnerThread("read pending transition");
        return m_PendingTransition;
    }

    Keire::AssetId EditorDocumentWorkspaceCoordinator::PendingTransitionAsset() const
    {
        m_Lifetime.RequireOwnerThread("read pending transition asset");
        return m_PendingTransitionAsset;
    }

    void EditorDocumentWorkspaceCoordinator::Shutdown() noexcept
    {
        if (!m_Lifetime.BeginShutdown())
            return;

        m_PendingTransition = EditorDocumentTransitionAction::None;
        m_PendingTransitionAsset = {};

        const auto close = [this](const std::string_view operation, const auto& action) noexcept
        {
            try
            {
                action();
            }
            catch (...)
            {
                ReportShutdownFailure(operation, std::current_exception());
            }
        };
        if (!m_ProjectSettingsClosed)
        {
            close("save-project-settings",
                  [this]
                  {
                      if (m_Documents.ProjectSettings->Dirty() && m_Dependencies.ProjectWritable())
                          m_Documents.ProjectSettings->Save();
                  });
            close("close-project-settings",
                  [this]
                  {
                      m_Documents.ProjectSettings->Close();
                      m_ProjectSettingsClosed = true;
                  });
        }
        close("close-input-actions", [this] { m_Documents.InputActions->Close(); });
        close("close-animator-controller", [this] { m_Documents.AnimatorController->Close(); });
        close("close-audio-mixer", [this] { m_Documents.AudioMixer->Close(); });
        close("close-vfx-effect", [this] { m_Documents.VfxEffect->Close(); });
        close("close-shader-graph", [this] { m_Documents.ShaderGraph->Close(); });
        close("close-material-graph", [this] { m_Documents.MaterialGraph->Close(); });
        close("close-scene", [this] { m_Documents.Scene->Close(); });
        close("close-supplemental-documents", [this] { m_Dependencies.CloseSupplementalDocuments(); });
    }

    SceneDocument& EditorDocumentWorkspaceCoordinator::Scene()
    {
        m_Lifetime.RequireOwnerThread("access scene document");
        return *m_Documents.Scene;
    }

    InputActionsDocument& EditorDocumentWorkspaceCoordinator::InputActions()
    {
        m_Lifetime.RequireOwnerThread("access input-actions document");
        return *m_Documents.InputActions;
    }

    AnimatorControllerDocument& EditorDocumentWorkspaceCoordinator::AnimatorController()
    {
        m_Lifetime.RequireOwnerThread("access animator-controller document");
        return *m_Documents.AnimatorController;
    }

    AudioMixerDocument& EditorDocumentWorkspaceCoordinator::AudioMixer()
    {
        m_Lifetime.RequireOwnerThread("access audio-mixer document");
        return *m_Documents.AudioMixer;
    }

    VfxEffectDocument& EditorDocumentWorkspaceCoordinator::VfxEffect()
    {
        m_Lifetime.RequireOwnerThread("access VFX document");
        return *m_Documents.VfxEffect;
    }

    ShaderGraphDocument& EditorDocumentWorkspaceCoordinator::ShaderGraph()
    {
        m_Lifetime.RequireOwnerThread("access Shader Graph document");
        return *m_Documents.ShaderGraph;
    }

    MaterialGraphDocument& EditorDocumentWorkspaceCoordinator::MaterialGraph()
    {
        m_Lifetime.RequireOwnerThread("access Material Graph document");
        return *m_Documents.MaterialGraph;
    }

    MaterialDocument& EditorDocumentWorkspaceCoordinator::Material()
    {
        m_Lifetime.RequireOwnerThread("access material document");
        return *m_Documents.Material;
    }

    ProjectSettingsDocument& EditorDocumentWorkspaceCoordinator::ProjectSettings()
    {
        m_Lifetime.RequireOwnerThread("access project-settings document");
        return *m_Documents.ProjectSettings;
    }

    std::unique_ptr<SceneDocument>
    EditorDocumentWorkspaceCoordinator::ReplaceScene(std::unique_ptr<SceneDocument> replacement)
    {
        m_Lifetime.RequireOwnerThread("replace scene document");
        if (!replacement)
            throw std::invalid_argument("The replacement scene document must be available.");
        replacement.swap(m_Documents.Scene);
        return replacement;
    }

    EditorWorkspaceCallbackToken EditorDocumentWorkspaceCoordinator::CaptureCallbackToken() const
    {
        return m_Lifetime.CaptureCallbackToken();
    }

    bool EditorDocumentWorkspaceCoordinator::ShutdownComplete() const { return m_Lifetime.ShutdownComplete(); }

    void EditorDocumentWorkspaceCoordinator::ReportShutdownFailure(const std::string_view operation,
                                                                   const std::exception_ptr& failure) noexcept
    {
        if (!m_Dependencies.ReportShutdownFailure)
            return;
        try
        {
            m_Dependencies.ReportShutdownFailure(operation, failure);
        }
        catch (...)
        {
        }
    }
} // namespace KeireEditor
