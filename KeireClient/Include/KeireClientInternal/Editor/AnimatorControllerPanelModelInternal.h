#pragma once

#include "KeireClient/Editor/AnimatorControllerPanel.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace KeireEditor::AnimatorControllerPanelInternal
{
    inline constexpr std::array<std::string_view, 4> ParameterTypeNames{"Float", "Integer", "Boolean", "Trigger"};
    inline constexpr std::array<std::string_view, 3> MotionTypeNames{"Clip", "1D Blend Tree", "2D Blend Tree"};
    inline constexpr std::array<std::string_view, 2> LayerModeNames{"Override", "Additive"};
    inline constexpr std::array<std::string_view, 4> ComparisonNames{"Greater", "Less", "Equal", "Not Equal"};

    template <typename Range, typename Projection>
    [[nodiscard]] std::string UniqueName(const Range& values, const std::string& base, Projection projection)
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
                                                             std::string_view id);
    [[nodiscard]] Keire::AnimationStateDefinition* FindState(Keire::AnimationLayerDefinition& layer,
                                                             std::string_view id);
    [[nodiscard]] Keire::AnimationParameterDefinition* FindParameter(Keire::AnimationGraphDefinition& graph,
                                                                     std::string_view id);
    [[nodiscard]] Keire::AnimationTransition* FindTransition(Keire::AnimationStateDefinition& state,
                                                             std::string_view id);
    [[nodiscard]] bool DrawEnumCombo(Keire::UiFrame& ui, std::string_view label, std::uint8_t& value,
                                     std::span<const std::string_view> names);
    [[nodiscard]] bool EditAssetReference(Keire::UiFrame& ui, std::string_view label, Keire::AssetId& asset,
                                          Keire::AssetTypeId expectedType,
                                          const Keire::Ref<Keire::AssetDatabase>& database, std::string& message);
    [[nodiscard]] Keire::Vector2 DisplayPosition(const Keire::AnimationStateDefinition& state,
                                                 std::size_t index) noexcept;
    void RemoveParameterReferences(Keire::AnimationGraphDefinition& graph, std::string_view parameter);
    void RemoveStateReferences(Keire::AnimationLayerDefinition& layer, std::string_view state);
    void RepairEntryStates(Keire::AnimationLayerDefinition& layer);
    [[nodiscard]] float TimelineFraction(float normalizedTime) noexcept;
    [[nodiscard]] StableNodeId AnimatorCanvasId(std::string_view value, std::uint64_t salt) noexcept;
} // namespace KeireEditor::AnimatorControllerPanelInternal
