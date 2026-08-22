#include "KeireClient/Editor/AuthoringWidgets.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <ranges>

namespace KeireEditor
{
    void StableNodeGraphCanvas::RestoreViewport(const NodeGraphViewport viewport) noexcept
    {
        if (!std::isfinite(viewport.Pan.X) || !std::isfinite(viewport.Pan.Y) || !std::isfinite(viewport.Zoom))
            return;
        m_Pan = viewport.Pan;
        m_Zoom = std::clamp(viewport.Zoom, 0.2F, 2.5F);
        CancelInteractions();
    }

    namespace
    {
        [[nodiscard]] std::optional<StableNodeId>
        CanvasIdentity(const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities,
                       const Keire::AssetId asset)
        {
            const auto found = std::ranges::find(identities, asset, &decltype(identities)::value_type::second);
            return found == identities.end() ? std::nullopt : std::optional(found->first);
        }
    } // namespace

    std::vector<StableNodeId> StableNodeGraphCanvas::MarqueeSelection(const std::span<const NodeGraphNode> nodes,
                                                                      const Keire::Vector2 first,
                                                                      const Keire::Vector2 second)
    {
        const Keire::Vector2 minimum{std::min(first.X, second.X), std::min(first.Y, second.Y)};
        const Keire::Vector2 maximum{std::max(first.X, second.X), std::max(first.Y, second.Y)};
        std::vector<StableNodeId> result;
        for (const auto& node : nodes)
        {
            const Keire::Vector2 nodeMaximum{node.Position.X + node.Size.X, node.Position.Y + node.Size.Y};
            if (node.Position.X <= maximum.X && nodeMaximum.X >= minimum.X && node.Position.Y <= maximum.Y &&
                nodeMaximum.Y >= minimum.Y)
                result.push_back(node.Id);
        }
        return result;
    }

    NodeGraphCommentCreateRequest
    StableNodeGraphCanvas::CommentFromSelection(const std::span<const NodeGraphNode> nodes,
                                                const std::span<const StableNodeId> selected,
                                                const Keire::Vector2 fallbackPosition)
    {
        NodeGraphCommentCreateRequest result;
        result.Members.assign(selected.begin(), selected.end());
        if (selected.empty())
        {
            result.Position = fallbackPosition;
            return result;
        }
        Keire::Vector2 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        Keire::Vector2 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        for (const auto selectedNode : selected)
            if (const auto node = std::ranges::find(nodes, selectedNode, &NodeGraphNode::Id); node != nodes.end())
            {
                minimum.X = std::min(minimum.X, node->Position.X);
                minimum.Y = std::min(minimum.Y, node->Position.Y);
                maximum.X = std::max(maximum.X, node->Position.X + node->Size.X);
                maximum.Y = std::max(maximum.Y, node->Position.Y + node->Size.Y);
            }
        result.Position = {minimum.X - 40.0F, minimum.Y - 64.0F};
        result.Size = {std::max(240.0F, maximum.X - minimum.X + 80.0F),
                       std::max(120.0F, maximum.Y - minimum.Y + 104.0F)};
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
            const auto pinHeight = [](const std::span<const NodeGraphPin> pins)
            {
                const auto inputs = std::ranges::count(pins, NodeGraphPinDirection::Input, &NodeGraphPin::Direction);
                const auto outputs = std::ranges::count(pins, NodeGraphPinDirection::Output, &NodeGraphPin::Direction);
                return pins.empty() ? 0.0F : static_cast<float>(std::max(inputs, outputs)) * 22.0F + 8.0F;
            };
            float effectiveHeight = (node.Subtitle.empty() ? 34.0F : 48.0F) + pinHeight(node.Pins);
            if (!node.Blocks.empty())
                for (const auto& block : node.Blocks)
                    effectiveHeight += 34.0F + pinHeight(block.Pins);
            effectiveHeight = std::max(effectiveHeight, node.Size.Y);
            minimum.X = std::min(minimum.X, node.Position.X);
            minimum.Y = std::min(minimum.Y, node.Position.Y);
            maximum.X = std::max(maximum.X, node.Position.X + node.Size.X);
            maximum.Y = std::max(maximum.Y, node.Position.Y + effectiveHeight);
        }
        const float width = std::max(maximum.X - minimum.X, 1.0F);
        const float height = std::max(maximum.Y - minimum.Y, 1.0F);
        m_Zoom =
            std::clamp(std::min((canvasSize.Width - 48.0F) / width, (canvasSize.Height - 48.0F) / height), 0.35F, 2.5F);
        m_Pan = {(canvasSize.Width / m_Zoom - width) * 0.5F - minimum.X,
                 (canvasSize.Height / m_Zoom - height) * 0.5F - minimum.Y};
    }

    void StableNodeGraphCanvas::Select(const std::optional<StableNodeId> node)
    {
        ClearSelection();
        if (node && *node != 0)
        {
            m_Selection = node;
            m_Selections.push_back(*node);
        }
    }

    void StableNodeGraphCanvas::Select(const std::span<const StableNodeId> nodes,
                                       const std::optional<StableNodeId> primary)
    {
        ClearSelection();
        for (const auto node : nodes)
            if (node != 0 && !IsSelected(node))
                m_Selections.push_back(node);
        if (primary && IsSelected(*primary))
            MakePrimary(*primary);
        else if (!m_Selections.empty())
            m_Selection = m_Selections.back();
    }

    void StableNodeGraphCanvas::ToggleSelection(const StableNodeId node)
    {
        if (node == 0)
            return;
        const auto found = std::ranges::find(m_Selections, node);
        if (found != m_Selections.end())
        {
            m_Selections.erase(found);
            m_Selection = m_Selections.empty() ? std::nullopt : std::optional(m_Selections.back());
            return;
        }
        m_Selections.push_back(node);
        m_Selection = node;
    }

    void StableNodeGraphCanvas::SelectAll(const std::span<const NodeGraphNode> nodes)
    {
        ClearSelection();
        m_Selections.reserve(nodes.size());
        std::ranges::transform(nodes, std::back_inserter(m_Selections), &NodeGraphNode::Id);
        if (!m_Selections.empty())
            m_Selection = m_Selections.back();
    }

    bool StableNodeGraphCanvas::IsSelected(const StableNodeId node) const noexcept
    {
        return std::ranges::find(m_Selections, node) != m_Selections.end();
    }

    void StableNodeGraphCanvas::MakePrimary(const StableNodeId node)
    {
        const auto found = std::ranges::find(m_Selections, node);
        if (found == m_Selections.end())
            m_Selections.push_back(node);
        else if (std::next(found) != m_Selections.end())
        {
            m_Selections.erase(found);
            m_Selections.push_back(node);
        }
        m_Selection = node;
    }

    void StableNodeGraphCanvas::ClearSelection() noexcept
    {
        m_Selection.reset();
        m_Selections.clear();
    }

    void SynchronizeGraphSelection(StableNodeGraphCanvas& canvas,
                                   const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities,
                                   std::vector<Keire::AssetId>& selected, const std::optional<Keire::AssetId> primary)
    {
        if (!primary)
            selected.clear();
        else if (std::ranges::find(selected, *primary) == selected.end())
            selected = {*primary};
        std::vector<StableNodeId> canvasSelection;
        for (const auto asset : selected)
            if (const auto identity = CanvasIdentity(identities, asset))
                canvasSelection.push_back(*identity);
        canvas.Select(canvasSelection, primary ? CanvasIdentity(identities, *primary) : std::nullopt);
    }

    std::vector<Keire::AssetId>
    ResolveGraphSelection(const std::span<const StableNodeId> selected,
                          const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
    {
        std::vector<Keire::AssetId> result;
        for (const auto canvas : selected)
            if (const auto found = std::ranges::find(identities, canvas, &decltype(identities)::value_type::first);
                found != identities.end())
                result.push_back(found->second);
        return result;
    }
} // namespace KeireEditor
