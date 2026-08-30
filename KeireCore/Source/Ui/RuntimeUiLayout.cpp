#include "KeireInternal/Ui/RuntimeUiLayoutInternal.h"

#include "KeireInternal/Ui/RuntimeUiStyleInternal.h"

#include <algorithm>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] std::optional<std::size_t> FindNode(const std::span<const RuntimeUiTreeNode> nodes,
                                                          const RuntimeUiElementId id) noexcept
        {
            if (!id)
                return std::nullopt;
            const auto slot = static_cast<std::uint32_t>(id.Value() & 0xffffffffULL);
            const auto generation = static_cast<std::uint32_t>(id.Value() >> 32U);
            if (slot == 0 || slot > nodes.size())
                return std::nullopt;
            const auto index = static_cast<std::size_t>(slot - 1U);
            return nodes[index].Alive && nodes[index].Generation == generation ? std::optional(index) : std::nullopt;
        }

        [[nodiscard]] float ResolveIntrinsicLength(const float percent, const float pixels, const float extent,
                                                   const float scale) noexcept
        {
            if (percent >= 0.0F)
                return percent * extent;
            return pixels > 0.0F ? pixels * scale : 0.0F;
        }
    } // namespace

    Vector2 MeasureRuntimeUiIntrinsic(const std::span<const RuntimeUiTreeNode> nodes, const std::size_t index,
                                      const RuntimeUiRect available, const float scale)
    {
        const auto& node = nodes[index];
        const auto& state = node.State;
        const auto& style = state.Style;
        float width = ResolveIntrinsicLength(style.WidthPercent, style.Width, available.Width, scale);
        float height = ResolveIntrinsicLength(style.HeightPercent, style.Height, available.Height, scale);
        if (width <= 0.0F || height <= 0.0F)
        {
            const auto contentWidth =
                std::max(0.0F, available.Width - (style.Padding.Left + style.Padding.Right) * scale);
            const auto contentHeight =
                std::max(0.0F, available.Height - (style.Padding.Top + style.Padding.Bottom) * scale);
            const RuntimeUiRect content{0.0F, 0.0F, contentWidth, contentHeight};
            const bool horizontal = state.Type == RuntimeUiElementType::HorizontalLayout;
            const bool vertical =
                state.Type == RuntimeUiElementType::VerticalLayout || state.Type == RuntimeUiElementType::ScrollView;
            float childrenWidth = 0.0F;
            float childrenHeight = 0.0F;
            std::size_t flowingChildren = 0;
            for (const auto childId : node.Children)
            {
                const auto childIndex = FindNode(nodes, childId);
                if (!childIndex || !nodes[*childIndex].State.Visible ||
                    nodes[*childIndex].State.Style.Position == RuntimeUiPositionMode::Absolute)
                {
                    continue;
                }
                const auto child = MeasureRuntimeUiIntrinsic(nodes, *childIndex, content, scale);
                const auto& childStyle = nodes[*childIndex].State.Style;
                const float outerWidth = child.X + (childStyle.Margin.Left + childStyle.Margin.Right) * scale;
                const float outerHeight = child.Y + (childStyle.Margin.Top + childStyle.Margin.Bottom) * scale;
                if (horizontal)
                {
                    childrenWidth += outerWidth;
                    childrenHeight = std::max(childrenHeight, outerHeight);
                }
                else if (vertical)
                {
                    childrenWidth = std::max(childrenWidth, outerWidth);
                    childrenHeight += outerHeight;
                }
                else
                {
                    childrenWidth = std::max(childrenWidth, outerWidth);
                    childrenHeight = std::max(childrenHeight, outerHeight);
                }
                ++flowingChildren;
            }
            if (flowingChildren > 1)
            {
                const auto gaps = static_cast<float>(flowingChildren - 1) * style.Gap * scale;
                if (horizontal)
                    childrenWidth += gaps;
                else if (vertical)
                    childrenHeight += gaps;
            }

            const auto fontHeight = std::max(1.0F, style.FontSize * 1.35F * scale);
            const auto textWidth = static_cast<float>(state.Content.Text.size()) * style.FontSize * 0.55F * scale;
            if (width <= 0.0F)
            {
                width = childrenWidth + (style.Padding.Left + style.Padding.Right) * scale;
                if (width <= 0.0F && !state.Content.Text.empty())
                    width = textWidth + 16.0F * scale;
            }
            if (height <= 0.0F)
            {
                height = childrenHeight + (style.Padding.Top + style.Padding.Bottom) * scale;
                if (height <= 0.0F && !state.Content.Text.empty())
                    height = fontHeight + 8.0F * scale;
            }
        }

        const float minimumWidth =
            ResolveRuntimeUiPercent(style.MinimumWidthPercent, available.Width, style.MinimumWidth * scale);
        const float minimumHeight =
            ResolveRuntimeUiPercent(style.MinimumHeightPercent, available.Height, style.MinimumHeight * scale);
        const float maximumWidth =
            ResolveRuntimeUiPercent(style.MaximumWidthPercent, available.Width, style.MaximumWidth * scale);
        const float maximumHeight =
            ResolveRuntimeUiPercent(style.MaximumHeightPercent, available.Height, style.MaximumHeight * scale);
        return {std::clamp(width, minimumWidth, std::max(minimumWidth, maximumWidth)),
                std::clamp(height, minimumHeight, std::max(minimumHeight, maximumHeight))};
    }
} // namespace Keire::Detail
