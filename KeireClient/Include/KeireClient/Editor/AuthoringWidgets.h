#pragma once

#include "Keire/Core.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    using StableNodeId = std::uint64_t;

    struct NodeGraphNode
    {
        StableNodeId Id = 0;
        std::string Label;
        Keire::Vector2 Position;
        Keire::Vector2 Size{180.0F, 72.0F};
        Keire::UiColor Color{0.18F, 0.2F, 0.24F, 1.0F};
        std::string Subtitle;

        bool operator==(const NodeGraphNode&) const = default;
    };

    struct NodeGraphConnection
    {
        StableNodeId Id = 0;
        StableNodeId Source = 0;
        StableNodeId Target = 0;
        std::string Label;

        bool operator==(const NodeGraphConnection&) const = default;
    };

    struct NodeGraphCanvasResult
    {
        std::optional<StableNodeId> ActivatedNode;
        std::optional<StableNodeId> MovedNode;
        bool BackgroundActivated = false;
        bool Changed = false;
        std::optional<StableNodeId> MoveCompletedNode;
    };

    class StableNodeGraphIdMap final
    {
      public:
        [[nodiscard]] StableNodeId Assign(Keire::AssetId source, StableNodeId preferred);
        [[nodiscard]] std::optional<StableNodeId> Find(Keire::AssetId source) const noexcept;

      private:
        std::vector<std::pair<Keire::AssetId, StableNodeId>> m_Assignments;
        std::vector<StableNodeId> m_Used;
    };

    class StableNodeGraphCanvas final
    {
      public:
        static void Validate(std::span<const NodeGraphNode> nodes, std::span<const NodeGraphConnection> connections);

        [[nodiscard]] NodeGraphCanvasResult Draw(Keire::UiFrame& ui, std::string_view id,
                                                 std::span<NodeGraphNode> nodes,
                                                 std::span<const NodeGraphConnection> connections,
                                                 bool editable = true);

        void Focus(std::span<const NodeGraphNode> nodes, Keire::UiSize canvasSize);
        void Select(std::optional<StableNodeId> node) noexcept { m_Selection = node; }
        [[nodiscard]] std::optional<StableNodeId> Selection() const noexcept { return m_Selection; }
        [[nodiscard]] Keire::Vector2 Pan() const noexcept { return m_Pan; }
        [[nodiscard]] float Zoom() const noexcept { return m_Zoom; }

      private:
        [[nodiscard]] Keire::UiPosition ToScreen(Keire::Vector2 position, Keire::UiItemRect canvas) const noexcept;

        Keire::Vector2 m_Pan;
        float m_Zoom = 1.0F;
        std::optional<StableNodeId> m_Selection;
        std::optional<StableNodeId> m_Dragging;
        Keire::Vector2 m_DragPosition;
        bool m_DragMoved = false;
    };

    class AuthoringValueEditors final
    {
      public:
        [[nodiscard]] static bool Curve(Keire::UiFrame& ui, std::string_view label, Keire::Curve1D& value,
                                        float minimumTime = 0.0F, float maximumTime = 1.0F);
        [[nodiscard]] static bool Gradient(Keire::UiFrame& ui, std::string_view label, Keire::ColorGradient& value);
    };
} // namespace KeireEditor
