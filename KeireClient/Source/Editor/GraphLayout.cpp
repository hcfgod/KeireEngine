#include "KeireClient/Editor/GraphLayout.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::vector<const NodeGraphNode*> SelectedNodes(std::span<const NodeGraphNode> nodes,
                                                                      std::span<const StableNodeId> selection)
        {
            std::vector<const NodeGraphNode*> result;
            result.reserve(selection.size());
            for (const auto id : selection)
                if (const auto found = std::ranges::find(nodes, id, &NodeGraphNode::Id);
                    found != nodes.end() && std::ranges::find(result, &*found) == result.end())
                    result.push_back(&*found);
            return result;
        }
    } // namespace

    std::vector<std::pair<StableNodeId, Keire::Vector2>> AlignGraphNodes(const std::span<const NodeGraphNode> nodes,
                                                                         const std::span<const StableNodeId> selection,
                                                                         const GraphAlignment alignment)
    {
        const auto selected = SelectedNodes(nodes, selection);
        if (selected.size() < 2)
            return {};
        float target = 0.0F;
        switch (alignment)
        {
        case GraphAlignment::Left:
            target = std::ranges::min(selected, {}, [](const auto* node) { return node->Position.X; })->Position.X;
            break;
        case GraphAlignment::HorizontalCenter:
            target =
                std::ranges::min(selected, {}, [](const auto* node) { return node->Position.X + node->Size.X * 0.5F; })
                    ->Position.X;
            target +=
                std::ranges::min(selected, {}, [](const auto* node) { return node->Position.X + node->Size.X * 0.5F; })
                    ->Size.X *
                0.5F;
            break;
        case GraphAlignment::Right:
            target = std::ranges::max(selected, {}, [](const auto* node) { return node->Position.X + node->Size.X; })
                         ->Position.X;
            target += std::ranges::max(selected, {}, [](const auto* node) { return node->Position.X + node->Size.X; })
                          ->Size.X;
            break;
        case GraphAlignment::Top:
            target = std::ranges::min(selected, {}, [](const auto* node) { return node->Position.Y; })->Position.Y;
            break;
        case GraphAlignment::VerticalCenter:
            target =
                std::ranges::min(selected, {}, [](const auto* node) { return node->Position.Y + node->Size.Y * 0.5F; })
                    ->Position.Y;
            target +=
                std::ranges::min(selected, {}, [](const auto* node) { return node->Position.Y + node->Size.Y * 0.5F; })
                    ->Size.Y *
                0.5F;
            break;
        case GraphAlignment::Bottom:
            target = std::ranges::max(selected, {}, [](const auto* node) { return node->Position.Y + node->Size.Y; })
                         ->Position.Y;
            target += std::ranges::max(selected, {}, [](const auto* node) { return node->Position.Y + node->Size.Y; })
                          ->Size.Y;
            break;
        }

        std::vector<std::pair<StableNodeId, Keire::Vector2>> result;
        result.reserve(selected.size());
        for (const auto* node : selected)
        {
            auto position = node->Position;
            switch (alignment)
            {
            case GraphAlignment::Left:
                position.X = target;
                break;
            case GraphAlignment::HorizontalCenter:
                position.X = target - node->Size.X * 0.5F;
                break;
            case GraphAlignment::Right:
                position.X = target - node->Size.X;
                break;
            case GraphAlignment::Top:
                position.Y = target;
                break;
            case GraphAlignment::VerticalCenter:
                position.Y = target - node->Size.Y * 0.5F;
                break;
            case GraphAlignment::Bottom:
                position.Y = target - node->Size.Y;
                break;
            }
            result.emplace_back(node->Id, position);
        }
        return result;
    }

    std::vector<std::pair<StableNodeId, Keire::Vector2>>
    DistributeGraphNodes(const std::span<const NodeGraphNode> nodes, const std::span<const StableNodeId> selection,
                         const GraphDistribution distribution)
    {
        auto selected = SelectedNodes(nodes, selection);
        if (selected.size() < 3)
            return {};
        const bool horizontal = distribution == GraphDistribution::Horizontal;
        std::ranges::sort(selected, {},
                          [horizontal](const auto* node) { return horizontal ? node->Position.X : node->Position.Y; });
        const auto coordinate = [horizontal](const NodeGraphNode* node)
        { return horizontal ? node->Position.X : node->Position.Y; };
        const float step =
            (coordinate(selected.back()) - coordinate(selected.front())) / static_cast<float>(selected.size() - 1U);
        std::vector<std::pair<StableNodeId, Keire::Vector2>> result;
        result.reserve(selected.size());
        for (std::size_t index = 0; index < selected.size(); ++index)
        {
            auto position = selected[index]->Position;
            if (horizontal)
                position.X = coordinate(selected.front()) + step * static_cast<float>(index);
            else
                position.Y = coordinate(selected.front()) + step * static_cast<float>(index);
            result.emplace_back(selected[index]->Id, position);
        }
        return result;
    }

    std::vector<StableNodeId> InternalGraphConnections(const std::span<const NodeGraphConnection> connections,
                                                       const std::span<const StableNodeId> selection)
    {
        const std::set selected(selection.begin(), selection.end());
        std::vector<StableNodeId> result;
        for (const auto& connection : connections)
            if (selected.contains(connection.Source) && selected.contains(connection.Target))
                result.push_back(connection.Id);
        return result;
    }
} // namespace KeireEditor
