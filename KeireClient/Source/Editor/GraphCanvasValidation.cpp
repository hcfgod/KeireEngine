#include "KeireClient/Editor/AuthoringWidgets.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] const NodeGraphNode* FindNode(const std::span<const NodeGraphNode> nodes,
                                                    const StableNodeId id) noexcept
        {
            const auto found = std::ranges::find(nodes, id, &NodeGraphNode::Id);
            return found == nodes.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] const NodeGraphPin* FindPin(const std::span<const NodeGraphNode> nodes,
                                                  const NodeGraphPinAddress address) noexcept
        {
            const auto* node = FindNode(nodes, address.Node);
            if (!node)
                return nullptr;
            if (address.Block)
            {
                const auto block = std::ranges::find(node->Blocks, address.Block, &NodeGraphBlockRow::Id);
                if (block == node->Blocks.end())
                    return nullptr;
                const auto found = std::ranges::find(block->Pins, address.Pin, &NodeGraphPin::Id);
                return found == block->Pins.end() ? nullptr : std::addressof(*found);
            }
            const auto found = std::ranges::find(node->Pins, address.Pin, &NodeGraphPin::Id);
            return found == node->Pins.end() ? nullptr : std::addressof(*found);
        }
    } // namespace

    void StableNodeGraphCanvas::Validate(const std::span<const NodeGraphNode> nodes,
                                         const std::span<const NodeGraphConnection> connections)
    {
        std::unordered_set<StableNodeId> nodeIds;
        std::unordered_set<StableNodeId> blockIds;
        std::unordered_set<StableNodeId> pinIds;
        nodeIds.reserve(nodes.size());
        for (const auto& node : nodes)
        {
            if (node.Id == 0 || !nodeIds.emplace(node.Id).second)
                throw std::invalid_argument("Node graph IDs must be non-zero and unique.");
            if (node.Label.empty() || !std::isfinite(node.Position.X) || !std::isfinite(node.Position.Y) ||
                !std::isfinite(node.Size.X) || !std::isfinite(node.Size.Y) || node.Size.X < 40.0F ||
                node.Size.Y < 24.0F)
                throw std::invalid_argument("Node graph nodes require a label, finite position, and usable size.");

            for (const auto& pin : node.Pins)
            {
                if (pin.Id == 0 || !pinIds.emplace(pin.Id).second)
                    throw std::invalid_argument("Node graph pin IDs must be non-zero and unique.");
                if (pin.Label.empty() || !std::isfinite(pin.Color.Red) || !std::isfinite(pin.Color.Green) ||
                    !std::isfinite(pin.Color.Blue) || !std::isfinite(pin.Color.Alpha))
                    throw std::invalid_argument("Node graph pins require a label and finite color.");
            }
            for (const auto& block : node.Blocks)
            {
                if (block.Id == 0 || !blockIds.emplace(block.Id).second || block.Label.empty() ||
                    !std::isfinite(block.Color.Red) || !std::isfinite(block.Color.Green) ||
                    !std::isfinite(block.Color.Blue) || !std::isfinite(block.Color.Alpha))
                    throw std::invalid_argument("Node graph Blocks require a unique ID, label, and finite color.");
                for (const auto& pin : block.Pins)
                {
                    if (pin.Id == 0 || !pinIds.emplace(pin.Id).second)
                        throw std::invalid_argument("Node graph pin IDs must be non-zero and unique.");
                    if (pin.Label.empty() || !std::isfinite(pin.Color.Red) || !std::isfinite(pin.Color.Green) ||
                        !std::isfinite(pin.Color.Blue) || !std::isfinite(pin.Color.Alpha))
                        throw std::invalid_argument("Node graph pins require a label and finite color.");
                }
            }
        }

        std::unordered_set<StableNodeId> connectionIds;
        connectionIds.reserve(connections.size());
        for (const auto& connection : connections)
        {
            if (connection.Id == 0 || !connectionIds.emplace(connection.Id).second)
                throw std::invalid_argument("Node graph connection IDs must be non-zero and unique.");
            if (!nodeIds.contains(connection.Source) || !nodeIds.contains(connection.Target))
                throw std::invalid_argument("Node graph connections must reference existing nodes.");
            if (connection.RoutingPoints.size() > 64 ||
                std::ranges::any_of(connection.RoutingPoints, [](const Keire::Vector2 point)
                                    { return !std::isfinite(point.X) || !std::isfinite(point.Y); }))
                throw std::invalid_argument("Node graph connection routing points are invalid or exceed 64 entries.");

            const bool legacy = connection.SourcePin == 0 && connection.TargetPin == 0;
            if (legacy)
            {
                if (connection.SourceBlock != 0 || connection.TargetBlock != 0)
                    throw std::invalid_argument("Legacy node graph connections cannot reference Blocks.");
                continue;
            }
            if (connection.SourcePin == 0 || connection.TargetPin == 0)
                throw std::invalid_argument("Pin-aware node graph connections require both endpoint pins.");
            const NodeGraphConnectionRequest request{connection.Source,      connection.SourcePin,
                                                     connection.Target,      connection.TargetPin,
                                                     connection.SourceBlock, connection.TargetBlock};
            const auto validation = EvaluateConnection(nodes, request);
            if (!validation.CanConnect())
                throw std::invalid_argument(validation.Diagnostic);
        }
    }

    NodeGraphConnectionValidation
    StableNodeGraphCanvas::EvaluateConnection(const std::span<const NodeGraphNode> nodes,
                                              const NodeGraphConnectionRequest& connection,
                                              const NodeGraphConnectionValidator& validator)
    {
        const NodeGraphPinAddress sourceAddress{connection.SourceNode, connection.SourcePin, connection.SourceBlock};
        const NodeGraphPinAddress targetAddress{connection.TargetNode, connection.TargetPin, connection.TargetBlock};
        if (sourceAddress.Node == 0 || sourceAddress.Pin == 0 || targetAddress.Node == 0 || targetAddress.Pin == 0)
            return {NodeGraphConnectionValidationStatus::Reject, "A connection requires two stable pin endpoints."};
        if (sourceAddress == targetAddress)
            return {NodeGraphConnectionValidationStatus::Reject, "A pin cannot connect to itself."};

        const auto* source = FindPin(nodes, sourceAddress);
        const auto* target = FindPin(nodes, targetAddress);
        if (!source || !target)
            return {NodeGraphConnectionValidationStatus::Reject, "A connection references an unavailable pin."};
        if (source->Direction != NodeGraphPinDirection::Output || target->Direction != NodeGraphPinDirection::Input)
            return {NodeGraphConnectionValidationStatus::Reject, "Connections must run from an output to an input."};
        if (source->Type != target->Type)
            return {NodeGraphConnectionValidationStatus::Reject, "The selected pins have incompatible types."};
        return validator ? validator(connection) : NodeGraphConnectionValidation{};
    }
} // namespace KeireEditor
