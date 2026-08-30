#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/EditorAssetOperationCoordinator.h"
#include "KeireClient/Editor/EditorBuildCookCoordinator.h"
#include "KeireClient/Editor/EditorDocumentWorkspaceCoordinator.h"
#include "KeireClient/Editor/EditorManagedRuntimeCoordinator.h"
#include "KeireClient/Editor/EditorPackageCoordinator.h"
#include "KeireClient/Editor/EditorPlayModeCoordinator.h"
#include "KeireClient/Editor/EditorReplayProfilingCoordinator.h"
#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/PackageManagerPanel.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/UiBuilderDocument.h"
#include "KeireClient/Editor/UiBuilderLiveDraft.h"
#include "KeireClient/Editor/UiBuilderPanel.h"
#include "KeireClient/Editor/UiBuilderStyleSheetDocument.h"
#include "KeireClient/Editor/VfxEffectDocument.h"

#include "KeireInternal/Diagnostics/DiagnosticBundleUiInternal.h"

#include <exception>
#include <filesystem>

void EditorWorkspaceLayer::OnDetach() noexcept
{
    using Phase = KeireEditor::EditorWorkspaceShutdownPhase;

    m_LifecycleCoordinator->Shutdown(
        [&](const Phase phase)
        {
            switch (phase)
            {
            case Phase::DiagnosticBundle:
                m_DiagnosticBundle->Shutdown();
                m_ReplayProfilingCoordinator->Shutdown();
                break;
            case Phase::SessionPreferences:
                PersistEditorSessionPreferences();
                break;
            case Phase::BuildAndCook:
                m_BuildCookCoordinator->Shutdown();
                break;
            case Phase::Packages:
                m_PackageCoordinator->ShutdownPanel();
                break;
            case Phase::AssetOperations:
                m_AssetOperationCoordinator->ShutdownOperations();
                break;
            case Phase::ManagedRuntime:
                m_ManagedRuntimeCoordinator->DetachRuntimeServices();
                break;
            case Phase::MaterialDraft:
                m_DocumentCoordinator->CommitMaterialDraft();
                break;
            case Phase::MaterialCatalog:
                m_DocumentCoordinator->CancelMaterialCatalog();
                break;
            case Phase::SceneViewport:
            {
                const auto projectRoot = Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path{};
                m_SceneViewportPanel->Shutdown(projectRoot);
                break;
            }
            case Phase::ProjectSettings:
                m_DocumentCoordinator->CloseProjectSettings();
                break;
            case Phase::Input:
                EndInputTest();
                m_ManagedRuntimeCoordinator->Shutdown();
                StopInspectorAudioPreview();
                m_InspectorPanel->ClearSceneState();
                break;
            case Phase::PlayMode:
                m_PlayModeCoordinator->Shutdown();
                break;
            case Phase::TransientPanels:
                m_UiBuilderLiveDraft->Close();
                m_InputActionsPanel->ResetTransientState();
                m_UiBuilderPanel->ResetTransientState();
                m_AudioMixerPanel->StopTransientPreview();
                m_VfxEffectPanel->StopTransientPreview();
                StopEditModeVfxPreviews();
                ResetEditorVfxPreviewWorld();
                m_InputContext.Reset();
                break;
            case Phase::Undo:
                m_DocumentCoordinator->CloseUndoContexts();
                break;
            case Phase::SceneRecovery:
                m_DocumentCoordinator->PersistRecovery();
                break;
            case Phase::Documents:
                m_DocumentCoordinator->Shutdown();
                m_UiBuilderStyleSheetDocument->Close();
                m_UiBuilderDocument->Close();
                break;
            case Phase::AssetPackage:
                m_PackageCoordinator->Shutdown();
                break;
            case Phase::AssetBrowser:
                m_AssetOperationCoordinator->Shutdown();
                break;
            }
        },
        [](const Phase phase, const std::exception_ptr& failure)
        {
            try
            {
                if (failure)
                    std::rethrow_exception(failure);
            }
            catch (const std::exception& error)
            {
                KEIRE_CLIENT_ERROR("[Editor] Workspace shutdown phase '{}' failed: {}", KeireEditor::ToString(phase),
                                   error.what());
            }
            catch (...)
            {
                KEIRE_CLIENT_ERROR("[Editor] Workspace shutdown phase '{}' failed with a non-standard exception.",
                                   KeireEditor::ToString(phase));
            }
        });
}

void EditorWorkspaceLayer::OnFixedUpdate(const Keire::Time& time)
{
    if (m_PlayRuntimeWorld)
    {
        Keire::ProfileScope playFixed(Owner().GetProfiler(), Keire::ProfileCategory::Physics, "Play fixed + physics");
        m_PlayRuntimeWorld->FixedUpdate(static_cast<float>(time.FixedDeltaTime().Seconds()));
    }
}
