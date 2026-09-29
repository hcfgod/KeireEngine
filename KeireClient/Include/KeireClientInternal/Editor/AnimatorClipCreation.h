#pragma once

#include "KeireClientInternal/Editor/AnimatorControllerPanelModelInternal.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace KeireEditor
{
    [[nodiscard]] inline Keire::Vector2 FindFreeAnimatorStatePosition(const Keire::AnimationLayerDefinition& layer,
                                                                      const std::string_view subgraphId,
                                                                      std::size_t slot,
                                                                      const Keire::Vector2 origin = {})
    {
        for (;; ++slot)
        {
            const auto candidate = AnimatorControllerPanelInternal::StateGridPosition(slot, origin);
            std::size_t visibleIndex = 0;
            bool occupied = false;
            for (const auto& existing : layer.States)
            {
                if (existing.SubgraphId != subgraphId)
                    continue;
                const auto position = AnimatorControllerPanelInternal::DisplayPosition(existing, visibleIndex++);
                const auto size = AnimatorControllerPanelInternal::StateNodeSize;
                if (std::abs(position.X - candidate.X) < size.X + 16.0F &&
                    std::abs(position.Y - candidate.Y) < size.Y + 16.0F)
                {
                    occupied = true;
                    break;
                }
            }
            // Non-first zero positions are legacy automatic-layout markers, not literal canvas coordinates.
            const bool legacyMarker =
                visibleIndex != 0 && std::abs(candidate.X) <= 0.001F && std::abs(candidate.Y) <= 0.001F;
            if (!occupied && !legacyMarker)
                return candidate;
        }
    }

    [[nodiscard]] inline std::string AddAnimatorClipState(Keire::AnimationGraphDefinition& graph, std::string& layerId,
                                                          const std::string_view subgraphId, const Keire::AssetId clip,
                                                          const std::string& name)
    {
        if (!clip)
            throw std::invalid_argument("Choose an animation clip before creating a state.");
        auto layer = std::ranges::find(graph.Layers, layerId, &Keire::AnimationLayerDefinition::Id);
        if (layer == graph.Layers.end() && !graph.Layers.empty())
            layer = graph.Layers.begin();
        if (!subgraphId.empty() &&
            (layer == graph.Layers.end() ||
             std::ranges::find(layer->Subgraphs, subgraphId, &Keire::AnimationStateMachineSubgraphDefinition::Id) ==
                 layer->Subgraphs.end()))
            throw std::invalid_argument("Select an existing state-machine subgraph before adding an animation.");
        if (layer == graph.Layers.end())
        {
            Keire::AnimationLayerDefinition base;
            base.Id = Keire::AssetId::Generate().ToString();
            base.Name = "Base Layer";
            graph.Layers.push_back(std::move(base));
            layer = graph.Layers.begin();
        }
        Keire::AnimationStateDefinition state;
        state.Id = Keire::AssetId::Generate().ToString();
        state.Name = AnimatorControllerPanelInternal::UniqueName(layer->States, name.empty() ? "Animation" : name,
                                                                 &Keire::AnimationStateDefinition::Name);
        state.Clip = clip;
        state.Motion.Clip = clip;
        state.SubgraphId = subgraphId;
        const auto count = std::ranges::count(layer->States, subgraphId, &Keire::AnimationStateDefinition::SubgraphId);
        state.EditorPosition = FindFreeAnimatorStatePosition(*layer, subgraphId, static_cast<std::size_t>(count));
        auto& entry = subgraphId.empty() ? layer->EntryStateId
                                         : std::ranges::find(layer->Subgraphs, subgraphId,
                                                             &Keire::AnimationStateMachineSubgraphDefinition::Id)
                                               ->EntryStateId;
        if (entry.empty())
            entry = state.Id;
        const auto id = state.Id;
        layer->States.push_back(std::move(state));
        layerId = layer->Id;
        return id;
    }
} // namespace KeireEditor
