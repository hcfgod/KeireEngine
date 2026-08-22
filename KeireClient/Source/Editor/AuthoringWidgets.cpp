#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/GraphComments.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    constexpr float PinRowHeight = 22.0F;
    constexpr float PinBottomPadding = 8.0F;
    constexpr float BlockHeaderHeight = 28.0F;
    constexpr float BlockSpacing = 6.0F;
    constexpr float BlockInset = 8.0F;
    constexpr float ConnectionHitRadius = 7.0F;
    constexpr int ConnectionSegmentCount = 28;

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

    [[nodiscard]] float SquaredDistance(const Keire::UiPosition first, const Keire::UiPosition second) noexcept
    {
        const float x = first.X - second.X;
        const float y = first.Y - second.Y;
        return x * x + y * y;
    }

    [[nodiscard]] float SquaredDistanceToSegment(const Keire::UiPosition point, const Keire::UiPosition first,
                                                 const Keire::UiPosition second) noexcept
    {
        const float segmentX = second.X - first.X;
        const float segmentY = second.Y - first.Y;
        const float lengthSquared = segmentX * segmentX + segmentY * segmentY;
        if (lengthSquared <= std::numeric_limits<float>::epsilon())
            return SquaredDistance(point, first);
        const float amount =
            std::clamp(((point.X - first.X) * segmentX + (point.Y - first.Y) * segmentY) / lengthSquared, 0.0F, 1.0F);
        return SquaredDistance(point, {first.X + segmentX * amount, first.Y + segmentY * amount});
    }

    [[nodiscard]] float HeaderHeight(const KeireEditor::NodeGraphNode& node) noexcept
    {
        return node.Subtitle.empty() ? 34.0F : 48.0F;
    }

    [[nodiscard]] float PinAreaHeight(const std::span<const KeireEditor::NodeGraphPin> pins) noexcept
    {
        const auto inputCount = static_cast<std::size_t>(
            std::ranges::count(pins, KeireEditor::NodeGraphPinDirection::Input, &KeireEditor::NodeGraphPin::Direction));
        const auto outputCount = static_cast<std::size_t>(std::ranges::count(
            pins, KeireEditor::NodeGraphPinDirection::Output, &KeireEditor::NodeGraphPin::Direction));
        return pins.empty() ? 0.0F
                            : static_cast<float>(std::max(inputCount, outputCount)) * PinRowHeight + PinBottomPadding;
    }

    [[nodiscard]] float BlockHeight(const KeireEditor::NodeGraphBlockRow& block) noexcept
    {
        return BlockHeaderHeight + PinAreaHeight(block.Pins);
    }

    [[nodiscard]] float BlockAreaOffset(const KeireEditor::NodeGraphNode& node) noexcept
    {
        return HeaderHeight(node) + PinAreaHeight(node.Pins);
    }

    [[nodiscard]] Keire::UiSize EffectiveNodeSize(const KeireEditor::NodeGraphNode& node) noexcept
    {
        float contentHeight = BlockAreaOffset(node);
        if (!node.Blocks.empty())
        {
            contentHeight += BlockSpacing;
            for (const auto& block : node.Blocks)
                contentHeight += BlockHeight(block) + BlockSpacing;
        }
        return {node.Size.X, std::max(node.Size.Y, contentHeight)};
    }

    struct DrawnNode
    {
        KeireEditor::StableNodeId Id = 0;
        Keire::UiItemRect Rectangle;
    };

    struct DrawnPin
    {
        KeireEditor::NodeGraphPinAddress Address;
        const KeireEditor::NodeGraphPin* Pin = nullptr;
        Keire::UiPosition Position;
    };

    struct DrawnBlock
    {
        KeireEditor::NodeGraphBlockAddress Address;
        const KeireEditor::NodeGraphBlockRow* Block = nullptr;
        std::size_t Index = 0;
        Keire::UiItemRect Rectangle;
    };

    struct DrawnConnectionSegment
    {
        Keire::UiPosition Start;
        Keire::UiPosition FirstControl;
        Keire::UiPosition SecondControl;
        Keire::UiPosition End;
    };

    struct DrawnConnection
    {
        const KeireEditor::NodeGraphConnection* Connection = nullptr;
        Keire::UiColor Color;
        std::vector<DrawnConnectionSegment> Segments;
        std::vector<Keire::UiPosition> Reroutes;
    };

    [[nodiscard]] std::pair<Keire::UiPosition, Keire::UiPosition> BezierControls(const Keire::UiPosition start,
                                                                                 const Keire::UiPosition end,
                                                                                 const bool startPointsRight,
                                                                                 const bool endPointsLeft) noexcept
    {
        const float distance = std::max(std::abs(end.X - start.X) * 0.5F, 32.0F);
        return {{start.X + (startPointsRight ? distance : -distance), start.Y},
                {end.X + (endPointsLeft ? -distance : distance), end.Y}};
    }

    void DrawBezier(Keire::UiFrame& ui, const Keire::UiPosition start, const Keire::UiPosition first,
                    const Keire::UiPosition second, const Keire::UiPosition end, const Keire::UiColor color,
                    const float thickness)
    {
        Keire::UiPosition previous = start;
        for (int segment = 1; segment <= ConnectionSegmentCount; ++segment)
        {
            const float amount = static_cast<float>(segment) / static_cast<float>(ConnectionSegmentCount);
            const auto current = BezierPoint(start, first, second, end, amount);
            ui.DrawLine(previous, current, color, thickness);
            previous = current;
        }
    }

    [[nodiscard]] float DistanceToBezierSquared(const DrawnConnectionSegment& connection,
                                                const Keire::UiPosition point) noexcept
    {
        float distance = std::numeric_limits<float>::max();
        Keire::UiPosition previous = connection.Start;
        for (int segment = 1; segment <= ConnectionSegmentCount; ++segment)
        {
            const float amount = static_cast<float>(segment) / static_cast<float>(ConnectionSegmentCount);
            const auto current = BezierPoint(connection.Start, connection.FirstControl, connection.SecondControl,
                                             connection.End, amount);
            distance = std::min(distance, SquaredDistanceToSegment(point, previous, current));
            previous = current;
        }
        return distance;
    }

    [[nodiscard]] std::pair<float, std::size_t> DistanceToConnectionSquared(const DrawnConnection& connection,
                                                                            const Keire::UiPosition point) noexcept
    {
        float distance = std::numeric_limits<float>::max();
        std::size_t closestSegment = 0;
        for (std::size_t index = 0; index < connection.Segments.size(); ++index)
        {
            const float candidate = DistanceToBezierSquared(connection.Segments[index], point);
            if (candidate < distance)
            {
                distance = candidate;
                closestSegment = index;
            }
        }
        return {distance, closestSegment};
    }

    [[nodiscard]] const KeireEditor::NodeGraphNode* FindNode(const std::span<const KeireEditor::NodeGraphNode> nodes,
                                                             const KeireEditor::StableNodeId id) noexcept
    {
        const auto found = std::ranges::find(nodes, id, &KeireEditor::NodeGraphNode::Id);
        return found == nodes.end() ? nullptr : std::addressof(*found);
    }

    [[nodiscard]] const KeireEditor::NodeGraphPin* FindPin(const std::span<const KeireEditor::NodeGraphNode> nodes,
                                                           const KeireEditor::NodeGraphPinAddress address) noexcept
    {
        const auto* node = FindNode(nodes, address.Node);
        if (!node)
            return nullptr;
        if (address.Block)
        {
            const auto block = std::ranges::find(node->Blocks, address.Block, &KeireEditor::NodeGraphBlockRow::Id);
            if (block == node->Blocks.end())
                return nullptr;
            const auto found = std::ranges::find(block->Pins, address.Pin, &KeireEditor::NodeGraphPin::Id);
            return found == block->Pins.end() ? nullptr : std::addressof(*found);
        }
        const auto found = std::ranges::find(node->Pins, address.Pin, &KeireEditor::NodeGraphPin::Id);
        return found == node->Pins.end() ? nullptr : std::addressof(*found);
    }

    [[nodiscard]] std::optional<KeireEditor::NodeGraphConnectionRequest>
    ConnectionBetween(const std::span<const KeireEditor::NodeGraphNode> nodes,
                      const KeireEditor::NodeGraphPinAddress first,
                      const KeireEditor::NodeGraphPinAddress second) noexcept
    {
        const auto* firstPin = FindPin(nodes, first);
        const auto* secondPin = FindPin(nodes, second);
        if (!firstPin || !secondPin)
            return std::nullopt;
        if (firstPin->Direction == KeireEditor::NodeGraphPinDirection::Output &&
            secondPin->Direction == KeireEditor::NodeGraphPinDirection::Input)
        {
            return KeireEditor::NodeGraphConnectionRequest{first.Node, first.Pin,   second.Node,
                                                           second.Pin, first.Block, second.Block};
        }
        if (firstPin->Direction == KeireEditor::NodeGraphPinDirection::Input &&
            secondPin->Direction == KeireEditor::NodeGraphPinDirection::Output)
        {
            return KeireEditor::NodeGraphConnectionRequest{second.Node, second.Pin,   first.Node,
                                                           first.Pin,   second.Block, first.Block};
        }
        return KeireEditor::NodeGraphConnectionRequest{first.Node, first.Pin,   second.Node,
                                                       second.Pin, first.Block, second.Block};
    }
} // namespace

namespace KeireEditor
{
    NodeGraphCanvasDetail StableNodeGraphCanvas::DetailForZoom(const float zoom) noexcept
    {
        const float boundedZoom = std::isfinite(zoom) ? std::clamp(zoom, 0.35F, 2.5F) : 1.0F;
        return {.NodeSubtitle = boundedZoom >= 0.65F,
                .BlockLabels = boundedZoom >= 0.5F,
                .PinLabels = boundedZoom >= 0.5F,
                .ConnectionLabels = boundedZoom >= 0.55F};
    }

    NodeGraphCanvasResult StableNodeGraphCanvas::Draw(Keire::UiFrame& ui, const std::string_view id,
                                                      const std::span<NodeGraphNode> nodes,
                                                      const std::span<const NodeGraphConnection> connections,
                                                      const bool editable)
    {
        return Draw(ui, id, nodes, connections,
                    NodeGraphCanvasOptions{.Editable = editable, .InteractiveConnections = editable});
    }

    NodeGraphCanvasResult StableNodeGraphCanvas::Draw(Keire::UiFrame& ui, const std::string_view id,
                                                      const std::span<NodeGraphNode> nodes,
                                                      const std::span<const NodeGraphConnection> connections,
                                                      const NodeGraphCanvasOptions& options)
    {
        Validate(nodes, connections);
        NodeGraphCanvasResult result;
        const auto available = ui.ContentAvailable();
        const Keire::UiSize size{std::max(available.Width, 120.0F), std::max(available.Height, 80.0F)};
        (void)ui.InvisibleButton(id, size);
        const auto canvas = ui.LastItemRect();
        const auto canvasClip = ui.PushClipRect(canvas);
        const auto pointer = ui.PointerState();
        const auto canvasItem = ui.LastItemState();
        const bool canvasHovered = canvasItem.Hovered;
        if (canvasHovered)
            ui.CapturePointerWheel();
        result.PointerGraphPosition = ToGraph(pointer.Position, canvas);

        ui.DrawFilledRectangle(canvas, {0.055F, 0.06F, 0.073F, 1.0F}, 4.0F);
        if (canvasHovered && !m_DraggingPin)
        {
            if (pointer.Wheel != 0.0F)
            {
                const auto before = Scale(Subtract(pointer.Position, canvas.Minimum), 1.0F / m_Zoom);
                const float nextZoom = std::clamp(m_Zoom * std::pow(1.1F, pointer.Wheel), 0.35F, 2.5F);
                const auto after = Scale(Subtract(pointer.Position, canvas.Minimum), 1.0F / nextZoom);
                m_Pan.X += after.X - before.X;
                m_Pan.Y += after.Y - before.Y;
                m_Zoom = nextZoom;
                result.PointerGraphPosition = ToGraph(pointer.Position, canvas);
            }
            if (pointer.MiddleDown)
            {
                m_Pan.X += pointer.Delta.X / m_Zoom;
                m_Pan.Y += pointer.Delta.Y / m_Zoom;
                result.PointerGraphPosition = ToGraph(pointer.Position, canvas);
            }
        }
        const auto detail = DetailForZoom(m_Zoom);

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

        if (m_DraggingComment)
        {
            const auto comment = std::ranges::find(options.Comments, *m_DraggingComment, &NodeGraphComment::Id);
            if (comment == options.Comments.end())
                m_DraggingComment.reset();
            else if (options.Editable && pointer.LeftDown)
            {
                const Keire::Vector2 delta{pointer.Delta.X / m_Zoom, pointer.Delta.Y / m_Zoom};
                m_CommentDragPosition.X += delta.X;
                m_CommentDragPosition.Y += delta.Y;
                comment->Position = m_CommentDragPosition;
                for (auto& [node, position] : m_CommentMemberPositions)
                {
                    position.X += delta.X;
                    position.Y += delta.Y;
                    if (const auto member = std::ranges::find(nodes, node, &NodeGraphNode::Id); member != nodes.end())
                        member->Position = position;
                }
                for (auto& [nested, position] : m_CommentMemberCommentPositions)
                {
                    position.X += delta.X;
                    position.Y += delta.Y;
                    if (const auto member = std::ranges::find(options.Comments, nested, &NodeGraphComment::Id);
                        member != options.Comments.end())
                        member->Position = position;
                }
                result.MovedComment = comment->Id;
                result.Changed = delta.X != 0.0F || delta.Y != 0.0F;
            }
        }
        if (m_ResizingComment)
        {
            const auto comment = std::ranges::find(options.Comments, *m_ResizingComment, &NodeGraphComment::Id);
            if (comment == options.Comments.end())
                m_ResizingComment.reset();
            else if (options.Editable && pointer.LeftDown)
            {
                m_CommentResize.X = std::max(120.0F, m_CommentResize.X + pointer.Delta.X / m_Zoom);
                m_CommentResize.Y = std::max(64.0F, m_CommentResize.Y + pointer.Delta.Y / m_Zoom);
                comment->Size = m_CommentResize;
                result.ResizedComment = comment->Id;
                result.Changed = true;
            }
        }
        std::unordered_map<StableNodeId, NodeGraphComment*> collapsedOwners;
        for (auto& comment : options.Comments)
        {
            comment.SummaryInputs = 0;
            comment.SummaryOutputs = 0;
            if (!comment.Collapsed)
                continue;
            bool collapsedAncestor = false;
            auto parent = comment.Parent;
            while (parent)
            {
                const auto found = std::ranges::find(options.Comments, *parent, &NodeGraphComment::Id);
                if (found == options.Comments.end())
                    break;
                collapsedAncestor = collapsedAncestor || found->Collapsed;
                parent = found->Parent;
            }
            if (collapsedAncestor)
                continue;

            std::vector<StableNodeId> pending(comment.Members.begin(), comment.Members.end());
            while (!pending.empty())
            {
                const auto member = pending.back();
                pending.pop_back();
                if (const auto nested = std::ranges::find(options.Comments, member, &NodeGraphComment::Id);
                    nested != options.Comments.end())
                    pending.insert(pending.end(), nested->Members.begin(), nested->Members.end());
                else
                    collapsedOwners.emplace(member, std::addressof(comment));
            }
        }
        for (const auto& connection : connections)
        {
            const auto source = collapsedOwners.find(connection.Source);
            const auto target = collapsedOwners.find(connection.Target);
            if (source != collapsedOwners.end() &&
                (target == collapsedOwners.end() || source->second != target->second))
                ++source->second->SummaryOutputs;
            if (target != collapsedOwners.end() &&
                (source == collapsedOwners.end() || source->second != target->second))
                ++target->second->SummaryInputs;
        }
        const auto commentLayer = DrawNodeGraphComments(ui, options.Comments, canvas, m_Pan, m_Zoom, pointer.Position);
        if (m_CommentSelection)
            if (const auto selected = std::ranges::find(options.Comments, *m_CommentSelection, &NodeGraphComment::Id);
                selected != options.Comments.end())
            {
                const auto minimum = ToScreen(selected->Position, canvas);
                const float collapsedHeight =
                    34.0F + static_cast<float>(std::max(selected->SummaryInputs, selected->SummaryOutputs)) * 20.0F;
                const float height = (selected->Collapsed ? collapsedHeight : selected->Size.Y) * m_Zoom;
                ui.DrawRectangle({minimum, {minimum.X + selected->Size.X * m_Zoom, minimum.Y + height}},
                                 {0.3F, 0.78F, 1.0F, 1.0F}, 2.5F, 6.0F);
            }

        if (m_Dragging)
        {
            if (std::ranges::find(nodes, *m_Dragging, &NodeGraphNode::Id) == nodes.end())
            {
                m_Dragging.reset();
                m_DragPositions.clear();
                m_DragMoved = false;
            }
            else
            {
                if (options.Editable && pointer.LeftDown)
                {
                    result.Changed = pointer.Delta.X != 0.0F || pointer.Delta.Y != 0.0F;
                    m_DragMoved = m_DragMoved || result.Changed;
                    for (auto& [node, position] : m_DragPositions)
                    {
                        position.X += pointer.Delta.X / m_Zoom;
                        position.Y += pointer.Delta.Y / m_Zoom;
                        result.MovedNodes.push_back(node);
                    }
                }
                result.MovedNode = m_Dragging;
                for (const auto& [node, position] : m_DragPositions)
                    if (const auto found = std::ranges::find(nodes, node, &NodeGraphNode::Id); found != nodes.end())
                        found->Position = position;
            }
        }

        std::unordered_map<StableNodeId, const NodeGraphNode*> nodeLookup;
        nodeLookup.reserve(nodes.size());
        for (const auto& node : nodes)
            nodeLookup.emplace(node.Id, std::addressof(node));

        std::erase_if(m_Selections, [&](const StableNodeId node) { return !nodeLookup.contains(node); });
        if (m_Selection && !nodeLookup.contains(*m_Selection))
            m_Selection = m_Selections.empty() ? std::nullopt : std::optional(m_Selections.back());
        if (m_CommentSelection &&
            std::ranges::find(options.Comments, *m_CommentSelection, &NodeGraphComment::Id) == options.Comments.end())
            m_CommentSelection.reset();
        const auto blockAvailable = [&](const NodeGraphBlockAddress address)
        {
            const auto* node = FindNode(nodes, address.Node);
            return node && std::ranges::find(node->Blocks, address.Block, &NodeGraphBlockRow::Id) != node->Blocks.end();
        };
        if (m_BlockSelection && !blockAvailable(*m_BlockSelection))
            m_BlockSelection.reset();
        if (m_DraggingBlock && !blockAvailable(*m_DraggingBlock))
            m_DraggingBlock.reset();
        if (m_ConnectionSelection &&
            std::ranges::find(connections, *m_ConnectionSelection, &NodeGraphConnection::Id) == connections.end())
        {
            m_ConnectionSelection.reset();
        }
        const auto rerouteAvailable = [&](const NodeGraphRerouteAddress address)
        {
            const auto found = std::ranges::find(connections, address.Connection, &NodeGraphConnection::Id);
            return found != connections.end() && address.Index < found->RoutingPoints.size();
        };
        if (m_RerouteSelection && !rerouteAvailable(*m_RerouteSelection))
            m_RerouteSelection.reset();
        if (m_DraggingReroute && !rerouteAvailable(*m_DraggingReroute))
            m_DraggingReroute.reset();
        if (!options.InteractiveConnections)
        {
            m_ConnectionSelection.reset();
            m_RerouteSelection.reset();
        }
        if (!options.EditableReroutes)
        {
            m_RerouteSelection.reset();
            m_DraggingReroute.reset();
        }
        if (m_DraggingPin && !FindPin(nodes, *m_DraggingPin))
        {
            m_DraggingPin.reset();
            result.ConnectionDragCancelled = true;
        }
        if (!options.Editable && m_DraggingPin)
        {
            m_DraggingPin.reset();
            result.ConnectionDragCancelled = true;
        }
        if (!options.Editable)
        {
            m_DraggingBlock.reset();
            m_DraggingReroute.reset();
        }

        if (m_DraggingReroute && pointer.LeftDown)
        {
            m_RerouteDragPosition = result.PointerGraphPosition;
            result.Changed = pointer.Delta.X != 0.0F || pointer.Delta.Y != 0.0F;
        }

        std::vector<DrawnNode> drawnNodes;
        std::vector<DrawnBlock> drawnBlocks;
        std::vector<DrawnPin> drawnPins;
        drawnNodes.reserve(nodes.size());
        for (const auto& node : nodes)
        {
            if (collapsedOwners.contains(node.Id))
                continue;
            const auto minimum = ToScreen(node.Position, canvas);
            const auto effectiveSize = EffectiveNodeSize(node);
            const Keire::UiSize scaledSize{effectiveSize.Width * m_Zoom, effectiveSize.Height * m_Zoom};
            drawnNodes.push_back({node.Id, {minimum, Add(minimum, {scaledSize.Width, scaledSize.Height})}});

            std::size_t inputIndex = 0;
            std::size_t outputIndex = 0;
            for (const auto& pin : node.Pins)
            {
                const std::size_t row = pin.Direction == NodeGraphPinDirection::Input ? inputIndex++ : outputIndex++;
                const float authoredY =
                    node.Position.Y + HeaderHeight(node) + (static_cast<float>(row) + 0.5F) * PinRowHeight;
                const float authoredX = pin.Direction == NodeGraphPinDirection::Input
                                            ? node.Position.X
                                            : node.Position.X + effectiveSize.Width;
                drawnPins.push_back(
                    {{node.Id, pin.Id, 0}, std::addressof(pin), ToScreen({authoredX, authoredY}, canvas)});
            }

            float blockY = node.Position.Y + BlockAreaOffset(node) + BlockSpacing;
            for (std::size_t blockIndex = 0; blockIndex < node.Blocks.size(); ++blockIndex)
            {
                const auto& block = node.Blocks[blockIndex];
                const auto blockMinimum = ToScreen({node.Position.X + BlockInset, blockY}, canvas);
                const auto blockMaximum =
                    ToScreen({node.Position.X + effectiveSize.Width - BlockInset, blockY + BlockHeight(block)}, canvas);
                drawnBlocks.push_back(
                    {{node.Id, block.Id}, std::addressof(block), blockIndex, {blockMinimum, blockMaximum}});

                inputIndex = 0;
                outputIndex = 0;
                for (const auto& pin : block.Pins)
                {
                    const std::size_t row =
                        pin.Direction == NodeGraphPinDirection::Input ? inputIndex++ : outputIndex++;
                    const float authoredY =
                        blockY + BlockHeaderHeight + (static_cast<float>(row) + 0.5F) * PinRowHeight;
                    const float authoredX = pin.Direction == NodeGraphPinDirection::Input
                                                ? node.Position.X + BlockInset
                                                : node.Position.X + effectiveSize.Width - BlockInset;
                    drawnPins.push_back(
                        {{node.Id, pin.Id, block.Id}, std::addressof(pin), ToScreen({authoredX, authoredY}, canvas)});
                }
                blockY += BlockHeight(block) + BlockSpacing;
            }
        }

        const auto findDrawnPin = [&drawnPins](const NodeGraphPinAddress address) -> const DrawnPin*
        {
            const auto found = std::ranges::find(drawnPins, address, &DrawnPin::Address);
            return found == drawnPins.end() ? nullptr : std::addressof(*found);
        };

        std::vector<DrawnConnection> drawnConnections;
        drawnConnections.reserve(connections.size());
        std::unordered_map<StableNodeId, std::size_t> collapsedInputIndices;
        std::unordered_map<StableNodeId, std::size_t> collapsedOutputIndices;
        std::vector<std::pair<Keire::UiPosition, Keire::UiColor>> summaryPins;
        const auto collapsedEndpoint = [&](NodeGraphComment& comment, const bool input)
        {
            auto& indices = input ? collapsedInputIndices : collapsedOutputIndices;
            const auto index = indices[comment.Id]++;
            return ToScreen({comment.Position.X + (input ? 0.0F : comment.Size.X),
                             comment.Position.Y + 34.0F + (static_cast<float>(index) + 0.5F) * 20.0F},
                            canvas);
        };
        for (const auto& connection : connections)
        {
            const auto sourceNode = nodeLookup.find(connection.Source);
            const auto targetNode = nodeLookup.find(connection.Target);
            if (sourceNode == nodeLookup.end() || targetNode == nodeLookup.end())
                continue;
            const auto sourceOwner = collapsedOwners.find(connection.Source);
            const auto targetOwner = collapsedOwners.find(connection.Target);
            if (sourceOwner != collapsedOwners.end() && targetOwner != collapsedOwners.end() &&
                sourceOwner->second == targetOwner->second)
                continue;

            Keire::UiPosition sourcePosition;
            Keire::UiPosition targetPosition;
            Keire::UiColor cableColor{0.42F, 0.62F, 0.9F, 1.0F};
            if (connection.SourcePin != 0)
            {
                const NodeGraphPinAddress sourceAddress{connection.Source, connection.SourcePin,
                                                        connection.SourceBlock};
                const NodeGraphPinAddress targetAddress{connection.Target, connection.TargetPin,
                                                        connection.TargetBlock};
                const auto* source = FindPin(nodes, sourceAddress);
                const auto* target = FindPin(nodes, targetAddress);
                if (!source || !target)
                    continue;
                cableColor = source->Color;
                if (sourceOwner != collapsedOwners.end())
                {
                    sourcePosition = collapsedEndpoint(*sourceOwner->second, false);
                    summaryPins.emplace_back(sourcePosition, source->Color);
                }
                else
                    sourcePosition = findDrawnPin(sourceAddress)->Position;
                if (targetOwner != collapsedOwners.end())
                {
                    targetPosition = collapsedEndpoint(*targetOwner->second, true);
                    summaryPins.emplace_back(targetPosition, target->Color);
                }
                else
                    targetPosition = findDrawnPin(targetAddress)->Position;
            }
            else
            {
                const auto sourceSize = EffectiveNodeSize(*sourceNode->second);
                const auto targetSize = EffectiveNodeSize(*targetNode->second);
                sourcePosition = sourceOwner != collapsedOwners.end()
                                     ? collapsedEndpoint(*sourceOwner->second, false)
                                     : ToScreen({sourceNode->second->Position.X + sourceSize.Width,
                                                 sourceNode->second->Position.Y + sourceSize.Height * 0.5F},
                                                canvas);
                targetPosition = targetOwner != collapsedOwners.end()
                                     ? collapsedEndpoint(*targetOwner->second, true)
                                     : ToScreen({targetNode->second->Position.X,
                                                 targetNode->second->Position.Y + targetSize.Height * 0.5F},
                                                canvas);
                if (sourceOwner != collapsedOwners.end())
                    summaryPins.emplace_back(sourcePosition, cableColor);
                if (targetOwner != collapsedOwners.end())
                    summaryPins.emplace_back(targetPosition, cableColor);
            }
            DrawnConnection drawn{std::addressof(connection), cableColor};
            drawn.Reroutes.reserve(connection.RoutingPoints.size());
            std::vector<Keire::UiPosition> waypoints;
            waypoints.reserve(connection.RoutingPoints.size() + 2);
            waypoints.push_back(sourcePosition);
            for (std::size_t index = 0; index < connection.RoutingPoints.size(); ++index)
            {
                const NodeGraphRerouteAddress address{connection.Id, index};
                const auto position = m_DraggingReroute && *m_DraggingReroute == address
                                          ? m_RerouteDragPosition
                                          : connection.RoutingPoints[index];
                const auto screenPosition = ToScreen(position, canvas);
                drawn.Reroutes.push_back(screenPosition);
                waypoints.push_back(screenPosition);
            }
            waypoints.push_back(targetPosition);
            drawn.Segments.reserve(waypoints.size() - 1);
            for (std::size_t index = 1; index < waypoints.size(); ++index)
            {
                const auto [first, second] = BezierControls(waypoints[index - 1], waypoints[index], true, true);
                drawn.Segments.push_back({waypoints[index - 1], first, second, waypoints[index]});
            }
            drawnConnections.push_back(std::move(drawn));
        }

        std::optional<NodeGraphRerouteAddress> hoveredReroute;
        if (options.EditableReroutes)
        {
            const float rerouteHitRadius = std::clamp(10.0F * m_Zoom, 8.0F, 13.0F);
            float rerouteDistance = rerouteHitRadius * rerouteHitRadius;
            for (const auto& connection : drawnConnections)
                for (std::size_t index = 0; index < connection.Reroutes.size(); ++index)
                {
                    const float candidate = SquaredDistance(pointer.Position, connection.Reroutes[index]);
                    if (candidate <= rerouteDistance)
                    {
                        hoveredReroute = NodeGraphRerouteAddress{connection.Connection->Id, index};
                        rerouteDistance = candidate;
                    }
                }
        }

        std::optional<NodeGraphPinAddress> hoveredPin;
        const float pinHitRadius = std::clamp(9.0F * m_Zoom, 7.0F, 12.0F);
        float pinDistance = pinHitRadius * pinHitRadius;
        for (const auto& pin : drawnPins)
        {
            if (hoveredReroute)
                break;
            const float candidate = SquaredDistance(pointer.Position, pin.Position);
            if (candidate <= pinDistance)
            {
                hoveredPin = pin.Address;
                pinDistance = candidate;
            }
        }

        std::optional<NodeGraphBlockAddress> hoveredBlock;
        if (!hoveredReroute && !hoveredPin)
        {
            for (const auto& block : drawnBlocks)
            {
                if (block.Rectangle.Contains(pointer.Position))
                {
                    hoveredBlock = block.Address;
                    break;
                }
            }
        }

        std::optional<StableNodeId> hoveredNode;
        if (!hoveredReroute && !hoveredPin && !hoveredBlock)
        {
            for (const auto& node : drawnNodes)
            {
                if (node.Rectangle.Contains(pointer.Position))
                {
                    hoveredNode = node.Id;
                    break;
                }
            }
        }

        std::optional<StableNodeId> hoveredConnection;
        std::size_t hoveredConnectionSegment = 0;
        if (!hoveredReroute && !hoveredPin && !hoveredBlock && !hoveredNode)
        {
            float cableDistance = ConnectionHitRadius * ConnectionHitRadius;
            for (const auto& connection : drawnConnections)
            {
                const auto [candidate, segment] = DistanceToConnectionSquared(connection, pointer.Position);
                if (candidate <= cableDistance)
                {
                    hoveredConnection = connection.Connection->Id;
                    hoveredConnectionSegment = segment;
                    cableDistance = candidate;
                }
            }
        }
        const auto hoveredComment =
            !hoveredReroute && !hoveredPin && !hoveredBlock && !hoveredNode && !hoveredConnection ? commentLayer.Hovered
                                                                                                  : std::nullopt;

        result.HoveredPin = hoveredPin;
        result.HoveredBlock = hoveredBlock;
        result.HoveredNode = hoveredNode;
        result.HoveredConnection = hoveredConnection;
        result.HoveredReroute = hoveredReroute;

        if (canvasHovered && options.Editable && options.MultiSelection &&
            ui.Shortcut({.Key = Keire::UiKey::A, .Primary = true}))
        {
            SelectAll(nodes);
            result.SelectionChanged = true;
        }
        if (canvasHovered && ui.Shortcut({.Key = Keire::UiKey::F, .Shift = true}))
            Focus(nodes, size);
        else if (canvasHovered && ui.Shortcut({Keire::UiKey::F}))
        {
            std::vector<NodeGraphNode> selected;
            for (const auto& node : nodes)
                if (IsSelected(node.Id))
                    selected.push_back(node);
            if (selected.empty())
                Focus(nodes, size);
            else
                Focus(selected, size);
        }

        if (canvasHovered && canvasItem.DoubleClicked && options.Editable && options.EditableReroutes)
        {
            if (hoveredReroute)
                result.DeleteRerouteRequested = hoveredReroute;
            else if (hoveredConnection)
            {
                result.AddRerouteRequested =
                    NodeGraphRerouteRequest{*hoveredConnection, hoveredConnectionSegment, result.PointerGraphPosition};
            }
        }
        if (canvasHovered && canvasItem.DoubleClicked && hoveredComment && commentLayer.Header)
        {
            const auto comment = std::ranges::find(options.Comments, *hoveredComment, &NodeGraphComment::Id);
            if (comment != options.Comments.end() && comment->Collapsed)
                result.ToggleCommentCollapseRequested = hoveredComment;
            else
                result.RenameCommentRequested = hoveredComment;
        }

        if (canvasHovered && pointer.LeftPressed)
        {
            if (hoveredReroute)
            {
                ClearSelection();
                m_CommentSelection.reset();
                m_BlockSelection.reset();
                m_ConnectionSelection = hoveredReroute->Connection;
                m_RerouteSelection = hoveredReroute;
                m_CommentSelection.reset();
                result.ActivatedConnection = hoveredReroute->Connection;
                result.ActivatedReroute = hoveredReroute;
                if (options.Editable && options.EditableReroutes && !canvasItem.DoubleClicked)
                {
                    m_DraggingReroute = hoveredReroute;
                    const auto found =
                        std::ranges::find(connections, hoveredReroute->Connection, &NodeGraphConnection::Id);
                    m_RerouteDragPosition = found->RoutingPoints[hoveredReroute->Index];
                }
            }
            else if (hoveredPin)
            {
                m_CommentSelection.reset();
                if (options.MultiSelection && IsSelected(hoveredPin->Node))
                    MakePrimary(hoveredPin->Node);
                else
                    Select(hoveredPin->Node);
                m_BlockSelection = hoveredPin->Block
                                       ? std::optional(NodeGraphBlockAddress{hoveredPin->Node, hoveredPin->Block})
                                       : std::nullopt;
                m_ConnectionSelection.reset();
                m_CommentSelection.reset();
                m_RerouteSelection.reset();
                result.ActivatedNode = hoveredPin->Node;
                result.ActivatedPin = hoveredPin;
                if (options.Editable)
                    m_DraggingPin = hoveredPin;
            }
            else if (hoveredBlock)
            {
                m_CommentSelection.reset();
                if (options.MultiSelection && IsSelected(hoveredBlock->Node))
                    MakePrimary(hoveredBlock->Node);
                else
                    Select(hoveredBlock->Node);
                m_BlockSelection = hoveredBlock;
                m_ConnectionSelection.reset();
                m_CommentSelection.reset();
                m_RerouteSelection.reset();
                result.ActivatedNode = hoveredBlock->Node;
                result.ActivatedBlock = hoveredBlock;
                if (options.Editable)
                {
                    m_DraggingBlock = hoveredBlock;
                    const auto found = std::ranges::find(drawnBlocks, *hoveredBlock, &DrawnBlock::Address);
                    m_BlockDragDestination = found->Index;
                }
            }
            else if (hoveredNode)
            {
                m_CommentSelection.reset();
                if (options.MultiSelection && ui.ControlDown())
                    ToggleSelection(*hoveredNode);
                else if (options.MultiSelection && IsSelected(*hoveredNode))
                    MakePrimary(*hoveredNode);
                else
                    Select(hoveredNode);
                m_BlockSelection.reset();
                m_ConnectionSelection.reset();
                m_CommentSelection.reset();
                m_RerouteSelection.reset();
                result.ActivatedNode = m_Selection;
                result.SelectionChanged = true;
                if (options.Editable && IsSelected(*hoveredNode))
                {
                    m_Dragging = hoveredNode;
                    m_DragPositions.clear();
                    for (const auto selected : m_Selections)
                        if (const auto found = std::ranges::find(nodes, selected, &NodeGraphNode::Id);
                            found != nodes.end())
                            m_DragPositions.emplace_back(selected, found->Position);
                    m_DragMoved = false;
                }
            }
            else if (hoveredConnection && options.InteractiveConnections)
            {
                ClearSelection();
                m_CommentSelection.reset();
                m_BlockSelection.reset();
                m_ConnectionSelection = hoveredConnection;
                m_RerouteSelection.reset();
                result.ActivatedConnection = hoveredConnection;
            }
            else if (hoveredComment)
            {
                ClearSelection();
                m_BlockSelection.reset();
                m_ConnectionSelection.reset();
                m_RerouteSelection.reset();
                m_CommentSelection = hoveredComment;
                result.ActivatedComment = hoveredComment;
                const auto comment = std::ranges::find(options.Comments, *hoveredComment, &NodeGraphComment::Id);
                if (options.Editable && comment != options.Comments.end())
                {
                    if (commentLayer.ResizeHandle)
                    {
                        m_ResizingComment = hoveredComment;
                        m_CommentResize = comment->Size;
                    }
                    else if (commentLayer.Header)
                    {
                        m_DraggingComment = hoveredComment;
                        m_CommentDragPosition = comment->Position;
                        m_CommentMemberPositions.clear();
                        m_CommentMemberCommentPositions.clear();
                        if (comment->MoveContents)
                        {
                            std::vector<StableNodeId> pending(comment->Members.begin(), comment->Members.end());
                            while (!pending.empty())
                            {
                                const auto member = pending.back();
                                pending.pop_back();
                                if (const auto node = std::ranges::find(nodes, member, &NodeGraphNode::Id);
                                    node != nodes.end())
                                    m_CommentMemberPositions.emplace_back(member, node->Position);
                                else if (const auto nested =
                                             std::ranges::find(options.Comments, member, &NodeGraphComment::Id);
                                         nested != options.Comments.end())
                                {
                                    m_CommentMemberCommentPositions.emplace_back(member, nested->Position);
                                    pending.insert(pending.end(), nested->Members.begin(), nested->Members.end());
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                m_MarqueeStart = options.MultiSelection ? std::optional(result.PointerGraphPosition) : std::nullopt;
                m_MarqueeBase = ui.ControlDown() ? m_Selections : std::vector<StableNodeId>{};
                if (!ui.ControlDown())
                    ClearSelection();
                m_BlockSelection.reset();
                m_ConnectionSelection.reset();
                m_RerouteSelection.reset();
                m_CommentSelection.reset();
                result.BackgroundActivated = !ui.ControlDown();
                result.SelectionChanged = !ui.ControlDown();
            }
        }

        if (canvasHovered && pointer.RightPressed)
        {
            NodeGraphContextRequest request;
            request.GraphPosition = result.PointerGraphPosition;
            if (hoveredReroute)
            {
                request.Kind = NodeGraphContextTargetKind::Connection;
                request.Connection = hoveredReroute->Connection;
                ClearSelection();
                m_BlockSelection.reset();
                m_ConnectionSelection = hoveredReroute->Connection;
                m_RerouteSelection = hoveredReroute;
            }
            else if (hoveredPin)
            {
                request.Kind = NodeGraphContextTargetKind::Pin;
                request.Node = hoveredPin->Node;
                request.Pin = hoveredPin->Pin;
                request.Block = hoveredPin->Block;
                if (!options.MultiSelection || !IsSelected(hoveredPin->Node))
                    Select(hoveredPin->Node);
                else
                    MakePrimary(hoveredPin->Node);
                m_BlockSelection = hoveredPin->Block
                                       ? std::optional(NodeGraphBlockAddress{hoveredPin->Node, hoveredPin->Block})
                                       : std::nullopt;
                m_ConnectionSelection.reset();
            }
            else if (hoveredBlock)
            {
                request.Kind = NodeGraphContextTargetKind::Block;
                request.Node = hoveredBlock->Node;
                request.Block = hoveredBlock->Block;
                if (!options.MultiSelection || !IsSelected(hoveredBlock->Node))
                    Select(hoveredBlock->Node);
                else
                    MakePrimary(hoveredBlock->Node);
                m_BlockSelection = hoveredBlock;
                m_ConnectionSelection.reset();
            }
            else if (hoveredNode)
            {
                request.Kind = NodeGraphContextTargetKind::Node;
                request.Node = *hoveredNode;
                if (!options.MultiSelection || !IsSelected(*hoveredNode))
                    Select(hoveredNode);
                else
                    MakePrimary(*hoveredNode);
                m_BlockSelection.reset();
                m_ConnectionSelection.reset();
            }
            else if (hoveredConnection && options.InteractiveConnections)
            {
                request.Kind = NodeGraphContextTargetKind::Connection;
                request.Connection = *hoveredConnection;
                ClearSelection();
                m_BlockSelection.reset();
                m_ConnectionSelection = hoveredConnection;
                m_RerouteSelection.reset();
                m_CommentSelection.reset();
            }
            else if (hoveredComment)
            {
                request.Kind = NodeGraphContextTargetKind::Comment;
                request.Comment = *hoveredComment;
                ClearSelection();
                m_BlockSelection.reset();
                m_ConnectionSelection.reset();
                m_RerouteSelection.reset();
                m_CommentSelection = hoveredComment;
            }
            else
            {
                m_RerouteSelection.reset();
                m_CommentSelection.reset();
            }
            result.ContextRequested = request;
        }

        if (m_MarqueeStart && (pointer.LeftDown || pointer.LeftReleased))
        {
            auto selected = m_MarqueeBase;
            for (const auto node : MarqueeSelection(nodes, *m_MarqueeStart, result.PointerGraphPosition))
                if (std::ranges::find(selected, node) == selected.end())
                    selected.push_back(node);
            Select(selected);
            result.SelectionChanged = true;
        }

        if (m_DraggingBlock && pointer.LeftDown)
        {
            std::size_t destination = 0;
            for (const auto& block : drawnBlocks)
            {
                if (block.Address.Node != m_DraggingBlock->Node)
                    continue;
                destination = block.Index;
                if (pointer.Position.Y <= (block.Rectangle.Minimum.Y + block.Rectangle.Maximum.Y) * 0.5F)
                    break;
            }
            m_BlockDragDestination = destination;
        }

        std::optional<NodeGraphConnectionRequest> previewConnection;
        std::optional<NodeGraphConnectionValidation> previewValidation;
        const DrawnPin* dragStart = nullptr;
        const DrawnPin* dragTarget = nullptr;
        if (m_DraggingPin)
        {
            if (ui.KeyDown(Keire::UiKey::Escape))
            {
                m_DraggingPin.reset();
                result.ConnectionDragCancelled = true;
            }
            else
            {
                dragStart = findDrawnPin(*m_DraggingPin);
                if (!dragStart)
                {
                    m_DraggingPin.reset();
                    result.ConnectionDragCancelled = true;
                }
                else if (hoveredPin && *hoveredPin != *m_DraggingPin)
                {
                    dragTarget = findDrawnPin(*hoveredPin);
                    previewConnection = ConnectionBetween(nodes, *m_DraggingPin, *hoveredPin);
                    if (previewConnection)
                    {
                        previewValidation = EvaluateConnection(nodes, *previewConnection, options.ValidateConnection);
                        result.PreviewConnection = previewConnection;
                        result.PreviewValidation = previewValidation;
                    }
                }
            }
        }

        if (pointer.LeftReleased)
        {
            if (m_DraggingComment)
            {
                result.MoveCompletedComment = m_DraggingComment;
                for (const auto& [node, position] : m_CommentMemberPositions)
                {
                    (void)position;
                    result.CommentMemberNodes.push_back(node);
                }
                result.CommentMemberComments = m_CommentMemberCommentPositions;
                m_DraggingComment.reset();
                m_CommentMemberPositions.clear();
                m_CommentMemberCommentPositions.clear();
            }
            if (m_ResizingComment)
            {
                result.ResizeCompletedComment = m_ResizingComment;
                m_ResizingComment.reset();
            }
            if (m_DraggingReroute)
            {
                result.MoveRerouteRequested = NodeGraphRerouteRequest{m_DraggingReroute->Connection,
                                                                      m_DraggingReroute->Index, m_RerouteDragPosition};
                m_DraggingReroute.reset();
            }
            if (m_Dragging && m_DragMoved)
            {
                result.MoveCompletedNode = m_Dragging;
                for (const auto& [node, position] : m_DragPositions)
                {
                    (void)position;
                    result.MoveCompletedNodes.push_back(node);
                }
            }
            m_Dragging.reset();
            m_DragPositions.clear();
            m_MarqueeStart.reset();
            m_MarqueeBase.clear();
            m_DragMoved = false;

            if (m_DraggingBlock)
            {
                const auto source = std::ranges::find(drawnBlocks, *m_DraggingBlock, &DrawnBlock::Address);
                if (source != drawnBlocks.end() && source->Index != m_BlockDragDestination)
                {
                    result.BlockMoveRequested = NodeGraphBlockMoveRequest{m_DraggingBlock->Node, m_DraggingBlock->Block,
                                                                          m_BlockDragDestination};
                }
                m_DraggingBlock.reset();
            }

            if (m_DraggingPin)
            {
                if (previewConnection && previewValidation && previewValidation->CanConnect())
                    result.ConnectionRequested = previewConnection;
                else
                    result.ConnectionDragCancelled = true;
                m_DraggingPin.reset();
                dragStart = nullptr;
                dragTarget = nullptr;
            }
        }

        if (canvasHovered && options.Editable && m_CommentSelection && ui.Shortcut({Keire::UiKey::F2}))
            result.RenameCommentRequested = m_CommentSelection;
        if (canvasHovered && options.Editable && options.MultiSelection && !ui.ControlDown() &&
            ui.Shortcut({Keire::UiKey::C}))
            result.CreateCommentRequested = CommentFromSelection(nodes, m_Selections, result.PointerGraphPosition);
        if (canvasHovered && options.Editable && options.MultiSelection && !m_Selections.empty() &&
            ui.Shortcut({.Key = Keire::UiKey::D, .Primary = true}))
            result.DuplicateNodesRequested = m_Selections;
        if (canvasHovered && options.Editable && options.MultiSelection && !m_Selections.empty() &&
            ui.Shortcut({.Key = Keire::UiKey::C, .Primary = true}))
            result.CopyNodesRequested = m_Selections;
        if (canvasHovered && options.Editable && options.MultiSelection && !m_Selections.empty() &&
            ui.Shortcut({.Key = Keire::UiKey::X, .Primary = true}))
            result.CutNodesRequested = m_Selections;
        if (canvasHovered && options.Editable && options.MultiSelection &&
            ui.Shortcut({.Key = Keire::UiKey::V, .Primary = true}))
            result.PasteRequested = true;

        if (canvasHovered && options.Editable && ui.Shortcut({Keire::UiKey::Delete}))
        {
            if (m_RerouteSelection && options.EditableReroutes)
                result.DeleteRerouteRequested = m_RerouteSelection;
            else if (m_ConnectionSelection)
                result.DeleteConnectionRequested = m_ConnectionSelection;
            else if (m_BlockSelection)
                result.DeleteBlockRequested = m_BlockSelection;
            else if (m_Selection)
            {
                for (const auto node : m_Selections)
                {
                    const auto found = nodeLookup.find(node);
                    if (found != nodeLookup.end() && found->second->Deletable)
                        result.DeleteNodesRequested.push_back(node);
                    else
                        result.ProtectedNodes.push_back(node);
                }
                if (!result.DeleteNodesRequested.empty())
                    result.DeleteNodeRequested = result.DeleteNodesRequested.front();
            }
            else if (m_CommentSelection)
                result.DeleteCommentRequested = m_CommentSelection;
        }

        for (const auto& connection : drawnConnections)
        {
            const bool hovered = hoveredConnection && *hoveredConnection == connection.Connection->Id;
            const bool selected = m_ConnectionSelection && *m_ConnectionSelection == connection.Connection->Id;
            const auto color = selected  ? ScaleColor(connection.Color, 1.35F, 1.0F)
                               : hovered ? ScaleColor(connection.Color, 1.2F, 1.0F)
                                         : connection.Color;
            for (const auto& segment : connection.Segments)
            {
                DrawBezier(ui, segment.Start, segment.FirstControl, segment.SecondControl, segment.End,
                           {0.008F, 0.012F, 0.02F, 0.88F}, selected ? 8.0F : 6.0F);
                DrawBezier(ui, segment.Start, segment.FirstControl, segment.SecondControl, segment.End, color,
                           selected  ? 3.5F
                           : hovered ? 3.0F
                                     : 2.0F);
            }

            if (!connection.Connection->Label.empty() && detail.ConnectionLabels && !connection.Segments.empty())
            {
                constexpr float labelFontSize = 11.0F;
                const auto& labelSegment = connection.Segments[(connection.Segments.size() - 1) / 2];
                const auto labelPosition = BezierPoint(labelSegment.Start, labelSegment.FirstControl,
                                                       labelSegment.SecondControl, labelSegment.End, 0.5F);
                const auto labelSize = ui.MeasureText(connection.Connection->Label, labelFontSize);
                const Keire::UiItemRect labelRectangle{
                    {labelPosition.X - labelSize.Width * 0.5F - 5.0F, labelPosition.Y - labelSize.Height * 0.5F - 2.0F},
                    {labelPosition.X + labelSize.Width * 0.5F + 5.0F,
                     labelPosition.Y + labelSize.Height * 0.5F + 2.0F}};
                ui.DrawFilledRectangle(labelRectangle, {0.055F, 0.065F, 0.085F, 0.97F}, 4.0F);
                ui.DrawRectangle(labelRectangle, color, 1.0F, 4.0F);
                ui.DrawOverlayText({labelRectangle.Minimum.X + 5.0F, labelRectangle.Minimum.Y + 2.0F},
                                   {0.76F, 0.82F, 0.92F, 1.0F}, connection.Connection->Label, labelFontSize, canvas);
            }

            for (std::size_t index = 0; index < connection.Reroutes.size(); ++index)
            {
                const NodeGraphRerouteAddress address{connection.Connection->Id, index};
                const bool rerouteHovered = hoveredReroute && *hoveredReroute == address;
                const bool rerouteSelected = m_RerouteSelection && *m_RerouteSelection == address;
                const float radius = std::clamp(6.0F * m_Zoom, 5.0F, 8.0F);
                ui.DrawFilledCircle(connection.Reroutes[index], radius + 2.0F, {0.008F, 0.012F, 0.02F, 0.95F});
                ui.DrawFilledCircle(connection.Reroutes[index], radius,
                                    rerouteSelected  ? Keire::UiColor{0.3F, 0.78F, 1.0F, 1.0F}
                                    : rerouteHovered ? ScaleColor(connection.Color, 1.35F, 1.0F)
                                                     : connection.Color);
            }
        }
        for (const auto& [position, color] : summaryPins)
        {
            const float radius = std::clamp(6.0F * m_Zoom, 5.0F, 8.0F);
            ui.DrawFilledCircle(position, radius + 2.0F, {0.008F, 0.012F, 0.02F, 0.95F});
            ui.DrawFilledCircle(position, radius, color);
            ui.DrawCircle(position, radius, ScaleColor(color, 1.25F, 1.0F), 1.5F);
        }

        if (dragStart)
        {
            const auto end = dragTarget ? dragTarget->Position : pointer.Position;
            const bool startsAtOutput = dragStart->Pin->Direction == NodeGraphPinDirection::Output;
            const bool endsAtInput =
                dragTarget ? dragTarget->Pin->Direction == NodeGraphPinDirection::Input : startsAtOutput;
            const auto [first, second] = BezierControls(dragStart->Position, end, startsAtOutput, endsAtInput);
            Keire::UiColor previewColor = ScaleColor(dragStart->Pin->Color, 1.18F, 1.0F);
            if (previewValidation)
            {
                switch (previewValidation->Status)
                {
                case NodeGraphConnectionValidationStatus::Accept:
                    previewColor = {0.24F, 0.9F, 0.58F, 1.0F};
                    break;
                case NodeGraphConnectionValidationStatus::AcceptWithWarning:
                    previewColor = {1.0F, 0.67F, 0.2F, 1.0F};
                    break;
                case NodeGraphConnectionValidationStatus::Reject:
                    previewColor = {1.0F, 0.28F, 0.3F, 1.0F};
                    break;
                }
            }
            DrawBezier(ui, dragStart->Position, first, second, end, {0.008F, 0.012F, 0.02F, 0.9F}, 7.0F);
            DrawBezier(ui, dragStart->Position, first, second, end, previewColor, 3.0F);
        }

        for (auto iterator = nodes.rbegin(); iterator != nodes.rend(); ++iterator)
        {
            const auto& node = *iterator;
            const auto drawn = std::ranges::find(drawnNodes, node.Id, &DrawnNode::Id);
            const auto rectangle = drawn->Rectangle;
            const bool hovered =
                (hoveredNode && *hoveredNode == node.Id) || (hoveredBlock && hoveredBlock->Node == node.Id);
            const bool selected = IsSelected(node.Id);
            const Keire::UiItemRect shadow{{rectangle.Minimum.X + 4.0F, rectangle.Minimum.Y + 6.0F},
                                           {rectangle.Maximum.X + 4.0F, rectangle.Maximum.Y + 6.0F}};
            ui.DrawFilledRectangle(shadow, {0.0F, 0.0F, 0.0F, 0.42F}, 8.0F);
            ui.DrawFilledRectangle(rectangle, ScaleColor(node.Color, 0.43F, 1.0F), 8.0F);

            const float headerHeight = std::min(rectangle.Size().Height, HeaderHeight(node) * m_Zoom);
            const Keire::UiItemRect header{rectangle.Minimum,
                                           {rectangle.Maximum.X, rectangle.Minimum.Y + headerHeight}};
            ui.DrawFilledRectangle(header, ScaleColor(node.Color, hovered ? 1.14F : 1.0F, 1.0F), 8.0F);
            ui.DrawLine({header.Minimum.X, header.Maximum.Y}, {header.Maximum.X, header.Maximum.Y},
                        ScaleColor(node.Color, 1.25F, 0.8F), 1.0F);

            const float accentWidth = std::clamp(3.0F * m_Zoom, 2.0F, 4.0F);
            const Keire::UiItemRect accent{{rectangle.Minimum.X, rectangle.Minimum.Y + 7.0F},
                                           {rectangle.Minimum.X + accentWidth, rectangle.Maximum.Y - 7.0F}};
            ui.DrawFilledRectangle(accent, ScaleColor(node.Color, 1.7F, 1.0F), 2.0F);

            const auto borderColor = selected  ? Keire::UiColor{0.3F, 0.72F, 1.0F, 1.0F}
                                     : hovered ? Keire::UiColor{0.46F, 0.54F, 0.68F, 1.0F}
                                               : Keire::UiColor{0.22F, 0.25F, 0.32F, 1.0F};
            ui.DrawRectangle(rectangle, borderColor, selected ? 2.5F : hovered ? 1.5F : 1.0F, 8.0F);

            DrawNodeGraphAnnotation(ui, node, rectangle, m_Zoom);

            const float labelX = rectangle.Minimum.X + std::clamp(13.0F * m_Zoom, 6.0F, 13.0F);
            const float titleY = rectangle.Minimum.Y + std::clamp(8.0F * m_Zoom, 3.0F, 8.0F);
            const float titleFontSize = detail.NodeSubtitle ? 0.0F : 11.0F;
            ui.DrawOverlayText({labelX, titleY}, {0.97F, 0.98F, 1.0F, 1.0F}, node.Label, titleFontSize, rectangle);
            if (!node.Subtitle.empty() && detail.NodeSubtitle)
                ui.DrawOverlayText({labelX, rectangle.Minimum.Y + 27.0F}, {0.68F, 0.73F, 0.82F, 1.0F}, node.Subtitle,
                                   11.0F, rectangle);

            for (const auto& block : drawnBlocks)
            {
                if (block.Address.Node != node.Id)
                    continue;
                const bool blockHovered = hoveredBlock && *hoveredBlock == block.Address;
                const bool blockSelected = m_BlockSelection && *m_BlockSelection == block.Address;
                const auto fill = block.Block->Enabled
                                      ? ScaleColor(block.Block->Color, blockHovered ? 1.12F : 0.9F, 0.96F)
                                      : ScaleColor(block.Block->Color, 0.58F, 0.72F);
                ui.DrawFilledRectangle(block.Rectangle, fill, 5.0F);
                const auto blockBorder = blockSelected  ? Keire::UiColor{0.34F, 0.76F, 1.0F, 1.0F}
                                         : blockHovered ? Keire::UiColor{0.45F, 0.58F, 0.76F, 1.0F}
                                                        : Keire::UiColor{0.2F, 0.24F, 0.31F, 1.0F};
                ui.DrawRectangle(block.Rectangle, blockBorder, blockSelected ? 2.0F : 1.0F, 5.0F);
                const Keire::UiItemRect blockAccent{
                    block.Rectangle.Minimum,
                    {block.Rectangle.Minimum.X + std::clamp(3.0F * m_Zoom, 2.0F, 4.0F), block.Rectangle.Maximum.Y}};
                ui.DrawFilledRectangle(blockAccent, ScaleColor(node.Color, block.Block->Enabled ? 1.45F : 0.65F, 1.0F),
                                       3.0F);

                if (detail.BlockLabels)
                {
                    const float blockFontSize = std::clamp(11.0F * m_Zoom, 8.0F, 11.0F);
                    const auto order = std::to_string(block.Index + 1);
                    const float blockTextY = block.Rectangle.Minimum.Y + std::clamp(7.0F * m_Zoom, 2.0F, 7.0F);
                    ui.DrawOverlayText({block.Rectangle.Minimum.X + std::clamp(9.0F * m_Zoom, 5.0F, 9.0F), blockTextY},
                                       {0.48F, 0.58F, 0.72F, 1.0F}, order, blockFontSize, block.Rectangle);
                    constexpr std::string_view enabledText = "ON";
                    constexpr std::string_view disabledText = "OFF";
                    const auto state = block.Block->Enabled ? enabledText : disabledText;
                    const auto stateSize = ui.MeasureText(state, blockFontSize);
                    const Keire::UiItemRect labelClip{
                        {block.Rectangle.Minimum.X + 24.0F, block.Rectangle.Minimum.Y},
                        {block.Rectangle.Maximum.X - stateSize.Width - 14.0F, block.Rectangle.Maximum.Y}};
                    ui.DrawOverlayText(
                        {block.Rectangle.Minimum.X + std::clamp(28.0F * m_Zoom, 14.0F, 28.0F), blockTextY},
                        block.Block->Enabled ? Keire::UiColor{0.9F, 0.93F, 0.98F, 1.0F}
                                             : Keire::UiColor{0.5F, 0.54F, 0.62F, 1.0F},
                        block.Block->Label, blockFontSize, labelClip);
                    ui.DrawOverlayText({block.Rectangle.Maximum.X - stateSize.Width - 10.0F, blockTextY},
                                       block.Block->Enabled ? Keire::UiColor{0.3F, 0.9F, 0.58F, 1.0F}
                                                            : Keire::UiColor{0.62F, 0.65F, 0.72F, 1.0F},
                                       state, blockFontSize, block.Rectangle);
                }

                if (m_DraggingBlock && m_DraggingBlock->Node == node.Id && block.Index == m_BlockDragDestination)
                {
                    const auto source = std::ranges::find(drawnBlocks, *m_DraggingBlock, &DrawnBlock::Address);
                    const bool insertAfter = source != drawnBlocks.end() && source->Index < m_BlockDragDestination;
                    const float y = insertAfter ? block.Rectangle.Maximum.Y : block.Rectangle.Minimum.Y;
                    ui.DrawLine({block.Rectangle.Minimum.X + 3.0F, y}, {block.Rectangle.Maximum.X - 3.0F, y},
                                {0.3F, 0.78F, 1.0F, 1.0F}, 3.0F);
                }
            }

            if (node.Pins.empty() && node.Blocks.empty())
            {
                const float portRadius = std::clamp(4.5F * m_Zoom, 2.5F, 5.0F);
                const float portY = rectangle.Minimum.Y + rectangle.Size().Height * 0.5F;
                ui.DrawFilledCircle({rectangle.Minimum.X, portY}, portRadius, {0.32F, 0.6F, 0.9F, 1.0F});
                ui.DrawCircle({rectangle.Minimum.X, portY}, portRadius, {0.72F, 0.85F, 1.0F, 1.0F});
                ui.DrawFilledCircle({rectangle.Maximum.X, portY}, portRadius, {0.32F, 0.6F, 0.9F, 1.0F});
                ui.DrawCircle({rectangle.Maximum.X, portY}, portRadius, {0.72F, 0.85F, 1.0F, 1.0F});
            }
        }

        for (const auto& pin : drawnPins)
        {
            const bool hovered = hoveredPin && *hoveredPin == pin.Address;
            const bool linkOrigin = m_DraggingPin && *m_DraggingPin == pin.Address;
            Keire::UiColor ring{0.72F, 0.78F, 0.88F, 1.0F};
            if (linkOrigin)
                ring = {0.92F, 0.96F, 1.0F, 1.0F};
            else if (hovered && previewValidation)
            {
                switch (previewValidation->Status)
                {
                case NodeGraphConnectionValidationStatus::Accept:
                    ring = {0.24F, 0.9F, 0.58F, 1.0F};
                    break;
                case NodeGraphConnectionValidationStatus::AcceptWithWarning:
                    ring = {1.0F, 0.67F, 0.2F, 1.0F};
                    break;
                case NodeGraphConnectionValidationStatus::Reject:
                    ring = {1.0F, 0.28F, 0.3F, 1.0F};
                    break;
                }
            }
            else if (hovered)
            {
                ring = {0.92F, 0.95F, 1.0F, 1.0F};
            }

            const float radius = std::clamp(5.0F * m_Zoom, 4.0F, 6.0F);
            if (hovered || linkOrigin)
                ui.DrawFilledCircle(pin.Position, radius + 3.0F, {ring.Red, ring.Green, ring.Blue, 0.18F});
            ui.DrawFilledCircle(pin.Position, radius, ScaleColor(pin.Pin->Color, hovered ? 1.2F : 0.88F, 1.0F));
            ui.DrawCircle(pin.Position, radius, ring, hovered || linkOrigin ? 2.0F : 1.0F);

            if (detail.PinLabels)
            {
                auto labelRectangle = std::ranges::find(drawnNodes, pin.Address.Node, &DrawnNode::Id)->Rectangle;
                if (pin.Address.Block)
                {
                    const auto block = std::ranges::find(
                        drawnBlocks, NodeGraphBlockAddress{pin.Address.Node, pin.Address.Block}, &DrawnBlock::Address);
                    if (block != drawnBlocks.end())
                        labelRectangle = block->Rectangle;
                }
                const float midpoint = (labelRectangle.Minimum.X + labelRectangle.Maximum.X) * 0.5F;
                const float pinFontSize = std::clamp(11.0F * m_Zoom, 8.0F, 11.0F);
                if (pin.Pin->Direction == NodeGraphPinDirection::Input)
                {
                    labelRectangle.Maximum.X = midpoint - 3.0F;
                    ui.DrawOverlayText({pin.Position.X + 9.0F, pin.Position.Y - pinFontSize * 0.5F},
                                       {0.78F, 0.82F, 0.9F, 1.0F}, pin.Pin->Label, pinFontSize, labelRectangle);
                }
                else
                {
                    const auto labelSize = ui.MeasureText(pin.Pin->Label, pinFontSize);
                    labelRectangle.Minimum.X = midpoint + 3.0F;
                    ui.DrawOverlayText({pin.Position.X - labelSize.Width - 9.0F, pin.Position.Y - pinFontSize * 0.5F},
                                       {0.78F, 0.82F, 0.9F, 1.0F}, pin.Pin->Label, pinFontSize, labelRectangle);
                }
            }
        }

        if (previewValidation && !previewValidation->Diagnostic.empty() && dragStart)
        {
            const auto textSize = ui.MeasureText(previewValidation->Diagnostic, 11.0F);
            const Keire::UiPosition minimum{pointer.Position.X + 14.0F, pointer.Position.Y + 16.0F};
            const Keire::UiItemRect diagnostic{
                minimum, {minimum.X + textSize.Width + 12.0F, minimum.Y + textSize.Height + 8.0F}};
            const auto diagnosticColor = previewValidation->Status == NodeGraphConnectionValidationStatus::Reject
                                             ? Keire::UiColor{0.88F, 0.22F, 0.25F, 1.0F}
                                             : Keire::UiColor{0.96F, 0.58F, 0.14F, 1.0F};
            ui.DrawFilledRectangle(diagnostic, {0.045F, 0.05F, 0.065F, 0.97F}, 5.0F);
            ui.DrawRectangle(diagnostic, diagnosticColor, 1.0F, 5.0F);
            ui.DrawOverlayText({minimum.X + 6.0F, minimum.Y + 4.0F}, {0.94F, 0.95F, 0.98F, 1.0F},
                               previewValidation->Diagnostic, 11.0F, canvas);
        }

        if (m_MarqueeStart && pointer.LeftDown)
        {
            const auto start = ToScreen(*m_MarqueeStart, canvas);
            const Keire::UiItemRect marquee{
                {std::min(start.X, pointer.Position.X), std::min(start.Y, pointer.Position.Y)},
                {std::max(start.X, pointer.Position.X), std::max(start.Y, pointer.Position.Y)}};
            ui.DrawFilledRectangle(marquee, {0.2F, 0.58F, 0.95F, 0.12F}, 2.0F);
            ui.DrawRectangle(marquee, {0.3F, 0.72F, 1.0F, 0.9F}, 1.0F, 2.0F);
        }

        result.SelectedNodes = m_Selections;

        return result;
    }

    Keire::UiPosition StableNodeGraphCanvas::ToScreen(const Keire::Vector2 position,
                                                      const Keire::UiItemRect canvas) const noexcept
    {
        return {canvas.Minimum.X + (position.X + m_Pan.X) * m_Zoom, canvas.Minimum.Y + (position.Y + m_Pan.Y) * m_Zoom};
    }

    Keire::Vector2 StableNodeGraphCanvas::ToGraph(const Keire::UiPosition position,
                                                  const Keire::UiItemRect canvas) const noexcept
    {
        return {(position.X - canvas.Minimum.X) / m_Zoom - m_Pan.X, (position.Y - canvas.Minimum.Y) / m_Zoom - m_Pan.Y};
    }
} // namespace KeireEditor
