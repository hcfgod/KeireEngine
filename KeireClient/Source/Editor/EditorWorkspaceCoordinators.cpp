#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/EditorAssetOperationCoordinator.h"
#include "KeireClient/Editor/EditorBuildCookCoordinator.h"
#include "KeireClient/Editor/EditorDocumentWorkspaceCoordinator.h"
#include "KeireClient/Editor/EditorManagedRuntimeCoordinator.h"
#include "KeireClient/Editor/EditorPackageCoordinator.h"
#include "KeireClient/Editor/EditorPlayModeCoordinator.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/PackageManagerPanel.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/VfxEffectDocument.h"

#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    void ReportCoordinatorShutdownFailure(const std::string_view authority, const std::string_view operation,
                                          const std::exception_ptr& failure) noexcept
    {
        try
        {
            if (failure)
                std::rethrow_exception(failure);
        }
        catch (const std::exception& error)
        {
            KEIRE_CLIENT_ERROR("[Editor] {} shutdown operation '{}' failed: {}", authority, operation, error.what());
        }
        catch (...)
        {
            KEIRE_CLIENT_ERROR("[Editor] {} shutdown operation '{}' failed.", authority, operation);
        }
    }
} // namespace

std::unique_ptr<KeireEditor::EditorDocumentWorkspaceDocuments> EditorWorkspaceLayer::CreateDocumentWorkspaceDocuments()
{
    auto documents = std::make_unique<KeireEditor::EditorDocumentWorkspaceDocuments>();
    documents->Scene = std::make_unique<KeireEditor::SceneDocument>();
    documents->InputActions = std::make_unique<KeireEditor::InputActionsDocument>();
    documents->AnimatorController = std::make_unique<KeireEditor::AnimatorControllerDocument>();
    documents->AudioMixer =
        std::make_unique<KeireEditor::AudioMixerDocument>(KeireEditor::AudioMixerDocumentSpecification{
            .Preview = [this](const Keire::AssetId asset, const Keire::AudioMixerDefinition& definition)
            { PreviewAudioMixer(asset, definition); },
            .StopPreview = [this](const Keire::AssetId) { StopAudioMixerPreview(); },
            .Persist = [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
            { PersistAudioMixer(asset, bytes); },
        });
    documents->VfxEffect = std::make_unique<KeireEditor::VfxEffectDocument>(KeireEditor::VfxEffectDocumentSpecification{
        .Preview = [this](const Keire::AssetId asset, const Keire::VfxEffectDefinition& definition)
        { PreviewVfxEffect(asset, definition); },
        .StopPreview = [this](const Keire::AssetId) { StopVfxEffectPreview(); },
        .Persist = [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
        { PersistVfxEffect(asset, bytes); },
    });
    documents->ShaderGraph =
        std::make_unique<KeireEditor::ShaderGraphDocument>(KeireEditor::ShaderGraphDocumentSpecification{
            .Preview =
                [this](const Keire::AssetId, const Keire::ShaderGraphCompilation& compilation,
                       const KeireEditor::ShaderGraphPreviewSettings& settings)
            {
                if (m_ShaderGraphPanel)
                    m_ShaderGraphPanel->UpdatePreview(compilation, settings);
            },
            .LiveApply = [this](const Keire::AssetId asset, const Keire::ShaderGraphDefinition& definition,
                                const Keire::ShaderGraphCompilation& compilation,
                                const std::span<const Keire::Ref<Keire::ShaderAsset>> developmentShaders)
            { ApplyShaderGraphDevelopmentRevision(asset, definition, compilation, developmentShaders); },
            .StopPreview =
                [this](const Keire::AssetId)
            {
                if (m_ShaderGraphPanel)
                    m_ShaderGraphPanel->ClearPreview();
            },
            .Persist = [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
            { PersistShaderGraph(asset, bytes); },
        });
    documents->MaterialGraph =
        std::make_unique<KeireEditor::MaterialGraphDocument>(KeireEditor::MaterialGraphDocumentSpecification{
            .ResolveInterface = [this](const Keire::MaterialShaderReference& shader)
            { return ResolveMaterialGraphInterface(shader); },
            .ResolveTemplate = [this](const Keire::MaterialShaderReference& shader)
            { return ResolveMaterialGraphTemplate(shader); },
            .ResolveFunction = [this](const Keire::AssetId asset) { return ResolveReusableGraph(asset); },
            .ResolveShader = [this](const Keire::MaterialShaderReference& shader)
            { return ResolveMaterialGraphShader(shader); },
            .Preview = [this](const Keire::AssetId asset, const Keire::MaterialAssetDefinition& material)
            { ApplyMaterialGraphDevelopmentRevision(asset, material); },
            .StopPreview = [](const Keire::AssetId) {},
            .Persist = [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
            { PersistMaterialGraph(asset, bytes); },
        });
    documents->ProjectSettings = std::make_unique<KeireEditor::ProjectSettingsDocument>();
    documents->Material = std::make_unique<KeireEditor::MaterialDocument>();
    return documents;
}

std::unique_ptr<KeireEditor::EditorDocumentWorkspaceCoordinator> EditorWorkspaceLayer::CreateDocumentCoordinator()
{
    if (!m_ConstructedDocuments)
        throw std::logic_error("Editor document construction ownership is unavailable.");
    auto coordinator = std::make_unique<KeireEditor::EditorDocumentWorkspaceCoordinator>(
        std::move(*m_ConstructedDocuments),
        KeireEditor::EditorDocumentWorkspaceDependencies{
            .CommitMaterialDraft = [this] { CommitMaterialDraft(); },
            .CancelMaterialCatalog = [this] { CancelMaterialCatalogRefresh(); },
            .UpdateMaterialGraphAutosave = [this](const double seconds) { UpdateMaterialGraphAutosave(seconds); },
            .UpdateMaterialCatalog = [this](const double seconds) { UpdateMaterialCatalogRefresh(seconds); },
            .WriteSceneRecovery = [this] { WriteSceneRecovery(); },
            .ReportSceneError = [this](std::string message) { ReportError("Scene", std::move(message)); },
            .CloseWorkspaceUndo =
                [this]
            {
                if (m_ThemeUndoContext)
                    m_ThemeUndoContext->Close();
                if (m_ManagedDataUndoContext)
                    m_ManagedDataUndoContext->Close();
                m_ActiveUndoContext.Reset();
                m_ThemeUndoContext.Reset();
                m_ManagedDataUndoContext.Reset();
            },
            .CloseSupplementalDocuments =
                [this]
            {
                if (m_PrefabReturnDocument)
                    m_PrefabReturnDocument->Close();
                m_PrefabReturnDocument.reset();
                m_PrefabEditingStage.reset();
            },
            .ProjectWritable =
                [this]
            {
                const auto project = Owner().GetProject();
                return project && project->Writable();
            },
            .ReportShutdownFailure = [](const std::string_view operation, const std::exception_ptr& failure)
            { ReportCoordinatorShutdownFailure("Document", operation, failure); },
        });
    m_ConstructedDocuments.reset();
    return coordinator;
}

std::unique_ptr<KeireEditor::EditorPackageCoordinator> EditorWorkspaceLayer::CreatePackageCoordinator()
{
    return std::make_unique<KeireEditor::EditorPackageCoordinator>(KeireEditor::EditorPackageCoordinatorDependencies{
        .ShutdownPanel = [this] { m_PackageManagerPanel->Shutdown(); },
        .AssetDatabase = [this] { return m_AssetDatabase; },
        .AssetRecords = [this] { return std::span<const Keire::AssetSourceRecord>(m_AssetRecords); },
        .Windows = [this] { return Owner().Windows(); },
        .MainWindow = [this] { return Owner().MainWindow()->Id(); },
        .SetStatus = [this](std::string status) { m_AssetStatus = std::move(status); },
        .SetError = [this](std::string message) { SetAssetError(std::move(message)); },
        .ReportShutdownFailure = [](const std::string_view operation, const std::exception_ptr& failure)
        { ReportCoordinatorShutdownFailure("Package", operation, failure); },
    });
}

std::unique_ptr<KeireEditor::EditorManagedRuntimeCoordinator> EditorWorkspaceLayer::CreateManagedRuntimeCoordinator()
{
    return std::make_unique<KeireEditor::EditorManagedRuntimeCoordinator>(KeireEditor::EditorManagedRuntimeDependencies{
        .StartBuild = [this] { StartManagedBuild(); },
        .PollBuild = [this] { UpdateManagedBuild(); },
        .ReportBuildError = [this](std::string message) { ReportError("Managed Build", std::move(message)); },
        .DetachRuntimeServices =
            [this]
        {
            if (const auto scripts = Owner().Scripts())
                scripts->SetRuntimeServices(nullptr);
        },
        .ResetRuntimeInput =
            [this]
        {
            m_ManagedInputOperations.CancelAll();
            m_ManagedInputContexts.ReleaseAll();
            m_ManagedInputCaptureOverride.reset();
            m_GameplayInputContext.Reset();
            m_GameplayInputMap = {};
            m_ManagedCursorLocked = false;
            m_ManagedCursorVisible = true;
            m_GameViewportCaptureSuspended = false;
            ApplyManagedCursorMode();
        },
        .ReportShutdownFailure = [](const std::string_view operation, const std::exception_ptr& failure)
        { ReportCoordinatorShutdownFailure("Managed runtime", operation, failure); },
    });
}

std::unique_ptr<KeireEditor::EditorPlayModeCoordinator> EditorWorkspaceLayer::CreatePlayModeCoordinator()
{
    return std::make_unique<KeireEditor::EditorPlayModeCoordinator>(KeireEditor::EditorPlayModeDependencies{
        .ProcessSceneTransition = [this] { ProcessSceneTransition(); },
        .FinalizeEditorMutation = [this] { FinalizePendingPlayEditorMutation(); },
        .CompletePendingTransition = [this] { CompletePendingPlayTransition(); },
        .UpdateRuntime = [this](const double delta, const double alpha) { UpdatePlayRuntime(delta, alpha); },
        .ContinuePendingPlay = [this] { ContinuePendingPlayMode(); },
        .CloseRuntime =
            [this]
        {
            if (m_PlayRuntimeWorld)
                m_PlayRuntimeWorld->Close();
            m_PlayRuntimeWorld.Reset();
            m_ManagedSceneOperations.clear();
            m_SceneDocument->EndPlay();
            m_GameEditPresentation.Reset();
            m_GameRenderView.Reset();
            m_GameDynamicResolution.Reset();
        },
        .ReportShutdownFailure = [](const std::string_view operation, const std::exception_ptr& failure)
        { ReportCoordinatorShutdownFailure("Play mode", operation, failure); },
    });
}

std::unique_ptr<KeireEditor::EditorAssetOperationCoordinator> EditorWorkspaceLayer::CreateAssetOperationCoordinator()
{
    return std::make_unique<KeireEditor::EditorAssetOperationCoordinator>(KeireEditor::EditorAssetOperationDependencies{
        .UpdateOperations = [this] { UpdateAssetOperations(); },
        .DrainQueuedMutation = [this] { DrainQueuedAssetMutation(); },
        .DrainQueuedPrefab = [this] { DrainQueuedPrefabCreation(); },
        .BusyOrPending =
            [this]
        {
            return (m_ExternalAssetImport && m_ExternalAssetImport->Pending()) ||
                   (m_AssetOperations && m_AssetOperations->Busy());
        },
        .PollHotReload = [this] { PollAssetHotReload(); },
        .ShutdownOperations =
            [this]
        {
            if (m_AssetOperations)
                m_AssetOperations->Shutdown();
        },
        .CloseAssetWorkspace =
            [this]
        {
            m_ManagedIdeWorkspaceOpened = false;
            m_AssetBrowserPanel->Close();
            m_AssetDatabase.Reset();
        },
        .ReportShutdownFailure = [](const std::string_view operation, const std::exception_ptr& failure)
        { ReportCoordinatorShutdownFailure("Asset operation", operation, failure); },
    });
}

std::unique_ptr<KeireEditor::EditorBuildCookCoordinator> EditorWorkspaceLayer::CreateBuildCookCoordinator()
{
    return std::make_unique<KeireEditor::EditorBuildCookCoordinator>(KeireEditor::EditorBuildCookDependencies{
        .UpdateBuild = [this] { UpdatePlayerBuild(); },
        .AssetDatabaseReady = [this] { return static_cast<bool>(m_AssetDatabase); },
        .ShutdownBuild = [this] { ShutdownPlayerBuild(); },
        .ReportShutdownFailure = [](const std::string_view operation, const std::exception_ptr& failure)
        { ReportCoordinatorShutdownFailure("Build and cook", operation, failure); },
    });
}
