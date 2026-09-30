#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace KeireEditor::Detail
{
    inline constexpr float InspectorCompactFieldWidth = 300.0F;
    inline constexpr float InspectorDescriptiveFieldWidth = 380.0F;
    inline constexpr float InspectorInlineActionSpacing = 8.0F;

    struct InspectorFieldLayout final
    {
        bool Stacked = false;
        float ControlWidth = 0.0F;
    };

    [[nodiscard]] inline InspectorFieldLayout
    ResolveInspectorFieldLayout(const float availableWidth, const float trailingActionWidth = 0.0F,
                                const float actionSpacing = InspectorInlineActionSpacing,
                                const float compactFieldWidth = InspectorCompactFieldWidth) noexcept
    {
        const float available = std::isfinite(availableWidth) ? std::max(availableWidth, 1.0F) : 1.0F;
        const float action = std::isfinite(trailingActionWidth) ? std::max(trailingActionWidth, 0.0F) : 0.0F;
        const float spacing = action > 0.0F && std::isfinite(actionSpacing) ? std::max(actionSpacing, 0.0F) : 0.0F;
        const float compactWidth = std::isfinite(compactFieldWidth) ? std::max(compactFieldWidth, 1.0F) : 1.0F;
        const bool stacked = available < compactWidth;
        return {.Stacked = stacked, .ControlWidth = stacked ? std::max(available - action - spacing, 1.0F) : 0.0F};
    }

    [[nodiscard]] inline bool
    ShouldStackInspectorAction(const float availableWidth, const float contentWidth, const float actionWidth,
                               const float actionSpacing = InspectorInlineActionSpacing) noexcept
    {
        if (!std::isfinite(availableWidth) || !std::isfinite(contentWidth) || !std::isfinite(actionWidth) ||
            !std::isfinite(actionSpacing))
        {
            return true;
        }
        const auto layout = ResolveInspectorFieldLayout(availableWidth);
        const float content = std::max(contentWidth, 0.0F);
        const float action = std::max(actionWidth, 0.0F);
        const float spacing = std::max(actionSpacing, 0.0F);
        return layout.Stacked || content + action + spacing > std::max(availableWidth, 1.0F);
    }

    [[nodiscard]] inline std::string_view InspectorVisibleLabel(const std::string_view label) noexcept
    {
        const auto suffix = label.find("##");
        return label.substr(0, suffix);
    }

    [[nodiscard]] inline std::string InspectorControlLabel(const std::string_view label, const bool stacked)
    {
        return stacked ? "##" + std::string(label) : std::string(label);
    }
} // namespace KeireEditor::Detail
