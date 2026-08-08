#include "Keire/Ui/RuntimeUi.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool Finite(const float value) noexcept { return std::isfinite(value); }

        void ValidateInsets(const RuntimeUiInsets& value)
        {
            if (!Finite(value.Left) || !Finite(value.Top) || !Finite(value.Right) || !Finite(value.Bottom) ||
                value.Left < 0.0F || value.Top < 0.0F || value.Right < 0.0F || value.Bottom < 0.0F)
                throw std::invalid_argument("Runtime UI insets must be finite and non-negative.");
        }

        void ValidateStyle(const RuntimeUiStyle& style)
        {
            const float values[] = {
                style.X,
                style.Y,
                style.Width,
                style.Height,
                style.AnchorMinimum.X,
                style.AnchorMinimum.Y,
                style.AnchorMaximum.X,
                style.AnchorMaximum.Y,
                style.Pivot.X,
                style.Pivot.Y,
                style.AnchoredPosition.X,
                style.AnchoredPosition.Y,
                style.SizeDelta.X,
                style.SizeDelta.Y,
                style.LocalScale.X,
                style.LocalScale.Y,
                style.MinimumWidth,
                style.MinimumHeight,
                style.MaximumWidth,
                style.MaximumHeight,
                style.FlexGrow,
                style.Gap,
                style.BorderWidth,
                style.CornerRadius,
                style.Opacity,
                style.FontSize,
                style.GridCellSize.X,
                style.GridCellSize.Y,
            };
            if (!std::ranges::all_of(values, Finite) || style.Width < 0.0F || style.Height < 0.0F ||
                style.AnchorMinimum.X < 0.0F || style.AnchorMinimum.Y < 0.0F || style.AnchorMaximum.X > 1.0F ||
                style.AnchorMaximum.Y > 1.0F || style.AnchorMinimum.X > style.AnchorMaximum.X ||
                style.AnchorMinimum.Y > style.AnchorMaximum.Y || style.Pivot.X < 0.0F || style.Pivot.X > 1.0F ||
                style.Pivot.Y < 0.0F || style.Pivot.Y > 1.0F || style.LocalScale.X <= 0.0F ||
                style.LocalScale.Y <= 0.0F || style.GridCellSize.X < 0.0F || style.GridCellSize.Y < 0.0F ||
                style.MinimumWidth < 0.0F || style.MinimumHeight < 0.0F || style.MaximumWidth < style.MinimumWidth ||
                style.MaximumHeight < style.MinimumHeight || style.FlexGrow < 0.0F || style.Gap < 0.0F ||
                style.BorderWidth < 0.0F || style.CornerRadius < 0.0F || style.Opacity < 0.0F || style.Opacity > 1.0F ||
                style.FontSize <= 0.0F)
                throw std::invalid_argument("Runtime UI style contains invalid dimensions.");
            ValidateInsets(style.Margin);
            ValidateInsets(style.Padding);
        }

        [[nodiscard]] Color WithOpacity(Color color, const float opacity) noexcept
        {
            color.Alpha *= opacity;
            return color;
        }
    } // namespace

    bool RuntimeUiRect::Contains(const float x, const float y) const noexcept
    {
        return !Empty() && x >= X && y >= Y && x <= X + Width && y <= Y + Height;
    }

    RuntimeUiRect RuntimeUiRect::Intersect(const RuntimeUiRect other) const noexcept
    {
        const auto left = std::max(X, other.X);
        const auto top = std::max(Y, other.Y);
        const auto right = std::min(X + Width, other.X + other.Width);
        const auto bottom = std::min(Y + Height, other.Y + other.Height);
        return {left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
    }

    class RuntimeUiTree::Impl final
    {
      public:
        struct Node final
        {
            std::uint32_t Generation = 1;
            bool Alive = false;
            RuntimeUiElementState State;
            std::vector<RuntimeUiElementId> Children;
        };

        Impl(const std::size_t maximumElements, const std::size_t maximumEvents)
            : MaximumElements(maximumElements), MaximumEvents(maximumEvents)
        {
            if (maximumElements == 0 || maximumElements > 1'000'000 || maximumEvents == 0 || maximumEvents > 1'000'000)
                throw std::invalid_argument("Runtime UI capacity is invalid.");
            Nodes.reserve(std::min<std::size_t>(maximumElements, 1024));
            Draws.reserve(std::min<std::size_t>(maximumElements * 2, 8192));
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
        }

        void LayoutNode(const std::size_t index, RuntimeUiRect available, const RuntimeUiRect inheritedClip,
                        const float scale, const bool widthControlled = false, const bool heightControlled = false)
        {
            auto& node = Nodes[index];
            auto& state = node.State;
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
                    rect.X = available.X + style.X * scale;
                    rect.Y = available.Y + style.Y * scale;
                    rect.Width = style.Width > 0.0F ? style.Width * scale : available.Width - style.X * scale;
                    rect.Height = style.Height > 0.0F ? style.Height * scale : available.Height - style.Y * scale;
                }
            }
            else
            {
                if (!widthControlled && style.Width > 0.0F)
                    rect.Width = style.Width * scale;
                if (!heightControlled && style.Height > 0.0F)
                    rect.Height = style.Height * scale;
            }
            rect.X += style.Margin.Left * scale;
            rect.Y += style.Margin.Top * scale;
            rect.Width = std::max(0.0F, rect.Width - (style.Margin.Left + style.Margin.Right) * scale);
            rect.Height = std::max(0.0F, rect.Height - (style.Margin.Top + style.Margin.Bottom) * scale);
            rect.Width = std::clamp(rect.Width, style.MinimumWidth * scale, style.MaximumWidth * scale);
            rect.Height = std::clamp(rect.Height, style.MinimumHeight * scale, style.MaximumHeight * scale);
            const auto scaledWidth = rect.Width * style.LocalScale.X;
            const auto scaledHeight = rect.Height * style.LocalScale.Y;
            rect.X += (rect.Width - scaledWidth) * style.Pivot.X;
            rect.Y += (rect.Height - scaledHeight) * style.Pivot.Y;
            rect.Width = scaledWidth;
            rect.Height = scaledHeight;
            state.Rect = rect;
            state.ClipRect = rect.Intersect(inheritedClip);
            if (state.ClipRect.Empty())
                ++ClippedElements;
            else
                ++VisibleElements;
            if (state.Interactable && state.Enabled)
                ++InteractableElements;

            EmitDraw(index);

            RuntimeUiRect content{
                rect.X + style.Padding.Left * scale,
                rect.Y + style.Padding.Top * scale,
                std::max(0.0F, rect.Width - (style.Padding.Left + style.Padding.Right) * scale),
                std::max(0.0F, rect.Height - (style.Padding.Top + style.Padding.Bottom) * scale),
            };
            const auto childClip = style.ClipChildren ? state.ClipRect : inheritedClip;
            if (style.ClipChildren)
                Draws.push_back({.Type = RuntimeUiDrawType::PushClip,
                                 .Element = MakeId(index),
                                 .Rect = state.ClipRect,
                                 .ClipRect = state.ClipRect});

            const bool horizontal = state.Type == RuntimeUiElementType::HorizontalLayout;
            const bool vertical = state.Type == RuntimeUiElementType::VerticalLayout;
            const bool grid = state.Type == RuntimeUiElementType::GridLayout;
            float cursor = horizontal ? content.X : content.Y;
            std::size_t flowingChildren = 0;
            float fixedSize = 0.0F;
            float flex = 0.0F;
            for (const auto childId : node.Children)
            {
                const auto childIndex = Index(childId);
                if (!childIndex || !Nodes[*childIndex].State.Visible || grid ||
                    Nodes[*childIndex].State.Style.Position == RuntimeUiPositionMode::Absolute)
                    continue;
                ++flowingChildren;
                const auto& childStyle = Nodes[*childIndex].State.Style;
                fixedSize += (horizontal ? childStyle.Width + childStyle.Margin.Left + childStyle.Margin.Right
                                         : childStyle.Height + childStyle.Margin.Top + childStyle.Margin.Bottom) *
                             scale;
                auto childFlex = childStyle.FlexGrow;
                if ((horizontal && style.ForceExpandWidth) || (vertical && style.ForceExpandHeight))
                    childFlex = std::max(childFlex, 1.0F);
                flex += childFlex;
            }
            if (flowingChildren > 1)
                fixedSize += static_cast<float>(flowingChildren - 1) * style.Gap * scale;
            const float availableFlow = horizontal ? content.Width : content.Height;
            const float flexibleSpace = std::max(0.0F, availableFlow - fixedSize);

            std::size_t flowIndex = 0;
            for (const auto childId : node.Children)
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
                    const float authored = (horizontal ? childStyle.Width : childStyle.Height) * scale;
                    auto childFlex = childStyle.FlexGrow;
                    if ((horizontal && style.ForceExpandWidth) || (vertical && style.ForceExpandHeight))
                        childFlex = std::max(childFlex, 1.0F);
                    const float flexible = flex > 0.0F ? flexibleSpace * (childFlex / flex) : 0.0F;
                    const float extent = authored + flexible;
                    if (horizontal)
                    {
                        childAvailable.X = cursor;
                        childAvailable.Width = extent;
                        childWidthControlled = true;
                        if (style.ControlChildHeight || style.ForceExpandHeight)
                            childHeightControlled = true;
                        else if (childStyle.Height > 0.0F)
                        {
                            childAvailable.Height = std::min(content.Height, childStyle.Height * scale);
                            if (style.ChildVerticalAlignment == RuntimeUiAlignment::Center)
                                childAvailable.Y += (content.Height - childAvailable.Height) * 0.5F;
                            else if (style.ChildVerticalAlignment == RuntimeUiAlignment::End)
                                childAvailable.Y += content.Height - childAvailable.Height;
                            childHeightControlled = true;
                        }
                        cursor +=
                            extent + style.Gap * scale + (childStyle.Margin.Left + childStyle.Margin.Right) * scale;
                    }
                    else
                    {
                        childAvailable.Y = cursor;
                        childAvailable.Height = extent;
                        childHeightControlled = true;
                        if (style.ControlChildWidth || style.ForceExpandWidth)
                            childWidthControlled = true;
                        else if (childStyle.Width > 0.0F)
                        {
                            childAvailable.Width = std::min(content.Width, childStyle.Width * scale);
                            if (style.ChildHorizontalAlignment == RuntimeUiAlignment::Center)
                                childAvailable.X += (content.Width - childAvailable.Width) * 0.5F;
                            else if (style.ChildHorizontalAlignment == RuntimeUiAlignment::End)
                                childAvailable.X += content.Width - childAvailable.Width;
                            childWidthControlled = true;
                        }
                        cursor +=
                            extent + style.Gap * scale + (childStyle.Margin.Top + childStyle.Margin.Bottom) * scale;
                    }
                }
                LayoutNode(*childIndex, childAvailable, childClip, scale, childWidthControlled, childHeightControlled);
            }

            if (style.ClipChildren)
                Draws.push_back({.Type = RuntimeUiDrawType::PopClip,
                                 .Element = MakeId(index),
                                 .Rect = state.ClipRect,
                                 .ClipRect = state.ClipRect});
        }

        void EmitDraw(const std::size_t index)
        {
            const auto& state = Nodes[index].State;
            const auto id = MakeId(index);
            auto background = state.Style.Background;
            if (!state.Enabled && state.Style.DisabledBackground.Alpha > 0.0F)
                background = state.Style.DisabledBackground;
            else if (state.Pressed && state.Style.PressedBackground.Alpha > 0.0F)
                background = state.Style.PressedBackground;
            else if ((state.Hovered || state.Focused) && state.Style.HoverBackground.Alpha > 0.0F)
                background = state.Style.HoverBackground;
            if (background.Alpha > 0.0F)
                Draws.push_back({.Type = RuntimeUiDrawType::Quad,
                                 .Element = id,
                                 .Rect = state.Rect,
                                 .ClipRect = state.ClipRect,
                                 .ColorValue = WithOpacity(background, state.Style.Opacity),
                                 .BorderColor = WithOpacity(state.Style.Border, state.Style.Opacity),
                                 .CornerRadius = state.Style.CornerRadius,
                                 .BorderWidth = state.Style.BorderWidth});
            if (state.Content.Image)
                Draws.push_back({.Type = RuntimeUiDrawType::Image,
                                 .Element = id,
                                 .Rect = state.Rect,
                                 .ClipRect = state.ClipRect,
                                 .ColorValue = WithOpacity(state.Style.Foreground, state.Style.Opacity),
                                 .Asset = state.Content.Image,
                                 .CornerRadius = state.Style.CornerRadius});
            if (!state.Content.Text.empty())
                Draws.push_back({.Type = RuntimeUiDrawType::Text,
                                 .Element = id,
                                 .Rect = state.Rect,
                                 .ClipRect = state.ClipRect,
                                 .ColorValue = WithOpacity(state.Style.Foreground, state.Style.Opacity),
                                 .Asset = state.Content.Font,
                                 .Text = state.Content.Text,
                                 .FontSize = state.Style.FontSize * LastScale,
                                 .HorizontalAlignment = state.Style.HorizontalAlignment,
                                 .VerticalAlignment = state.Style.VerticalAlignment});
        }

        std::size_t MaximumElements;
        std::size_t MaximumEvents;
        std::vector<Node> Nodes;
        std::vector<std::size_t> Free;
        std::vector<RuntimeUiDrawCommand> Draws;
        std::deque<RuntimeUiEvent> Events;
        RuntimeUiElementId Hovered;
        RuntimeUiElementId Pressed;
        RuntimeUiElementId Focused;
        std::uint64_t TreeGeneration = 1;
        std::size_t VisibleElements = 0;
        std::size_t InteractableElements = 0;
        std::size_t ClippedElements = 0;
        std::size_t DroppedEvents = 0;
        float LastScale = 1.0F;
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
        node.State = {.Type = type, .Parent = parent};
        node.State.Interactable = type == RuntimeUiElementType::Button;
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
            std::erase(m_Impl->Nodes[*parentIndex].Children, element);
        if (m_Impl->Focused == element)
            m_Impl->Focused = {};
        if (m_Impl->Hovered == element)
            m_Impl->Hovered = {};
        if (m_Impl->Pressed == element)
            m_Impl->Pressed = {};
        auto& node = m_Impl->Nodes[*index];
        node.Alive = false;
        node.State = {};
        node.Children.clear();
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
            node.State = {};
            node.Children.clear();
            ++node.Generation;
            if (node.Generation == 0)
                node.Generation = 1;
        }
        m_Impl->Free.clear();
        for (std::size_t index = 0; index < m_Impl->Nodes.size(); ++index)
            m_Impl->Free.push_back(index);
        m_Impl->Draws.clear();
        m_Impl->Events.clear();
        m_Impl->Focused = {};
        m_Impl->Hovered = {};
        m_Impl->Pressed = {};
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
        m_Impl->Nodes[*index].State.Type = type;
        return true;
    }

    bool RuntimeUiTree::SetStyle(const RuntimeUiElementId element, RuntimeUiStyle style)
    {
        ValidateStyle(style);
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        m_Impl->Nodes[*index].State.Style = style;
        return true;
    }

    bool RuntimeUiTree::SetContent(const RuntimeUiElementId element, RuntimeUiContent content)
    {
        const auto index = m_Impl->Index(element);
        if (!index || content.Text.size() > 1'048'576 || content.AccessibilityLabel.size() > 16'384)
            return false;
        m_Impl->Nodes[*index].State.Content = std::move(content);
        return true;
    }

    bool RuntimeUiTree::SetVisible(const RuntimeUiElementId element, const bool visible)
    {
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        m_Impl->Nodes[*index].State.Visible = visible;
        return true;
    }

    bool RuntimeUiTree::SetEnabled(const RuntimeUiElementId element, const bool enabled)
    {
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        m_Impl->Nodes[*index].State.Enabled = enabled;
        return true;
    }

    bool RuntimeUiTree::SetInteractable(const RuntimeUiElementId element, const bool interactable)
    {
        const auto index = m_Impl->Index(element);
        if (!index)
            return false;
        m_Impl->Nodes[*index].State.Interactable = interactable;
        return true;
    }

    bool RuntimeUiTree::SetParent(const RuntimeUiElementId element, const RuntimeUiElementId parent)
    {
        const auto index = m_Impl->Index(element);
        const auto parentIndex = parent ? m_Impl->Index(parent) : std::optional<std::size_t>{};
        if (!index || (parent && !parentIndex) || parent == element || (parent && m_Impl->IsAncestor(element, parent)))
            return false;
        const auto oldParent = m_Impl->Nodes[*index].State.Parent;
        if (const auto oldParentIndex = m_Impl->Index(oldParent))
            std::erase(m_Impl->Nodes[*oldParentIndex].Children, element);
        m_Impl->Nodes[*index].State.Parent = parent;
        if (parentIndex)
            m_Impl->Nodes[*parentIndex].Children.push_back(element);
        return true;
    }

    std::vector<RuntimeUiElementId> RuntimeUiTree::Children(const RuntimeUiElementId element) const
    {
        const auto index = m_Impl->Index(element);
        return index ? m_Impl->Nodes[*index].Children : std::vector<RuntimeUiElementId>{};
    }

    void RuntimeUiTree::Layout(const float viewportWidth, const float viewportHeight, const RuntimeUiInsets safeArea,
                               const RuntimeUiCanvasSettings settings)
    {
        if (!Finite(viewportWidth) || !Finite(viewportHeight) || viewportWidth <= 0.0F || viewportHeight <= 0.0F ||
            !Finite(settings.ReferenceWidth) || !Finite(settings.ReferenceHeight) || settings.ReferenceWidth <= 0.0F ||
            settings.ReferenceHeight <= 0.0F || !Finite(settings.MatchWidthOrHeight) ||
            settings.MatchWidthOrHeight < 0.0F || settings.MatchWidthOrHeight > 1.0F ||
            !Finite(settings.AccessibilityScale) || settings.AccessibilityScale < 0.5F ||
            settings.AccessibilityScale > 3.0F)
            throw std::invalid_argument("Runtime UI viewport or canvas settings are invalid.");
        ValidateInsets(safeArea);

        float scale = 1.0F;
        if (settings.ScaleMode == RuntimeUiScaleMode::ScaleWithViewport)
        {
            const auto widthScale = viewportWidth / settings.ReferenceWidth;
            const auto heightScale = viewportHeight / settings.ReferenceHeight;
            scale = std::exp(std::log(widthScale) * (1.0F - settings.MatchWidthOrHeight) +
                             std::log(heightScale) * settings.MatchWidthOrHeight);
        }
        scale *= settings.AccessibilityScale;
        m_Impl->LastScale = scale;
        m_Impl->Draws.clear();
        m_Impl->VisibleElements = 0;
        m_Impl->InteractableElements = 0;
        m_Impl->ClippedElements = 0;
        RuntimeUiRect viewport{0.0F, 0.0F, viewportWidth, viewportHeight};
        RuntimeUiRect root = viewport;
        if (settings.RespectSafeArea)
        {
            root.X += safeArea.Left;
            root.Y += safeArea.Top;
            root.Width = std::max(0.0F, root.Width - safeArea.Left - safeArea.Right);
            root.Height = std::max(0.0F, root.Height - safeArea.Top - safeArea.Bottom);
        }
        std::vector<std::size_t> roots;
        for (std::size_t index = 0; index < m_Impl->Nodes.size(); ++index)
            if (m_Impl->Nodes[index].Alive && !m_Impl->Nodes[index].State.Parent)
                roots.push_back(index);
        std::ranges::stable_sort(
            roots, [this](const std::size_t left, const std::size_t right)
            { return m_Impl->Nodes[left].State.Style.SortingOrder < m_Impl->Nodes[right].State.Style.SortingOrder; });
        for (const auto index : roots)
            m_Impl->LayoutNode(index, root, viewport, scale);
    }

    std::span<const RuntimeUiDrawCommand> RuntimeUiTree::DrawCommands() const noexcept { return m_Impl->Draws; }

    std::optional<RuntimeUiElementId> RuntimeUiTree::HitTest(const float x, const float y) const noexcept
    {
        for (auto iterator = m_Impl->Draws.rbegin(); iterator != m_Impl->Draws.rend(); ++iterator)
        {
            const auto index = m_Impl->Index(iterator->Element);
            if (!index)
                continue;
            const auto& state = m_Impl->Nodes[*index].State;
            if (state.Visible && state.Enabled && state.Interactable && state.Rect.Contains(x, y) &&
                state.ClipRect.Contains(x, y))
                return iterator->Element;
        }
        return std::nullopt;
    }

    void RuntimeUiTree::PointerMove(const float x, const float y)
    {
        const auto hit = HitTest(x, y).value_or(RuntimeUiElementId{});
        if (hit == m_Impl->Hovered)
            return;
        if (const auto old = m_Impl->Index(m_Impl->Hovered))
        {
            m_Impl->Nodes[*old].State.Hovered = false;
            m_Impl->Queue(
                {.Type = RuntimeUiEventType::PointerExit, .Target = m_Impl->Hovered, .PointerX = x, .PointerY = y});
        }
        m_Impl->Hovered = hit;
        if (const auto current = m_Impl->Index(hit))
        {
            m_Impl->Nodes[*current].State.Hovered = true;
            m_Impl->Queue({.Type = RuntimeUiEventType::PointerEnter, .Target = hit, .PointerX = x, .PointerY = y});
        }
    }

    void RuntimeUiTree::PointerButton(const float x, const float y, const RuntimeUiPointerButton button,
                                      const bool pressed)
    {
        PointerMove(x, y);
        const auto target = m_Impl->Hovered;
        if (pressed)
        {
            m_Impl->Pressed = target;
            if (const auto index = m_Impl->Index(target))
            {
                m_Impl->Nodes[*index].State.Pressed = true;
                (void)SetFocus(target);
                m_Impl->Queue({.Type = RuntimeUiEventType::PointerDown,
                               .Target = target,
                               .PointerX = x,
                               .PointerY = y,
                               .Button = button});
            }
            return;
        }
        const auto released = m_Impl->Pressed;
        if (const auto index = m_Impl->Index(released))
        {
            m_Impl->Nodes[*index].State.Pressed = false;
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
        m_Impl->Pressed = {};
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
        const auto current = std::ranges::find(focusable, m_Impl->Focused);
        const bool backwards = navigation == RuntimeUiNavigation::Previous || navigation == RuntimeUiNavigation::Left ||
                               navigation == RuntimeUiNavigation::Up;
        std::size_t index = current == focusable.end() ? 0 : static_cast<std::size_t>(current - focusable.begin());
        index = backwards ? (index + focusable.size() - 1) % focusable.size() : (index + 1) % focusable.size();
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
            m_Impl->Queue({.Type = RuntimeUiEventType::Blur, .Target = m_Impl->Focused});
        }
        m_Impl->Focused = element;
        if (next)
        {
            m_Impl->Nodes[*next].State.Focused = true;
            m_Impl->Queue({.Type = RuntimeUiEventType::Focus, .Target = element});
        }
        return true;
    }

    RuntimeUiElementId RuntimeUiTree::Focus() const noexcept { return m_Impl->Focused; }

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
            if (!m_Impl->Index(event.Target) || event.Type > RuntimeUiEventType::Cancel ||
                event.Button > RuntimeUiPointerButton::Middle || !std::isfinite(event.PointerX) ||
                !std::isfinite(event.PointerY))
            {
                throw std::invalid_argument("Runtime UI event checkpoint is invalid or stale.");
            }
        }
        m_Impl->Events.assign(events.begin(), events.end());
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
        return result;
    }
} // namespace Keire
