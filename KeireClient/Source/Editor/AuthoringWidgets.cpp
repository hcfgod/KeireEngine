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
} // namespace

namespace KeireEditor
{
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
            if (connection.Source == connection.Target || !nodeIds.contains(connection.Source) ||
                !nodeIds.contains(connection.Target))
                throw std::invalid_argument("Node graph connections must join two existing, distinct nodes.");
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

        ui.DrawFilledRectangle(canvas, {0.075F, 0.08F, 0.095F, 1.0F}, 4.0F);
        const float gridStep = 32.0F * m_Zoom;
        if (gridStep >= 8.0F)
        {
            const float xOffset = std::fmod(m_Pan.X * m_Zoom, gridStep);
            const float yOffset = std::fmod(m_Pan.Y * m_Zoom, gridStep);
            for (float x = canvas.Minimum.X + xOffset; x < canvas.Maximum.X; x += gridStep)
                ui.DrawLine({x, canvas.Minimum.Y}, {x, canvas.Maximum.Y}, {0.13F, 0.14F, 0.17F, 1.0F});
            for (float y = canvas.Minimum.Y + yOffset; y < canvas.Maximum.Y; y += gridStep)
                ui.DrawLine({canvas.Minimum.X, y}, {canvas.Maximum.X, y}, {0.13F, 0.14F, 0.17F, 1.0F});
        }

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

        std::unordered_map<StableNodeId, const NodeGraphNode*> lookup;
        lookup.reserve(nodes.size());
        for (const auto& node : nodes)
            lookup.emplace(node.Id, &node);
        for (const auto& connection : connections)
        {
            const auto& source = *lookup.at(connection.Source);
            const auto& target = *lookup.at(connection.Target);
            const auto sourcePosition =
                ToScreen({source.Position.X + source.Size.X, source.Position.Y + source.Size.Y * 0.5F}, canvas);
            const auto targetPosition = ToScreen({target.Position.X, target.Position.Y + target.Size.Y * 0.5F}, canvas);
            const auto distance = std::max(std::abs(targetPosition.X - sourcePosition.X) * 0.5F, 24.0F);
            Keire::UiPosition previous = sourcePosition;
            constexpr int segmentCount = 20;
            for (int segment = 1; segment <= segmentCount; ++segment)
            {
                const float amount = static_cast<float>(segment) / static_cast<float>(segmentCount);
                const float inverse = 1.0F - amount;
                const Keire::UiPosition first{sourcePosition.X + distance, sourcePosition.Y};
                const Keire::UiPosition second{targetPosition.X - distance, targetPosition.Y};
                const Keire::UiPosition current{
                    inverse * inverse * inverse * sourcePosition.X + 3.0F * inverse * inverse * amount * first.X +
                        3.0F * inverse * amount * amount * second.X + amount * amount * amount * targetPosition.X,
                    inverse * inverse * inverse * sourcePosition.Y + 3.0F * inverse * inverse * amount * first.Y +
                        3.0F * inverse * amount * amount * second.Y + amount * amount * amount * targetPosition.Y};
                ui.DrawLine(previous, current, {0.48F, 0.55F, 0.7F, 1.0F}, 2.0F);
                previous = current;
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
            ui.DrawFilledRectangle(rectangle, node.Color, 6.0F);
            ui.DrawRectangle(rectangle,
                             selected ? Keire::UiColor{0.35F, 0.7F, 1.0F, 1.0F}
                                      : Keire::UiColor{0.28F, 0.31F, 0.37F, 1.0F},
                             selected ? 3.0F : 1.0F, 6.0F);
            ui.DrawOverlayText({minimum.X + 12.0F, minimum.Y + 10.0F}, {0.95F, 0.96F, 0.98F, 1.0F}, node.Label);

            if (hovered && pointer.LeftPressed)
            {
                m_Selection = node.Id;
                m_Dragging = editable ? std::optional(node.Id) : std::nullopt;
                result.ActivatedNode = node.Id;
                result.BackgroundActivated = false;
            }
            if (editable && m_Dragging && *m_Dragging == node.Id && pointer.LeftDown)
            {
                node.Position.X += pointer.Delta.X / m_Zoom;
                node.Position.Y += pointer.Delta.Y / m_Zoom;
                result.MovedNode = node.Id;
                result.Changed = pointer.Delta.X != 0.0F || pointer.Delta.Y != 0.0F;
            }
        }
        if (pointer.LeftReleased)
            m_Dragging.reset();
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
