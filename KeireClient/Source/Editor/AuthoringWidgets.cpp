#include "KeireClient/Editor/AuthoringWidgets.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace
{
    [[nodiscard]] Keire::UiPosition Add(const Keire::UiPosition left, const Keire::UiPosition right) noexcept
    {
        return {left.X + right.X, left.Y + right.Y};
    }

    [[nodiscard]] Keire::UiPosition Subtract(const Keire::UiPosition left, const Keire::UiPosition right) noexcept
    {
        return {left.X - right.X, left.Y - right.Y};
    }

    [[nodiscard]] Keire::UiPosition Scale(const Keire::UiPosition value, const float scale) noexcept
    {
        return {value.X * scale, value.Y * scale};
    }

    [[nodiscard]] Keire::UiColor ScaleColor(const Keire::UiColor color, const float scale, const float alpha) noexcept
    {
        return {std::clamp(color.Red * scale, 0.0F, 1.0F), std::clamp(color.Green * scale, 0.0F, 1.0F),
                std::clamp(color.Blue * scale, 0.0F, 1.0F), alpha};
    }

    [[nodiscard]] Keire::UiPosition BezierPoint(const Keire::UiPosition start, const Keire::UiPosition first,
                                                const Keire::UiPosition second, const Keire::UiPosition end,
                                                const float amount) noexcept
    {
        const float inverse = 1.0F - amount;
        return {inverse * inverse * inverse * start.X + 3.0F * inverse * inverse * amount * first.X +
                    3.0F * inverse * amount * amount * second.X + amount * amount * amount * end.X,
                inverse * inverse * inverse * start.Y + 3.0F * inverse * inverse * amount * first.Y +
                    3.0F * inverse * amount * amount * second.Y + amount * amount * amount * end.Y};
    }
} // namespace

namespace KeireEditor
{
    StableNodeId StableNodeGraphIdMap::Assign(const Keire::AssetId source, StableNodeId preferred)
    {
        if (const auto existing = std::ranges::find(m_Assignments, source, &decltype(m_Assignments)::value_type::first);
            existing != m_Assignments.end())
            return existing->second;

        if (preferred == 0)
            preferred = 1;
        while (std::ranges::find(m_Used, preferred) != m_Used.end())
        {
            ++preferred;
            if (preferred == 0)
                preferred = 1;
        }
        m_Assignments.emplace_back(source, preferred);
        m_Used.push_back(preferred);
        return preferred;
    }

    std::optional<StableNodeId> StableNodeGraphIdMap::Find(const Keire::AssetId source) const noexcept
    {
        const auto found = std::ranges::find(m_Assignments, source, &decltype(m_Assignments)::value_type::first);
        if (found == m_Assignments.end())
            return std::nullopt;
        return found->second;
    }

    void StableNodeGraphCanvas::Validate(const std::span<const NodeGraphNode> nodes,
                                         const std::span<const NodeGraphConnection> connections)
    {
        std::unordered_set<StableNodeId> nodeIds;
        nodeIds.reserve(nodes.size());
        for (const auto& node : nodes)
        {
            if (node.Id == 0)
                throw std::invalid_argument("Node graph IDs must be non-zero.");
            if (!nodeIds.emplace(node.Id).second)
                throw std::invalid_argument("Node graph IDs must be unique.");
            if (node.Label.empty() || !std::isfinite(node.Position.X) || !std::isfinite(node.Position.Y) ||
                !std::isfinite(node.Size.X) || !std::isfinite(node.Size.Y) || node.Size.X < 40.0F ||
                node.Size.Y < 24.0F)
                throw std::invalid_argument("Node graph nodes require a label, finite position, and usable size.");
        }

        std::unordered_set<StableNodeId> connectionIds;
        connectionIds.reserve(connections.size());
        for (const auto& connection : connections)
        {
            if (connection.Id == 0 || !connectionIds.emplace(connection.Id).second)
                throw std::invalid_argument("Node graph connection IDs must be non-zero and unique.");
            if (!nodeIds.contains(connection.Source) || !nodeIds.contains(connection.Target))
                throw std::invalid_argument("Node graph connections must reference existing nodes.");
        }
    }

    NodeGraphCanvasResult StableNodeGraphCanvas::Draw(Keire::UiFrame& ui, const std::string_view id,
                                                      const std::span<NodeGraphNode> nodes,
                                                      const std::span<const NodeGraphConnection> connections,
                                                      const bool editable)
    {
        Validate(nodes, connections);
        NodeGraphCanvasResult result;
        const auto available = ui.ContentAvailable();
        const Keire::UiSize size{std::max(available.Width, 120.0F), std::max(available.Height, 80.0F)};
        (void)ui.InvisibleButton(id, size);
        const auto canvas = ui.LastItemRect();
        const auto pointer = ui.PointerState();

        ui.DrawFilledRectangle(canvas, {0.055F, 0.06F, 0.073F, 1.0F}, 4.0F);
        const float minorGridStep = 24.0F * m_Zoom;
        if (minorGridStep >= 8.0F)
        {
            const float xOffset = std::fmod(m_Pan.X * m_Zoom, minorGridStep);
            const float yOffset = std::fmod(m_Pan.Y * m_Zoom, minorGridStep);
            for (float x = canvas.Minimum.X + xOffset; x < canvas.Maximum.X; x += minorGridStep)
                ui.DrawLine({x, canvas.Minimum.Y}, {x, canvas.Maximum.Y}, {0.095F, 0.105F, 0.125F, 1.0F});
            for (float y = canvas.Minimum.Y + yOffset; y < canvas.Maximum.Y; y += minorGridStep)
                ui.DrawLine({canvas.Minimum.X, y}, {canvas.Maximum.X, y}, {0.095F, 0.105F, 0.125F, 1.0F});

            const float majorGridStep = minorGridStep * 4.0F;
            const float majorXOffset = std::fmod(m_Pan.X * m_Zoom, majorGridStep);
            const float majorYOffset = std::fmod(m_Pan.Y * m_Zoom, majorGridStep);
            for (float x = canvas.Minimum.X + majorXOffset; x < canvas.Maximum.X; x += majorGridStep)
                ui.DrawLine({x, canvas.Minimum.Y}, {x, canvas.Maximum.Y}, {0.13F, 0.145F, 0.175F, 1.0F});
            for (float y = canvas.Minimum.Y + majorYOffset; y < canvas.Maximum.Y; y += majorGridStep)
                ui.DrawLine({canvas.Minimum.X, y}, {canvas.Maximum.X, y}, {0.13F, 0.145F, 0.175F, 1.0F});
        }
        ui.DrawRectangle(canvas, {0.18F, 0.2F, 0.24F, 1.0F}, 1.0F, 4.0F);

        if (ui.LastItemState().Hovered)
        {
            if (pointer.Wheel != 0.0F)
            {
                const auto before = Scale(Subtract(pointer.Position, canvas.Minimum), 1.0F / m_Zoom);
                const float nextZoom = std::clamp(m_Zoom * std::pow(1.1F, pointer.Wheel), 0.35F, 2.5F);
                const auto after = Scale(Subtract(pointer.Position, canvas.Minimum), 1.0F / nextZoom);
                m_Pan.X += after.X - before.X;
                m_Pan.Y += after.Y - before.Y;
                m_Zoom = nextZoom;
            }
            if (pointer.MiddleDown)
            {
                m_Pan.X += pointer.Delta.X / m_Zoom;
                m_Pan.Y += pointer.Delta.Y / m_Zoom;
            }
            if (pointer.LeftPressed)
            {
                m_Selection.reset();
                result.BackgroundActivated = true;
            }
        }

        if (m_Dragging)
        {
            const auto dragged = std::ranges::find(nodes, *m_Dragging, &NodeGraphNode::Id);
            if (dragged == nodes.end())
            {
                m_Dragging.reset();
                m_DragMoved = false;
            }
            else
            {
                if (editable && pointer.LeftDown)
                {
                    m_DragPosition.X += pointer.Delta.X / m_Zoom;
                    m_DragPosition.Y += pointer.Delta.Y / m_Zoom;
                    result.MovedNode = dragged->Id;
                    result.Changed = pointer.Delta.X != 0.0F || pointer.Delta.Y != 0.0F;
                    m_DragMoved = m_DragMoved || result.Changed;
                }
                dragged->Position = m_DragPosition;
            }
        }

        std::unordered_map<StableNodeId, const NodeGraphNode*> lookup;
        lookup.reserve(nodes.size());
        for (const auto& node : nodes)
            lookup.emplace(node.Id, &node);
        if (m_Dragging && !lookup.contains(*m_Dragging))
        {
            m_Dragging.reset();
            m_DragMoved = false;
        }
        for (const auto& connection : connections)
        {
            const auto& source = *lookup.at(connection.Source);
            const auto& target = *lookup.at(connection.Target);
            const auto sourcePosition =
                ToScreen({source.Position.X + source.Size.X, source.Position.Y + source.Size.Y * 0.5F}, canvas);
            const auto targetPosition = ToScreen({target.Position.X, target.Position.Y + target.Size.Y * 0.5F}, canvas);
            const auto distance = std::max(std::abs(targetPosition.X - sourcePosition.X) * 0.5F, 24.0F);
            const Keire::UiPosition first{sourcePosition.X + distance, sourcePosition.Y};
            const Keire::UiPosition second{targetPosition.X - distance, targetPosition.Y};
            constexpr int segmentCount = 20;
            const auto drawConnection = [&](const Keire::UiColor color, const float thickness)
            {
                Keire::UiPosition previous = sourcePosition;
                for (int segment = 1; segment <= segmentCount; ++segment)
                {
                    const float amount = static_cast<float>(segment) / static_cast<float>(segmentCount);
                    const auto current = BezierPoint(sourcePosition, first, second, targetPosition, amount);
                    ui.DrawLine(previous, current, color, thickness);
                    previous = current;
                }
            };
            drawConnection({0.015F, 0.02F, 0.03F, 0.8F}, 5.0F);
            drawConnection({0.42F, 0.62F, 0.9F, 1.0F}, 2.0F);

            if (!connection.Label.empty())
            {
                constexpr float labelFontSize = 11.0F;
                const auto labelPosition = BezierPoint(sourcePosition, first, second, targetPosition, 0.5F);
                const auto labelSize = ui.MeasureText(connection.Label, labelFontSize);
                const Keire::UiItemRect labelRectangle{
                    {labelPosition.X - labelSize.Width * 0.5F - 5.0F, labelPosition.Y - labelSize.Height * 0.5F - 2.0F},
                    {labelPosition.X + labelSize.Width * 0.5F + 5.0F,
                     labelPosition.Y + labelSize.Height * 0.5F + 2.0F}};
                ui.DrawFilledRectangle(labelRectangle, {0.075F, 0.085F, 0.105F, 0.96F}, 3.0F);
                ui.DrawRectangle(labelRectangle, {0.24F, 0.31F, 0.42F, 1.0F}, 1.0F, 3.0F);
                ui.DrawOverlayText({labelRectangle.Minimum.X + 5.0F, labelRectangle.Minimum.Y + 2.0F},
                                   {0.68F, 0.76F, 0.9F, 1.0F}, connection.Label, labelFontSize, canvas);
            }
        }

        for (auto iterator = nodes.rbegin(); iterator != nodes.rend(); ++iterator)
        {
            auto& node = *iterator;
            const auto minimum = ToScreen(node.Position, canvas);
            const Keire::UiSize scaledSize{node.Size.X * m_Zoom, node.Size.Y * m_Zoom};
            const Keire::UiItemRect rectangle{minimum, Add(minimum, {scaledSize.Width, scaledSize.Height})};
            const bool hovered = rectangle.Contains(pointer.Position);
            const bool selected = m_Selection && *m_Selection == node.Id;
            const Keire::UiItemRect shadow{{rectangle.Minimum.X + 4.0F, rectangle.Minimum.Y + 5.0F},
                                           {rectangle.Maximum.X + 4.0F, rectangle.Maximum.Y + 5.0F}};
            ui.DrawFilledRectangle(shadow, {0.0F, 0.0F, 0.0F, 0.38F}, 7.0F);
            ui.DrawFilledRectangle(rectangle, ScaleColor(node.Color, 0.48F, 1.0F), 7.0F);

            const float authoredHeaderHeight = node.Subtitle.empty() ? 34.0F : 48.0F;
            const float headerHeight = std::min(node.Size.Y * m_Zoom, authoredHeaderHeight * m_Zoom);
            const Keire::UiItemRect header{rectangle.Minimum,
                                           {rectangle.Maximum.X, rectangle.Minimum.Y + headerHeight}};
            ui.DrawFilledRectangle(header, ScaleColor(node.Color, hovered ? 1.12F : 1.0F, 1.0F), 7.0F);

            const float accentWidth = std::clamp(3.0F * m_Zoom, 2.0F, 4.0F);
            const Keire::UiItemRect accent{{rectangle.Minimum.X, rectangle.Minimum.Y + 6.0F},
                                           {rectangle.Minimum.X + accentWidth, rectangle.Maximum.Y - 6.0F}};
            ui.DrawFilledRectangle(accent, ScaleColor(node.Color, 1.65F, 1.0F), 2.0F);

            const auto borderColor = selected  ? Keire::UiColor{0.35F, 0.7F, 1.0F, 1.0F}
                                     : hovered ? Keire::UiColor{0.4F, 0.46F, 0.56F, 1.0F}
                                               : Keire::UiColor{0.24F, 0.27F, 0.33F, 1.0F};
            ui.DrawRectangle(rectangle, borderColor, selected ? 3.0F : 1.0F, 7.0F);

            const float portRadius = std::clamp(4.5F * m_Zoom, 2.5F, 5.0F);
            const float portY = rectangle.Minimum.Y + scaledSize.Height * 0.5F;
            ui.DrawFilledCircle({rectangle.Minimum.X, portY}, portRadius, {0.32F, 0.6F, 0.9F, 1.0F});
            ui.DrawCircle({rectangle.Minimum.X, portY}, portRadius, {0.72F, 0.85F, 1.0F, 1.0F});
            ui.DrawFilledCircle({rectangle.Maximum.X, portY}, portRadius, {0.32F, 0.6F, 0.9F, 1.0F});
            ui.DrawCircle({rectangle.Maximum.X, portY}, portRadius, {0.72F, 0.85F, 1.0F, 1.0F});

            const float labelX = minimum.X + 12.0F;
            ui.DrawOverlayText({labelX, minimum.Y + 8.0F}, {0.96F, 0.97F, 0.99F, 1.0F}, node.Label, 0.0F, rectangle);
            if (!node.Subtitle.empty())
                ui.DrawOverlayText({labelX, minimum.Y + 27.0F}, {0.68F, 0.72F, 0.8F, 1.0F}, node.Subtitle, 11.0F,
                                   rectangle);

            if (hovered && pointer.LeftPressed)
            {
                m_Selection = node.Id;
                m_Dragging = editable ? std::optional(node.Id) : std::nullopt;
                m_DragPosition = node.Position;
                m_DragMoved = false;
                result.ActivatedNode = node.Id;
                result.BackgroundActivated = false;
            }
        }
        if (pointer.LeftReleased)
        {
            if (m_Dragging && m_DragMoved)
                result.MoveCompletedNode = m_Dragging;
            m_Dragging.reset();
            m_DragMoved = false;
        }
        return result;
    }

    void StableNodeGraphCanvas::Focus(const std::span<const NodeGraphNode> nodes, const Keire::UiSize canvasSize)
    {
        if (nodes.empty())
        {
            m_Pan = {};
            m_Zoom = 1.0F;
            return;
        }
        Keire::Vector2 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        Keire::Vector2 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        for (const auto& node : nodes)
        {
            minimum.X = std::min(minimum.X, node.Position.X);
            minimum.Y = std::min(minimum.Y, node.Position.Y);
            maximum.X = std::max(maximum.X, node.Position.X + node.Size.X);
            maximum.Y = std::max(maximum.Y, node.Position.Y + node.Size.Y);
        }
        const float width = std::max(maximum.X - minimum.X, 1.0F);
        const float height = std::max(maximum.Y - minimum.Y, 1.0F);
        m_Zoom =
            std::clamp(std::min((canvasSize.Width - 48.0F) / width, (canvasSize.Height - 48.0F) / height), 0.35F, 2.5F);
        m_Pan = {(canvasSize.Width / m_Zoom - width) * 0.5F - minimum.X,
                 (canvasSize.Height / m_Zoom - height) * 0.5F - minimum.Y};
    }

    Keire::UiPosition StableNodeGraphCanvas::ToScreen(const Keire::Vector2 position,
                                                      const Keire::UiItemRect canvas) const noexcept
    {
        return {canvas.Minimum.X + (position.X + m_Pan.X) * m_Zoom, canvas.Minimum.Y + (position.Y + m_Pan.Y) * m_Zoom};
    }

    bool AuthoringValueEditors::Curve(Keire::UiFrame& ui, const std::string_view label, Keire::Curve1D& value,
                                      const float minimumTime, const float maximumTime)
    {
        if (!std::isfinite(minimumTime) || !std::isfinite(maximumTime) || maximumTime <= minimumTime)
            throw std::invalid_argument("Curve editor time range must be finite and increasing.");
        ui.Text(label);
        std::vector<Keire::CurveKey> keys(value.Keys().begin(), value.Keys().end());
        bool changed = false;
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(index));
            double time = keys[index].Time;
            double keyValue = keys[index].Value;
            const float lower = index == 0 ? minimumTime : std::nextafter(keys[index - 1].Time, maximumTime);
            const float upper =
                index + 1 == keys.size() ? maximumTime : std::nextafter(keys[index + 1].Time, minimumTime);
            if (ui.DragScalar("Time", time, 0.01, lower, upper))
            {
                keys[index].Time = static_cast<float>(time);
                changed = true;
            }
            ui.SameLine();
            if (ui.DragScalar("Value", keyValue, 0.01))
            {
                keys[index].Value = static_cast<float>(keyValue);
                changed = true;
            }
        }
        if (changed)
            value.SetKeys(std::move(keys));
        return changed;
    }

    bool AuthoringValueEditors::Gradient(Keire::UiFrame& ui, const std::string_view label, Keire::ColorGradient& value)
    {
        ui.Text(label);
        std::vector<Keire::ColorGradientKey> keys(value.Keys().begin(), value.Keys().end());
        bool changed = false;
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(index));
            double time = keys[index].Time;
            const float lower = index == 0 ? 0.0F : std::nextafter(keys[index - 1].Time, 1.0F);
            const float upper = index + 1 == keys.size() ? 1.0F : std::nextafter(keys[index + 1].Time, 0.0F);
            if (ui.DragScalar("Time", time, 0.01, lower, upper))
            {
                keys[index].Time = static_cast<float>(time);
                changed = true;
            }
            ui.SameLine();
            Keire::UiColor color{keys[index].Value.Red, keys[index].Value.Green, keys[index].Value.Blue,
                                 keys[index].Value.Alpha};
            if (ui.ColorEdit("Color", color))
            {
                keys[index].Value = {color.Red, color.Green, color.Blue, color.Alpha};
                changed = true;
            }
        }
        if (changed)
            value.SetKeys(std::move(keys));
        return changed;
    }
} // namespace KeireEditor
