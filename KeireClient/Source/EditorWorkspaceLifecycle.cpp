#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/PackageManagerPanel.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/VfxEffectDocument.h"

#include <exception>
#include <filesystem>

void EditorWorkspaceLayer::OnDetach() noexcept
{
    PersistEditorSessionPreferences();
    ShutdownPlayerBuild();
    m_PackageManagerPanel->Shutdown();
    if (m_AssetOperations)
        m_AssetOperations->Shutdown();
    try
    {
        if (const auto scripts = Owner().Scripts())
            scripts->SetRuntimeServices(nullptr);
    }
    catch (...)
    {
    }
    CommitMaterialDraft();
    CancelMaterialCatalogRefresh();
    const auto projectRoot = Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path{};
    m_SceneViewportPanel->Shutdown(projectRoot);
    if (m_ProjectSettingsDocument && m_ProjectSettingsDocument->Dirty())
    {
        try
        {
            if (const auto project = Owner().GetProject(); project && project->Writable())
                m_ProjectSettingsDocument->Save();
        }
        catch (const std::exception& error)
        {
            KEIRE_CLIENT_ERROR("[Rendering] Could not save project settings during shutdown: {}", error.what());
        }
    }
    if (m_ProjectSettingsDocument)
        m_ProjectSettingsDocument->Close();
    EndInputTest();
    m_ManagedInputOperations.CancelAll();
    m_ManagedInputCaptureOverride.reset();
    m_GameplayInputContext.Reset();
    m_ManagedCursorLocked = false;
    m_ManagedCursorVisible = true;
    m_GameViewportCaptureSuspended = false;
    ApplyManagedCursorMode();
    StopInspectorAudioPreview();
    m_InspectorPanel->ClearSceneState();
    if (m_PlayRuntimeWorld)
        m_PlayRuntimeWorld->Close();
    m_PlayRuntimeWorld.Reset();
    m_ManagedSceneOperations.clear();
    m_SceneDocument->EndPlay();
    m_GameEditPresentation.Reset();
    m_GameRenderView.Reset();
    m_InputActionsPanel->ResetTransientState();
    m_AudioMixerPanel->StopTransientPreview();
    m_VfxEffectPanel->StopTransientPreview();
    StopEditModeVfxPreviews();
    ResetEditorVfxPreviewWorld();
    m_InputContext.Reset();
    if (m_InputActionsDocument->UndoContext())
        m_InputActionsDocument->UndoContext()->Close();
    if (m_AudioMixerDocument->UndoContext())
        m_AudioMixerDocument->UndoContext()->Close();
    if (m_VfxEffectDocument->UndoContext())
        m_VfxEffectDocument->UndoContext()->Close();
    if (m_ShaderGraphDocument->UndoContext())
        m_ShaderGraphDocument->UndoContext()->Close();
    if (m_MaterialGraphDocument->UndoContext())
        m_MaterialGraphDocument->UndoContext()->Close();
    if (m_SceneDocument->UndoContext())
        m_SceneDocument->UndoContext()->Close();
    if (m_ThemeUndoContext)
        m_ThemeUndoContext->Close();
    if (m_ManagedDataUndoContext)
        m_ManagedDataUndoContext->Close();
    m_ActiveUndoContext.Reset();
    m_ThemeUndoContext.Reset();
    m_ManagedDataUndoContext.Reset();
    if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
    {
        try
        {
            WriteSceneRecovery();
        }
        catch (...)
        {
        }
    }
    m_InputActionsDocument->Close();
    m_AudioMixerDocument->Close();
    m_VfxEffectDocument->Close();
    m_ShaderGraphDocument->Close();
    m_MaterialGraphDocument->Close();
    m_SceneDocument->Close();
    if (m_PrefabReturnDocument)
        m_PrefabReturnDocument->Close();
    m_PrefabReturnDocument.reset();
    m_PrefabEditingStage.reset();
    m_PendingAssetPackageDialog.reset();
    if (m_AssetPackageExport.valid())
    {
        try
        {
            static_cast<void>(m_AssetPackageExport.get());
        }
        catch (const std::exception& error)
        {
            KEIRE_CLIENT_ERROR("[Assets] Asset-package export failed during shutdown: {}", error.what());
        }
    }
    m_AssetPackageOutput.clear();
    m_ManagedIdeWorkspaceOpened = false;
    m_AssetBrowserPanel->Close();
    m_AssetDatabase.Reset();
}

void EditorWorkspaceLayer::OnFixedUpdate(const Keire::Time& time)
{
    if (m_PlayRuntimeWorld)
    {
        Keire::ProfileScope playFixed(Owner().GetProfiler(), Keire::ProfileCategory::Physics, "Play fixed + physics");
        m_PlayRuntimeWorld->FixedUpdate(static_cast<float>(time.FixedDeltaTime().Seconds()));
    }
}
