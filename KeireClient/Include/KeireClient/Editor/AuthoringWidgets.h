#pragma once

#include "Keire/Core.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    using StableNodeId = std::uint64_t;

    enum class NodeGraphPinDirection : std::uint8_t
    {
        Input,
        Output
    };

    struct NodeGraphPin
    {
        StableNodeId Id = 0;
        std::string Label;
        NodeGraphPinDirection Direction = NodeGraphPinDirection::Input;
        StableNodeId Type = 0;
        Keire::UiColor Color{0.32F, 0.6F, 0.9F, 1.0F};

        bool operator==(const NodeGraphPin&) const = default;
    };

    struct NodeGraphPinAddress
    {
        StableNodeId Node = 0;
        StableNodeId Pin = 0;
        StableNodeId Block = 0;

        bool operator==(const NodeGraphPinAddress&) const = default;
    };

    struct NodeGraphBlockAddress
    {
        StableNodeId Node = 0;
        StableNodeId Block = 0;

        bool operator==(const NodeGraphBlockAddress&) const = default;
    };

    struct NodeGraphConnectionRequest
    {
        StableNodeId SourceNode = 0;
        StableNodeId SourcePin = 0;
        StableNodeId TargetNode = 0;
        StableNodeId TargetPin = 0;
        StableNodeId SourceBlock = 0;
        StableNodeId TargetBlock = 0;

        bool operator==(const NodeGraphConnectionRequest&) const = default;
    };

    enum class NodeGraphConnectionValidationStatus : std::uint8_t
    {
        Accept,
        AcceptWithWarning,
        Reject
    };

    struct NodeGraphConnectionValidation
    {
        NodeGraphConnectionValidationStatus Status = NodeGraphConnectionValidationStatus::Accept;
        std::string Diagnostic;

        [[nodiscard]] bool CanConnect() const noexcept { return Status != NodeGraphConnectionValidationStatus::Reject; }

        bool operator==(const NodeGraphConnectionValidation&) const = default;
    };

    using NodeGraphConnectionValidator =
        std::function<NodeGraphConnectionValidation(const NodeGraphConnectionRequest&)>;

    enum class NodeGraphContextTargetKind : std::uint8_t
    {
        Background,
        Node,
        Pin,
        Connection,
        Block
    };

    struct NodeGraphContextRequest
    {
        NodeGraphContextTargetKind Kind = NodeGraphContextTargetKind::Background;
        StableNodeId Node = 0;
        StableNodeId Pin = 0;
        StableNodeId Connection = 0;
        Keire::Vector2 GraphPosition;
        StableNodeId Block = 0;

        bool operator==(const NodeGraphContextRequest&) const = default;
    };

    struct NodeGraphBlockRow
    {
        StableNodeId Id = 0;
        std::string Label;
        bool Enabled = true;
        Keire::UiColor Color{0.14F, 0.17F, 0.22F, 1.0F};
        std::vector<NodeGraphPin> Pins;

        bool operator==(const NodeGraphBlockRow&) const = default;
    };

    struct NodeGraphBlockMoveRequest
    {
        StableNodeId Node = 0;
        StableNodeId Block = 0;
        std::size_t Destination = 0;

        bool operator==(const NodeGraphBlockMoveRequest&) const = default;
    };

    struct NodeGraphNode
    {
        StableNodeId Id = 0;
        std::string Label;
        Keire::Vector2 Position;
        Keire::Vector2 Size{180.0F, 72.0F};
        Keire::UiColor Color{0.18F, 0.2F, 0.24F, 1.0F};
        std::string Subtitle;
        std::vector<NodeGraphPin> Pins;
        std::vector<NodeGraphBlockRow> Blocks;

        bool operator==(const NodeGraphNode&) const = default;
    };

    struct NodeGraphConnection
    {
        StableNodeId Id = 0;
        StableNodeId Source = 0;
        StableNodeId Target = 0;
        std::string Label;
        StableNodeId SourcePin = 0;
        StableNodeId TargetPin = 0;
        StableNodeId SourceBlock = 0;
        StableNodeId TargetBlock = 0;

        bool operator==(const NodeGraphConnection&) const = default;
    };

    struct NodeGraphCanvasOptions
    {
        bool Editable = true;
        bool InteractiveConnections = true;
        NodeGraphConnectionValidator ValidateConnection;
    };

    struct NodeGraphCanvasResult
    {
        std::optional<StableNodeId> ActivatedNode;
        std::optional<StableNodeId> MovedNode;
        bool BackgroundActivated = false;
        bool Changed = false;
        std::optional<StableNodeId> MoveCompletedNode;
        std::optional<NodeGraphPinAddress> ActivatedPin;
        std::optional<StableNodeId> ActivatedConnection;
        std::optional<StableNodeId> HoveredNode;
        std::optional<NodeGraphPinAddress> HoveredPin;
        std::optional<StableNodeId> HoveredConnection;
        std::optional<NodeGraphConnectionRequest> PreviewConnection;
        std::optional<NodeGraphConnectionValidation> PreviewValidation;
        std::optional<NodeGraphConnectionRequest> ConnectionRequested;
        std::optional<NodeGraphContextRequest> ContextRequested;
        std::optional<StableNodeId> DeleteNodeRequested;
        std::optional<StableNodeId> DeleteConnectionRequested;
        Keire::Vector2 PointerGraphPosition;
        bool ConnectionDragCancelled = false;
        std::optional<NodeGraphBlockAddress> ActivatedBlock;
        std::optional<NodeGraphBlockAddress> HoveredBlock;
        std::optional<NodeGraphBlockMoveRequest> BlockMoveRequested;
        std::optional<NodeGraphBlockAddress> DeleteBlockRequested;
    };

    /// Screen-space graph detail selected from zoom. Geometry and interaction remain available at every level while
    /// text that cannot fit its scaled row is removed before it can overlap adjacent pins or blocks.
    struct NodeGraphCanvasDetail
    {
        bool NodeSubtitle = false;
        bool BlockLabels = false;
        bool PinLabels = false;
        bool ConnectionLabels = false;

        [[nodiscard]] bool operator==(const NodeGraphCanvasDetail&) const noexcept = default;
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
        [[nodiscard]] static NodeGraphConnectionValidation
        EvaluateConnection(std::span<const NodeGraphNode> nodes, const NodeGraphConnectionRequest& connection,
                           const NodeGraphConnectionValidator& validator = {});
        [[nodiscard]] static NodeGraphCanvasDetail DetailForZoom(float zoom) noexcept;

        [[nodiscard]] NodeGraphCanvasResult Draw(Keire::UiFrame& ui, std::string_view id,
                                                 std::span<NodeGraphNode> nodes,
                                                 std::span<const NodeGraphConnection> connections,
                                                 bool editable = true);
        [[nodiscard]] NodeGraphCanvasResult Draw(Keire::UiFrame& ui, std::string_view id,
                                                 std::span<NodeGraphNode> nodes,
                                                 std::span<const NodeGraphConnection> connections,
                                                 const NodeGraphCanvasOptions& options);

        void Focus(std::span<const NodeGraphNode> nodes, Keire::UiSize canvasSize);
        void Select(std::optional<StableNodeId> node) noexcept { m_Selection = node; }
        void SelectBlock(std::optional<NodeGraphBlockAddress> block) noexcept { m_BlockSelection = block; }
        void SelectConnection(std::optional<StableNodeId> connection) noexcept { m_ConnectionSelection = connection; }
        void CancelConnectionDrag() noexcept { m_DraggingPin.reset(); }
        void CancelInteractions() noexcept
        {
            m_Dragging.reset();
            m_DraggingBlock.reset();
            m_DraggingPin.reset();
            m_DragMoved = false;
        }
        [[nodiscard]] std::optional<StableNodeId> Selection() const noexcept { return m_Selection; }
        [[nodiscard]] std::optional<NodeGraphBlockAddress> BlockSelection() const noexcept { return m_BlockSelection; }
        [[nodiscard]] std::optional<StableNodeId> ConnectionSelection() const noexcept { return m_ConnectionSelection; }
        [[nodiscard]] bool ConnectionDragActive() const noexcept { return m_DraggingPin.has_value(); }
        [[nodiscard]] Keire::Vector2 Pan() const noexcept { return m_Pan; }
        [[nodiscard]] float Zoom() const noexcept { return m_Zoom; }

      private:
        [[nodiscard]] Keire::UiPosition ToScreen(Keire::Vector2 position, Keire::UiItemRect canvas) const noexcept;
        [[nodiscard]] Keire::Vector2 ToGraph(Keire::UiPosition position, Keire::UiItemRect canvas) const noexcept;

        Keire::Vector2 m_Pan;
        float m_Zoom = 1.0F;
        std::optional<StableNodeId> m_Selection;
        std::optional<NodeGraphBlockAddress> m_BlockSelection;
        std::optional<StableNodeId> m_ConnectionSelection;
        std::optional<StableNodeId> m_Dragging;
        std::optional<NodeGraphBlockAddress> m_DraggingBlock;
        std::size_t m_BlockDragDestination = 0;
        std::optional<NodeGraphPinAddress> m_DraggingPin;
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
