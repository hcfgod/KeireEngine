#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/ConsolePanel.h"
#include "KeireClient/Editor/DiagnosticsPanel.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/PlayModeReadiness.h"
#include "KeireClient/Editor/PrefabAuthoring.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ScenePlayChanges.h"
#include "KeireClient/Editor/ScenePlayChangesPanel.h"
#include "KeireClient/Editor/SceneTransitionCoordinator.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"
#include "KeireClient/Editor/ViewportInputRouting.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
namespace
{
    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open scene asset: " + path.string());
        const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        std::vector<std::byte> bytes(characters.size());
        std::ranges::transform(characters, bytes.begin(), [](const char value) { return std::byte(value); });
        return bytes;
    }

    [[nodiscard]] Keire::Ref<Keire::Scene> RenderedScene(const Keire::Ref<Keire::Scene>& editing,
                                                         const Keire::Ref<Keire::SceneRuntimeSession>& play)
    {
        if (play && play->State() != Keire::ScenePlayState::Stopped)
        {
            const auto runtime = play->RuntimeScene();
            return runtime && runtime->IsOpen() ? runtime : Keire::Ref<Keire::Scene>{};
        }
        return editing && editing->IsOpen() ? editing : Keire::Ref<Keire::Scene>{};
    }

    struct SceneCamera final
    {
        Keire::Entity Entity;
        Keire::Ref<Keire::CameraComponent> Camera;
        Keire::Ref<Keire::TransformComponent> Transform;
    };

    [[nodiscard]] std::optional<SceneCamera> SelectGameCamera(const Keire::Ref<Keire::Scene>& scene)
    {
        if (!scene || !scene->IsOpen())
            return std::nullopt;
        std::optional<SceneCamera> selected;
        bool selectedPrimary = false;
        for (const auto& entity : scene->Query<Keire::CameraComponent>())
        {
            const auto camera = entity.GetComponent<Keire::CameraComponent>();
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!camera || !transform || !camera->Enabled() || !entity.ActiveInHierarchy())
                continue;
            if (!selected || (camera->Primary() && !selectedPrimary) ||
                (camera->Primary() == selectedPrimary && (camera->Priority() > selected->Camera->Priority() ||
                                                          (camera->Priority() == selected->Camera->Priority() &&
                                                           entity.Id().Value() < selected->Entity.Id().Value()))))
            {
                selected = SceneCamera{entity, camera, transform};
                selectedPrimary = camera->Primary();
            }
        }
        return selected;
    }

    [[nodiscard]] Keire::UiSize PrepareRenderSurface(const Keire::Ref<Keire::RenderView>& view,
                                                     const Keire::UiSize logicalSize, const float displayScale)
    {
        if (!view || !view->Surface())
            return {};
        const float width = std::max(logicalSize.Width, 1.0F);
        const float height = std::max(logicalSize.Height, 1.0F);
        const auto pixelWidth =
            static_cast<std::uint32_t>(std::round(std::clamp(width * std::max(displayScale, 1.0F), 1.0F, 16384.0F)));
        const auto pixelHeight =
            static_cast<std::uint32_t>(std::round(std::clamp(height * std::max(displayScale, 1.0F), 1.0F, 16384.0F)));
        view->Surface()->RequestSize(pixelWidth, pixelHeight);
        return {width, height};
    }

    class ContinuousUndoCommand final : public Keire::UndoCommand
    {
      public:
        ContinuousUndoCommand(std::string name, std::string mergeKey, Keire::UndoOperation redo,
                              Keire::UndoOperation undo, const std::size_t estimatedBytes,
                              Keire::UndoAvailability available)
            : m_Name(std::move(name)), m_MergeKey(std::move(mergeKey)), m_Redo(std::move(redo)),
              m_Undo(std::move(undo)), m_EstimatedBytes(std::max<std::size_t>(estimatedBytes, 1)),
              m_Available(std::move(available))
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override { return m_Name; }
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return m_EstimatedBytes; }
        [[nodiscard]] bool Available() const noexcept override
        {
            try
            {
                return !m_Available || m_Available();
            }
            catch (...)
            {
                return false;
            }
        }
        void Redo() override { m_Redo(); }
        void Undo() override { m_Undo(); }
        [[nodiscard]] bool TryMerge(const Keire::UndoCommand& newer) override
        {
            const auto* command = dynamic_cast<const ContinuousUndoCommand*>(&newer);
            return command && !m_MergeKey.empty() && command->m_MergeKey == m_MergeKey;
        }

      private:
        std::string m_Name;
        std::string m_MergeKey;
        Keire::UndoOperation m_Redo;
        Keire::UndoOperation m_Undo;
        std::size_t m_EstimatedBytes = 1;
        Keire::UndoAvailability m_Available;
    };

} // namespace

void EditorWorkspaceLayer::CreateScene()
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return;
    try
    {
        if (m_AssetOperations->Busy())
            throw std::runtime_error("Wait for the active asset operation before creating a scene.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        auto destination = directory / "Untitled.keirescene";
        for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
            destination = directory / ("Untitled " + std::to_string(copy) + ".keirescene");
        auto definition = Keire::SceneAsset::EmptyDefinition(destination.stem().string());
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::SceneAsset::Encode(definition), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenScene, .UndoName = "Create Scene"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Scene creation failed: ") + error.what());
    }
}

bool EditorWorkspaceLayer::CreateSceneAsset(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Scene name must be one non-empty path component.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keirescene");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A scene with that name already exists in this folder.");
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition(std::string(name))), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenScene, .UndoName = "Create Scene"});
        m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Scene creation failed: ") + error.what());
        return false;
    }
}

void EditorWorkspaceLayer::RequestCreateScene()
{
    m_PlayStartPending = false;
    if (m_PendingSceneAction != PendingSceneAction::None || (m_SceneTransitions && m_SceneTransitions->Pending()))
    {
        m_Notice = "Finish the pending scene transition before starting another one.";
        m_NoticeColor = m_Theme.Warning;
        return;
    }
    if (m_SceneDocument->PlaySession())
    {
        m_PendingSceneAction = PendingSceneAction::Create;
        RequestStopPlayMode();
        return;
    }
    if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Create;
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    QueueSceneTransition(PendingSceneAction::Create);
}

void EditorWorkspaceLayer::OpenScene(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        throw std::logic_error("Asset database is unavailable.");
    if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
        throw std::runtime_error("Save or revert the current scene before opening another scene.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->RelativePath.extension() != ".keirescene")
        throw std::invalid_argument("Only .keirescene assets can be opened as scenes.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    const auto definition = Keire::SceneAsset::Decode(ReadBytes(source))->Definition();
    auto scene = Keire::CreateRef<Keire::Scene>(asset, definition, Owner().Scenes()->Components());
    scene->MarkSaved();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext({.Name = "Scene: " + record->RelativePath.stem().string()});
    const auto previousAsset = m_SceneDocument->Asset();
    if (const auto scenes = Owner().Scenes(); scenes && previousAsset)
        (void)scenes->Unload(previousAsset);
    m_InspectorPanel->ClearSceneState();
    DiscardSceneRecovery();
    if (m_SceneDocument->UndoContext())
        m_SceneDocument->UndoContext()->Close();
    m_SceneDocument->Open(std::move(scene), asset, source, std::move(context));
    if (const auto project = Owner().GetProject())
        m_SceneDocument->SetRecoveryPath(project->SceneRecoveryDirectory() /
                                         (asset.ToString() + ".keirescene.recovery"));
    m_SceneDocument->SetRecoveryAvailable(!m_SceneDocument->RecoveryPath().empty() &&
                                          std::filesystem::is_regular_file(m_SceneDocument->RecoveryPath()));
    m_SelectedAsset = asset;
    m_ActiveUndoContext = m_SceneDocument->UndoContext();
    if (const auto scenes = Owner().Scenes())
        m_SceneDocument->SetLoadOperation(scenes->Load(asset, Keire::SceneLoadMode::Single));
    m_SceneDocument->SetStatus("Opening " + record->RelativePath.generic_string() + ".");
}

void EditorWorkspaceLayer::RequestOpenScene(const Keire::AssetId asset)
{
    m_PlayStartPending = false;
    if (m_PrefabEditingStage)
        throw std::runtime_error("Save or discard the active prefab stage before opening a scene.");
    if (asset == m_SceneDocument->Asset())
        return;
    if (m_PendingSceneAction != PendingSceneAction::None || (m_SceneTransitions && m_SceneTransitions->Pending()))
    {
        m_Notice = "A scene transition is already pending; the additional request was ignored.";
        m_NoticeColor = m_Theme.Warning;
        return;
    }
    if (m_SceneDocument->PlaySession())
    {
        m_PendingSceneAction = PendingSceneAction::Open;
        m_PendingSceneAsset = asset;
        RequestStopPlayMode();
        return;
    }
    if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Open;
        m_PendingSceneAsset = asset;
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    QueueSceneTransition(PendingSceneAction::Open, asset);
}

void EditorWorkspaceLayer::SaveScene()
{
    if (m_PrefabEditingStage)
    {
        SavePrefabEditingStage();
        return;
    }
    if (!m_SceneDocument->EditingScene() || !m_AssetDatabase || !m_SceneDocument->Asset())
        return;
    try
    {
        m_SceneDocument->Save();
        QueueMaterialCatalogRefresh(m_SceneDocument->Asset());
        m_SceneDocument->SetStatus("Scene saved atomically; refreshing runtime content in the background.");
        AddConsoleMessage("Scene", "Saved " + Keire::Detail::PathToUtf8(m_SceneDocument->Source().filename()),
                          m_Theme.Success);
    }
    catch (const std::exception& error)
    {
        m_SceneDocument->SetStatus(std::string("Scene save failed: ") + error.what());
        ReportError("Scene", m_SceneDocument->Status());
    }
}

void EditorWorkspaceLayer::SaveSceneAs()
{
    if (!m_SceneDocument->EditingScene() || !m_AssetDatabase || m_SceneDocument->SaveDialog())
        return;
    const auto assets = m_AssetDatabase->Specification().ProjectRoot / m_AssetDatabase->Specification().SourceDirectory;
    Keire::SaveFileDialogSpecification dialog;
    dialog.Title = "Save Scene As";
    dialog.DefaultLocation = assets / "Scenes";
    dialog.DefaultName = m_SceneDocument->EditingScene()->Name() + " Copy.keirescene";
    dialog.FilterName = "Kéire Scene";
    dialog.Extension = "keirescene";
    m_SceneDocument->SetSaveDialog(Owner().Windows()->ShowSaveFileDialog(Owner().MainWindow()->Id(), dialog));
    m_SceneDocument->SetStatus("Choose a new scene path under this project's Assets directory.");
}

void EditorWorkspaceLayer::CompleteSaveSceneAs()
{
    if (!m_SceneDocument->SaveDialog() ||
        m_SceneDocument->SaveDialog()->Status() == Keire::SaveFileDialogStatus::Pending)
        return;
    const auto operation = m_SceneDocument->TakeSaveDialog();
    if (operation->Status() == Keire::SaveFileDialogStatus::Cancelled)
        return;
    if (operation->Status() == Keire::SaveFileDialogStatus::Failed)
    {
        m_SceneDocument->SetStatus("Save As dialog failed: " + operation->Diagnostic());
        return;
    }
    try
    {
        auto destination = operation->SelectedPath();
        if (destination.extension() != ".keirescene")
            destination += ".keirescene";
        const auto assets = std::filesystem::weakly_canonical(m_AssetDatabase->Specification().ProjectRoot /
                                                              m_AssetDatabase->Specification().SourceDirectory);
        const auto parent = std::filesystem::weakly_canonical(destination.parent_path());
        const auto relativeParent = std::filesystem::relative(parent, assets);
        if (relativeParent.empty() || relativeParent.native().starts_with(std::filesystem::path("..").native()) ||
            destination.filename().empty())
            throw std::invalid_argument("Scene Save As must remain inside the project's Assets directory.");
        if (std::filesystem::exists(destination))
            throw std::invalid_argument("Scene Save As requires a new path and will not overwrite an existing asset.");
        auto definition = m_SceneDocument->EditingScene()->Snapshot();
        definition.Name = destination.stem().string();
        const auto bytes = Keire::SceneAsset::Encode(definition);
        const auto relative = relativeParent / destination.filename();
        if (!m_AssetOperations)
            throw std::logic_error("The isolated asset worker is unavailable.");
        m_AssetOperations->QueueCreateAsset(relative, bytes, {},
                                            {.FollowUp = KeireEditor::AssetOperationFollowUp::AdoptSceneCopy,
                                             .UndoName = "Save Scene As",
                                             .SceneSnapshot = definition,
                                             .SourceSceneAsset = m_SceneDocument->Asset(),
                                             .SceneSource = destination});
        m_SceneDocument->SetStatus("Saving the scene copy in the isolated asset worker.");
    }
    catch (const std::exception& error)
    {
        m_SceneDocument->SetStatus(std::string("Scene Save As failed: ") + error.what());
        ReportError("Scene", m_SceneDocument->Status());
    }
}

void EditorWorkspaceLayer::RequestCloseScene()
{
    m_PlayStartPending = false;
    if (m_PendingSceneAction != PendingSceneAction::None || (m_SceneTransitions && m_SceneTransitions->Pending()))
    {
        m_Notice = "A scene transition is already pending; the additional request was ignored.";
        m_NoticeColor = m_Theme.Warning;
        return;
    }
    if (m_SceneDocument->PlaySession())
    {
        m_PendingSceneAction = PendingSceneAction::Close;
        RequestStopPlayMode();
        return;
    }
    if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Close;
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    QueueSceneTransition(PendingSceneAction::Close);
}

void EditorWorkspaceLayer::CloseScene()
{
    if (m_PrefabEditingStage)
    {
        SetAssetError("Save or discard the active prefab stage before closing the scene.");
        return;
    }
    if (const auto scenes = Owner().Scenes(); scenes && m_SceneDocument->Asset())
        (void)scenes->Unload(m_SceneDocument->Asset());
    m_InspectorPanel->ClearSceneState();
    DiscardSceneRecovery();
    if (m_SceneDocument->UndoContext())
        m_SceneDocument->UndoContext()->Close();
    m_SceneDocument->Close();
    if (m_ActiveUndoContext && !m_ActiveUndoContext->IsOpen())
        m_ActiveUndoContext.Reset();
    m_SceneDocument->SetStatus("No scene is open.");
}

void EditorWorkspaceLayer::WriteSceneRecovery()
{
    if (m_SceneDocument->WriteRecovery())
        m_SceneDocument->SetStatus("Scene recovery snapshot updated.");
}

void EditorWorkspaceLayer::RestoreSceneRecovery()
{
    if (!m_SceneDocument->RecoveryAvailable() || !m_SceneDocument->Asset())
        return;
    m_SceneDocument->RestoreRecovery();
    if (m_SceneDocument->UndoContext())
        m_SceneDocument->UndoContext()->Clear();
    m_SceneDocument->SetStatus("Recovered unsaved scene changes. Save to commit them to the project.");
}

void EditorWorkspaceLayer::DiscardSceneRecovery() noexcept { m_SceneDocument->DiscardRecovery(); }

void EditorWorkspaceLayer::ExecutePendingSceneAction()
{
    const auto action = std::exchange(m_PendingSceneAction, PendingSceneAction::None);
    const auto asset = std::exchange(m_PendingSceneAsset, Keire::AssetId{});
    m_Dialog = Dialog::None;
    if (action == PendingSceneAction::Exit && m_ShaderGraphDocument && m_ShaderGraphDocument->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Exit;
        OpenDialog(Dialog::DirtyShaderGraph);
        return;
    }
    if (action == PendingSceneAction::Exit && m_MaterialGraphDocument && m_MaterialGraphDocument->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Exit;
        OpenDialog(Dialog::DirtyMaterialGraph);
        return;
    }
    QueueSceneTransition(action, asset);
}

void EditorWorkspaceLayer::RequestEditorExit()
{
    if (m_SceneDocument->PlaySession())
    {
        m_PendingSceneAction = PendingSceneAction::Exit;
        RequestStopPlayMode();
        return;
    }
    m_PendingSceneAction = PendingSceneAction::Exit;
    if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
    {
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    if (m_ShaderGraphDocument && m_ShaderGraphDocument->Dirty())
    {
        OpenDialog(Dialog::DirtyShaderGraph);
        return;
    }
    if (m_MaterialGraphDocument && m_MaterialGraphDocument->Dirty())
    {
        OpenDialog(Dialog::DirtyMaterialGraph);
        return;
    }
    ExecutePendingSceneAction();
}

void EditorWorkspaceLayer::QueueSceneTransition(const PendingSceneAction action, const Keire::AssetId asset)
{
    if (action == PendingSceneAction::None || !m_SceneTransitions)
        return;
    KeireEditor::SceneTransitionKind kind = KeireEditor::SceneTransitionKind::Open;
    switch (action)
    {
    case PendingSceneAction::Create:
        kind = KeireEditor::SceneTransitionKind::Create;
        break;
    case PendingSceneAction::Open:
        kind = KeireEditor::SceneTransitionKind::Open;
        break;
    case PendingSceneAction::Close:
        kind = KeireEditor::SceneTransitionKind::Close;
        break;
    case PendingSceneAction::Exit:
        kind = KeireEditor::SceneTransitionKind::Exit;
        break;
    case PendingSceneAction::None:
        return;
    }
    if (!m_SceneTransitions->Request({kind, asset}))
    {
        m_Notice = "A scene transition is already pending; the additional request was ignored.";
        m_NoticeColor = m_Theme.Warning;
    }
}

void EditorWorkspaceLayer::ProcessSceneTransition()
{
    if (!m_SceneTransitions)
        return;
    const auto request = m_SceneTransitions->BeginCommit();
    if (!request)
        return;
    try
    {
        switch (request->Kind)
        {
        case KeireEditor::SceneTransitionKind::Create:
            CreateScene();
            break;
        case KeireEditor::SceneTransitionKind::Open:
            OpenScene(request->Asset);
            break;
        case KeireEditor::SceneTransitionKind::Close:
            CloseScene();
            break;
        case KeireEditor::SceneTransitionKind::Exit:
            CloseScene();
            Owner().RequestExit();
            break;
        }
        m_SceneTransitions->Complete();
    }
    catch (const std::exception& error)
    {
        const std::string diagnostic = std::string("Scene operation failed: ") + error.what();
        m_SceneTransitions->Fail(diagnostic);
        m_SceneDocument->SetStatus(diagnostic);
        ReportError("Scene", m_SceneDocument->Status());
    }
}

Keire::Ref<Keire::Scene> EditorWorkspaceLayer::ActiveScene() const noexcept
{
    return m_SceneDocument ? m_SceneDocument->ActiveScene() : Keire::Ref<Keire::Scene>{};
}

void EditorWorkspaceLayer::SelectSceneEntity(const Keire::AssetId entity, const bool additive)
{
    if (m_SceneDocument)
        m_SceneDocument->Select(entity, additive);
    m_SelectedAsset = {};
}

void EditorWorkspaceLayer::SetSceneSelection(const std::span<const Keire::EntityId> entities, const bool additive)
{
    if (!m_SceneDocument)
        return;
    std::vector<Keire::AssetId> selected = additive ? std::vector<Keire::AssetId>(m_SceneDocument->Selections().begin(),
                                                                                  m_SceneDocument->Selections().end())
                                                    : std::vector<Keire::AssetId>{};
    for (const auto entity : entities)
        if (std::ranges::find(selected, entity.Value()) == selected.end())
            selected.push_back(entity.Value());
    m_SceneDocument->SetSelections(selected);
    m_SelectedAsset = {};
}

void EditorWorkspaceLayer::BeginPlayMode()
{
    if (!m_SceneDocument->EditingScene() || m_SceneDocument->PlaySession() || m_PlayStartPending)
        return;
    const bool requiresManagedRuntime = ProjectRequiresManagedRuntime();
    const auto scripts = Owner().Scripts();
    const auto readiness = KeireEditor::EvaluatePlayModeReadiness(
        requiresManagedRuntime, scripts && scripts->RuntimeHostAvailable(),
        scripts ? scripts->BuildStatus().State : Keire::ManagedBuildState::Idle,
        scripts ? scripts->ReloadStatus().State : Keire::ManagedReloadState::Idle);
    if (readiness == KeireEditor::PlayModeReadiness::WaitingForManagedRuntime)
    {
        m_PlayStartPending = true;
        m_SceneDocument->SetStatus("Play queued while the gameplay script generation becomes ready.");
        AddConsoleMessage("Play Mode", "Waiting for the gameplay script build and reload before entering Play.",
                          m_Theme.Accent);
        return;
    }
    if (readiness == KeireEditor::PlayModeReadiness::ManagedRuntimeUnavailable)
    {
        const auto reload = scripts ? scripts->ReloadStatus() : Keire::ManagedReloadStatus{};
        const std::string reason = reload.Diagnostic.empty()
                                       ? "The gameplay script runtime is unavailable. Resolve the Managed Build "
                                         "diagnostics before entering Play."
                                       : reload.Diagnostic;
        ReportError("Play Mode", reason);
        return;
    }
    m_ManagedInputCaptureOverride.reset();
    m_GameplayInputContext.Reset();
    if (const auto input = Owner().Input(); input && m_EditorInputUser)
    {
        const auto project = Owner().GetProject();
        if (project && project->Descriptor().DefaultInput)
        {
            try
            {
                m_GameplayInputContext =
                    input->CreateActionContext(project->Descriptor().DefaultInput, m_EditorInputUser);
                if (!m_GameplayInputContext || !m_GameplayInputContext->EnableMap("Player"))
                    throw std::runtime_error("The default input action asset does not contain a Player action map.");
                m_ManagedInputCaptureOverride.emplace(m_GameplayInputContext->OverrideUiCapture("Player"));
            }
            catch (const std::exception& error)
            {
                m_ManagedInputCaptureOverride.reset();
                m_GameplayInputContext.Reset();
                ReportError("Play Mode Input", error.what());
                return;
            }
        }
    }
    m_PlayEditorTouchedEntities.clear();
    m_PlayChangeTracker = std::make_unique<KeireEditor::ScenePlayChangeTracker>();
    m_PendingPlayEditorBefore.reset();
    m_PlayChanges.reset();
    Keire::Ref<Keire::UndoContext> playUndo;
    if (const auto undo = Owner().Undo())
        playUndo = undo->CreateContext({.Name = "Play Mode"});
    const auto defaultMixer =
        m_ProjectSettingsDocument ? m_ProjectSettingsDocument->AuthoringSettings().DefaultMixer : Keire::AssetId{};
    m_SceneDocument->BeginPlay(std::move(playUndo), Owner().Assets(), Owner().Audio(), Owner().Physics(), defaultMixer);
    m_PlayFaultReported = false;
    m_ActiveUndoContext = m_SceneDocument->History();
    m_GameViewportInputActive = false;
    m_GameViewportCaptureSuspended = false;
    m_Game.RequestFocus();
}

bool EditorWorkspaceLayer::ProjectRequiresManagedRuntime() const noexcept
{
    if (!m_AssetDatabase)
        return false;
    const auto project = Owner().GetProject();
    if (!project)
        return false;
    for (const auto& record : m_AssetDatabase->Records())
    {
        if (record.Type != Keire::ManagedAssemblyAsset::StaticType())
            continue;
        try
        {
            const auto assembly =
                Keire::ManagedAssemblyAsset::Decode(ReadBytes(project->Root() / "Assets" / record.RelativePath));
            if (assembly->Definition().Classification != Keire::ManagedAssemblyClassification::Tests)
                return true;
        }
        catch (...)
        {
            // A transiently unreadable assembly still requires the managed build to produce a valid generation.
            return true;
        }
    }
    return false;
}

void EditorWorkspaceLayer::ContinuePendingPlayMode()
{
    if (!m_PlayStartPending || m_SceneDocument->PlaySession() || !m_SceneDocument->EditingScene())
        return;
    const auto scripts = Owner().Scripts();
    const auto readiness = KeireEditor::EvaluatePlayModeReadiness(
        true, scripts && scripts->RuntimeHostAvailable(),
        scripts ? scripts->BuildStatus().State : Keire::ManagedBuildState::Idle,
        scripts ? scripts->ReloadStatus().State : Keire::ManagedReloadState::Idle);
    if (readiness == KeireEditor::PlayModeReadiness::WaitingForManagedRuntime)
        return;
    m_PlayStartPending = false;
    if (readiness == KeireEditor::PlayModeReadiness::ManagedRuntimeUnavailable)
    {
        const auto reload = scripts ? scripts->ReloadStatus() : Keire::ManagedReloadStatus{};
        const std::string reason = reload.Diagnostic.empty()
                                       ? "The gameplay script build did not produce a runnable generation. Play was "
                                         "not started."
                                       : reload.Diagnostic;
        ReportError("Play Mode", reason);
        return;
    }
    m_SceneDocument->SetStatus("Gameplay scripts are ready. Entering Play.");
    BeginPlayMode();
}

void EditorWorkspaceLayer::RequestStopPlayMode()
{
    if (m_PlayStartPending)
    {
        m_PlayStartPending = false;
        m_SceneDocument->SetStatus("Queued Play request cancelled.");
        return;
    }
    if (!m_SceneDocument->PlaySession() || m_SceneDocument->PlaySession()->State() == Keire::ScenePlayState::Stopped ||
        m_PlayChanges || m_PendingPlayTransition != PendingPlayTransition::None)
        return;
    FinalizePendingPlayEditorMutation();
    m_PlayResumeState = m_SceneDocument->PlaySession()->State();
    if (m_PlayResumeState == Keire::ScenePlayState::Playing)
        m_SceneDocument->PlaySession()->Pause(true);
    try
    {
        m_PlayChanges = std::make_unique<KeireEditor::ScenePlayChangeSet>(
            m_SceneDocument->EditingScene(), m_SceneDocument->PlaySession()->RuntimeScene(), *m_PlayChangeTracker);
        if (m_PlayChanges->Empty())
        {
            m_PendingPlayTransition = PendingPlayTransition::Discard;
            return;
        }
        m_PlayChangesPanel->Open();
    }
    catch (const std::exception& error)
    {
        ReportError("Play Mode", std::string("Could not review runtime changes: ") + error.what());
        if (m_PlayResumeState == Keire::ScenePlayState::Playing)
            m_SceneDocument->PlaySession()->Pause(false);
        m_PlayChanges.reset();
    }
}

void EditorWorkspaceLayer::FinishPlayMode(const bool apply)
{
    if (!m_SceneDocument->PlaySession())
        return;
    try
    {
        std::optional<Keire::SceneDefinition> applied;
        if (apply && m_PlayChanges && m_PlayChanges->HasSelectedChanges())
            applied = m_PlayChanges->BuildAppliedDefinition();
        m_SceneDocument->EndPlay();
        m_ManagedInputCaptureOverride.reset();
        m_GameplayInputContext.Reset();
        m_ManagedCursorLocked = false;
        m_ManagedCursorVisible = true;
        m_GameViewportCaptureSuspended = false;
        ApplyManagedCursorMode();
        if (applied)
        {
            RecordSceneUndo("Apply Play Mode Changes");
            const auto components = m_SceneDocument->EditingScene()->Components();
            auto scene = Keire::CreateRef<Keire::Scene>(m_SceneDocument->Asset(), std::move(*applied), components);
            scene->MarkDirty();
            m_SceneDocument->ReplaceEditingScene(std::move(scene));
            if (m_SceneDocument->Selection() &&
                !m_SceneDocument->EditingScene()->FindEntity(Keire::EntityId(m_SceneDocument->Selection())))
                m_SceneDocument->ClearSelection();
        }
    }
    catch (const std::exception& error)
    {
        ReportError("Play Mode", std::string("Could not apply runtime changes: ") + error.what());
        if (m_SceneDocument->PlaySession() && m_PlayResumeState == Keire::ScenePlayState::Playing)
            m_SceneDocument->PlaySession()->Pause(false);
        if (m_PlayChanges)
            m_PlayChangesPanel->Open();
        return;
    }
    m_PlayChangesPanel->Close();
    m_PlayChanges.reset();
    m_PlayChangeTracker.reset();
    m_PendingPlayEditorBefore.reset();
    m_PlayEditorTouchedEntities.clear();
    m_PlayResumeState = Keire::ScenePlayState::Stopped;
    m_PlayFaultReported = false;
    m_ActiveUndoContext = m_SceneDocument->UndoContext();
    m_SceneViewportPanel->Registration().RequestFocus();
    if (m_PendingSceneAction != PendingSceneAction::None)
    {
        if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
            OpenDialog(Dialog::DirtyScene);
        else
            ExecutePendingSceneAction();
    }
}

void EditorWorkspaceLayer::DrawPlayChanges(Keire::UiFrame& ui)
{
    if (!m_PlayChanges)
        return;
    switch (m_PlayChangesPanel->Draw(ui, *m_PlayChanges))
    {
    case KeireEditor::ScenePlayDecision::Apply:
        m_PendingPlayTransition = PendingPlayTransition::Apply;
        break;
    case KeireEditor::ScenePlayDecision::Discard:
        m_PendingPlayTransition = PendingPlayTransition::Discard;
        break;
    case KeireEditor::ScenePlayDecision::Cancel:
        if (m_SceneDocument->PlaySession() && m_PlayResumeState == Keire::ScenePlayState::Playing)
            m_SceneDocument->PlaySession()->Pause(false);
        m_PlayChanges.reset();
        m_PlayResumeState = Keire::ScenePlayState::Stopped;
        m_PendingSceneAction = PendingSceneAction::None;
        m_PendingSceneAsset = {};
        m_Game.RequestFocus();
        break;
    case KeireEditor::ScenePlayDecision::None:
        break;
    }
}

void EditorWorkspaceLayer::RecordSceneUndo(const std::string_view name, std::string mergeKey)
{
    const auto scene = ActiveScene();
    const auto context = m_SceneDocument->History();
    if (!scene || !context || !context->IsOpen())
        return;
    if (m_SceneDocument->PlaySession())
    {
        FinalizePendingPlayEditorMutation();
        m_PendingPlayEditorBefore = scene->Snapshot();
        for (const auto selected : m_SceneDocument->Selections())
            m_PlayEditorTouchedEntities.insert(selected);
    }
    auto before = scene->Snapshot();
    auto after = std::make_shared<std::optional<Keire::SceneDefinition>>();
    const auto asset = m_SceneDocument->Asset();
    const bool play = static_cast<bool>(m_SceneDocument->PlaySession());
    const auto apply = [this, asset, play](const Keire::SceneDefinition& definition)
    {
        auto target = play ? ActiveScene() : m_SceneDocument->EditingScene();
        if (!target || m_SceneDocument->Asset() != asset)
            return;
        const auto components = target->Components();
        if (play && m_SceneDocument->PlaySession())
        {
            m_SceneDocument->PlaySession()->ReplaceRuntime(definition);
            return;
        }
        auto replacementScene = Keire::CreateRef<Keire::Scene>(asset, definition, components);
        replacementScene->MarkDirty();
        m_SceneDocument->ReplaceEditingScene(std::move(replacementScene), false);
    };
    const auto estimatedBytes = Keire::SceneAsset::Encode(before).size();
    Keire::UndoOperation redo = [after, apply]
    {
        if (after->has_value())
            apply(**after);
    };
    Keire::UndoOperation undo = [this, before = std::move(before), after, apply]() mutable
    {
        if (!after->has_value() && ActiveScene())
            *after = ActiveScene()->Snapshot();
        apply(before);
    };
    Keire::UndoAvailability available = [this, asset] { return ActiveScene() && m_SceneDocument->Asset() == asset; };
    if (mergeKey.empty())
    {
        context->RecordApplied(Keire::CreateUndoCommand(std::string(name), std::move(redo), std::move(undo),
                                                        estimatedBytes, std::move(available)));
    }
    else
    {
        context->RecordApplied(std::make_unique<ContinuousUndoCommand>(std::string(name), std::move(mergeKey),
                                                                       std::move(redo), std::move(undo), estimatedBytes,
                                                                       std::move(available)));
    }
}

void EditorWorkspaceLayer::FinalizePendingPlayEditorMutation()
{
    if (!m_PendingPlayEditorBefore || !m_PlayChangeTracker || !m_SceneDocument->PlaySession())
        return;
    if (const auto scene = ActiveScene())
        m_PlayChangeTracker->RecordMutation(*m_PendingPlayEditorBefore, scene->Snapshot());
    m_PendingPlayEditorBefore.reset();
}

void EditorWorkspaceLayer::MarkPlayEditorEntity(const Keire::AssetId entity)
{
    if (m_SceneDocument->PlaySession() && entity)
        m_PlayEditorTouchedEntities.insert(entity);
}

void EditorWorkspaceLayer::UndoSceneEdit()
{
    const auto context = m_SceneDocument->History();
    if (context)
    {
        const auto before =
            m_SceneDocument->PlaySession() && ActiveScene() ? std::optional(ActiveScene()->Snapshot()) : std::nullopt;
        if (context->Undo() && before && m_PlayChangeTracker && ActiveScene())
            m_PlayChangeTracker->RecordMutation(*before, ActiveScene()->Snapshot());
    }
}

void EditorWorkspaceLayer::RedoSceneEdit()
{
    const auto context = m_SceneDocument->History();
    if (context)
    {
        const auto before =
            m_SceneDocument->PlaySession() && ActiveScene() ? std::optional(ActiveScene()->Snapshot()) : std::nullopt;
        if (context->Redo() && before && m_PlayChangeTracker && ActiveScene())
            m_PlayChangeTracker->RecordMutation(*before, ActiveScene()->Snapshot());
    }
}

void EditorWorkspaceLayer::ApplyActiveUndo(const bool redo)
{
    if (!m_ActiveUndoContext)
        return;
    try
    {
        FinalizePendingPlayEditorMutation();
        const auto before =
            m_SceneDocument->PlaySession() && ActiveScene() ? std::optional(ActiveScene()->Snapshot()) : std::nullopt;
        if (redo)
            (void)m_ActiveUndoContext->Redo();
        else
            (void)m_ActiveUndoContext->Undo();
        if (before && m_PlayChangeTracker && ActiveScene())
            m_PlayChangeTracker->RecordMutation(*before, ActiveScene()->Snapshot());
    }
    catch (const std::exception& error)
    {
        m_Notice = std::string(redo ? "Redo failed: " : "Undo failed: ") + error.what();
        m_NoticeColor = m_Theme.Error;
        ReportError("Undo", m_Notice);
    }
}

KeireEditor::SceneDocument& EditorWorkspaceLayer::SceneViewportDocument() noexcept { return *m_SceneDocument; }

const Keire::UiThemeDefinition& EditorWorkspaceLayer::SceneViewportTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::SceneViewportAssetDatabase() const noexcept
{
    return m_AssetDatabase;
}

Keire::Ref<Keire::AssetSystem> EditorWorkspaceLayer::SceneViewportAssetSystem() const noexcept
{
    return Owner().Assets();
}

Keire::Ref<Keire::RenderSystem> EditorWorkspaceLayer::SceneViewportRenderer() const noexcept
{
    return Owner().Renderer();
}

const Keire::RenderEnvironmentSettings& EditorWorkspaceLayer::SceneViewportSettings() const noexcept
{
    return m_ProjectSettingsDocument->Settings();
}

Keire::Ref<Keire::WindowSystem> EditorWorkspaceLayer::SceneViewportWindows() const noexcept
{
    return Owner().Windows();
}

Keire::WindowId EditorWorkspaceLayer::SceneViewportWindow() const noexcept { return Owner().MainWindow()->Id(); }

float EditorWorkspaceLayer::SceneViewportDisplayScale() const noexcept { return Owner().MainWindow()->DisplayScale(); }

const Keire::Time& EditorWorkspaceLayer::SceneViewportTime() const noexcept { return Owner().GetTime(); }

bool EditorWorkspaceLayer::SceneViewportPlayReviewActive() const noexcept { return m_PlayChanges != nullptr; }

void EditorWorkspaceLayer::ActivateSceneViewportHistory() noexcept { m_ActiveUndoContext = m_SceneDocument->History(); }

void EditorWorkspaceLayer::RestoreSceneViewportRecovery() { RestoreSceneRecovery(); }

void EditorWorkspaceLayer::DiscardSceneViewportRecovery() noexcept { DiscardSceneRecovery(); }

void EditorWorkspaceLayer::ReportSceneViewportError(std::string message) noexcept
{
    ReportError("Scene", std::move(message));
}

void EditorWorkspaceLayer::SetSceneViewportSelectedAsset(const Keire::AssetId asset) noexcept
{
    m_SelectedAsset = asset;
}

void EditorWorkspaceLayer::RequestSceneViewportNewScene() { RequestCreateScene(); }

void EditorWorkspaceLayer::RevealSceneViewportScenes()
{
    if (m_AssetBrowserPanel)
    {
        m_AssetBrowserPanel->Registration().SetVisible(true);
        m_AssetBrowserPanel->Registration().RequestFocus();
    }
    m_AssetStatus = "Choose or drop a .keirescene asset from the Project panel.";
}

void EditorWorkspaceLayer::RouteSceneViewportAsset(const Keire::AssetTypeId type, const Keire::AssetId asset,
                                                   const Keire::EntityId target)
{
    m_ViewportAssetDropRouter->Route(type, asset, target, *this);
}

void EditorWorkspaceLayer::RecordSceneViewportUndo(const std::string_view name) { RecordSceneUndo(name); }

void EditorWorkspaceLayer::SelectSceneViewportEntity(const Keire::AssetId entity, const bool additive)
{
    SelectSceneEntity(entity, additive);
}

void EditorWorkspaceLayer::SetSceneViewportSelection(const std::span<const Keire::EntityId> entities,
                                                     const bool additive)
{
    SetSceneSelection(entities, additive);
}

void EditorWorkspaceLayer::OpenDroppedScene(const Keire::AssetId asset)
{
    RequestOpenScene(asset);
    const auto record = m_AssetDatabase ? m_AssetDatabase->Find(asset) : std::nullopt;
    m_SceneDocument->SetStatus(record ? "Opening " + record->RelativePath.generic_string() + "."
                                      : "Opening dropped scene.");
}

void EditorWorkspaceLayer::OpenDroppedInputActions(const Keire::AssetId asset)
{
    OpenInputActions(asset);
    m_InputActionsPanel->Registration().SetVisible(true);
}

void EditorWorkspaceLayer::InstantiateDroppedPrefab(const Keire::AssetId asset)
{
    const auto scene = ActiveScene();
    if (!scene || !m_AssetDatabase || !Owner().GetProject())
        throw std::runtime_error("Open a scene before dropping a prefab.");
    const auto projectRoot = Owner().GetProject()->Root();
    const auto composed = Keire::ComposePrefab(asset,
                                               [&](const Keire::AssetId prefab)
                                               {
                                                   const auto record = m_AssetDatabase->Find(prefab);
                                                   if (!record || record->Type != Keire::PrefabAsset::StaticType())
                                                       return Keire::Ref<Keire::PrefabAsset>{};
                                                   return Keire::PrefabAsset::Decode(
                                                       ReadBytes(projectRoot / "Assets" / record->RelativePath));
                                               });
    RecordSceneUndo("Instantiate Prefab");
    auto replacement = scene->Snapshot();
    const auto instance = KeireEditor::InstantiatePrefab(replacement, asset, composed);
    auto rebuilt = Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
    rebuilt->MarkDirty();
    m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
    m_SceneDocument->Select(instance.Root);
    m_SceneDocument->SetStatus("Instantiated prefab in the active scene.");
}

void EditorWorkspaceLayer::CreateDroppedMeshEntity(const Keire::AssetId asset)
{
    const auto scene = ActiveScene();
    if (!scene)
        throw std::runtime_error("Open a scene before dropping a mesh.");
    const auto record = m_AssetDatabase ? m_AssetDatabase->Find(asset) : std::nullopt;
    if (!record)
        throw std::runtime_error("The dropped mesh no longer exists in the project database.");

    RecordSceneUndo("Create Mesh Entity");
    const auto entity = m_SceneDocument->CreateEntity(record->RelativePath.stem().string(), {},
                                                      Keire::MeshRendererComponent::StaticType());
    m_SceneDocument->SetComponentProperty(entity, Keire::MeshRendererComponent::StaticType(), "mesh", asset);
    m_SceneDocument->Select(entity.Value());
    if (m_SceneDocument->PlaySession())
        m_PlayEditorTouchedEntities.insert(entity.Value());
    m_SelectedAsset = {};
    m_SceneDocument->SetStatus("Created " + scene->FindEntity(entity).Name() + " from " +
                               record->RelativePath.filename().string() + ".");
}

void EditorWorkspaceLayer::AssignDroppedMaterial(const Keire::EntityId entity, const Keire::AssetId asset)
{
    const auto scene = ActiveScene();
    if (!scene)
        throw std::runtime_error("Open a scene before dropping a material.");
    const auto destination = scene->FindEntity(entity);
    const auto destinations = KeireEditor::ResolveMaterialDropTargets(destination);
    if (destinations.empty())
        throw std::runtime_error("Drop a material over a rendered entity or a model root with rendered children.");
    const auto record = m_AssetDatabase ? m_AssetDatabase->Find(asset) : std::nullopt;
    if (!record)
        throw std::runtime_error("The dropped material no longer exists in the project database.");

    auto runtimeMaterial = asset;
    if (record->Type == Keire::MaterialGraphAsset::StaticType() ||
        record->Type == Keire::MaterialInstanceAsset::StaticType() ||
        record->Type == Keire::ShaderGraphInstanceAsset::StaticType())
    {
        const auto assets = Owner().Assets();
        const auto generated =
            assets ? std::ranges::find_if(record->SubAssets,
                                          [&assets](const Keire::AssetId subAsset)
                                          {
                                              const auto type = assets->TryGetType(subAsset);
                                              return type && *type == Keire::MaterialAsset::StaticType();
                                          })
                   : record->SubAssets.end();
        if (generated == record->SubAssets.end())
        {
            if (!m_AssetOperations)
                throw std::runtime_error(
                    "The material source runtime material is not available in the mounted catalog.");
            m_AssetOperations->QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction, {.ReloadAsset = asset});
            m_PendingMaterialAssignment = PendingMaterialAssignment{entity, asset};
            m_SceneDocument->SetStatus("Compiling " + record->RelativePath.stem().string() + " before assigning it...");
            return;
        }
        runtimeMaterial = *generated;
    }
    else if (record->Type != Keire::MaterialAsset::StaticType())
        throw std::runtime_error(
            "Only Materials, Material Graphs, and Material Instances can be assigned to a Mesh Renderer.");

    std::optional<Keire::Color> materialTint;
    if (record->Type == Keire::MaterialAsset::StaticType())
    {
        const auto source = m_AssetDatabase->Specification().ProjectRoot /
                            m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
        const auto material = Keire::MaterialAsset::DecodeSource(ReadBytes(source));
        if (const auto tint = material.Properties.find("Tint"); tint != material.Properties.end())
            if (const auto* color = std::get_if<Keire::Color>(&tint->second))
                materialTint = *color;
    }
    else if (record->Type == Keire::MaterialGraphAsset::StaticType())
    {
        const auto source = m_AssetDatabase->Specification().ProjectRoot /
                            m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
        const auto graph = Keire::MaterialGraphAsset::DecodeSource(ReadBytes(source));
        const auto tint =
            std::ranges::find(graph.Properties, std::string_view("Tint"), &Keire::MaterialGraphPropertyBinding::Name);
        if (tint != graph.Properties.end())
            if (const auto* color = std::get_if<Keire::Color>(&tint->Value))
                materialTint = *color;
    }
    RecordSceneUndo("Assign Material");
    for (const auto target : destinations)
    {
        m_SceneDocument->SetMeshRendererMaterial(target, 0, runtimeMaterial);
        if (materialTint)
            m_SceneDocument->SetComponentProperty(target, Keire::MeshRendererComponent::StaticType(), "tint",
                                                  *materialTint);
    }
    if (record->Type == Keire::ShaderGraphAsset::StaticType() && m_ShaderGraphDocument->Asset() == asset)
        m_ShaderGraphDocument->ApplyLiveRevision();
    m_SceneDocument->Select(destination.Id().Value());
    m_SelectedAsset = {};
    m_SceneDocument->SetStatus(
        "Assigned " + record->RelativePath.stem().string() + " to " + destination.Name() +
        (destinations.size() == 1 ? "." : " (" + std::to_string(destinations.size()) + " rendered children)."));
}

void EditorWorkspaceLayer::CompletePendingMaterialAssignment(const Keire::AssetId refreshedAsset)
{
    if (!m_PendingMaterialAssignment)
        return;
    const auto pending = *m_PendingMaterialAssignment;
    const auto record = m_AssetDatabase ? m_AssetDatabase->Find(pending.Source) : std::nullopt;
    const auto assets = Owner().Assets();
    const bool available = record && assets &&
                           std::ranges::any_of(record->SubAssets,
                                               [&assets](const Keire::AssetId id)
                                               {
                                                   const auto type = assets->TryGetType(id);
                                                   return type && *type == Keire::MaterialAsset::StaticType();
                                               });
    if (!available)
    {
        if (refreshedAsset != pending.Source)
            return;
        m_PendingMaterialAssignment.reset();
        m_SceneDocument->SetStatus(
            "Material source compilation did not publish a runtime material; the previous material was kept.");
        return;
    }
    m_PendingMaterialAssignment.reset();
    AssignDroppedMaterial(pending.Entity, pending.Source);
}

void EditorWorkspaceLayer::DrawGame(Keire::UiFrame& ui)
{
    m_GameViewportRect = {};
    if (auto gamePanel = ui.BeginPanel(m_Game); gamePanel)
    {
        constexpr std::array aspectLabels{std::string_view("Free"), std::string_view("16:9"), std::string_view("16:10"),
                                          std::string_view("4:3")};
        if (auto aspect = ui.BeginCombo("Preview", aspectLabels[static_cast<std::size_t>(m_GameAspect)]); aspect)
        {
            for (std::size_t index = 0; index < aspectLabels.size(); ++index)
                if (ui.Selectable(aspectLabels[index], m_GameAspect == static_cast<int>(index)))
                    m_GameAspect = static_cast<int>(index);
        }
        const auto scene = RenderedScene(m_SceneDocument->EditingScene(), m_SceneDocument->PlaySession());
        if (!scene)
        {
            SetGameViewportInputActive(false);
            DrawEmptyState(ui, "GAME", "No scene is loaded.", "Open a scene to preview its active primary camera.");
            return;
        }
        if (!m_GameRenderView)
        {
            SetGameViewportInputActive(false);
            DrawEmptyState(ui, "GAME", "The renderer is disabled.",
                           "Enable rendered or headless rendering in the application specification.");
            return;
        }

        const auto selected = SelectGameCamera(scene);
        if (!selected)
        {
            SetGameViewportInputActive(false);
            DrawEmptyState(ui, "GAME", "No active primary camera.",
                           "Add an enabled Camera component and mark it Primary.");
            return;
        }

        ui.TextColored(m_Theme.MutedText,
                       "Camera: " + selected->Entity.Name() +
                           (m_SceneDocument->PlaySession() &&
                                    m_SceneDocument->PlaySession()->State() != Keire::ScenePlayState::Stopped
                                ? " (Play)"
                                : " (Edit)"));

        const auto available = ui.ContentAvailable();
        auto previewSize = available;
        constexpr std::array aspectRatios{0.0F, 16.0F / 9.0F, 16.0F / 10.0F, 4.0F / 3.0F};
        const float requestedAspect = aspectRatios[static_cast<std::size_t>(m_GameAspect)];
        if (requestedAspect > 0.0F)
        {
            previewSize.Width = std::min(available.Width, available.Height * requestedAspect);
            previewSize.Height = previewSize.Width / requestedAspect;
        }
        const auto size = PrepareRenderSurface(m_GameRenderView, previewSize, Owner().MainWindow()->DisplayScale());
        const float aspect = size.Width / std::max(size.Height, 1.0F);
        Keire::RenderCamera camera;
        camera.View = Keire::Math::Inverse(selected->Transform->WorldMatrix());
        camera.Projection = selected->Camera->ProjectionMatrix(aspect);
        camera.ClearColor = selected->Camera->ClearColor();
        camera.NearPlane = selected->Camera->NearPlane();
        camera.FarPlane = selected->Camera->FarPlane();
        m_GameRenderView->SetCamera(camera);
        auto environment = m_ProjectSettingsDocument->Settings();
        environment.SkyVisible =
            environment.SkyVisible && selected->Camera->ClearMode() == Keire::CameraClearMode::Skybox;
        Keire::SceneRenderRequest renderRequest{scene, m_GameRenderView, false, environment};
        const auto& materialTime = Owner().GetTime();
        renderRequest.MaterialTimeSeconds = static_cast<float>(materialTime.TimeSinceStartup().Seconds());
        renderRequest.MaterialDeltaSeconds = static_cast<float>(materialTime.DeltaTime().Seconds());
        renderRequest.FrameIndex = materialTime.FrameCount();
        if (const auto play = m_SceneDocument->PlaySession(); play && play->State() != Keire::ScenePlayState::Stopped)
            if (const auto vfx = play->Vfx())
                renderRequest.Vfx = vfx->CaptureRenderSnapshot();
        Owner().Renderer()->Submit(std::move(renderRequest));
        ui.Image(m_GameRenderView->Surface(), size);
        const auto imageState = ui.LastItemState();
        const auto imageRect = ui.LastItemRect();
        m_GameViewportRect = imageRect;

        Keire::Ref<Keire::ScenePresentationRuntime> presentation;
        const auto playSession = m_SceneDocument->PlaySession();
        const bool playActive = playSession && playSession->State() != Keire::ScenePlayState::Stopped;
        if (playActive && m_GameViewportCaptureSuspended && imageState.Hovered && ui.PointerState().LeftPressed)
            m_GameViewportCaptureSuspended = false;
        const auto mainWindow = Owner().MainWindow();
        SetGameViewportInputActive(KeireEditor::GameViewportOwnsRuntimeInput(
            playActive, mainWindow && mainWindow->Focused(), ui.WindowFocused(), imageState.Hovered,
            m_GameViewportInputActive, m_ManagedCursorLocked, m_GameViewportCaptureSuspended));
        if (playActive)
        {
            playSession->SetPresentationViewport(size.Width, size.Height);
            presentation = playSession->Presentation();
        }
        else
        {
            if (!m_GameEditPresentation)
            {
                if (const auto assets = SceneViewportAssetSystem())
                {
                    m_GameEditPresentation =
                        Keire::CreateRef<Keire::ScenePresentationRuntime>(assets, Keire::Ref<Keire::AudioSystem>{});
                }
            }
            if (m_GameEditPresentation)
            {
                m_GameEditPresentation->Synchronize(scene, size.Width, size.Height, false);
                presentation = m_GameEditPresentation;
            }
        }

        if (presentation)
        {
            presentation->Draw(ui, imageRect.Minimum.X, imageRect.Minimum.Y);
            if (playActive)
            {
                const auto pointer = ui.PointerState();
                const float localX = pointer.Position.X - imageRect.Minimum.X;
                const float localY = pointer.Position.Y - imageRect.Minimum.Y;
                presentation->PointerMove(localX, localY);
                if (imageRect.Contains(pointer.Position))
                {
                    if (pointer.LeftPressed)
                        presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Primary, true);
                    if (pointer.RightPressed)
                        presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Secondary, true);
                    if (pointer.MiddlePressed)
                        presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Middle, true);
                }
                if (pointer.LeftReleased)
                    presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Primary, false);
                if (pointer.RightReleased)
                    presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Secondary, false);
                if (pointer.MiddleReleased)
                    presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Middle, false);
            }
        }
        if (playActive)
            DrawPerformanceOverlay(ui, imageRect, "GAME");
        return;
    }
    SetGameViewportInputActive(false);
}

void EditorWorkspaceLayer::DrawPerformanceOverlay(Keire::UiFrame& ui, const Keire::UiItemRect viewport,
                                                  const std::string_view label)
{
    if (!m_ShowPerformanceOverlay || viewport.Size().Width < 220.0F || viewport.Size().Height < 80.0F)
        return;
    const auto profiler = Owner().GetProfiler();
    if (!profiler || !profiler->IsOpen())
        return;
    const auto frame = profiler->LatestSummary();
    if (frame.Sequence == 0 || frame.DurationMicroseconds <= 0.0)
        return;

    const auto decimal = [](const double value)
    {
        const auto scaled = static_cast<std::int64_t>(std::lround(std::max(0.0, value) * 10.0));
        return std::to_string(scaled / 10) + "." + std::to_string(scaled % 10);
    };
    const double frameMilliseconds = frame.DurationMicroseconds / 1000.0;
    const auto framesPerSecond = static_cast<std::uint32_t>(std::lround(1'000'000.0 / frame.DurationMicroseconds));
    const auto performanceColor =
        frameMilliseconds <= 16.7 ? m_Theme.Success : (frameMilliseconds <= 33.3 ? m_Theme.Warning : m_Theme.Error);
    const Keire::UiItemRect overlay{{viewport.Minimum.X + 12.0F, viewport.Minimum.Y + 12.0F},
                                    {viewport.Minimum.X + 292.0F, viewport.Minimum.Y + 102.0F}};
    ui.DrawFilledRectangle(overlay, {0.018F, 0.024F, 0.035F, 0.92F}, 7.0F);
    ui.DrawRectangle(overlay, {performanceColor.Red, performanceColor.Green, performanceColor.Blue, 0.72F}, 1.0F, 7.0F);
    ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 9.0F}, m_Theme.MutedText,
                       "PERFORMANCE / " + std::string(label), 11.0F, overlay);
    ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 29.0F}, performanceColor,
                       std::to_string(framesPerSecond) + " FPS   " + decimal(frameMilliseconds) + " ms", 16.0F,
                       overlay);
    ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 54.0F}, m_Theme.Text,
                       "Scripts " + decimal(frame.ScriptingMicroseconds / 1000.0) + " ms   " +
                           std::to_string(frame.SpanCount) + " spans",
                       11.0F, overlay);
    if (const auto renderer = Owner().Renderer())
    {
        const auto statistics = renderer->Statistics();
        ui.DrawOverlayText({overlay.Minimum.X + 12.0F, overlay.Minimum.Y + 72.0F}, m_Theme.MutedText,
                           std::to_string(statistics.DrawCalls) + " draws   " + std::to_string(statistics.Triangles) +
                               " triangles",
                           11.0F, overlay);
    }
}

Keire::Ref<Keire::Scene> EditorWorkspaceLayer::ActiveHierarchyScene() const noexcept { return ActiveScene(); }

KeireEditor::SceneDocument& EditorWorkspaceLayer::HierarchyDocument() noexcept { return *m_SceneDocument; }

void EditorWorkspaceLayer::ReportHierarchyError(std::string message) noexcept
{
    ReportError("Hierarchy", std::move(message));
}

void EditorWorkspaceLayer::UnpackHierarchyPrefab(const Keire::AssetId entity, const bool completely)
{
    if (m_SceneDocument->PlaySession())
        throw std::runtime_error("Exit Play mode before unpacking a prefab.");
    const auto scene = m_SceneDocument->EditingScene();
    if (!scene)
        throw std::runtime_error("Open a scene before unpacking a prefab.");
    auto replacement = scene->Snapshot();
    const auto instance = std::ranges::find_if(replacement.PrefabInstances,
                                               [&](const Keire::PrefabInstanceDefinition& candidate)
                                               {
                                                   return std::ranges::any_of(
                                                       candidate.Objects, [&](const Keire::PrefabObjectMapping& mapping)
                                                       { return mapping.Instance == entity; });
                                               });
    if (instance == replacement.PrefabInstances.end())
        throw std::invalid_argument("The selected GameObject is not part of a prefab instance.");
    const auto root = instance->Root;
    RecordSceneUndo(completely ? "Unpack Prefab Completely" : "Unpack Prefab");
    if (!KeireEditor::UnpackPrefab(replacement, root, completely))
        throw std::runtime_error("The prefab instance changed before it could be unpacked.");
    auto rebuilt = Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
    rebuilt->MarkDirty();
    m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
    m_SceneDocument->Select(root);
    m_SceneDocument->SetStatus(completely ? "Prefab instance hierarchy unpacked." : "Prefab instance unpacked.");
}

Keire::UiColor EditorWorkspaceLayer::HierarchyAccent() const noexcept { return m_Theme.Accent; }

void EditorWorkspaceLayer::ActivateHierarchyHistory() noexcept { m_ActiveUndoContext = m_SceneDocument->History(); }

void EditorWorkspaceLayer::DeleteHierarchySelection()
{
    (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::DeleteSelection);
}

void EditorWorkspaceLayer::RecordHierarchyUndo() { RecordSceneUndo(); }

void EditorWorkspaceLayer::MarkHierarchyEntity(const Keire::AssetId entity) { MarkPlayEditorEntity(entity); }

void EditorWorkspaceLayer::RequestHierarchyRename(const Keire::AssetId entity, std::string name)
{
    m_SceneDocument->Select(entity);
    m_ProfileName = std::move(name);
    OpenDialog(Dialog::RenameEntity);
}
