#pragma once

#include "KeireClient/Editor/AuthoringWidgets.h"

#include <span>
#include <vector>

namespace KeireEditor
{
    enum class GraphAlignment
    {
        Left,
        HorizontalCenter,
        Right,
        Top,
        VerticalCenter,
        Bottom
    };

    enum class GraphDistribution
    {
        Horizontal,
        Vertical
    };

    [[nodiscard]] std::vector<std::pair<StableNodeId, Keire::Vector2>>
    AlignGraphNodes(std::span<const NodeGraphNode> nodes, std::span<const StableNodeId> selection,
                    GraphAlignment alignment);
    [[nodiscard]] std::vector<std::pair<StableNodeId, Keire::Vector2>>
    DistributeGraphNodes(std::span<const NodeGraphNode> nodes, std::span<const StableNodeId> selection,
                         GraphDistribution distribution);
    [[nodiscard]] std::vector<StableNodeId> InternalGraphConnections(std::span<const NodeGraphConnection> connections,
                                                                     std::span<const StableNodeId> selection);
} // namespace KeireEditor
