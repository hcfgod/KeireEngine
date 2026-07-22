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
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ScenePlayChanges.h"
#include "KeireClient/Editor/ScenePlayChangesPanel.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"
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

    void WriteBytesAtomically(const std::filesystem::path& path, const std::span<const std::byte> bytes)
    {
        const std::string text =
            bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::Detail::WriteTextFileAtomically(path, text);
    }

    [[nodiscard]] Keire::Ref<Keire::Scene> RenderedScene(const Keire::Ref<Keire::Scene>& editing,
                                                         const Keire::Ref<Keire::SceneRuntimeSession>& play)
    {
        if (play && play->State() != Keire::ScenePlayState::Stopped)
            return play->RuntimeScene();
        return editing;
    }

    struct SceneCamera final
    {
        Keire::Entity Entity;
        Keire::Ref<Keire::CameraComponent> Camera;
        Keire::Ref<Keire::TransformComponent> Transform;
    };

    [[nodiscard]] std::optional<SceneCamera> SelectGameCamera(const Keire::Ref<Keire::Scene>& scene)
    {
        if (!scene)
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

void EditorWorkspaceLayer::RequestCreateScene()
{
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
    CreateScene();
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
    auto scene = Keire::CreateRef<Keire::Scene>(asset, definition);
    scene->MarkSaved();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext({.Name = "Scene: " + record->RelativePath.stem().string()});
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
    if (asset == m_SceneDocument->Asset())
        return;
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
    OpenScene(asset);
}

void EditorWorkspaceLayer::SaveScene()
{
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
    CloseScene();
}

void EditorWorkspaceLayer::CloseScene()
{
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
    if (action == PendingSceneAction::Exit)
    {
        CloseScene();
        Owner().RequestExit();
        return;
    }
    CloseScene();
    try
    {
        if (action == PendingSceneAction::Create)
            CreateScene();
        else if (action == PendingSceneAction::Open)
            OpenScene(asset);
    }
    catch (const std::exception& error)
    {
        m_SceneDocument->SetStatus(std::string("Scene operation failed: ") + error.what());
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
    if (!m_SceneDocument->EditingScene() || m_SceneDocument->PlaySession())
        return;
    m_PlayEditorTouchedEntities.clear();
    m_PlayChangeTracker = std::make_unique<KeireEditor::ScenePlayChangeTracker>();
    m_PendingPlayEditorBefore.reset();
    m_PlayChanges.reset();
    Keire::Ref<Keire::UndoContext> playUndo;
    if (const auto undo = Owner().Undo())
        playUndo = undo->CreateContext({.Name = "Play Mode"});
    m_SceneDocument->BeginPlay(std::move(playUndo));
    m_PlayFaultReported = false;
    m_ActiveUndoContext = m_SceneDocument->History();
    m_Game.RequestFocus();
}

void EditorWorkspaceLayer::RequestStopPlayMode()
{
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
        auto scene = Keire::CreateRef<Keire::Scene>(asset, definition, components);
        scene->MarkDirty();
        m_SceneDocument->ReplaceEditingScene(std::move(scene), false);
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

void EditorWorkspaceLayer::CreateDroppedMeshEntity(const Keire::AssetId asset)
{
    const auto scene = ActiveScene();
    if (!scene)
        throw std::runtime_error("Open a scene before dropping a mesh.");
    const auto record = m_AssetDatabase ? m_AssetDatabase->Find(asset) : std::nullopt;
    if (!record)
        throw std::runtime_error("The dropped mesh no longer exists in the project database.");

    RecordSceneUndo("Create Mesh Entity");
    auto entity = scene->CreateEntity(record->RelativePath.stem().string());
    const auto renderer = entity.AddComponent<Keire::MeshRendererComponent>();
    renderer->SetMesh(asset);
    m_SceneDocument->Select(entity.Id().Value());
    if (m_SceneDocument->PlaySession())
        m_PlayEditorTouchedEntities.insert(entity.Id().Value());
    m_SelectedAsset = {};
    m_SceneDocument->SetStatus("Created " + entity.Name() + " from " + record->RelativePath.filename().string() + ".");
}

void EditorWorkspaceLayer::AssignDroppedMaterial(const Keire::EntityId entity, const Keire::AssetId asset)
{
    const auto scene = ActiveScene();
    if (!scene)
        throw std::runtime_error("Open a scene before dropping a material.");
    const auto destination = scene->FindEntity(entity);
    const auto renderer = destination.GetComponent<Keire::MeshRendererComponent>();
    if (!renderer)
        throw std::runtime_error("Drop a material directly over an entity with a Mesh Renderer.");
    const auto record = m_AssetDatabase ? m_AssetDatabase->Find(asset) : std::nullopt;
    if (!record)
        throw std::runtime_error("The dropped material no longer exists in the project database.");

    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    const auto material = Keire::MaterialAsset::DecodeSource(ReadBytes(source));
    std::optional<Keire::Color> materialTint;
    if (const auto tint = material.Properties.find("Tint"); tint != material.Properties.end())
    {
        if (const auto* color = std::get_if<Keire::Color>(&tint->second))
            materialTint = *color;
    }
    RecordSceneUndo("Assign Material");
    renderer->SetMaterial(asset);
    if (materialTint)
        renderer->SetTint(*materialTint);
    m_SceneDocument->Select(destination.Id().Value());
    m_SelectedAsset = {};
    m_SceneDocument->SetStatus("Assigned " + record->RelativePath.stem().string() + " to " + destination.Name() + ".");
}

void EditorWorkspaceLayer::DrawGame(Keire::UiFrame& ui)
{
    if (auto gamePanel = ui.BeginPanel(m_Game); gamePanel)
    {
        ui.TextColored(m_Theme.Accent, "GAME");
        ui.Separator();
        const auto scene = RenderedScene(m_SceneDocument->EditingScene(), m_SceneDocument->PlaySession());
        if (!scene)
        {
            DrawEmptyState(ui, "GAME", "No scene is loaded.", "Open a scene to preview its active primary camera.");
            return;
        }
        if (!m_GameRenderView)
        {
            DrawEmptyState(ui, "GAME", "The renderer is disabled.",
                           "Enable rendered or headless rendering in the application specification.");
            return;
        }

        const auto selected = SelectGameCamera(scene);
        if (!selected)
        {
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
        const auto size = PrepareRenderSurface(m_GameRenderView, available, Owner().MainWindow()->DisplayScale());
        const float aspect = size.Width / std::max(size.Height, 1.0F);
        Keire::RenderCamera camera;
        camera.View = Keire::Math::Inverse(selected->Transform->WorldMatrix());
        camera.Projection = selected->Camera->ProjectionMatrix(aspect);
        camera.ClearColor = selected->Camera->ClearColor();
        camera.NearPlane = selected->Camera->NearPlane();
        camera.FarPlane = selected->Camera->FarPlane();
        m_GameRenderView->SetCamera(camera);
        Owner().Renderer()->Submit({scene, m_GameRenderView, false, m_ProjectSettingsDocument->Settings()});
        ui.Image(m_GameRenderView->Surface(), size);
    }
}

Keire::Ref<Keire::Scene> EditorWorkspaceLayer::ActiveHierarchyScene() const noexcept { return ActiveScene(); }

KeireEditor::SceneDocument& EditorWorkspaceLayer::HierarchyDocument() noexcept { return *m_SceneDocument; }

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
