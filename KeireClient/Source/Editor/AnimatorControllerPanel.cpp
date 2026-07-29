#include "KeireClient/Editor/AnimatorControllerPanel.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        constexpr std::array<std::string_view, 4> ParameterTypeNames{"Float", "Integer", "Boolean", "Trigger"};
        constexpr std::array<std::string_view, 3> MotionTypeNames{"Clip", "1D Blend Tree", "2D Blend Tree"};
        constexpr std::array<std::string_view, 2> LayerModeNames{"Override", "Additive"};
        constexpr std::array<std::string_view, 4> ComparisonNames{"Greater", "Less", "Equal", "Not Equal"};

        template <typename Range, typename Projection>
        [[nodiscard]] std::string UniqueName(const Range& values, std::string base, Projection projection)
        {
            std::string candidate = base;
            for (std::size_t copy = 2; std::ranges::any_of(values, [&](const auto& value)
                                                           { return std::invoke(projection, value) == candidate; });
                 ++copy)
            {
                candidate = base + " " + std::to_string(copy);
            }
            return candidate;
        }

        [[nodiscard]] Keire::AnimationLayerDefinition* FindLayer(Keire::AnimationGraphDefinition& graph,
                                                                 const std::string_view id)
        {
            const auto found = std::ranges::find(graph.Layers, id, &Keire::AnimationLayerDefinition::Id);
            return found == graph.Layers.end() ? nullptr : &*found;
        }

        [[nodiscard]] Keire::AnimationStateDefinition* FindState(Keire::AnimationLayerDefinition& layer,
                                                                 const std::string_view id)
        {
            const auto found = std::ranges::find(layer.States, id, &Keire::AnimationStateDefinition::Id);
            return found == layer.States.end() ? nullptr : &*found;
        }

        [[nodiscard]] Keire::AnimationParameterDefinition* FindParameter(Keire::AnimationGraphDefinition& graph,
                                                                         const std::string_view id)
        {
            const auto found =
                std::ranges::find(graph.ParameterDefinitions, id, &Keire::AnimationParameterDefinition::Id);
            return found == graph.ParameterDefinitions.end() ? nullptr : &*found;
        }

        [[nodiscard]] Keire::AnimationTransition* FindTransition(Keire::AnimationStateDefinition& state,
                                                                 const std::string_view id)
        {
            const auto found = std::ranges::find(state.Transitions, id, &Keire::AnimationTransition::Id);
            return found == state.Transitions.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool DrawEnumCombo(Keire::UiFrame& ui, const std::string_view label, std::uint8_t& value,
                                         const std::span<const std::string_view> names)
        {
            const auto index = std::min<std::size_t>(value, names.size() - 1);
            bool changed = false;
            if (auto combo = ui.BeginCombo(label, names[index]); combo)
            {
                for (std::size_t candidate = 0; candidate < names.size(); ++candidate)
                {
                    if (ui.Selectable(names[candidate], candidate == index))
                    {
                        value = static_cast<std::uint8_t>(candidate);
                        changed = true;
                    }
                }
            }
            return changed;
        }

        [[nodiscard]] bool EditAssetReference(Keire::UiFrame& ui, const std::string_view label, Keire::AssetId& asset,
                                              const Keire::AssetTypeId expectedType,
                                              const Keire::Ref<Keire::AssetDatabase>& database, std::string& message)
        {
            bool changed = false;
            std::string value = asset ? asset.ToString() : std::string{};
            if (ui.InputText(label, value))
            {
                try
                {
                    const auto replacement = value.empty() ? Keire::AssetId{} : Keire::AssetId::Parse(value);
                    if (replacement && database)
                    {
                        const auto record = database->Find(replacement);
                        if (!record || record->Type != expectedType)
                            throw std::invalid_argument("The dropped or entered asset has the wrong type.");
                    }
                    asset = replacement;
                    changed = true;
                    message.clear();
                }
                catch (const std::exception& error)
                {
                    message = error.what();
                }
            }

            const auto field = ui.LastItemRect();
            if (auto target = ui.BeginDragTarget(field, std::string(label) + "Drop"); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
                {
                    try
                    {
                        const auto assets = AssetBrowserPanel::DecodeDragPayload(payload);
                        const auto found = std::ranges::find_if(assets,
                                                                [&](const Keire::AssetId candidate)
                                                                {
                                                                    const auto record = database
                                                                                            ? database->Find(candidate)
                                                                                            : std::nullopt;
                                                                    return record && record->Type == expectedType;
                                                                });
                        if (found == assets.end())
                            throw std::invalid_argument("Drop an asset of the required type.");
                        asset = *found;
                        changed = true;
                        message.clear();
                    }
                    catch (const std::exception& error)
                    {
                        message = error.what();
                    }
                }
            }
            return changed;
        }

        [[nodiscard]] Keire::Vector2 DisplayPosition(const Keire::AnimationStateDefinition& state,
                                                     const std::size_t index) noexcept
        {
            if (std::abs(state.EditorPosition.X) > 0.001F || std::abs(state.EditorPosition.Y) > 0.001F || index == 0)
                return state.EditorPosition;
            return {static_cast<float>(index % 3) * 190.0F, static_cast<float>(index / 3) * 104.0F};
        }

        void RemoveParameterReferences(Keire::AnimationGraphDefinition& graph, const std::string_view parameter)
        {
            for (auto& layer : graph.Layers)
            {
                for (auto& state : layer.States)
                {
                    for (auto& transition : state.Transitions)
                    {
                        std::erase_if(transition.Conditions,
                                      [&](const auto& condition) { return condition.ParameterId == parameter; });
                    }
                    if (state.Motion.ParameterX == parameter)
                        state.Motion.ParameterX.clear();
                    if (state.Motion.ParameterY == parameter)
                        state.Motion.ParameterY.clear();
                }
            }
        }

        void RemoveStateReferences(Keire::AnimationLayerDefinition& layer, const std::string_view state)
        {
            for (auto& source : layer.States)
                std::erase_if(source.Transitions,
                              [&](const auto& transition) { return transition.DestinationId == state; });
        }

        void DrawStateGraph(Keire::UiFrame& ui, const Keire::AnimationLayerDefinition& layer,
                            const std::string_view selectedState, const Keire::UiThemeDefinition& theme)
        {
            const auto canvas = ui.ContentRect();
            ui.DrawFilledRectangle(canvas, {0.055F, 0.065F, 0.078F, 1.0F}, 5.0F);
            for (float x = canvas.Minimum.X; x < canvas.Maximum.X; x += 32.0F)
                ui.DrawLine({x, canvas.Minimum.Y}, {x, canvas.Maximum.Y}, {0.12F, 0.14F, 0.17F, 0.6F});
            for (float y = canvas.Minimum.Y; y < canvas.Maximum.Y; y += 32.0F)
                ui.DrawLine({canvas.Minimum.X, y}, {canvas.Maximum.X, y}, {0.12F, 0.14F, 0.17F, 0.6F});

            const auto nodeRect = [&](const Keire::AnimationStateDefinition& state, const std::size_t index)
            {
                const auto position = DisplayPosition(state, index);
                const float x = canvas.Minimum.X + 28.0F + position.X;
                const float y = canvas.Minimum.Y + 34.0F + position.Y;
                return Keire::UiItemRect{{x, y}, {x + 154.0F, y + 58.0F}};
            };
            for (std::size_t sourceIndex = 0; sourceIndex < layer.States.size(); ++sourceIndex)
            {
                const auto& source = layer.States[sourceIndex];
                const auto sourceRect = nodeRect(source, sourceIndex);
                for (const auto& transition : source.Transitions)
                {
                    const auto destination =
                        std::ranges::find(layer.States, transition.DestinationId, &Keire::AnimationStateDefinition::Id);
                    if (destination == layer.States.end())
                        continue;
                    const auto destinationIndex =
                        static_cast<std::size_t>(std::distance(layer.States.begin(), destination));
                    const auto destinationRect = nodeRect(*destination, destinationIndex);
                    ui.DrawLine(
                        {sourceRect.Maximum.X, (sourceRect.Minimum.Y + sourceRect.Maximum.Y) * 0.5F},
                        {destinationRect.Minimum.X, (destinationRect.Minimum.Y + destinationRect.Maximum.Y) * 0.5F},
                        theme.MutedText, 2.0F);
                }
            }
            for (std::size_t index = 0; index < layer.States.size(); ++index)
            {
                const auto& state = layer.States[index];
                const auto rectangle = nodeRect(state, index);
                const bool entry = state.Id == layer.EntryStateId;
                const bool selected = state.Id == selectedState;
                const Keire::UiColor fill = selected ? Keire::UiColor{0.10F, 0.34F, 0.52F, 1.0F}
                                            : entry  ? Keire::UiColor{0.10F, 0.31F, 0.22F, 1.0F}
                                                     : Keire::UiColor{0.12F, 0.14F, 0.18F, 1.0F};
                ui.DrawFilledRectangle(rectangle, fill, 7.0F);
                ui.DrawRectangle(rectangle, selected ? theme.Accent : theme.MutedText, selected ? 2.0F : 1.0F, 7.0F);
                ui.DrawOverlayText({rectangle.Minimum.X + 12.0F, rectangle.Minimum.Y + 10.0F}, {1, 1, 1, 1},
                                   state.Name);
                ui.DrawOverlayText({rectangle.Minimum.X + 12.0F, rectangle.Minimum.Y + 32.0F}, theme.MutedText,
                                   entry ? "ENTRY" : MotionTypeNames[static_cast<std::size_t>(state.Motion.Type)]);
            }
        }
    } // namespace

    void AnimatorControllerPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.animator-controller", "Animator Controller", false});
    }

    void AnimatorControllerPanel::ResetTransientState() noexcept
    {
        m_SelectedTransition.clear();
        m_Message.clear();
    }

    void AnimatorControllerPanel::Draw(Keire::UiFrame& ui)
    {
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
            return;

        auto& document = m_Controller.AnimatorControllerState();
        const auto& theme = m_Controller.AnimatorControllerTheme();
        const auto database = m_Controller.AnimatorControllerDatabase();
        const auto assets = m_Controller.AnimatorControllerAssets();
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
                m_Controller.UndoAnimatorControllerEdit();
                return;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.UndoContext() || !document.UndoContext()->CanRedo()); disabled)
        {
            if (ui.Button("Redo"))
            {
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

        auto graph = document.Definition();
        auto before = graph;
        std::string selectedParameter(document.SelectedParameter());
        std::string selectedLayer(document.SelectedLayer());
        std::string selectedState(document.SelectedState());
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
                    m_SelectedTransition.clear();
                }
                if (selectedLayer == layer.Id)
                {
                    for (const auto& state : layer.States)
                    {
                        auto stateId = ui.PushId(state.Id);
                        if (ui.Selectable("   " + state.Name, selectedState == state.Id))
                        {
                            selectedParameter.clear();
                            selectedLayer = layer.Id;
                            selectedState = state.Id;
                            m_SelectedTransition.clear();
                        }
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
            if (layer)
            {
                ui.TextColored(theme.MutedText, layer->Name);
                ui.SameLine();
                if (ui.Button("Auto Layout"))
                {
                    for (std::size_t index = 0; index < layer->States.size(); ++index)
                    {
                        layer->States[index].EditorPosition = {static_cast<float>(index % 3) * 190.0F,
                                                               static_cast<float>(index / 3) * 104.0F};
                    }
                    markChanged("Auto Layout Animator States");
                }
            }
            ui.Separator();

            const auto canvas = ui.ContentRect();
            if (layer)
                DrawStateGraph(ui, *layer, selectedState, theme);
            else
            {
                ui.DrawFilledRectangle(canvas, {0.055F, 0.065F, 0.078F, 1.0F}, 5.0F);
                ui.DrawOverlayText({canvas.Minimum.X + 24.0F, canvas.Minimum.Y + 24.0F}, theme.MutedText,
                                   "Create a layer, then drag animation clips here.");
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
                        }
                        std::size_t added = 0;
                        for (const auto& clip : clips)
                        {
                            Keire::AnimationStateDefinition state;
                            state.Id = Keire::AssetId::Generate().ToString();
                            state.Name = UniqueName(layer->States, clip.Name, &Keire::AnimationStateDefinition::Name);
                            state.Clip = clip.Id;
                            state.Motion.Clip = clip.Id;
                            const auto index = layer->States.size();
                            state.EditorPosition = {static_cast<float>(index % 3) * 190.0F,
                                                    static_cast<float>(index / 3) * 104.0F};
                            selectedState = state.Id;
                            if (layer->EntryStateId.empty())
                                layer->EntryStateId = state.Id;
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
                if (!state)
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
                    if (state->Id != layer->EntryStateId && ui.Button("Set As Entry"))
                    {
                        layer->EntryStateId = state->Id;
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
                            const std::string_view preview = selected == graph.ParameterDefinitions.end()
                                                                 ? "Select Float Parameter"
                                                                 : selected->Name;
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
                            const std::string_view preview = conditionParameter == graph.ParameterDefinitions.end()
                                                                 ? "Missing Parameter"
                                                                 : conditionParameter->Name;
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
                        if (layer->EntryStateId == stateId)
                            layer->EntryStateId = layer->States.empty() ? std::string{} : layer->States.front().Id;
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
