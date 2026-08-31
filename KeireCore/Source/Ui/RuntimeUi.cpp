#include "Keire/Ui/RuntimeUi.h"

#include "KeireInternal/Ui/RuntimeUiDiagnosticsInternal.h"
#include "KeireInternal/Ui/RuntimeUiDrawCommandsInternal.h"
#include "KeireInternal/Ui/RuntimeUiLayoutInternal.h"
#include "KeireInternal/Ui/RuntimeUiStyleInternal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] constexpr std::size_t PointerButtonIndex(const RuntimeUiPointerButton button) noexcept
        {
            return static_cast<std::size_t>(button);
        }

        [[nodiscard]] bool Finite(const float value) noexcept { return std::isfinite(value); }

        [[nodiscard]] RuntimeUiRect TransformedBounds(const RuntimeUiRect rectangle, const RuntimeUiStyle& style,
                                                      const float layoutScale) noexcept
        {
            const Vector2 origin{rectangle.X + rectangle.Width * style.TransformOrigin.X,
                                 rectangle.Y + rectangle.Height * style.TransformOrigin.Y};
            const Vector2 translation{style.Translation.X * layoutScale, style.Translation.Y * layoutScale};
            const float radians = style.RotationDegrees * std::numbers::pi_v<float> / 180.0F;
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            const auto transform = [&](const float x, const float y)
            {
                const float localX = (x - origin.X) * style.TransformScale.X;
                const float localY = (y - origin.Y) * style.TransformScale.Y;
                return Vector2{origin.X + localX * cosine - localY * sine + translation.X,
                               origin.Y + localX * sine + localY * cosine + translation.Y};
            };
            const std::array corners{transform(rectangle.X, rectangle.Y),
                                     transform(rectangle.X + rectangle.Width, rectangle.Y),
                                     transform(rectangle.X + rectangle.Width, rectangle.Y + rectangle.Height),
                                     transform(rectangle.X, rectangle.Y + rectangle.Height)};
            float minimumX = corners.front().X;
            float maximumX = corners.front().X;
            float minimumY = corners.front().Y;
            float maximumY = corners.front().Y;
            for (const auto corner : corners)
            {
                minimumX = std::min(minimumX, corner.X);
                maximumX = std::max(maximumX, corner.X);
                minimumY = std::min(minimumY, corner.Y);
                maximumY = std::max(maximumY, corner.Y);
            }
            return {minimumX, minimumY, maximumX - minimumX, maximumY - minimumY};
        }
    } // namespace

    class RuntimeUiTree::Impl final
    {
      public:
        using Node = Detail::RuntimeUiTreeNode;

        Impl(const std::size_t maximumElements, const std::size_t maximumEvents)
            : MaximumElements(maximumElements), MaximumEvents(maximumEvents), Diagnostics(maximumEvents)
        {
            if (maximumElements == 0 || maximumElements > 1'000'000 || maximumEvents == 0 || maximumEvents > 1'000'000)
                throw std::invalid_argument("Runtime UI capacity is invalid.");
            Nodes.reserve(std::min<std::size_t>(maximumElements, 1024));
            Draws.reserve(std::min<std::size_t>(maximumElements * 2, 8192));
            HitElements.reserve(std::min<std::size_t>(maximumElements, 4096));
        }

        [[nodiscard]] RuntimeUiElementId MakeId(const std::size_t index) const noexcept
        {
            return RuntimeUiElementId((static_cast<std::uint64_t>(Nodes[index].Generation) << 32U) |
                                      static_cast<std::uint64_t>(index + 1U));
        }

        [[nodiscard]] std::optional<std::size_t> Index(const RuntimeUiElementId id) const noexcept
        {
            if (!id)
                return std::nullopt;
            const auto slot = static_cast<std::uint32_t>(id.Value() & 0xffffffffULL);
            const auto generation = static_cast<std::uint32_t>(id.Value() >> 32U);
            if (slot == 0 || slot > Nodes.size())
                return std::nullopt;
            const auto index = static_cast<std::size_t>(slot - 1U);
            return Nodes[index].Alive && Nodes[index].Generation == generation ? std::optional(index) : std::nullopt;
        }

        [[nodiscard]] bool IsAncestor(const RuntimeUiElementId candidate, RuntimeUiElementId element) const noexcept
        {
            while (const auto index = Index(element))
            {
                if (element == candidate)
                    return true;
                element = Nodes[*index].State.Parent;
            }
            return false;
        }

        void Queue(RuntimeUiEvent event)
        {
            if (Events.size() >= MaximumEvents)
            {
                ++DroppedEvents;
                return;
            }
            Events.push_back(event);
            std::vector<RuntimeUiElementId> route;
            auto current = event.Target;
            while (const auto index = Index(current))
            {
                route.push_back(current);
                current = Nodes[*index].State.Parent;
            }
            Diagnostics.RecordEvent(event, route);
        }

        void MarkDirty(const std::size_t index, const RuntimeUiDirtyReason reason)
        {
            auto current = std::optional(index);
            while (current)
            {
                auto& node = Nodes[*current];
                const bool resetReason = !node.Dirty;
                Diagnostics.MarkDirty(MakeId(*current), *current == index ? reason : RuntimeUiDirtyReason::Descendant,
                                      resetReason);
                if (!node.Dirty)
                {
                    node.Dirty = true;
                    ++DirtyElements;
                }
                current = Index(node.State.Parent);
            }
            LayoutDirty = true;
            ++TreeGeneration;
        }

        [[nodiscard]] Vector2 MeasureIntrinsic(const std::size_t index, const RuntimeUiRect available,
                                               const float scale) const
        {
            return Detail::MeasureRuntimeUiIntrinsic(Nodes, index, available, scale);
        }

        void LayoutNode(const std::size_t index, RuntimeUiRect available, const RuntimeUiRect inheritedClip,
                        const float scale, const bool widthControlled = false, const bool heightControlled = false)
        {
            auto& node = Nodes[index];
            auto& state = node.State;
            state.LayoutScale = scale;
            if (!state.Visible)
                return;

            const auto& style = state.Style;
            RuntimeUiRect rect = available;
            if (style.Position == RuntimeUiPositionMode::Absolute)
            {
                if (style.UseAnchors)
                {
                    const auto anchorLeft = available.X + available.Width * style.AnchorMinimum.X;
                    const auto anchorTop = available.Y + available.Height * style.AnchorMinimum.Y;
                    const auto anchorWidth = available.Width * (style.AnchorMaximum.X - style.AnchorMinimum.X);
                    const auto anchorHeight = available.Height * (style.AnchorMaximum.Y - style.AnchorMinimum.Y);
                    rect.Width = anchorWidth + style.SizeDelta.X * scale;
                    rect.Height = anchorHeight + style.SizeDelta.Y * scale;
                    const auto anchorReferenceX = anchorLeft + anchorWidth * style.Pivot.X;
                    const auto anchorReferenceY = anchorTop + anchorHeight * style.Pivot.Y;
                    rect.X = anchorReferenceX + style.AnchoredPosition.X * scale - rect.Width * style.Pivot.X;
                    rect.Y = anchorReferenceY + style.AnchoredPosition.Y * scale - rect.Height * style.Pivot.Y;
                }
                else
                {
                    const float offsetX =
                        Detail::ResolveRuntimeUiPercent(style.XPercent, available.Width, style.X * scale);
                    const float offsetY =
                        Detail::ResolveRuntimeUiPercent(style.YPercent, available.Height, style.Y * scale);
                    rect.X = available.X + offsetX;
                    rect.Y = available.Y + offsetY;
                    rect.Width = style.WidthPercent >= 0.0F
                                     ? style.WidthPercent * available.Width
                                     : (style.Width > 0.0F ? style.Width * scale : available.Width - offsetX);
                    rect.Height = style.HeightPercent >= 0.0F
                                      ? style.HeightPercent * available.Height
                                      : (style.Height > 0.0F ? style.Height * scale : available.Height - offsetY);
                }
            }
            else
            {
                if (!widthControlled)
                {
                    if (style.WidthPercent >= 0.0F)
                        rect.Width = style.WidthPercent * available.Width;
                    else if (style.Width > 0.0F)
                        rect.Width = style.Width * scale;
                }
                if (!heightControlled)
                {
                    if (style.HeightPercent >= 0.0F)
                        rect.Height = style.HeightPercent * available.Height;
                    else if (style.Height > 0.0F)
                        rect.Height = style.Height * scale;
                }
            }
            rect.X += style.Margin.Left * scale;
            rect.Y += style.Margin.Top * scale;
            rect.Width = std::max(0.0F, rect.Width - (style.Margin.Left + style.Margin.Right) * scale);
            rect.Height = std::max(0.0F, rect.Height - (style.Margin.Top + style.Margin.Bottom) * scale);
            const float minimumWidth =
                Detail::ResolveRuntimeUiPercent(style.MinimumWidthPercent, available.Width, style.MinimumWidth * scale);
            const float minimumHeight = Detail::ResolveRuntimeUiPercent(style.MinimumHeightPercent, available.Height,
                                                                        style.MinimumHeight * scale);
            const float maximumWidth =
                Detail::ResolveRuntimeUiPercent(style.MaximumWidthPercent, available.Width, style.MaximumWidth * scale);
            const float maximumHeight = Detail::ResolveRuntimeUiPercent(style.MaximumHeightPercent, available.Height,
                                                                        style.MaximumHeight * scale);
            rect.Width = std::clamp(rect.Width, minimumWidth, std::max(minimumWidth, maximumWidth));
            rect.Height = std::clamp(rect.Height, minimumHeight, std::max(minimumHeight, maximumHeight));
            const auto scaledWidth = rect.Width * style.LocalScale.X;
            const auto scaledHeight = rect.Height * style.LocalScale.Y;
            rect.X += (rect.Width - scaledWidth) * style.Pivot.X;
            rect.Y += (rect.Height - scaledHeight) * style.Pivot.Y;
            rect.Width = scaledWidth;
            rect.Height = scaledHeight;
            state.Rect = rect;
            state.ClipRect = inheritedClip;
            const auto transformedBounds = TransformedBounds(rect, style, scale);
            const auto visibleBounds = transformedBounds.Intersect(inheritedClip);
            if (visibleBounds.Empty())
                ++ClippedElements;
            else
                ++VisibleElements;
            if (state.Interactable && state.Enabled)
                ++InteractableElements;

            HitElements.push_back(MakeId(index));
            EmitDraw(index, scale);

            RuntimeUiRect content{
                rect.X + style.Padding.Left * scale,
                rect.Y + style.Padding.Top * scale,
                std::max(0.0F, rect.Width - (style.Padding.Left + style.Padding.Right) * scale),
                std::max(0.0F, rect.Height - (style.Padding.Top + style.Padding.Bottom) * scale),
            };
            content.X -= style.ContentOffset.X * scale;
            content.Y -= style.ContentOffset.Y * scale;
            const auto childClip = style.ClipChildren ? visibleBounds : inheritedClip;
            if (style.ClipChildren)
                Draws.push_back({.Type = RuntimeUiDrawType::PushClip,
                                 .Element = MakeId(index),
                                 .Rect = childClip,
                                 .ClipRect = childClip});

            const bool horizontal = state.Type == RuntimeUiElementType::HorizontalLayout;
            const bool vertical =
                state.Type == RuntimeUiElementType::VerticalLayout || state.Type == RuntimeUiElementType::ScrollView;
            const bool grid = state.Type == RuntimeUiElementType::GridLayout;
            auto orderedChildren = node.Children;
            if (style.ReverseChildren)
                std::ranges::reverse(orderedChildren);
            if ((horizontal || vertical) && style.Wrap != RuntimeUiWrapMode::NoWrap)
            {
                struct WrapItem final
                {
                    std::size_t Index = 0;
                    float Main = 0.0F;
                    float Cross = 0.0F;
                    float MainMargin = 0.0F;
                    float CrossMargin = 0.0F;
                };
                struct WrapLine final
                {
                    std::vector<WrapItem> Items;
                    float Main = 0.0F;
                    float Cross = 0.0F;
                };

                const float mainCapacity = horizontal ? content.Width : content.Height;
                const float crossCapacity = horizontal ? content.Height : content.Width;
                const float authoredGap = style.Gap * scale;
                std::vector<WrapLine> lines(1);
                for (const auto childId : orderedChildren)
                {
                    const auto childIndex = Index(childId);
                    if (!childIndex || !Nodes[*childIndex].State.Visible ||
                        Nodes[*childIndex].State.Style.Position == RuntimeUiPositionMode::Absolute)
                        continue;
                    const auto& childStyle = Nodes[*childIndex].State.Style;
                    const auto intrinsic = MeasureIntrinsic(*childIndex, content, scale);
                    WrapItem item;
                    item.Index = *childIndex;
                    item.Main = horizontal ? Detail::ResolveRuntimeUiPercent(
                                                 childStyle.WidthPercent, content.Width,
                                                 childStyle.Width > 0.0F ? childStyle.Width * scale : intrinsic.X)
                                           : Detail::ResolveRuntimeUiPercent(
                                                 childStyle.HeightPercent, content.Height,
                                                 childStyle.Height > 0.0F ? childStyle.Height * scale : intrinsic.Y);
                    item.Cross = horizontal ? Detail::ResolveRuntimeUiPercent(
                                                  childStyle.HeightPercent, content.Height,
                                                  childStyle.Height > 0.0F ? childStyle.Height * scale : intrinsic.Y)
                                            : Detail::ResolveRuntimeUiPercent(
                                                  childStyle.WidthPercent, content.Width,
                                                  childStyle.Width > 0.0F ? childStyle.Width * scale : intrinsic.X);
                    item.MainMargin = (horizontal ? childStyle.Margin.Left + childStyle.Margin.Right
                                                  : childStyle.Margin.Top + childStyle.Margin.Bottom) *
                                      scale;
                    item.CrossMargin = (horizontal ? childStyle.Margin.Top + childStyle.Margin.Bottom
                                                   : childStyle.Margin.Left + childStyle.Margin.Right) *
                                       scale;
                    auto& line = lines.back();
                    const float separator = line.Items.empty() ? 0.0F : authoredGap;
                    if (!line.Items.empty() && line.Main + separator + item.Main + item.MainMargin > mainCapacity)
                        lines.push_back({});
                    auto& target = lines.back();
                    if (!target.Items.empty())
                        target.Main += authoredGap;
                    target.Main += item.Main + item.MainMargin;
                    target.Cross = std::max(target.Cross, item.Cross + item.CrossMargin);
                    target.Items.push_back(item);
                }
                if (lines.size() == 1 && lines.front().Items.empty())
                    lines.clear();
                if (style.Wrap == RuntimeUiWrapMode::WrapReverse)
                    std::ranges::reverse(lines);

                float crossCursor = horizontal ? content.Y : content.X;
                for (const auto& line : lines)
                {
                    float grow = 0.0F;
                    float shrinkWeight = 0.0F;
                    for (const auto& item : line.Items)
                    {
                        const auto& childStyle = Nodes[item.Index].State.Style;
                        grow += childStyle.FlexGrow;
                        shrinkWeight += item.Main * childStyle.FlexShrink;
                    }
                    const float free = std::max(0.0F, mainCapacity - line.Main);
                    const float overflow = std::max(0.0F, line.Main - mainCapacity);
                    const float unclaimed = grow > 0.0F ? 0.0F : free;
                    float lineGap = authoredGap;
                    float mainCursor = horizontal ? content.X : content.Y;
                    switch (style.JustifyContent)
                    {
                    case RuntimeUiJustification::Center:
                        mainCursor += unclaimed * 0.5F;
                        break;
                    case RuntimeUiJustification::End:
                        mainCursor += unclaimed;
                        break;
                    case RuntimeUiJustification::SpaceBetween:
                        if (line.Items.size() > 1)
                            lineGap += unclaimed / static_cast<float>(line.Items.size() - 1);
                        break;
                    case RuntimeUiJustification::SpaceAround:
                        if (!line.Items.empty())
                        {
                            const float share = unclaimed / static_cast<float>(line.Items.size());
                            mainCursor += share * 0.5F;
                            lineGap += share;
                        }
                        break;
                    case RuntimeUiJustification::SpaceEvenly:
                        if (!line.Items.empty())
                        {
                            const float share = unclaimed / static_cast<float>(line.Items.size() + 1);
                            mainCursor += share;
                            lineGap += share;
                        }
                        break;
                    case RuntimeUiJustification::Start:
                        break;
                    }

                    for (const auto& item : line.Items)
                    {
                        const auto& childStyle = Nodes[item.Index].State.Style;
                        const float expanded = grow > 0.0F ? free * childStyle.FlexGrow / grow : 0.0F;
                        const float reduced =
                            shrinkWeight > 0.0F ? overflow * item.Main * childStyle.FlexShrink / shrinkWeight : 0.0F;
                        const float minimum =
                            horizontal
                                ? Detail::ResolveRuntimeUiPercent(childStyle.MinimumWidthPercent, content.Width,
                                                                  childStyle.MinimumWidth * scale)
                                : Detail::ResolveRuntimeUiPercent(childStyle.MinimumHeightPercent, content.Height,
                                                                  childStyle.MinimumHeight * scale);
                        const float main = std::max(minimum, item.Main + expanded - reduced);
                        const auto alignment = childStyle.HasAlignSelf ? childStyle.AlignSelf
                                                                       : (horizontal ? style.ChildVerticalAlignment
                                                                                     : style.ChildHorizontalAlignment);
                        float cross = item.Cross;
                        if (alignment == RuntimeUiAlignment::Stretch || cross <= 0.0F)
                            cross = std::max(0.0F, line.Cross - item.CrossMargin);
                        float crossOffset = 0.0F;
                        if (alignment == RuntimeUiAlignment::Center)
                            crossOffset = (line.Cross - cross - item.CrossMargin) * 0.5F;
                        else if (alignment == RuntimeUiAlignment::End)
                            crossOffset = line.Cross - cross - item.CrossMargin;
                        RuntimeUiRect childAvailable;
                        if (horizontal)
                            childAvailable = {mainCursor, crossCursor + crossOffset, main, cross};
                        else
                            childAvailable = {crossCursor + crossOffset, mainCursor, cross, main};
                        LayoutNode(item.Index, childAvailable, childClip, scale, true, true);
                        mainCursor += main + item.MainMargin + lineGap;
                    }
                    crossCursor += std::min(line.Cross, crossCapacity) + authoredGap;
                }

                for (const auto childId : orderedChildren)
                {
                    const auto childIndex = Index(childId);
                    if (childIndex && Nodes[*childIndex].State.Visible &&
                        Nodes[*childIndex].State.Style.Position == RuntimeUiPositionMode::Absolute)
                        LayoutNode(*childIndex, content, childClip, scale);
                }
                if (style.ClipChildren)
                    Draws.push_back({.Type = RuntimeUiDrawType::PopClip,
                                     .Element = MakeId(index),
                                     .Rect = childClip,
                                     .ClipRect = childClip});
                return;
            }
            float cursor = horizontal ? content.X : content.Y;
            std::size_t flowingChildren = 0;
            float fixedSize = 0.0F;
            float flex = 0.0F;
            float shrinkWeight = 0.0F;
            for (const auto childId : orderedChildren)
            {
                const auto childIndex = Index(childId);
                if (!childIndex || !Nodes[*childIndex].State.Visible || grid ||
                    Nodes[*childIndex].State.Style.Position == RuntimeUiPositionMode::Absolute)
                    continue;
                ++flowingChildren;
                const auto& childStyle = Nodes[*childIndex].State.Style;
                const auto intrinsic = MeasureIntrinsic(*childIndex, content, scale);
                const float authored =
                    horizontal ? Detail::ResolveRuntimeUiPercent(childStyle.WidthPercent, content.Width,
                                                                 childStyle.Width > 0.0F ? childStyle.Width * scale
                                                                                         : intrinsic.X)
                               : Detail::ResolveRuntimeUiPercent(childStyle.HeightPercent, content.Height,
                                                                 childStyle.Height > 0.0F ? childStyle.Height * scale
                                                                                          : intrinsic.Y);
                fixedSize += authored + (horizontal ? childStyle.Margin.Left + childStyle.Margin.Right
                                                    : childStyle.Margin.Top + childStyle.Margin.Bottom) *
                                            scale;
                shrinkWeight += authored * childStyle.FlexShrink;
                auto childFlex = childStyle.FlexGrow;
                if ((horizontal && style.ForceExpandWidth) || (vertical && style.ForceExpandHeight))
                    childFlex = std::max(childFlex, 1.0F);
                flex += childFlex;
            }
            if (flowingChildren > 1)
                fixedSize += static_cast<float>(flowingChildren - 1) * style.Gap * scale;
            const float availableFlow = horizontal ? content.Width : content.Height;
            const float flexibleSpace = std::max(0.0F, availableFlow - fixedSize);
            const float overflow = std::max(0.0F, fixedSize - availableFlow);
            float effectiveGap = style.Gap * scale;
            const float unclaimed = flex > 0.0F ? 0.0F : flexibleSpace;
            switch (style.JustifyContent)
            {
            case RuntimeUiJustification::Center:
                cursor += unclaimed * 0.5F;
                break;
            case RuntimeUiJustification::End:
                cursor += unclaimed;
                break;
            case RuntimeUiJustification::SpaceBetween:
                if (flowingChildren > 1)
                    effectiveGap += unclaimed / static_cast<float>(flowingChildren - 1);
                break;
            case RuntimeUiJustification::SpaceAround:
                if (flowingChildren > 0)
                {
                    const float share = unclaimed / static_cast<float>(flowingChildren);
                    cursor += share * 0.5F;
                    effectiveGap += share;
                }
                break;
            case RuntimeUiJustification::SpaceEvenly:
                if (flowingChildren > 0)
                {
                    const float share = unclaimed / static_cast<float>(flowingChildren + 1);
                    cursor += share;
                    effectiveGap += share;
                }
                break;
            case RuntimeUiJustification::Start:
                break;
            }

            std::size_t flowIndex = 0;
            for (const auto childId : orderedChildren)
            {
                const auto childIndex = Index(childId);
                if (!childIndex || !Nodes[*childIndex].State.Visible)
                    continue;
                const auto& childStyle = Nodes[*childIndex].State.Style;
                RuntimeUiRect childAvailable = content;
                bool childWidthControlled = false;
                bool childHeightControlled = false;
                if (childStyle.Position != RuntimeUiPositionMode::Absolute && grid)
                {
                    const auto cellWidth = std::max(style.GridCellSize.X * scale, 1.0F);
                    const auto cellHeight = std::max(style.GridCellSize.Y * scale, 1.0F);
                    const auto columns =
                        std::max<std::size_t>(1, static_cast<std::size_t>((content.Width + style.Gap * scale) /
                                                                          (cellWidth + style.Gap * scale)));
                    const auto column = flowIndex % columns;
                    const auto row = flowIndex / columns;
                    childAvailable = {content.X + static_cast<float>(column) * (cellWidth + style.Gap * scale),
                                      content.Y + static_cast<float>(row) * (cellHeight + style.Gap * scale), cellWidth,
                                      cellHeight};
                    childWidthControlled = true;
                    childHeightControlled = true;
                    ++flowIndex;
                }
                else if (childStyle.Position != RuntimeUiPositionMode::Absolute && (horizontal || vertical))
                {
                    const auto intrinsic = MeasureIntrinsic(*childIndex, content, scale);
                    const float authored =
                        horizontal ? Detail::ResolveRuntimeUiPercent(childStyle.WidthPercent, content.Width,
                                                                     childStyle.Width > 0.0F ? childStyle.Width * scale
                                                                                             : intrinsic.X)
                                   : Detail::ResolveRuntimeUiPercent(
                                         childStyle.HeightPercent, content.Height,
                                         childStyle.Height > 0.0F ? childStyle.Height * scale : intrinsic.Y);
                    auto childFlex = childStyle.FlexGrow;
                    if ((horizontal && style.ForceExpandWidth) || (vertical && style.ForceExpandHeight))
                        childFlex = std::max(childFlex, 1.0F);
                    const float flexible = flex > 0.0F ? flexibleSpace * (childFlex / flex) : 0.0F;
                    const float shrink =
                        shrinkWeight > 0.0F ? overflow * (authored * childStyle.FlexShrink / shrinkWeight) : 0.0F;
                    const float minimum =
                        horizontal ? Detail::ResolveRuntimeUiPercent(childStyle.MinimumWidthPercent, content.Width,
                                                                     childStyle.MinimumWidth * scale)
                                   : Detail::ResolveRuntimeUiPercent(childStyle.MinimumHeightPercent, content.Height,
                                                                     childStyle.MinimumHeight * scale);
                    const float extent = std::max(minimum, authored + flexible - shrink);
                    if (horizontal)
                    {
                        childAvailable.X = cursor;
                        childAvailable.Width = extent;
                        childWidthControlled = true;
                        if (style.ControlChildHeight || style.ForceExpandHeight)
                            childHeightControlled = true;
                        else
                        {
                            childAvailable.Height =
                                std::min(content.Height,
                                         Detail::ResolveRuntimeUiPercent(
                                             childStyle.HeightPercent, content.Height,
                                             childStyle.Height > 0.0F ? childStyle.Height * scale : intrinsic.Y));
                            if (style.ChildVerticalAlignment == RuntimeUiAlignment::Center)
                                childAvailable.Y += (content.Height - childAvailable.Height) * 0.5F;
                            else if (style.ChildVerticalAlignment == RuntimeUiAlignment::End)
                                childAvailable.Y += content.Height - childAvailable.Height;
                            childHeightControlled = true;
                        }
                        cursor += extent + effectiveGap + (childStyle.Margin.Left + childStyle.Margin.Right) * scale;
                    }
                    else
                    {
                        childAvailable.Y = cursor;
                        childAvailable.Height = extent;
                        childHeightControlled = true;
                        if (style.ControlChildWidth || style.ForceExpandWidth)
                            childWidthControlled = true;
                        else
                        {
                            childAvailable.Width = std::min(
                                content.Width, Detail::ResolveRuntimeUiPercent(
                                                   childStyle.WidthPercent, content.Width,
                                                   childStyle.Width > 0.0F ? childStyle.Width * scale : intrinsic.X));
                            if (style.ChildHorizontalAlignment == RuntimeUiAlignment::Center)
                                childAvailable.X += (content.Width - childAvailable.Width) * 0.5F;
                            else if (style.ChildHorizontalAlignment == RuntimeUiAlignment::End)
                                childAvailable.X += content.Width - childAvailable.Width;
                            childWidthControlled = true;
                        }
                        cursor += extent + effectiveGap + (childStyle.Margin.Top + childStyle.Margin.Bottom) * scale;
                    }
                }
                LayoutNode(*childIndex, childAvailable, childClip, scale, childWidthControlled, childHeightControlled);
            }

            if (style.ClipChildren)
                Draws.push_back({.Type = RuntimeUiDrawType::PopClip,
                                 .Element = MakeId(index),
                                 .Rect = childClip,
                                 .ClipRect = childClip});
        }

        void EmitDraw(const std::size_t index, const float scale)
        {
            Detail::AppendRuntimeUiDrawCommands(Draws, Nodes[index].State, MakeId(index), scale);
        }
        void ClearPointerOwnership(const RuntimeUiElementId element) noexcept
        {
            if (Hovered == element)
                Hovered = {};
            for (auto& pressed : Pressed)
                if (pressed == element)
                    pressed = {};
        }

        std::size_t MaximumElements;
        std::size_t MaximumEvents;
        Detail::RuntimeUiDiagnostics Diagnostics;
        std::vector<Node> Nodes;
        std::vector<std::size_t> Free;
        std::vector<RuntimeUiDrawCommand> Draws;
        std::vector<RuntimeUiElementId> HitElements;
        std::deque<RuntimeUiEvent> Events;
        RuntimeUiElementId Hovered;
        std::array<RuntimeUiElementId, 3> Pressed;
        RuntimeUiElementId Focused;
        std::uint64_t TreeGeneration = 1;
        std::uint64_t LayoutPasses = 0;
        std::uint64_t ReusedLayoutPasses = 0;
        std::size_t DirtyElements = 0;
        std::size_t VisibleElements = 0;
        std::size_t InteractableElements = 0;
        std::size_t ClippedElements = 0;
        std::size_t DroppedEvents = 0;
        float LastScale = 1.0F;
        float LastViewportWidth = 0.0F;
        float LastViewportHeight = 0.0F;
        RuntimeUiInsets LastSafeArea;
        RuntimeUiCanvasSettings LastCanvasSettings;
        float LastLayoutMilliseconds = 0.0F;
        bool LayoutDirty = true;
        bool HasLayout = false;
    };

    RuntimeUiTree::RuntimeUiTree(const std::size_t maximumElements, const std::size_t maximumEvents)
        : m_Impl(std::make_unique<Impl>(maximumElements, maximumEvents))
    {
    }

    RuntimeUiTree::~RuntimeUiTree() = default;

    RuntimeUiElementId RuntimeUiTree::Create(const RuntimeUiElementType type, const RuntimeUiElementId parent)
    {
        std::size_t index = 0;
        if (!m_Impl->Free.empty())
        {
            index = m_Impl->Free.back();
            m_Impl->Free.pop_back();
        }
        else
        {
            if (m_Impl->Nodes.size() >= m_Impl->MaximumElements)
                throw std::length_error("Runtime UI element capacity was exceeded.");
            index = m_Impl->Nodes.size();
            m_Impl->Nodes.emplace_back();
        }
        auto& node = m_Impl->Nodes[index];
        node.Alive = true;
        node.Dirty = false;
        node.HasStyle = false;
        node.State = {.Type = type, .Parent = parent};
        node.TransitionStartStyle = {};
        node.TargetStyle = {};
        node.ActiveTransitions.clear();
        node.RootCanvasSettings.reset();
        node.State.Interactable = type == RuntimeUiElementType::Button || type == RuntimeUiElementType::Slider ||
                                  type == RuntimeUiElementType::Toggle || type == RuntimeUiElementType::InputField ||
                                  type == RuntimeUiElementType::ScrollView;
        node.Children.clear();
        const auto id = m_Impl->MakeId(index);
        if (parent)
        {
            const auto parentIndex = m_Impl->Index(parent);
            if (!parentIndex)
            {
                node.Alive = false;
                m_Impl->Free.push_back(index);
                throw std::invalid_argument("Runtime UI parent handle is stale.");
            }
            m_Impl->Nodes[*parentIndex].Children.push_back(id);
        }
        m_Impl->MarkDirty(index, RuntimeUiDirtyReason::Hierarchy);
        return id;
    }

    bool RuntimeUiTree::Destroy(const RuntimeUiElementId element)
    {
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        const auto children = m_Impl->Nodes[*index].Children;
        for (const auto child : children)
            (void)Destroy(child);
        const auto parent = m_Impl->Nodes[*index].State.Parent;
        if (const auto parentIndex = m_Impl->Index(parent))
        {
            std::erase(m_Impl->Nodes[*parentIndex].Children, element);
            m_Impl->MarkDirty(*parentIndex, RuntimeUiDirtyReason::Hierarchy);
        }
        else
        {
            m_Impl->LayoutDirty = true;
            ++m_Impl->TreeGeneration;
        }
        if (m_Impl->Focused == element)
            m_Impl->Focused = {};
        if (m_Impl->Hovered == element)
            m_Impl->Hovered = {};
        for (auto& pressed : m_Impl->Pressed)
            if (pressed == element)
                pressed = {};
        auto& node = m_Impl->Nodes[*index];
        m_Impl->Diagnostics.Forget(element);
        node.Alive = false;
        node.Dirty = false;
        node.HasStyle = false;
        node.State = {};
        node.TransitionStartStyle = {};
        node.TargetStyle = {};
        node.ActiveTransitions.clear();
        node.Children.clear();
        node.RootCanvasSettings.reset();
        ++node.Generation;
        if (node.Generation == 0)
            node.Generation = 1;
        m_Impl->Free.push_back(*index);
        return true;
    }

    void RuntimeUiTree::Clear()
    {
        for (auto& node : m_Impl->Nodes)
        {
            node.Alive = false;
            node.HasStyle = false;
            node.State = {};
            node.TransitionStartStyle = {};
            node.TargetStyle = {};
            node.ActiveTransitions.clear();
            node.Children.clear();
            node.RootCanvasSettings.reset();
            ++node.Generation;
            if (node.Generation == 0)
                node.Generation = 1;
        }
        m_Impl->Free.clear();
        for (std::size_t index = 0; index < m_Impl->Nodes.size(); ++index)
            m_Impl->Free.push_back(index);
        m_Impl->Draws.clear();
        m_Impl->HitElements.clear();
        m_Impl->Events.clear();
        m_Impl->Diagnostics.Clear();
        m_Impl->Focused = {};
        m_Impl->Hovered = {};
        m_Impl->Pressed.fill({});
        m_Impl->DirtyElements = 0;
        m_Impl->LayoutDirty = true;
        m_Impl->HasLayout = false;
        ++m_Impl->TreeGeneration;
    }

    bool RuntimeUiTree::Exists(const RuntimeUiElementId element) const noexcept
    {
        return m_Impl->Index(element).has_value();
    }

    std::optional<RuntimeUiElementState> RuntimeUiTree::State(const RuntimeUiElementId element) const
    {
        const auto index = m_Impl->Index(element);
        return index ? std::optional(m_Impl->Nodes[*index].State) : std::nullopt;
    }

    bool RuntimeUiTree::SetType(const RuntimeUiElementId element, const RuntimeUiElementType type)
    {
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        if (m_Impl->Nodes[*index].State.Type == type)
            return true;
        m_Impl->Nodes[*index].State.Type = type;
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Style);
        return true;
    }

    bool RuntimeUiTree::SetStyle(const RuntimeUiElementId element, RuntimeUiStyle style)
    {
        Detail::ValidateRuntimeUiStyle(style);
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        auto& node = m_Impl->Nodes[*index];
        if (node.HasStyle && node.TargetStyle == style)
            return true;
        if (!node.HasStyle)
        {
            node.State.Style = style;
            node.TargetStyle = std::move(style);
            node.TransitionStartStyle = node.State.Style;
            node.ActiveTransitions.clear();
            node.HasStyle = true;
            m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Style);
            return true;
        }

        const auto current = node.State.Style;
        node.State.Style = style;
        node.TransitionStartStyle = current;
        node.TargetStyle = std::move(style);
        node.ActiveTransitions.clear();
        for (const auto property : Detail::RuntimeUiTransitionProperties())
        {
            const auto duration = Detail::RuntimeUiTransitionDuration(node.TargetStyle, property);
            if (!duration || *duration <= 0.0F ||
                !Detail::CanInterpolateRuntimeUiProperty(property, current, node.TargetStyle) ||
                Detail::RuntimeUiPropertyEqual(property, current, node.TargetStyle))
                continue;
            Detail::InterpolateRuntimeUiProperty(node.State.Style, current, node.TargetStyle, property, 0.0F);
            node.ActiveTransitions.push_back({property, 0.0F,
                                              Detail::RuntimeUiTransitionDelay(node.TargetStyle, property), *duration,
                                              Detail::RuntimeUiTransitionEasingFor(node.TargetStyle, property)});
        }
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Style);
        return true;
    }

    bool RuntimeUiTree::SetContent(const RuntimeUiElementId element, RuntimeUiContent content)
    {
        const auto index = m_Impl->Index(element);
        if (!index || content.Text.size() > 1'048'576 || content.AccessibilityLabel.size() > 16'384 ||
            content.AccessibilityHint.size() > 16'384 || (content.Image && content.RenderTexture))
            return false;
        if (m_Impl->Nodes[*index].State.Content == content)
            return true;
        m_Impl->Nodes[*index].State.Content = std::move(content);
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Content);
        return true;
    }

    bool RuntimeUiTree::SetControl(const RuntimeUiElementId element, const RuntimeUiControlState control)
    {
        Detail::ValidateRuntimeUiControl(control);
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        if (m_Impl->Nodes[*index].State.Control == control)
            return true;
        m_Impl->Nodes[*index].State.Control = control;
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Control);
        return true;
    }

    bool RuntimeUiTree::SetVisible(const RuntimeUiElementId element, const bool visible)
    {
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        auto& state = m_Impl->Nodes[*index].State;
        if (state.Visible == visible)
            return true;
        state.Visible = visible;
        if (!visible)
        {
            state.Hovered = false;
            state.Pressed = false;
            m_Impl->ClearPointerOwnership(element);
        }
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Visibility);
        return true;
    }

    bool RuntimeUiTree::SetEnabled(const RuntimeUiElementId element, const bool enabled)
    {
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        auto& state = m_Impl->Nodes[*index].State;
        if (state.Enabled == enabled)
            return true;
        state.Enabled = enabled;
        if (!enabled)
        {
            state.Hovered = false;
            state.Pressed = false;
            m_Impl->ClearPointerOwnership(element);
        }
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Interaction);
        return true;
    }

    bool RuntimeUiTree::SetInteractable(const RuntimeUiElementId element, const bool interactable)
    {
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        auto& state = m_Impl->Nodes[*index].State;
        if (state.Interactable == interactable)
            return true;
        state.Interactable = interactable;
        if (!interactable)
        {
            state.Hovered = false;
            state.Pressed = false;
            m_Impl->ClearPointerOwnership(element);
        }
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Interaction);
        return true;
    }

    bool RuntimeUiTree::SetParent(const RuntimeUiElementId element, const RuntimeUiElementId parent)
    {
        const auto index = m_Impl->Index(element);
        const auto parentIndex = parent ? m_Impl->Index(parent) : std::optional<std::size_t>{};
        if (!index || (parent && !parentIndex) || parent == element || (parent && m_Impl->IsAncestor(element, parent)))
            return false;
        const auto oldParent = m_Impl->Nodes[*index].State.Parent;
        if (oldParent == parent)
            return true;
        if (const auto oldParentIndex = m_Impl->Index(oldParent))
        {
            std::erase(m_Impl->Nodes[*oldParentIndex].Children, element);
            m_Impl->MarkDirty(*oldParentIndex, RuntimeUiDirtyReason::Hierarchy);
        }
        m_Impl->Nodes[*index].State.Parent = parent;
        if (parent)
            m_Impl->Nodes[*index].RootCanvasSettings.reset();
        if (parentIndex)
            m_Impl->Nodes[*parentIndex].Children.push_back(element);
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Hierarchy);
        return true;
    }

    bool RuntimeUiTree::SetRootCanvasSettings(const RuntimeUiElementId root,
                                              std::optional<RuntimeUiCanvasSettings> settings)
    {
        if (settings)
            Detail::ValidateRuntimeUiCanvasSettings(*settings);
        const auto index = m_Impl->Index(root);
        if (!index || m_Impl->Nodes[*index].State.Parent)
            return false;
        if (m_Impl->Nodes[*index].RootCanvasSettings == settings)
            return true;
        m_Impl->Nodes[*index].RootCanvasSettings = std::move(settings);
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::LayoutSettings);
        return true;
    }

    std::vector<RuntimeUiElementId> RuntimeUiTree::Children(const RuntimeUiElementId element) const
    {
        const auto index = m_Impl->Index(element);
        return index ? m_Impl->Nodes[*index].Children : std::vector<RuntimeUiElementId>{};
    }

    bool RuntimeUiTree::AdvanceTransitions(const float deltaSeconds)
    {
        if (!Finite(deltaSeconds) || deltaSeconds < 0.0F)
            throw std::invalid_argument("Runtime UI transition delta must be finite and non-negative.");
        if (deltaSeconds == 0.0F)
            return false;

        bool changed = false;
        for (std::size_t index = 0; index < m_Impl->Nodes.size(); ++index)
        {
            auto& node = m_Impl->Nodes[index];
            if (!node.Alive || node.ActiveTransitions.empty())
                continue;
            auto next = node.State.Style;
            for (auto& transition : node.ActiveTransitions)
            {
                transition.ElapsedSeconds = std::min(transition.DelaySeconds + transition.DurationSeconds,
                                                     transition.ElapsedSeconds + deltaSeconds);
                if (transition.ElapsedSeconds <= transition.DelaySeconds)
                    continue;
                const float alpha = Detail::ApplyRuntimeUiTransitionEasing(
                    transition.Easing,
                    (transition.ElapsedSeconds - transition.DelaySeconds) / transition.DurationSeconds);
                Detail::InterpolateRuntimeUiProperty(next, node.TransitionStartStyle, node.TargetStyle,
                                                     transition.Property, alpha);
            }
            std::erase_if(
                node.ActiveTransitions, [](const Impl::Node::ActiveTransition& transition)
                { return transition.ElapsedSeconds >= transition.DelaySeconds + transition.DurationSeconds; });
            if (next == node.State.Style)
                continue;
            node.State.Style = std::move(next);
            m_Impl->MarkDirty(index, RuntimeUiDirtyReason::Transition);
            changed = true;
        }
        return changed;
    }

    void RuntimeUiTree::Layout(const float viewportWidth, const float viewportHeight, const RuntimeUiInsets safeArea,
                               const RuntimeUiCanvasSettings settings)
    {
        if (!Finite(viewportWidth) || !Finite(viewportHeight) || viewportWidth <= 0.0F || viewportHeight <= 0.0F)
            throw std::invalid_argument("Runtime UI viewport is invalid.");
        Detail::ValidateRuntimeUiCanvasSettings(settings);
        Detail::ValidateRuntimeUiInsets(safeArea);

        if (m_Impl->HasLayout && !m_Impl->LayoutDirty && m_Impl->LastViewportWidth == viewportWidth &&
            m_Impl->LastViewportHeight == viewportHeight && m_Impl->LastSafeArea == safeArea &&
            m_Impl->LastCanvasSettings == settings)
        {
            ++m_Impl->ReusedLayoutPasses;
            m_Impl->LastLayoutMilliseconds = 0.0F;
            return;
        }
        const auto layoutStarted = std::chrono::steady_clock::now();

        m_Impl->LastScale = Detail::ResolveRuntimeUiScale(viewportWidth, viewportHeight, settings);
        m_Impl->Draws.clear();
        m_Impl->HitElements.clear();
        m_Impl->VisibleElements = 0;
        m_Impl->InteractableElements = 0;
        m_Impl->ClippedElements = 0;
        RuntimeUiRect viewport{0.0F, 0.0F, viewportWidth, viewportHeight};
        std::vector<std::size_t> roots;
        for (std::size_t index = 0; index < m_Impl->Nodes.size(); ++index)
            if (m_Impl->Nodes[index].Alive && !m_Impl->Nodes[index].State.Parent)
                roots.push_back(index);
        std::ranges::stable_sort(
            roots, [this](const std::size_t left, const std::size_t right)
            { return m_Impl->Nodes[left].State.Style.SortingOrder < m_Impl->Nodes[right].State.Style.SortingOrder; });
        for (const auto index : roots)
        {
            const auto& canvas = m_Impl->Nodes[index].RootCanvasSettings.value_or(settings);
            m_Impl->LayoutNode(index, Detail::ResolveRuntimeUiRoot(viewport, safeArea, canvas), viewport,
                               Detail::ResolveRuntimeUiScale(viewportWidth, viewportHeight, canvas));
        }
        for (auto& node : m_Impl->Nodes)
            node.Dirty = false;
        m_Impl->DirtyElements = 0;
        m_Impl->LayoutDirty = false;
        m_Impl->HasLayout = true;
        m_Impl->LastViewportWidth = viewportWidth;
        m_Impl->LastViewportHeight = viewportHeight;
        m_Impl->LastSafeArea = safeArea;
        m_Impl->LastCanvasSettings = settings;
        ++m_Impl->LayoutPasses;
        m_Impl->LastLayoutMilliseconds =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - layoutStarted).count();
    }

    std::span<const RuntimeUiDrawCommand> RuntimeUiTree::DrawCommands() const noexcept { return m_Impl->Draws; }

    std::optional<RuntimeUiElementId> RuntimeUiTree::HitTest(const float x, const float y) const noexcept
    {
        for (auto iterator = m_Impl->HitElements.rbegin(); iterator != m_Impl->HitElements.rend(); ++iterator)
        {
            const auto index = m_Impl->Index(*iterator);
            if (!index)
                continue;
            const auto& state = m_Impl->Nodes[*index].State;
            if (state.Visible && state.Enabled && state.Interactable &&
                Detail::RuntimeUiStyleContainsPoint(state, x, y) && state.ClipRect.Contains(x, y))
                return *iterator;
        }
        return std::nullopt;
    }

    std::optional<RuntimeUiElementId> RuntimeUiTree::HitTestWithin(const RuntimeUiElementId root, const float x,
                                                                   const float y) const noexcept
    {
        if (!root)
            return HitTest(x, y);
        for (auto iterator = m_Impl->HitElements.rbegin(); iterator != m_Impl->HitElements.rend(); ++iterator)
        {
            const auto index = m_Impl->Index(*iterator);
            if (!index)
                continue;
            auto ancestor = *iterator;
            while (ancestor && ancestor != root)
            {
                const auto ancestorIndex = m_Impl->Index(ancestor);
                ancestor = ancestorIndex ? m_Impl->Nodes[*ancestorIndex].State.Parent : RuntimeUiElementId{};
            }
            if (ancestor != root)
                continue;
            const auto& state = m_Impl->Nodes[*index].State;
            if (state.Visible && state.Enabled && state.Interactable &&
                Detail::RuntimeUiStyleContainsPoint(state, x, y) && state.ClipRect.Contains(x, y))
                return *iterator;
        }
        return std::nullopt;
    }

    void RuntimeUiTree::PointerMove(const float x, const float y)
    {
        PointerMoveTo(HitTest(x, y).value_or(RuntimeUiElementId{}), x, y);
    }

    void RuntimeUiTree::PointerMoveTo(const RuntimeUiElementId target, const float x, const float y)
    {
        RuntimeUiElementId hit;
        if (const auto index = m_Impl->Index(target))
        {
            const auto& state = m_Impl->Nodes[*index].State;
            if (state.Visible && state.Enabled && state.Interactable)
                hit = target;
        }
        if (hit == m_Impl->Hovered)
            return;
        if (const auto old = m_Impl->Index(m_Impl->Hovered))
        {
            m_Impl->Nodes[*old].State.Hovered = false;
            m_Impl->MarkDirty(*old, RuntimeUiDirtyReason::Interaction);
            m_Impl->Queue(
                {.Type = RuntimeUiEventType::PointerExit, .Target = m_Impl->Hovered, .PointerX = x, .PointerY = y});
        }
        m_Impl->Hovered = hit;
        if (const auto current = m_Impl->Index(hit))
        {
            m_Impl->Nodes[*current].State.Hovered = true;
            m_Impl->MarkDirty(*current, RuntimeUiDirtyReason::Interaction);
            m_Impl->Queue({.Type = RuntimeUiEventType::PointerEnter, .Target = hit, .PointerX = x, .PointerY = y});
        }
    }

    void RuntimeUiTree::PointerLeave()
    {
        if (const auto old = m_Impl->Index(m_Impl->Hovered))
        {
            m_Impl->Nodes[*old].State.Hovered = false;
            m_Impl->MarkDirty(*old, RuntimeUiDirtyReason::Interaction);
            m_Impl->Queue({.Type = RuntimeUiEventType::PointerExit, .Target = m_Impl->Hovered});
        }
        m_Impl->Hovered = {};
    }

    bool RuntimeUiTree::PointerButton(const float x, const float y, const RuntimeUiPointerButton button,
                                      const bool pressed)
    {
        return PointerButtonTo(HitTest(x, y).value_or(RuntimeUiElementId{}), x, y, button, pressed);
    }

    bool RuntimeUiTree::PointerButtonTo(const RuntimeUiElementId requestedTarget, const float x, const float y,
                                        const RuntimeUiPointerButton button, const bool pressed)
    {
        if (button > RuntimeUiPointerButton::Middle)
            throw std::invalid_argument("Runtime UI pointer button is invalid.");
        PointerMoveTo(requestedTarget, x, y);
        const auto target = m_Impl->Hovered;
        const auto buttonIndex = PointerButtonIndex(button);
        auto& pressedElement = m_Impl->Pressed[buttonIndex];
        const auto pressedByAnotherButton = [this, buttonIndex](const RuntimeUiElementId element)
        {
            for (std::size_t index = 0; index < m_Impl->Pressed.size(); ++index)
                if (index != buttonIndex && m_Impl->Pressed[index] == element)
                    return true;
            return false;
        };
        if (pressed)
        {
            if (const auto previous = m_Impl->Index(pressedElement); previous && pressedElement != target)
            {
                m_Impl->Nodes[*previous].State.Pressed = pressedByAnotherButton(pressedElement);
                m_Impl->MarkDirty(*previous, RuntimeUiDirtyReason::Interaction);
            }
            pressedElement = target;
            if (const auto index = m_Impl->Index(target))
            {
                m_Impl->Nodes[*index].State.Pressed = true;
                m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Interaction);
                (void)SetFocus(target);
                m_Impl->Queue({.Type = RuntimeUiEventType::PointerDown,
                               .Target = target,
                               .PointerX = x,
                               .PointerY = y,
                               .Button = button});
                return true;
            }
            return false;
        }
        const auto released = pressedElement;
        pressedElement = {};
        bool handled = false;
        if (const auto index = m_Impl->Index(released))
        {
            handled = true;
            m_Impl->Nodes[*index].State.Pressed = pressedByAnotherButton(released);
            m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Interaction);
            m_Impl->Queue({.Type = RuntimeUiEventType::PointerUp,
                           .Target = released,
                           .PointerX = x,
                           .PointerY = y,
                           .Button = button});
            if (released == target)
                m_Impl->Queue({.Type = RuntimeUiEventType::Click,
                               .Target = released,
                               .PointerX = x,
                               .PointerY = y,
                               .Button = button});
        }
        return handled;
    }

    bool RuntimeUiTree::CancelPointerButton(const RuntimeUiPointerButton button) noexcept
    {
        if (button > RuntimeUiPointerButton::Middle)
            return false;
        auto& pressedElement = m_Impl->Pressed[PointerButtonIndex(button)];
        const auto released = std::exchange(pressedElement, {});
        const auto index = m_Impl->Index(released);
        if (!index)
            return false;
        m_Impl->Nodes[*index].State.Pressed = std::ranges::any_of(
            m_Impl->Pressed, [released](const RuntimeUiElementId candidate) { return candidate == released; });
        m_Impl->MarkDirty(*index, RuntimeUiDirtyReason::Interaction);
        try
        {
            m_Impl->Queue({.Type = RuntimeUiEventType::PointerUp, .Target = released, .Button = button});
        }
        catch (...)
        {
        }
        return true;
    }

    void RuntimeUiTree::Navigate(const RuntimeUiNavigation navigation)
    {
        if (navigation == RuntimeUiNavigation::Accept)
        {
            if (m_Impl->Focused)
                m_Impl->Queue({.Type = RuntimeUiEventType::Submit, .Target = m_Impl->Focused});
            return;
        }
        if (navigation == RuntimeUiNavigation::Cancel)
        {
            if (m_Impl->Focused)
                m_Impl->Queue({.Type = RuntimeUiEventType::Cancel, .Target = m_Impl->Focused});
            return;
        }
        std::vector<RuntimeUiElementId> focusable;
        for (std::size_t index = 0; index < m_Impl->Nodes.size(); ++index)
        {
            const auto& state = m_Impl->Nodes[index].State;
            if (m_Impl->Nodes[index].Alive && state.Visible && state.Enabled && state.Interactable)
                focusable.push_back(m_Impl->MakeId(index));
        }
        if (focusable.empty())
            return;
        std::ranges::stable_sort(focusable,
                                 [this](const RuntimeUiElementId left, const RuntimeUiElementId right)
                                 {
                                     const auto leftOrder =
                                         m_Impl->Nodes[*m_Impl->Index(left)].State.Style.NavigationOrder;
                                     const auto rightOrder =
                                         m_Impl->Nodes[*m_Impl->Index(right)].State.Style.NavigationOrder;
                                     if (leftOrder == 0 || rightOrder == 0)
                                         return leftOrder != 0 && rightOrder == 0;
                                     return leftOrder < rightOrder;
                                 });
        const auto current = std::ranges::find(focusable, m_Impl->Focused);
        const bool backwards = navigation == RuntimeUiNavigation::Previous || navigation == RuntimeUiNavigation::Left ||
                               navigation == RuntimeUiNavigation::Up;
        std::size_t index = 0;
        if (current == focusable.end())
            index = backwards ? focusable.size() - 1 : 0;
        else
        {
            index = static_cast<std::size_t>(current - focusable.begin());
            index = backwards ? (index + focusable.size() - 1) % focusable.size() : (index + 1) % focusable.size();
        }
        (void)SetFocus(focusable[index]);
    }

    bool RuntimeUiTree::SetFocus(const RuntimeUiElementId element)
    {
        const auto next = m_Impl->Index(element);
        if (element && (!next || !m_Impl->Nodes[*next].State.Visible || !m_Impl->Nodes[*next].State.Enabled ||
                        !m_Impl->Nodes[*next].State.Interactable))
            return false;
        if (element == m_Impl->Focused)
            return true;
        if (const auto old = m_Impl->Index(m_Impl->Focused))
        {
            m_Impl->Nodes[*old].State.Focused = false;
            m_Impl->MarkDirty(*old, RuntimeUiDirtyReason::Interaction);
            m_Impl->Queue({.Type = RuntimeUiEventType::Blur, .Target = m_Impl->Focused});
        }
        m_Impl->Focused = element;
        if (next)
        {
            m_Impl->Nodes[*next].State.Focused = true;
            m_Impl->MarkDirty(*next, RuntimeUiDirtyReason::Interaction);
            m_Impl->Queue({.Type = RuntimeUiEventType::Focus, .Target = element});
        }
        return true;
    }

    RuntimeUiElementId RuntimeUiTree::Focus() const noexcept { return m_Impl->Focused; }

    bool RuntimeUiTree::DispatchEvent(RuntimeUiEvent event)
    {
        if (!m_Impl->Index(event.Target) || event.Type > RuntimeUiEventType::TextChanged ||
            event.Button > RuntimeUiPointerButton::Middle || !std::isfinite(event.PointerX) ||
            !std::isfinite(event.PointerY))
            return false;
        const auto previous = m_Impl->Events.size();
        m_Impl->Queue(std::move(event));
        return m_Impl->Events.size() > previous;
    }

    bool RuntimeUiTree::PollEvent(RuntimeUiEvent& event)
    {
        if (m_Impl->Events.empty())
            return false;
        event = m_Impl->Events.front();
        m_Impl->Events.pop_front();
        return true;
    }

    std::vector<RuntimeUiEvent> RuntimeUiTree::PendingEvents() const
    {
        return {m_Impl->Events.begin(), m_Impl->Events.end()};
    }

    void RuntimeUiTree::ReplacePendingEvents(const std::span<const RuntimeUiEvent> events)
    {
        if (events.size() > m_Impl->MaximumEvents)
            throw std::length_error("Runtime UI event checkpoint exceeds the configured event capacity.");
        for (const auto& event : events)
        {
            if (!m_Impl->Index(event.Target) || event.Type > RuntimeUiEventType::TextChanged ||
                event.Button > RuntimeUiPointerButton::Middle || !std::isfinite(event.PointerX) ||
                !std::isfinite(event.PointerY))
            {
                throw std::invalid_argument("Runtime UI event checkpoint is invalid or stale.");
            }
        }
        m_Impl->Events.assign(events.begin(), events.end());
    }

    RuntimeUiDirtyReason RuntimeUiTree::DirtyReasons(const RuntimeUiElementId element) const noexcept
    {
        return m_Impl->Index(element) ? m_Impl->Diagnostics.DirtyReasons(element) : RuntimeUiDirtyReason::None;
    }

    std::vector<RuntimeUiEventRouteEntry> RuntimeUiTree::EventRouteHistory() const
    {
        return m_Impl->Diagnostics.EventRouteHistory();
    }

    void RuntimeUiTree::ReportStylePass(const float milliseconds) { m_Impl->Diagnostics.ReportStylePass(milliseconds); }

    void RuntimeUiTree::ReportRepaintPass(const float milliseconds)
    {
        m_Impl->Diagnostics.ReportRepaintPass(milliseconds);
    }

    RuntimeUiStatistics RuntimeUiTree::Statistics() const noexcept
    {
        RuntimeUiStatistics result;
        result.Generation = m_Impl->TreeGeneration;
        result.Elements = static_cast<std::size_t>(
            std::ranges::count_if(m_Impl->Nodes, [](const Impl::Node& node) { return node.Alive; }));
        result.VisibleElements = m_Impl->VisibleElements;
        result.InteractableElements = m_Impl->InteractableElements;
        result.DrawCommands = m_Impl->Draws.size();
        AssetId previousAsset;
        RuntimeUiDrawType previousType = RuntimeUiDrawType::PopClip;
        for (const auto& command : m_Impl->Draws)
        {
            if (command.Type != previousType || command.Asset != previousAsset)
            {
                ++result.DrawBatches;
                previousType = command.Type;
                previousAsset = command.Asset;
            }
        }
        result.ClippedElements = m_Impl->ClippedElements;
        result.PendingEvents = m_Impl->Events.size();
        result.DroppedEvents = m_Impl->DroppedEvents;
        result.DirtyElements = m_Impl->DirtyElements;
        result.LayoutPasses = m_Impl->LayoutPasses;
        result.ReusedLayoutPasses = m_Impl->ReusedLayoutPasses;
        result.LayoutMilliseconds = m_Impl->LastLayoutMilliseconds;
        result.Scale = m_Impl->LastScale;
        m_Impl->Diagnostics.Populate(result);
        return result;
    }
} // namespace Keire
