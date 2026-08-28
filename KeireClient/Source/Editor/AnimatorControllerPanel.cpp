#include "KeireClient/Editor/AnimatorControllerPanel.h"

#include "KeireClientInternal/Editor/AnimatorControllerPanelModelInternal.h"
#include "KeireClientInternal/Editor/AnimatorControllerPreviewInternal.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    using AnimatorControllerPanelInternal::AnimatorCanvasId;
    using AnimatorControllerPanelInternal::ComparisonNames;
    using AnimatorControllerPanelInternal::DisplayPosition;
    using AnimatorControllerPanelInternal::DrawEnumCombo;
    using AnimatorControllerPanelInternal::EditAssetReference;
    using AnimatorControllerPanelInternal::FindLayer;
    using AnimatorControllerPanelInternal::FindParameter;
    using AnimatorControllerPanelInternal::FindState;
    using AnimatorControllerPanelInternal::FindTransition;
    using AnimatorControllerPanelInternal::LayerModeNames;
    using AnimatorControllerPanelInternal::MotionTypeNames;
    using AnimatorControllerPanelInternal::ParameterTypeNames;
    using AnimatorControllerPanelInternal::RemoveParameterReferences;
    using AnimatorControllerPanelInternal::RemoveStateReferences;
    using AnimatorControllerPanelInternal::RepairEntryStates;
    using AnimatorControllerPanelInternal::TimelineFraction;
    using AnimatorControllerPanelInternal::UniqueName;

    AnimatorControllerPanel::AnimatorControllerPanel(IAnimatorControllerPanelController& controller) noexcept
        : m_Controller(controller), m_Preview(std::make_unique<PreviewState>())
    {
    }

    AnimatorControllerPanel::~AnimatorControllerPanel() { m_Preview->Stop(); }

    void AnimatorControllerPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.animator-controller", "Animator Controller", false});
    }

    void AnimatorControllerPanel::ResetTransientState() noexcept
    {
        m_Preview->Stop();
        m_GraphCanvas.CancelInteractions();
        m_GraphCanvas.Select(std::nullopt);
        m_GraphCanvas.SelectConnection(std::nullopt);
        m_GraphContext.reset();
        m_SelectedTransition.clear();
        m_GraphLayer.clear();
        m_GraphSubgraph.clear();
        m_Message.clear();
        m_FocusGraph = true;
    }

    void AnimatorControllerPanel::Draw(Keire::UiFrame& ui)
    {
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
        {
            m_Preview->Stop();
            return;
        }

        auto& document = m_Controller.AnimatorControllerState();
        const auto& theme = m_Controller.AnimatorControllerTheme();
        const auto database = m_Controller.AnimatorControllerDatabase();
        const auto assets = m_Controller.AnimatorControllerAssets();
        auto& sceneDocument = m_Controller.AnimatorControllerSceneDocument();
        if (ui.WindowFocused())
            m_Controller.ActivateAnimatorControllerHistory();
        if (!document.Asset())
        {
            ui.TextColored(theme.Accent, "ANIMATOR CONTROLLER");
            ui.Separator();
            ui.Text("No Animator Controller is open.");
            ui.TextColored(theme.MutedText, "Create or double-click a .keireanimgraph asset in the Project panel.");
            return;
        }

        ui.TextColored(theme.Accent, "ANIMATOR CONTROLLER");
        ui.SameLine();
        ui.Text(document.SourcePath().filename().string() + (document.Dirty() ? " *" : ""));
        ui.Separator();
        if (ui.Button("Save"))
        {
            try
            {
                m_Controller.SaveAnimatorControllerDocument();
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAnimatorControllerError(m_Message);
            }
        }
        ui.SameLine();
        if (ui.Button("Revert"))
        {
            try
            {
                m_Preview->Invalidate();
                m_Controller.ReloadAnimatorControllerDocument(document.Asset());
                return;
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAnimatorControllerError(m_Message);
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.UndoContext() || !document.UndoContext()->CanUndo()); disabled)
        {
            if (ui.Button("Undo"))
            {
                m_Preview->Invalidate();
                m_Controller.UndoAnimatorControllerEdit();
                return;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.UndoContext() || !document.UndoContext()->CanRedo()); disabled)
        {
            if (ui.Button("Redo"))
            {
                m_Preview->Invalidate();
                m_Controller.RedoAnimatorControllerEdit();
                return;
            }
        }
        ui.SameLine();
        if (ui.Button("Validate"))
        {
            try
            {
                Keire::ValidateAnimationGraph(document.Definition());
                m_Message = "Controller validation passed.";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAnimatorControllerError(m_Message);
            }
        }
        if (!m_Message.empty())
            ui.TextColored(theme.MutedText, m_Message);
        ui.Separator();

        const bool playMode =
            sceneDocument.PlaySession() && sceneDocument.PlaySession()->State() != Keire::ScenePlayState::Stopped;
        if (playMode)
            m_Preview->Stop();
        else if (m_Preview->Active)
        {
            try
            {
                m_Preview->Synchronize(sceneDocument, document, assets);
            }
            catch (const std::exception& error)
            {
                m_Preview->Playing = false;
                m_Preview->Diagnostic = error.what();
            }
        }

        const auto playbackScene = sceneDocument.ActiveScene();
        const auto playbackEntity = playbackScene && sceneDocument.Selection()
                                        ? playbackScene->FindEntity(Keire::EntityId(sceneDocument.Selection()))
                                        : Keire::Entity{};
        const auto playbackAnimator = playbackEntity ? playbackEntity.GetComponent<Keire::AnimatorComponent>()
                                                     : Keire::Ref<Keire::AnimatorComponent>{};
        const bool controllerMatches = playbackAnimator && playbackAnimator->Graph() == document.Asset();
        const auto playbackSnapshot = controllerMatches ? playbackAnimator->RuntimeDebugSnapshot()
                                                        : std::shared_ptr<const Keire::AnimatorDebugSnapshot>{};
        std::string playbackDiagnostic;
        if (!playbackAnimator)
            playbackDiagnostic = "Select an entity with an Animator component.";
        else if (!controllerMatches)
            playbackDiagnostic = "The selected Animator uses a different controller.";
        else if (!playbackAnimator->RuntimeDiagnostic().empty())
            playbackDiagnostic = playbackAnimator->RuntimeDiagnostic();
        else if (!playMode)
            playbackDiagnostic = m_Preview->Diagnostic;

        ui.TextColored(theme.Accent, playMode ? "LIVE PLAYBACK" : "ANIMATION PREVIEW SCENE");
        if (playbackEntity)
            ui.TextColored(theme.MutedText,
                           std::string(playMode ? "Live target: " : "Preview target: ") + playbackEntity.Name());
        if (!playMode)
        {
            const bool canPreview = controllerMatches && playbackAnimator->SkinnedMesh() && assets;
            if (auto disabled = ui.BeginDisabled(!canPreview); disabled)
            {
                if (ui.Button(m_Preview->Playing ? "Pause" : "Preview"))
                {
                    if (!m_Preview->Active)
                        m_Preview->Restart();
                    else
                        m_Preview->Playing = !m_Preview->Playing;
                    m_Preview->LastTick = std::chrono::steady_clock::now();
                }
                ui.SameLine();
                if (ui.Button("Restart"))
                    m_Preview->Restart();
                ui.SameLine();
                if (ui.Button("Stop"))
                    m_Preview->Stop();
            }
        }

        const Keire::AnimatorLayerDebugState* primaryPlayback = nullptr;
        if (playbackSnapshot && !playbackSnapshot->Layers.empty())
            primaryPlayback = &playbackSnapshot->Layers.front();
        const float progress =
            primaryPlayback ? TimelineFraction(primaryPlayback->NormalizedTime) : m_Preview->NormalizedTime;
        const std::string stateName =
            primaryPlayback && !primaryPlayback->State.empty() ? primaryPlayback->State : "Waiting for first sample";
        const auto progressLabel =
            stateName + "  " + std::to_string(static_cast<int>(std::clamp(progress, 0.0F, 1.0F) * 100.0F)) + "%";
        ui.ProgressBar(progress, {0.0F, 18.0F}, progressLabel);
        if (primaryPlayback && primaryPlayback->InTransition)
        {
            const auto& definition = document.Definition();
            std::string sourceName = primaryPlayback->SourceStateId;
            std::string destinationName = primaryPlayback->DestinationStateId;
            const auto layer =
                std::ranges::find(definition.Layers, primaryPlayback->Id, &Keire::AnimationLayerDefinition::Id);
            if (layer != definition.Layers.end())
            {
                const auto source = std::ranges::find(layer->States, primaryPlayback->SourceStateId,
                                                      &Keire::AnimationStateDefinition::Id);
                if (source != layer->States.end())
                    sourceName = source->Name;
                const auto destination = std::ranges::find(layer->States, primaryPlayback->DestinationStateId,
                                                           &Keire::AnimationStateDefinition::Id);
                if (destination != layer->States.end())
                    destinationName = destination->Name;
            }
            ui.TextColored(theme.Accent,
                           "Transition: " + sourceName + " -> " + destinationName + "  " +
                               std::to_string(static_cast<int>(
                                   std::clamp(primaryPlayback->TransitionProgress, 0.0F, 1.0F) * 100.0F)) +
                               "%");
        }
        if (!playMode && m_Preview->Active)
        {
            float timeline = progress;
            if (ui.SliderFloat("Timeline", timeline, 0.0F, 1.0F))
                m_Preview->Seek(timeline);
        }
        if (!playbackDiagnostic.empty())
            ui.TextColored(theme.MutedText, playbackDiagnostic);
        if (playbackSnapshot)
        {
            if (auto profiler = ui.BeginTreeNode("State-machine profiler", false); profiler)
            {
                const auto& profile = playbackSnapshot->Profile;
                ui.Text(std::to_string(static_cast<std::uint64_t>(profile.LastEvaluationMicroseconds)) +
                        " us last  |  " +
                        std::to_string(static_cast<std::uint64_t>(profile.AverageEvaluationMicroseconds)) +
                        " us average  |  " +
                        std::to_string(static_cast<std::uint64_t>(profile.PeakEvaluationMicroseconds)) + " us peak");
                ui.Text(std::to_string(profile.LayersEvaluated) + " layers  |  " +
                        std::to_string(profile.TransitionsTested) + " transitions tested  |  " +
                        std::to_string(profile.MotionsEvaluated) + " motions  |  " +
                        std::to_string(profile.ClipsSampled) + " clips");
                ui.TextColored(theme.MutedText, std::to_string(profile.UpdateCount) + " sampled state-machine updates");
            }
            if (auto pose = ui.BeginTreeNode(
                    "Pose debugger (" + std::to_string(playbackSnapshot->Pose.size()) + " bones)", false);
                pose)
            {
                for (const auto& bone : playbackSnapshot->Pose)
                {
                    ui.Text(bone.Name + "  [" + std::to_string(bone.WorldPosition.X) + ", " +
                            std::to_string(bone.WorldPosition.Y) + ", " + std::to_string(bone.WorldPosition.Z) + "]");
                }
            }
            if (auto trajectory = ui.BeginTreeNode(
                    "Motion trajectory (" + std::to_string(playbackSnapshot->MotionTrajectory.size()) + " samples)",
                    false);
                trajectory)
            {
                float pathLength = 0.0F;
                for (std::size_t index = 1; index < playbackSnapshot->MotionTrajectory.size(); ++index)
                {
                    const auto& previous = playbackSnapshot->MotionTrajectory[index - 1].Position;
                    const auto& current = playbackSnapshot->MotionTrajectory[index].Position;
                    const auto x = current.X - previous.X;
                    const auto y = current.Y - previous.Y;
                    const auto z = current.Z - previous.Z;
                    pathLength += std::sqrt(x * x + y * y + z * z);
                }
                ui.Text("Travelled " + std::to_string(pathLength) + " model units");
                const auto first =
                    playbackSnapshot->MotionTrajectory.size() > 16 ? playbackSnapshot->MotionTrajectory.size() - 16 : 0;
                for (std::size_t index = first; index < playbackSnapshot->MotionTrajectory.size(); ++index)
                {
                    const auto& point = playbackSnapshot->MotionTrajectory[index];
                    ui.Text(std::to_string(point.Time) + "s  [" + std::to_string(point.Position.X) + ", " +
                            std::to_string(point.Position.Y) + ", " + std::to_string(point.Position.Z) + "]");
                }
            }
        }
        ui.Separator();

        auto graph = document.Definition();
        auto before = graph;
        std::string selectedParameter(document.SelectedParameter());
        std::string selectedLayer(document.SelectedLayer());
        std::string selectedState(document.SelectedState());
        if (auto* layer = FindLayer(graph, selectedLayer); layer && !selectedState.empty())
            if (auto* state = FindState(*layer, selectedState); state)
                m_GraphSubgraph = state->SubgraphId;
        std::string editName;
        bool changed = false;
        const auto markChanged = [&](const std::string_view name)
        {
            changed = true;
            if (editName.empty())
                editName = name;
        };

        const auto available = ui.ContentAvailable();
        const float leftWidth = 224.0F;
        const float inspectorWidth = 318.0F;
        const float graphWidth = std::max(340.0F, available.Width - leftWidth - inspectorWidth - 18.0F);
        if (auto navigation = ui.BeginChild("AnimatorNavigation", {leftWidth, 0.0F}, true); navigation)
        {
            ui.TextColored(theme.Accent, "PARAMETERS");
            std::string removeParameter;
            for (const auto& parameter : graph.ParameterDefinitions)
            {
                auto id = ui.PushId(parameter.Id);
                if (ui.Selectable(parameter.Name, selectedParameter == parameter.Id))
                {
                    selectedParameter = parameter.Id;
                    selectedLayer.clear();
                    selectedState.clear();
                    m_SelectedTransition.clear();
                }
            }
            if (ui.Button("+ Parameter"))
            {
                Keire::AnimationParameterDefinition parameter;
                parameter.Id = Keire::AssetId::Generate().ToString();
                parameter.Name =
                    UniqueName(graph.ParameterDefinitions, "New Parameter", &Keire::AnimationParameterDefinition::Name);
                selectedParameter = parameter.Id;
                selectedLayer.clear();
                selectedState.clear();
                graph.ParameterDefinitions.push_back(std::move(parameter));
                markChanged("Add Animator Parameter");
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(selectedParameter.empty()); disabled)
            {
                if (ui.Button("Delete##Parameter"))
                    removeParameter = selectedParameter;
            }
            if (!removeParameter.empty())
            {
                RemoveParameterReferences(graph, removeParameter);
                std::erase_if(graph.ParameterDefinitions,
                              [&](const auto& parameter) { return parameter.Id == removeParameter; });
                selectedParameter.clear();
                markChanged("Delete Animator Parameter");
            }

            ui.Separator();
            ui.TextColored(theme.Accent, "LAYERS");
            std::string removeLayer;
            for (const auto& layer : graph.Layers)
            {
                auto id = ui.PushId(layer.Id);
                if (ui.Selectable(layer.Name, selectedLayer == layer.Id && selectedState.empty()))
                {
                    selectedParameter.clear();
                    selectedLayer = layer.Id;
                    selectedState.clear();
                    m_GraphSubgraph.clear();
                    m_SelectedTransition.clear();
                }
                if (selectedLayer == layer.Id)
                {
                    auto rootId = ui.PushId("root-state-machine");
                    if (ui.Selectable("   Root State Machine", m_GraphSubgraph.empty() && selectedState.empty()))
                    {
                        selectedParameter.clear();
                        selectedState.clear();
                        m_GraphSubgraph.clear();
                        m_SelectedTransition.clear();
                        m_FocusGraph = true;
                    }
                    for (const auto& subgraph : layer.Subgraphs)
                    {
                        auto subgraphId = ui.PushId(subgraph.Id);
                        if (ui.Selectable("   " + subgraph.Name,
                                          m_GraphSubgraph == subgraph.Id && selectedState.empty()))
                        {
                            selectedParameter.clear();
                            selectedState.clear();
                            m_GraphSubgraph = subgraph.Id;
                            m_SelectedTransition.clear();
                            m_FocusGraph = true;
                        }
                    }
                    for (const auto& state : layer.States)
                    {
                        if (state.SubgraphId != m_GraphSubgraph)
                            continue;
                        auto stateId = ui.PushId(state.Id);
                        if (ui.Selectable("      " + state.Name, selectedState == state.Id))
                        {
                            selectedParameter.clear();
                            selectedLayer = layer.Id;
                            selectedState = state.Id;
                            m_GraphSubgraph = state.SubgraphId;
                            m_SelectedTransition.clear();
                        }
                    }
                }
            }
            auto* selectedLayerDefinition = FindLayer(graph, selectedLayer);
            if (auto disabled = ui.BeginDisabled(!selectedLayerDefinition); disabled)
            {
                if (ui.Button("+ Subgraph"))
                {
                    Keire::AnimationStateMachineSubgraphDefinition subgraph;
                    subgraph.Id = Keire::AssetId::Generate().ToString();
                    subgraph.Name = UniqueName(selectedLayerDefinition->Subgraphs, "New Subgraph",
                                               &Keire::AnimationStateMachineSubgraphDefinition::Name);
                    m_GraphSubgraph = subgraph.Id;
                    selectedState.clear();
                    selectedParameter.clear();
                    selectedLayerDefinition->Subgraphs.push_back(std::move(subgraph));
                    m_FocusGraph = true;
                    markChanged("Add Animator State-Machine Subgraph");
                }
                ui.SameLine();
                if (auto removeDisabled = ui.BeginDisabled(m_GraphSubgraph.empty()); removeDisabled)
                {
                    if (ui.Button("Delete##Subgraph"))
                    {
                        for (auto& state : selectedLayerDefinition->States)
                            if (state.SubgraphId == m_GraphSubgraph)
                                state.SubgraphId.clear();
                        std::erase_if(selectedLayerDefinition->Subgraphs,
                                      [&](const auto& subgraph) { return subgraph.Id == m_GraphSubgraph; });
                        m_GraphSubgraph.clear();
                        selectedState.clear();
                        RepairEntryStates(*selectedLayerDefinition);
                        m_FocusGraph = true;
                        markChanged("Delete Animator State-Machine Subgraph");
                    }
                }
            }
            if (ui.Button("+ Layer"))
            {
                Keire::AnimationLayerDefinition layer;
                layer.Id = Keire::AssetId::Generate().ToString();
                layer.Name = UniqueName(graph.Layers, "New Layer", &Keire::AnimationLayerDefinition::Name);
                selectedParameter.clear();
                selectedLayer = layer.Id;
                selectedState.clear();
                m_GraphSubgraph.clear();
                graph.Layers.push_back(std::move(layer));
                markChanged("Add Animator Layer");
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(selectedLayer.empty()); disabled)
            {
                if (ui.Button("Delete##Layer"))
                    removeLayer = selectedLayer;
            }
            if (!removeLayer.empty())
            {
                std::erase_if(graph.Layers, [&](const auto& layer) { return layer.Id == removeLayer; });
                selectedLayer.clear();
                selectedState.clear();
                m_GraphSubgraph.clear();
                m_SelectedTransition.clear();
                markChanged("Delete Animator Layer");
            }
        }
        ui.SameLine();

        if (auto stateMachine = ui.BeginChild("AnimatorStateMachine", {graphWidth, 0.0F}, true); stateMachine)
        {
            ui.TextColored(theme.Accent, "STATE MACHINE");
            ui.SameLine();
            auto* layer = FindLayer(graph, selectedLayer);
            if (!layer && !graph.Layers.empty())
            {
                layer = &graph.Layers.front();
                selectedLayer = layer->Id;
            }
            Keire::AnimationStateMachineSubgraphDefinition* subgraph = nullptr;
            if (layer && !m_GraphSubgraph.empty())
            {
                const auto found = std::ranges::find(layer->Subgraphs, m_GraphSubgraph,
                                                     &Keire::AnimationStateMachineSubgraphDefinition::Id);
                if (found != layer->Subgraphs.end())
                    subgraph = std::addressof(*found);
                else
                    m_GraphSubgraph.clear();
            }
            std::vector<Keire::AnimationStateDefinition*> visibleStates;
            if (layer)
            {
                for (auto& state : layer->States)
                    if (state.SubgraphId == m_GraphSubgraph)
                        visibleStates.push_back(std::addressof(state));
            }
            std::string* entryStateId =
                layer ? (subgraph ? std::addressof(subgraph->EntryStateId) : std::addressof(layer->EntryStateId))
                      : nullptr;
            if (layer)
            {
                ui.TextColored(theme.MutedText, layer->Name + " / " + (subgraph ? subgraph->Name : "Root"));
                ui.SameLine();
                if (ui.Button("Auto Layout"))
                {
                    for (std::size_t index = 0; index < visibleStates.size(); ++index)
                    {
                        const auto row = index / 3U;
                        visibleStates[index]->EditorPosition = {static_cast<float>(index % 3U) * 190.0F,
                                                                static_cast<float>(row) * 104.0F};
                    }
                    m_FocusGraph = true;
                    markChanged("Auto Layout Animator States");
                }
            }
            ui.Separator();

            std::vector<NodeGraphNode> graphNodes;
            std::vector<NodeGraphConnection> graphConnections;
            const Keire::AnimatorLayerDebugState* layerPlayback = nullptr;
            if (layer)
            {
                if (playbackSnapshot)
                {
                    const auto found =
                        std::ranges::find(playbackSnapshot->Layers, layer->Id, &Keire::AnimatorLayerDebugState::Id);
                    if (found != playbackSnapshot->Layers.end())
                        layerPlayback = &*found;
                }

                graphNodes.reserve(visibleStates.size());
                for (std::size_t index = 0; index < visibleStates.size(); ++index)
                {
                    const auto& state = *visibleStates[index];
                    const bool entry = entryStateId && state.Id == *entryStateId;
                    const bool active = layerPlayback && state.Id == layerPlayback->StateId;
                    std::string subtitle;
                    if (active)
                    {
                        subtitle =
                            "PLAYING  " +
                            std::to_string(static_cast<int>(TimelineFraction(layerPlayback->NormalizedTime) * 100.0F)) +
                            "%";
                    }
                    else if (entry)
                    {
                        subtitle = "ENTRY  |  ";
                        subtitle += MotionTypeNames[static_cast<std::size_t>(state.Motion.Type)];
                    }
                    else
                    {
                        subtitle = MotionTypeNames[static_cast<std::size_t>(state.Motion.Type)];
                        subtitle += "  |  ";
                        subtitle += std::to_string(state.Transitions.size());
                        subtitle += state.Transitions.size() == 1 ? " TRANSITION" : " TRANSITIONS";
                    }

                    NodeGraphNode node{
                        .Id = AnimatorCanvasId(state.Id, 0x414e494d4e4f4445ULL),
                        .Label = state.Name,
                        .Position = DisplayPosition(state, index),
                        .Size = {214.0F, 86.0F},
                        .Color = active  ? Keire::UiColor{0.08F, 0.48F, 0.33F, 1.0F}
                                 : entry ? Keire::UiColor{0.10F, 0.40F, 0.30F, 1.0F}
                                         : Keire::UiColor{0.10F, 0.32F, 0.52F, 1.0F},
                        .Subtitle = std::move(subtitle),
                    };
                    node.Pins.push_back({.Id = AnimatorCanvasId(state.Id, 0x414e494d494e5054ULL),
                                         .Label = "Enter",
                                         .Direction = NodeGraphPinDirection::Input,
                                         .Type = 1,
                                         .Color = {0.24F, 0.78F, 1.0F, 1.0F}});
                    node.Pins.push_back({.Id = AnimatorCanvasId(state.Id, 0x414e494d4f555450ULL),
                                         .Label = "Transition",
                                         .Direction = NodeGraphPinDirection::Output,
                                         .Type = 1,
                                         .Color = {0.24F, 0.78F, 1.0F, 1.0F}});
                    graphNodes.push_back(std::move(node));
                }
                for (const auto* source : visibleStates)
                {
                    for (const auto& transition : source->Transitions)
                    {
                        const auto destination = std::ranges::find(visibleStates, transition.DestinationId,
                                                                   [](const auto* state) { return state->Id; });
                        if (destination == visibleStates.end())
                            continue;
                        std::string label = std::to_string(transition.Conditions.size());
                        label += transition.Conditions.size() == 1 ? " condition" : " conditions";
                        if (transition.HasExitTime)
                            label += "  |  exit";
                        graphConnections.push_back(
                            {.Id = AnimatorCanvasId(transition.Id, 0x414e494d4c494e4bULL),
                             .Source = AnimatorCanvasId(source->Id, 0x414e494d4e4f4445ULL),
                             .Target = AnimatorCanvasId((*destination)->Id, 0x414e494d4e4f4445ULL),
                             .Label = std::move(label),
                             .SourcePin = AnimatorCanvasId(source->Id, 0x414e494d4f555450ULL),
                             .TargetPin = AnimatorCanvasId((*destination)->Id, 0x414e494d494e5054ULL)});
                    }
                }
            }

            const auto findStateByCanvasId = [&](const StableNodeId id) -> Keire::AnimationStateDefinition*
            {
                if (!layer)
                    return nullptr;
                const auto found =
                    std::ranges::find_if(visibleStates, [&](const auto* state)
                                         { return AnimatorCanvasId(state->Id, 0x414e494d4e4f4445ULL) == id; });
                return found == visibleStates.end() ? nullptr : *found;
            };
            const auto findTransitionByCanvasId = [&](const StableNodeId id)
                -> std::optional<std::pair<Keire::AnimationStateDefinition*, Keire::AnimationTransition*>>
            {
                if (!layer)
                    return std::nullopt;
                for (auto* state : visibleStates)
                {
                    const auto transition =
                        std::ranges::find_if(state->Transitions, [&](const auto& candidate)
                                             { return AnimatorCanvasId(candidate.Id, 0x414e494d4c494e4bULL) == id; });
                    if (transition != state->Transitions.end())
                        return std::pair{state, std::addressof(*transition)};
                }
                return std::nullopt;
            };
            const auto removeState = [&](const std::string_view stateId)
            {
                if (!layer)
                    return;
                RemoveStateReferences(*layer, stateId);
                std::erase_if(layer->States, [&](const auto& candidate) { return candidate.Id == stateId; });
                RepairEntryStates(*layer);
                selectedState.clear();
                m_SelectedTransition.clear();
                m_GraphCanvas.Select(std::nullopt);
                m_GraphCanvas.SelectConnection(std::nullopt);
                markChanged("Delete Animator State");
            };

            m_GraphCanvas.Select(selectedState.empty()
                                     ? std::nullopt
                                     : std::optional{AnimatorCanvasId(selectedState, 0x414e494d4e4f4445ULL)});
            m_GraphCanvas.SelectConnection(
                m_SelectedTransition.empty()
                    ? std::nullopt
                    : std::optional{AnimatorCanvasId(m_SelectedTransition, 0x414e494d4c494e4bULL)});
            if (m_GraphLayer != selectedLayer)
            {
                m_GraphLayer = selectedLayer;
                m_FocusGraph = true;
            }
            if (m_FocusGraph)
            {
                m_GraphCanvas.Focus(graphNodes, ui.ContentAvailable());
                m_FocusGraph = false;
            }
            NodeGraphCanvasOptions graphOptions{
                .Editable = layer != nullptr,
                .InteractiveConnections = layer != nullptr,
                .ValidateConnection =
                    [&](const NodeGraphConnectionRequest& request)
                {
                    const auto* source = findStateByCanvasId(request.SourceNode);
                    const auto* destination = findStateByCanvasId(request.TargetNode);
                    if (!source || !destination)
                    {
                        return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                             "A transition endpoint is unavailable."};
                    }
                    if (source == destination)
                    {
                        return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                             "Self-transitions are not supported."};
                    }
                    if (std::ranges::any_of(source->Transitions, [&](const auto& transition)
                                            { return transition.DestinationId == destination->Id; }))
                    {
                        return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::AcceptWithWarning,
                                                             "Adds another transition between these states."};
                    }
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Accept, {}};
                },
            };
            const auto graphResult =
                m_GraphCanvas.Draw(ui, "AnimatorStateGraphCanvas", graphNodes, graphConnections, graphOptions);
            const auto canvas = ui.LastItemRect();
            if (!layer)
            {
                ui.DrawOverlayText({canvas.Minimum.X + 24.0F, canvas.Minimum.Y + 24.0F}, theme.MutedText,
                                   "Create a layer, then drag animation clips here.");
            }
            if (graphResult.ActivatedNode)
            {
                if (const auto* state = findStateByCanvasId(*graphResult.ActivatedNode); state)
                {
                    selectedParameter.clear();
                    selectedState = state->Id;
                    m_SelectedTransition.clear();
                }
            }
            if (graphResult.ActivatedConnection)
            {
                if (const auto transition = findTransitionByCanvasId(*graphResult.ActivatedConnection); transition)
                {
                    selectedParameter.clear();
                    selectedState = transition->first->Id;
                    m_SelectedTransition = transition->second->Id;
                }
            }
            if (graphResult.BackgroundActivated)
            {
                selectedState.clear();
                m_SelectedTransition.clear();
            }
            if (graphResult.MoveCompletedNode)
            {
                auto state = findStateByCanvasId(*graphResult.MoveCompletedNode);
                const auto node = std::ranges::find(graphNodes, *graphResult.MoveCompletedNode, &NodeGraphNode::Id);
                if (state && node != graphNodes.end() && state->EditorPosition != node->Position)
                {
                    state->EditorPosition = node->Position;
                    markChanged("Move Animator State");
                }
            }
            if (graphResult.ConnectionRequested)
            {
                auto* source = findStateByCanvasId(graphResult.ConnectionRequested->SourceNode);
                auto* destination = findStateByCanvasId(graphResult.ConnectionRequested->TargetNode);
                if (source && destination && source != destination)
                {
                    Keire::AnimationTransition transition;
                    transition.Id = Keire::AssetId::Generate().ToString();
                    transition.DestinationId = destination->Id;
                    transition.Destination = destination->Name;
                    m_SelectedTransition = transition.Id;
                    selectedParameter.clear();
                    selectedState = source->Id;
                    source->Transitions.push_back(std::move(transition));
                    markChanged("Connect Animator States");
                }
            }
            if (graphResult.DeleteConnectionRequested)
            {
                if (const auto transition = findTransitionByCanvasId(*graphResult.DeleteConnectionRequested);
                    transition)
                {
                    const auto transitionId = transition->second->Id;
                    std::erase_if(transition->first->Transitions,
                                  [&](const auto& candidate) { return candidate.Id == transitionId; });
                    m_SelectedTransition.clear();
                    markChanged("Delete Animator Transition");
                }
            }
            if (!graphResult.DeleteNodesRequested.empty())
            {
                std::vector<std::string> statesToDelete;
                statesToDelete.reserve(graphResult.DeleteNodesRequested.size());
                for (const auto node : graphResult.DeleteNodesRequested)
                    if (const auto* state = findStateByCanvasId(node))
                        statesToDelete.push_back(state->Id);

                for (const auto& stateId : statesToDelete)
                    removeState(stateId);
            }
            else if (graphResult.DeleteNodeRequested)
            {
                if (const auto* state = findStateByCanvasId(*graphResult.DeleteNodeRequested); state)
                    removeState(state->Id);
            }
            if (graphResult.ContextRequested)
            {
                m_GraphContext = graphResult.ContextRequested;
                ui.OpenPopup("AnimatorGraphContext");
            }

            if (auto popup = ui.BeginPopup("AnimatorGraphContext"); popup)
            {
                if (!m_GraphContext)
                {
                    ui.TextColored(theme.MutedText, "The graph item is no longer available.");
                }
                else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Background)
                {
                    ui.TextColored(theme.Accent, "STATE MACHINE");
                    ui.TextColored(theme.MutedText, "Drop animation clips to create states.");
                    ui.Separator();
                    if (ui.MenuItem("Frame All States"))
                        m_GraphCanvas.Focus(graphNodes, canvas.Size());
                }
                else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Node)
                {
                    if (auto* state = findStateByCanvasId(m_GraphContext->Node); state)
                    {
                        ui.TextColored(theme.Accent, state->Name);
                        ui.TextColored(theme.MutedText, MotionTypeNames[static_cast<std::size_t>(state->Motion.Type)]);
                        ui.Separator();
                        if (ui.MenuItem("Set As Entry", false, entryStateId && state->Id != *entryStateId))
                        {
                            *entryStateId = state->Id;
                            markChanged("Set Animator Entry State");
                        }
                        if (ui.MenuItem("Remove All Transitions", false, !state->Transitions.empty()))
                        {
                            state->Transitions.clear();
                            m_SelectedTransition.clear();
                            markChanged("Remove Animator State Transitions");
                        }
                        ui.Separator();
                        if (ui.MenuItem("Delete State"))
                            removeState(state->Id);
                    }
                }
                else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Connection)
                {
                    if (const auto transition = findTransitionByCanvasId(m_GraphContext->Connection); transition)
                    {
                        ui.TextColored(theme.Accent, "TRANSITION");
                        ui.TextColored(theme.MutedText,
                                       transition->first->Name + "  ->  " + transition->second->Destination);
                        ui.Separator();
                        if (ui.MenuItem("Delete Transition"))
                        {
                            const auto transitionId = transition->second->Id;
                            std::erase_if(transition->first->Transitions,
                                          [&](const auto& candidate) { return candidate.Id == transitionId; });
                            m_SelectedTransition.clear();
                            markChanged("Delete Animator Transition");
                        }
                    }
                }
                else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Pin)
                {
                    if (auto* state = findStateByCanvasId(m_GraphContext->Node); state)
                    {
                        const bool output = m_GraphContext->Pin == AnimatorCanvasId(state->Id, 0x414e494d4f555450ULL);
                        ui.TextColored(theme.Accent, output ? "TRANSITION OUTPUT" : "STATE INPUT");
                        ui.TextColored(theme.MutedText, state->Name);
                        ui.Separator();
                        if (output && ui.MenuItem("Unlink Outgoing", false, !state->Transitions.empty()))
                        {
                            state->Transitions.clear();
                            m_SelectedTransition.clear();
                            markChanged("Unlink Animator State Output");
                        }
                        if (!output)
                        {
                            const bool connected = std::ranges::any_of(
                                layer->States,
                                [&](const auto& source)
                                {
                                    return std::ranges::any_of(source.Transitions, [&](const auto& transition)
                                                               { return transition.DestinationId == state->Id; });
                                });
                            if (ui.MenuItem("Unlink Incoming", false, connected))
                            {
                                RemoveStateReferences(*layer, state->Id);
                                m_SelectedTransition.clear();
                                markChanged("Unlink Animator State Input");
                            }
                        }
                    }
                }
            }

            if (auto target = ui.BeginDragTarget(canvas, "AnimatorControllerClipDrop"); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
                {
                    try
                    {
                        const auto droppedAssets = AssetBrowserPanel::DecodeDragPayload(payload);
                        struct ClipCandidate final
                        {
                            Keire::AssetId Id;
                            std::string Name;
                        };
                        std::vector<ClipCandidate> clips;
                        const auto appendClip = [&clips](const Keire::AssetId id, std::string name)
                        {
                            if (!id || std::ranges::any_of(clips, [id](const auto& value) { return value.Id == id; }))
                                return;
                            clips.push_back({id, std::move(name)});
                        };
                        for (const auto dropped : droppedAssets)
                        {
                            const auto record = database ? database->Find(dropped) : std::nullopt;
                            if (record && record->Type == Keire::AnimationClipAsset::StaticType())
                            {
                                appendClip(dropped, record->RelativePath.stem().string());
                                continue;
                            }
                            if (!record)
                            {
                                if (assets && assets->TryGetType(dropped) == Keire::AnimationClipAsset::StaticType())
                                    appendClip(dropped, "Animation");
                                continue;
                            }
                            if (record->Type != Keire::AnimationSourceAsset::StaticType() &&
                                record->Type != Keire::MeshAsset::StaticType())
                            {
                                continue;
                            }
                            std::size_t generatedClip = 0;
                            for (const auto subAsset : record->SubAssets)
                            {
                                if (!assets || assets->TryGetType(subAsset) != Keire::AnimationClipAsset::StaticType())
                                {
                                    continue;
                                }
                                ++generatedClip;
                                auto name = record->RelativePath.stem().string();
                                if (generatedClip > 1)
                                    name += " " + std::to_string(generatedClip);
                                appendClip(subAsset, std::move(name));
                            }
                        }
                        if (clips.empty())
                        {
                            throw std::invalid_argument(
                                "Drop an Animation Clip, Animation Source, or animated model asset.");
                        }
                        if (!layer)
                        {
                            Keire::AnimationLayerDefinition base;
                            base.Id = Keire::AssetId::Generate().ToString();
                            base.Name = "Base Layer";
                            selectedLayer = base.Id;
                            graph.Layers.push_back(std::move(base));
                            layer = &graph.Layers.back();
                            m_GraphSubgraph.clear();
                            entryStateId = std::addressof(layer->EntryStateId);
                        }
                        std::size_t added = 0;
                        for (const auto& clip : clips)
                        {
                            Keire::AnimationStateDefinition state;
                            state.Id = Keire::AssetId::Generate().ToString();
                            state.Name = UniqueName(layer->States, clip.Name, &Keire::AnimationStateDefinition::Name);
                            state.Clip = clip.Id;
                            state.Motion.Clip = clip.Id;
                            state.SubgraphId = m_GraphSubgraph;
                            state.EditorPosition = {
                                graphResult.PointerGraphPosition.X + static_cast<float>(added) * 36.0F,
                                graphResult.PointerGraphPosition.Y + static_cast<float>(added) * 28.0F};
                            selectedState = state.Id;
                            if (entryStateId && entryStateId->empty())
                                *entryStateId = state.Id;
                            layer->States.push_back(std::move(state));
                            ++added;
                        }
                        selectedParameter.clear();
                        m_Message = "Added " + std::to_string(added) + " animation state(s).";
                        markChanged("Add Animator States");
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                        m_Controller.ReportAnimatorControllerError(m_Message);
                    }
                }
            }
        }
        ui.SameLine();

        if (auto inspector = ui.BeginChild("AnimatorInspector", {inspectorWidth, 0.0F}, true); inspector)
        {
            ui.TextColored(theme.Accent, "INSPECTOR");
            ui.Separator();
            if (auto* parameter = FindParameter(graph, selectedParameter))
            {
                if (ui.InputText("Name", parameter->Name))
                    markChanged("Rename Animator Parameter");
                auto type = static_cast<std::uint8_t>(parameter->Type);
                if (DrawEnumCombo(ui, "Type", type, ParameterTypeNames))
                {
                    parameter->Type = static_cast<Keire::AnimationParameterType>(type);
                    markChanged("Change Animator Parameter Type");
                }
                switch (parameter->Type)
                {
                case Keire::AnimationParameterType::Float:
                {
                    double value = parameter->FloatDefault;
                    if (ui.DragScalar("Default", value, 0.01))
                    {
                        parameter->FloatDefault = static_cast<float>(value);
                        markChanged("Edit Animator Parameter");
                    }
                    break;
                }
                case Keire::AnimationParameterType::Integer:
                {
                    std::int64_t value = parameter->IntegerDefault;
                    if (ui.DragInteger("Default", value))
                    {
                        parameter->IntegerDefault = static_cast<std::int32_t>(value);
                        markChanged("Edit Animator Parameter");
                    }
                    break;
                }
                case Keire::AnimationParameterType::Boolean:
                    if (ui.Checkbox("Default", parameter->BooleanDefault))
                        markChanged("Edit Animator Parameter");
                    break;
                case Keire::AnimationParameterType::Trigger:
                    ui.TextColored(theme.MutedText, "Triggers always reset after consumption.");
                    break;
                }
            }
            else if (auto* layer = FindLayer(graph, selectedLayer))
            {
                auto* state = FindState(*layer, selectedState);
                auto selectedSubgraph = std::ranges::find(layer->Subgraphs, m_GraphSubgraph,
                                                          &Keire::AnimationStateMachineSubgraphDefinition::Id);
                if (!state && selectedSubgraph != layer->Subgraphs.end())
                {
                    ui.TextColored(theme.MutedText, "STATE-MACHINE SUBGRAPH");
                    if (ui.InputText("Name", selectedSubgraph->Name))
                        markChanged("Rename Animator State-Machine Subgraph");
                    const auto stateCount = std::ranges::count(layer->States, selectedSubgraph->Id,
                                                               &Keire::AnimationStateDefinition::SubgraphId);
                    ui.TextColored(theme.MutedText, std::to_string(stateCount) + " states");
                    ui.Text("Transitions may enter this subgraph through its entry state and leave through any "
                            "cross-graph destination.");
                }
                else if (!state)
                {
                    if (ui.InputText("Name", layer->Name))
                        markChanged("Rename Animator Layer");
                    auto mode = static_cast<std::uint8_t>(layer->Mode);
                    if (DrawEnumCombo(ui, "Blend Mode", mode, LayerModeNames))
                    {
                        layer->Mode = static_cast<Keire::AnimationLayerMode>(mode);
                        markChanged("Change Animator Layer Mode");
                    }
                    double weight = layer->DefaultWeight;
                    if (ui.DragScalar("Default Weight", weight, 0.01, 0.0, 1.0))
                    {
                        layer->DefaultWeight = static_cast<float>(weight);
                        markChanged("Edit Animator Layer Weight");
                    }
                    if (EditAssetReference(ui, "Avatar Mask", layer->AvatarMask, Keire::AvatarMaskAsset::StaticType(),
                                           database, m_Message))
                        markChanged("Assign Animator Avatar Mask");
                    if (layer->States.empty())
                        ui.TextColored(theme.MutedText, "Drag animation clips onto the state machine.");
                }
                else
                {
                    if (ui.InputText("Name", state->Name))
                        markChanged("Rename Animator State");
                    double speed = state->Speed;
                    if (ui.DragScalar("Speed", speed, 0.01))
                    {
                        state->Speed = static_cast<float>(speed);
                        markChanged("Edit Animator State Speed");
                    }
                    if (ui.Checkbox("Loop", state->Loop))
                        markChanged("Edit Animator State Looping");
                    if (ui.DragVector2("Graph Position", state->EditorPosition, 1.0F))
                        markChanged("Move Animator State");
                    auto* stateEntry = std::addressof(layer->EntryStateId);
                    if (!state->SubgraphId.empty())
                    {
                        const auto owner = std::ranges::find(layer->Subgraphs, state->SubgraphId,
                                                             &Keire::AnimationStateMachineSubgraphDefinition::Id);
                        if (owner != layer->Subgraphs.end())
                            stateEntry = std::addressof(owner->EntryStateId);
                    }
                    if (state->Id != *stateEntry && ui.Button("Set As Entry"))
                    {
                        *stateEntry = state->Id;
                        markChanged("Set Animator Entry State");
                    }
                    auto motion = static_cast<std::uint8_t>(state->Motion.Type);
                    if (DrawEnumCombo(ui, "Motion", motion, MotionTypeNames))
                    {
                        const auto replacement = static_cast<Keire::AnimationMotionType>(motion);
                        if (replacement == Keire::AnimationMotionType::Clip)
                        {
                            if (!state->Motion.Children.empty())
                                state->Motion.Clip = state->Motion.Children.front().Clip;
                            state->Clip = state->Motion.Clip;
                            state->Motion.Children.clear();
                            state->Motion.ParameterX.clear();
                            state->Motion.ParameterY.clear();
                        }
                        else
                        {
                            if (state->Motion.Children.empty() && state->Motion.Clip)
                            {
                                state->Motion.Children.push_back(
                                    {Keire::AssetId::Generate().ToString(), state->Motion.Clip});
                            }
                            state->Clip = {};
                            state->Motion.Clip = {};
                        }
                        state->Motion.Type = replacement;
                        markChanged("Change Animator State Motion");
                    }
                    if (state->Motion.Type == Keire::AnimationMotionType::Clip)
                    {
                        if (EditAssetReference(ui, "Animation Clip", state->Motion.Clip,
                                               Keire::AnimationClipAsset::StaticType(), database, m_Message))
                        {
                            state->Clip = state->Motion.Clip;
                            markChanged("Assign Animator State Clip");
                        }
                    }
                    else
                    {
                        const auto parameterCombo = [&](const std::string_view label, std::string& parameterId)
                        {
                            const auto selected = std::ranges::find(graph.ParameterDefinitions, parameterId,
                                                                    &Keire::AnimationParameterDefinition::Id);
                            std::string_view preview = "Select Float Parameter";
                            if (selected != graph.ParameterDefinitions.end())
                                preview = selected->Name;
                            bool parameterChanged = false;
                            if (auto combo = ui.BeginCombo(label, preview); combo)
                            {
                                for (const auto& candidate : graph.ParameterDefinitions)
                                {
                                    if (candidate.Type != Keire::AnimationParameterType::Float)
                                        continue;
                                    if (ui.Selectable(candidate.Name, candidate.Id == parameterId))
                                    {
                                        parameterId = candidate.Id;
                                        parameterChanged = true;
                                    }
                                }
                            }
                            return parameterChanged;
                        };
                        if (parameterCombo("Parameter X", state->Motion.ParameterX))
                            markChanged("Assign Animator Blend Parameter");
                        if (state->Motion.Type == Keire::AnimationMotionType::BlendTree2D &&
                            parameterCombo("Parameter Y", state->Motion.ParameterY))
                            markChanged("Assign Animator Blend Parameter");
                        ui.Separator();
                        ui.TextColored(theme.MutedText, "BLEND CHILDREN");
                        std::string removeChild;
                        for (auto& child : state->Motion.Children)
                        {
                            auto id = ui.PushId(child.Id);
                            if (EditAssetReference(ui, "Clip", child.Clip, Keire::AnimationClipAsset::StaticType(),
                                                   database, m_Message))
                                markChanged("Assign Animator Blend Clip");
                            double childSpeed = child.Speed;
                            if (ui.DragScalar("Speed", childSpeed, 0.01))
                            {
                                child.Speed = static_cast<float>(childSpeed);
                                markChanged("Edit Animator Blend Child");
                            }
                            if (state->Motion.Type == Keire::AnimationMotionType::BlendTree1D)
                            {
                                double threshold = child.Threshold;
                                if (ui.DragScalar("Threshold", threshold, 0.01))
                                {
                                    child.Threshold = static_cast<float>(threshold);
                                    markChanged("Edit Animator Blend Threshold");
                                }
                            }
                            else if (ui.DragVector2("Position", child.Position, 0.01F))
                                markChanged("Edit Animator Blend Position");
                            if (ui.Button("Remove Child"))
                                removeChild = child.Id;
                            ui.Separator();
                        }
                        if (!removeChild.empty())
                        {
                            std::erase_if(state->Motion.Children,
                                          [&](const auto& child) { return child.Id == removeChild; });
                            markChanged("Remove Animator Blend Child");
                        }
                        if (ui.Button("+ Blend Child"))
                        {
                            Keire::AnimationBlendTreeChild child;
                            child.Id = Keire::AssetId::Generate().ToString();
                            child.Threshold = static_cast<float>(state->Motion.Children.size());
                            child.Position = {static_cast<float>(state->Motion.Children.size()), 0.0F};
                            state->Motion.Children.push_back(std::move(child));
                            markChanged("Add Animator Blend Child");
                        }
                    }

                    ui.Separator();
                    ui.TextColored(theme.Accent, "TRANSITIONS");
                    for (const auto& transition : state->Transitions)
                    {
                        const auto destination = std::ranges::find(layer->States, transition.DestinationId,
                                                                   &Keire::AnimationStateDefinition::Id);
                        const auto name = destination == layer->States.end() ? "Missing State" : destination->Name;
                        auto id = ui.PushId(transition.Id);
                        if (ui.Selectable(name, m_SelectedTransition == transition.Id))
                            m_SelectedTransition = transition.Id;
                    }
                    if (auto add = ui.BeginCombo("+ Transition", "Choose destination"); add)
                    {
                        for (const auto& destination : layer->States)
                        {
                            if (destination.Id == state->Id)
                                continue;
                            if (ui.Selectable(destination.Name))
                            {
                                Keire::AnimationTransition transition;
                                transition.Id = Keire::AssetId::Generate().ToString();
                                transition.DestinationId = destination.Id;
                                transition.Destination = destination.Name;
                                m_SelectedTransition = transition.Id;
                                state->Transitions.push_back(std::move(transition));
                                markChanged("Add Animator Transition");
                            }
                        }
                    }
                    auto* transition = FindTransition(*state, m_SelectedTransition);
                    if (transition)
                    {
                        ui.Separator();
                        if (auto destinationCombo = ui.BeginCombo("Destination", transition->Destination);
                            destinationCombo)
                        {
                            for (const auto& destination : layer->States)
                            {
                                if (destination.Id == state->Id)
                                    continue;
                                if (ui.Selectable(destination.Name, destination.Id == transition->DestinationId))
                                {
                                    transition->DestinationId = destination.Id;
                                    transition->Destination = destination.Name;
                                    markChanged("Retarget Animator Transition");
                                }
                            }
                        }
                        double duration = transition->Duration;
                        if (ui.DragScalar("Duration", duration, 0.01, 0.0))
                        {
                            transition->Duration = static_cast<float>(duration);
                            markChanged("Edit Animator Transition");
                        }
                        if (ui.Checkbox("Has Exit Time", transition->HasExitTime))
                            markChanged("Edit Animator Transition");
                        if (transition->HasExitTime)
                        {
                            double exitTime = transition->ExitTime;
                            if (ui.DragScalar("Exit Time", exitTime, 0.01, 0.0))
                            {
                                transition->ExitTime = static_cast<float>(exitTime);
                                markChanged("Edit Animator Transition");
                            }
                        }
                        ui.TextColored(theme.MutedText, "CONDITIONS");
                        std::size_t removeCondition = transition->Conditions.size();
                        for (std::size_t index = 0; index < transition->Conditions.size(); ++index)
                        {
                            auto id = ui.PushId(std::to_string(index));
                            auto& condition = transition->Conditions[index];
                            auto conditionParameter =
                                std::ranges::find(graph.ParameterDefinitions, condition.ParameterId,
                                                  &Keire::AnimationParameterDefinition::Id);
                            std::string_view preview = "Missing Parameter";
                            if (conditionParameter != graph.ParameterDefinitions.end())
                                preview = conditionParameter->Name;
                            if (auto parameterCombo = ui.BeginCombo("Parameter", preview); parameterCombo)
                            {
                                for (const auto& candidate : graph.ParameterDefinitions)
                                {
                                    if (ui.Selectable(candidate.Name, candidate.Id == condition.ParameterId))
                                    {
                                        condition.ParameterId = candidate.Id;
                                        condition.Parameter = candidate.Name;
                                        if (candidate.Type == Keire::AnimationParameterType::Boolean ||
                                            candidate.Type == Keire::AnimationParameterType::Trigger)
                                            condition.Comparison = Keire::AnimationConditionComparison::Equal;
                                        markChanged("Edit Animator Transition Condition");
                                    }
                                }
                            }
                            conditionParameter = std::ranges::find(graph.ParameterDefinitions, condition.ParameterId,
                                                                   &Keire::AnimationParameterDefinition::Id);
                            auto comparison = static_cast<std::uint8_t>(condition.Comparison);
                            if (DrawEnumCombo(ui, "Comparison", comparison, ComparisonNames))
                            {
                                condition.Comparison = static_cast<Keire::AnimationConditionComparison>(comparison);
                                markChanged("Edit Animator Transition Condition");
                            }
                            if (conditionParameter != graph.ParameterDefinitions.end())
                            {
                                if (conditionParameter->Type == Keire::AnimationParameterType::Float)
                                {
                                    double value = condition.Value;
                                    if (ui.DragScalar("Value", value, 0.01))
                                    {
                                        condition.Value = static_cast<float>(value);
                                        markChanged("Edit Animator Transition Condition");
                                    }
                                }
                                else if (conditionParameter->Type == Keire::AnimationParameterType::Integer)
                                {
                                    std::int64_t value = condition.IntegerValue;
                                    if (ui.DragInteger("Value", value))
                                    {
                                        condition.IntegerValue = static_cast<std::int32_t>(value);
                                        markChanged("Edit Animator Transition Condition");
                                    }
                                }
                                else if (ui.Checkbox("Value", condition.BooleanValue))
                                    markChanged("Edit Animator Transition Condition");
                            }
                            if (ui.Button("Remove Condition"))
                                removeCondition = index;
                            ui.Separator();
                        }
                        if (removeCondition < transition->Conditions.size())
                        {
                            transition->Conditions.erase(transition->Conditions.begin() +
                                                         static_cast<std::ptrdiff_t>(removeCondition));
                            markChanged("Remove Animator Transition Condition");
                        }
                        if (!graph.ParameterDefinitions.empty() && ui.Button("+ Condition"))
                        {
                            Keire::AnimationTransitionCondition condition;
                            condition.ParameterId = graph.ParameterDefinitions.front().Id;
                            condition.Parameter = graph.ParameterDefinitions.front().Name;
                            transition->Conditions.push_back(std::move(condition));
                            markChanged("Add Animator Transition Condition");
                        }
                        if (ui.Button("Delete Transition"))
                        {
                            const auto transitionId = m_SelectedTransition;
                            std::erase_if(state->Transitions,
                                          [&](const auto& candidate) { return candidate.Id == transitionId; });
                            m_SelectedTransition.clear();
                            markChanged("Delete Animator Transition");
                        }
                    }

                    ui.Separator();
                    if (ui.Button("Delete State"))
                    {
                        const auto stateId = state->Id;
                        RemoveStateReferences(*layer, stateId);
                        std::erase_if(layer->States, [&](const auto& candidate) { return candidate.Id == stateId; });
                        RepairEntryStates(*layer);
                        selectedState.clear();
                        m_SelectedTransition.clear();
                        markChanged("Delete Animator State");
                    }
                }
            }
            else
                ui.TextColored(theme.MutedText, "Select a parameter, layer, or state.");
        }

        if (changed)
        {
            document.ReplaceDefinition(std::move(graph));
            document.RecordApplied(editName.empty() ? "Edit Animator Controller" : editName, std::move(before));
            m_Preview->Invalidate();
        }
        if (!selectedParameter.empty())
            document.SelectParameter(std::move(selectedParameter));
        else if (!selectedState.empty())
            document.SelectState(std::move(selectedLayer), std::move(selectedState));
        else if (!selectedLayer.empty())
            document.SelectLayer(std::move(selectedLayer));
        else
            document.ClearSelection();
    }
} // namespace KeireEditor
