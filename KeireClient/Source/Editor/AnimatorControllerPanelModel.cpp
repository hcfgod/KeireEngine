#include "KeireClientInternal/Editor/AnimatorControllerPanelModelInternal.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace KeireEditor::AnimatorControllerPanelInternal
{
    Keire::AnimationLayerDefinition* FindLayer(Keire::AnimationGraphDefinition& graph, const std::string_view id)
    {
        const auto found = std::ranges::find(graph.Layers, id, &Keire::AnimationLayerDefinition::Id);
        return found == graph.Layers.end() ? nullptr : &*found;
    }

    Keire::AnimationStateDefinition* FindState(Keire::AnimationLayerDefinition& layer, const std::string_view id)
    {
        const auto found = std::ranges::find(layer.States, id, &Keire::AnimationStateDefinition::Id);
        return found == layer.States.end() ? nullptr : &*found;
    }

    Keire::AnimationParameterDefinition* FindParameter(Keire::AnimationGraphDefinition& graph,
                                                       const std::string_view id)
    {
        const auto found = std::ranges::find(graph.ParameterDefinitions, id, &Keire::AnimationParameterDefinition::Id);
        return found == graph.ParameterDefinitions.end() ? nullptr : &*found;
    }

    Keire::AnimationTransition* FindTransition(Keire::AnimationStateDefinition& state, const std::string_view id)
    {
        const auto found = std::ranges::find(state.Transitions, id, &Keire::AnimationTransition::Id);
        return found == state.Transitions.end() ? nullptr : &*found;
    }

    bool DrawEnumCombo(Keire::UiFrame& ui, const std::string_view label, std::uint8_t& value,
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

    bool EditAssetReference(Keire::UiFrame& ui, const std::string_view label, Keire::AssetId& asset,
                            const Keire::AssetTypeId expectedType, const Keire::Ref<Keire::AssetDatabase>& database,
                            std::string& message)
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
                                                                const auto record =
                                                                    database ? database->Find(candidate) : std::nullopt;
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

    Keire::Vector2 DisplayPosition(const Keire::AnimationStateDefinition& state, const std::size_t index) noexcept
    {
        if (std::abs(state.EditorPosition.X) > 0.001F || std::abs(state.EditorPosition.Y) > 0.001F || index == 0)
            return state.EditorPosition;
        const auto row = index / 3U;
        return {static_cast<float>(index % 3U) * 190.0F, static_cast<float>(row) * 104.0F};
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

    void RepairEntryStates(Keire::AnimationLayerDefinition& layer)
    {
        const auto validRoot =
            std::ranges::find(layer.States, layer.EntryStateId, &Keire::AnimationStateDefinition::Id);
        if (validRoot == layer.States.end() || !validRoot->SubgraphId.empty())
        {
            const auto firstRoot =
                std::ranges::find(layer.States, std::string{}, &Keire::AnimationStateDefinition::SubgraphId);
            layer.EntryStateId = firstRoot == layer.States.end() ? std::string{} : firstRoot->Id;
        }
        for (auto& subgraph : layer.Subgraphs)
        {
            const auto entry =
                std::ranges::find(layer.States, subgraph.EntryStateId, &Keire::AnimationStateDefinition::Id);
            if (entry != layer.States.end() && entry->SubgraphId == subgraph.Id)
                continue;
            const auto first =
                std::ranges::find(layer.States, subgraph.Id, &Keire::AnimationStateDefinition::SubgraphId);
            subgraph.EntryStateId = first == layer.States.end() ? std::string{} : first->Id;
        }
    }

    float TimelineFraction(const float normalizedTime) noexcept
    {
        if (!std::isfinite(normalizedTime) || normalizedTime <= 0.0F)
            return 0.0F;
        return std::fmod(normalizedTime, 1.0F);
    }

    StableNodeId AnimatorCanvasId(const std::string_view value, const std::uint64_t salt) noexcept
    {
        std::uint64_t hash = 1469598103934665603ULL ^ salt;
        for (const auto character : value)
        {
            hash ^= static_cast<std::uint8_t>(character);
            hash *= 1099511628211ULL;
        }
        return hash == 0 ? salt | 1ULL : hash;
    }
} // namespace KeireEditor::AnimatorControllerPanelInternal
