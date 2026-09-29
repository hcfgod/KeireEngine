#pragma once

#include "Keire/Animation/AnimationSystem.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace KeireEditor
{
    [[nodiscard]] inline bool HasPreviewState(const Keire::AnimationGraphDefinition& graph,
                                              const std::string_view layerId, const std::string_view stateId)
    {
        const auto layer = std::ranges::find(graph.Layers, layerId, &Keire::AnimationLayerDefinition::Id);
        return layer != graph.Layers.end() &&
               std::ranges::any_of(layer->States, [&](const auto& state) { return state.Id == stateId; });
    }

    inline void ResetSelectedPreview(Keire::AnimatorInstance& instance, const Keire::AnimationGraphDefinition& graph,
                                     const std::string_view layerId, const std::string_view stateId,
                                     const float normalizedTime = 0.0F)
    {
        if (!HasPreviewState(graph, layerId, stateId))
            throw std::runtime_error("The previewed state or layer was removed. Select a state and choose Preview "
                                     "Selected, or choose Preview Graph to restart from the authored entry state.");
        instance.Reset();
        instance.Play(stateId, layerId, normalizedTime);
    }
} // namespace KeireEditor
