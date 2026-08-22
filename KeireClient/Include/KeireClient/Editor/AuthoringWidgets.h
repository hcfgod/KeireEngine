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
        Block,
        Comment
    };

    struct NodeGraphContextRequest
    {
        NodeGraphContextTargetKind Kind = NodeGraphContextTargetKind::Background;
        StableNodeId Node = 0;
        StableNodeId Pin = 0;
        StableNodeId Connection = 0;
        Keire::Vector2 GraphPosition;
        StableNodeId Block = 0;
        StableNodeId Comment = 0;

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
        std::string Comment;
        bool CommentPinned = false;
        std::vector<NodeGraphPin> Pins;
        std::vector<NodeGraphBlockRow> Blocks;
        bool Deletable = true;

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
        std::vector<Keire::Vector2> RoutingPoints;

        bool operator==(const NodeGraphConnection&) const = default;
    };

    struct NodeGraphComment
    {
        StableNodeId Id = 0;
        std::string Title = "Comment";
        std::string Description;
        Keire::Vector2 Position;
        Keire::Vector2 Size{320.0F, 180.0F};
        Keire::UiColor Color{0.18F, 0.34F, 0.58F, 0.32F};
        float FontSize = 18.0F;
        bool MoveContents = true;
        bool Collapsed = false;
        std::optional<StableNodeId> Parent;
        std::vector<StableNodeId> Members;
        std::size_t SummaryInputs = 0;
        std::size_t SummaryOutputs = 0;

        bool operator==(const NodeGraphComment&) const = default;
    };

    struct NodeGraphCommentCreateRequest
    {
        Keire::Vector2 Position;
        Keire::Vector2 Size{320.0F, 180.0F};
        std::vector<StableNodeId> Members;
    };

    struct NodeGraphRerouteAddress
    {
        StableNodeId Connection = 0;
        std::size_t Index = 0;

        bool operator==(const NodeGraphRerouteAddress&) const = default;
    };

    struct NodeGraphRerouteRequest
    {
        StableNodeId Connection = 0;
        std::size_t Index = 0;
        Keire::Vector2 GraphPosition;

        bool operator==(const NodeGraphRerouteRequest&) const = default;
    };

    struct NodeGraphCanvasOptions
    {
        bool Editable = true;
        bool InteractiveConnections = true;
        NodeGraphConnectionValidator ValidateConnection;
        bool EditableReroutes = false;
        bool MultiSelection = false;
        std::span<NodeGraphComment> Comments;
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
        std::optional<NodeGraphRerouteAddress> ActivatedReroute;
        std::optional<NodeGraphRerouteAddress> HoveredReroute;
        std::optional<NodeGraphRerouteRequest> AddRerouteRequested;
        std::optional<NodeGraphRerouteRequest> MoveRerouteRequested;
        std::optional<NodeGraphRerouteAddress> DeleteRerouteRequested;
        bool SelectionChanged = false;
        std::vector<StableNodeId> SelectedNodes;
        std::vector<StableNodeId> MovedNodes;
        std::vector<StableNodeId> MoveCompletedNodes;
        std::vector<StableNodeId> DeleteNodesRequested;
        std::vector<StableNodeId> DuplicateNodesRequested;
        std::vector<StableNodeId> CopyNodesRequested;
        std::vector<StableNodeId> CutNodesRequested;
        bool PasteRequested = false;
        std::vector<StableNodeId> ProtectedNodes;
        std::optional<StableNodeId> ActivatedComment;
        std::optional<StableNodeId> MovedComment;
        std::optional<StableNodeId> MoveCompletedComment;
        std::optional<StableNodeId> ResizedComment;
        std::optional<StableNodeId> ResizeCompletedComment;
        std::vector<StableNodeId> CommentMemberNodes;
        std::vector<std::pair<StableNodeId, Keire::Vector2>> CommentMemberComments;
        std::optional<StableNodeId> DeleteCommentRequested;
        std::optional<StableNodeId> RenameCommentRequested;
        std::optional<StableNodeId> ToggleCommentCollapseRequested;
        std::optional<NodeGraphCommentCreateRequest> CreateCommentRequested;
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

    struct NodeGraphViewport
    {
        Keire::Vector2 Pan;
        float Zoom = 1.0F;

        bool operator==(const NodeGraphViewport&) const = default;
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
        [[nodiscard]] static std::vector<StableNodeId> MarqueeSelection(std::span<const NodeGraphNode> nodes,
                                                                        Keire::Vector2 first, Keire::Vector2 second);
        [[nodiscard]] static NodeGraphCommentCreateRequest CommentFromSelection(std::span<const NodeGraphNode> nodes,
                                                                                std::span<const StableNodeId> selected,
                                                                                Keire::Vector2 fallbackPosition);

        [[nodiscard]] NodeGraphCanvasResult Draw(Keire::UiFrame& ui, std::string_view id,
                                                 std::span<NodeGraphNode> nodes,
                                                 std::span<const NodeGraphConnection> connections,
                                                 bool editable = true);
        [[nodiscard]] NodeGraphCanvasResult Draw(Keire::UiFrame& ui, std::string_view id,
                                                 std::span<NodeGraphNode> nodes,
                                                 std::span<const NodeGraphConnection> connections,
                                                 const NodeGraphCanvasOptions& options);

        void Focus(std::span<const NodeGraphNode> nodes, Keire::UiSize canvasSize);
        void Select(std::optional<StableNodeId> node);
        void Select(std::span<const StableNodeId> nodes, std::optional<StableNodeId> primary = {});
        void ToggleSelection(StableNodeId node);
        void SelectAll(std::span<const NodeGraphNode> nodes);
        void SelectBlock(std::optional<NodeGraphBlockAddress> block) noexcept { m_BlockSelection = block; }
        void SelectConnection(std::optional<StableNodeId> connection) noexcept
        {
            if (!m_RerouteSelection || !connection || m_RerouteSelection->Connection != *connection)
                m_RerouteSelection.reset();
            m_ConnectionSelection = connection;
        }
        void CancelConnectionDrag() noexcept { m_DraggingPin.reset(); }
        void CancelInteractions() noexcept
        {
            m_Dragging.reset();
            m_DraggingBlock.reset();
            m_DraggingPin.reset();
            m_DraggingReroute.reset();
            m_DraggingComment.reset();
            m_ResizingComment.reset();
            m_CommentMemberCommentPositions.clear();
            m_MarqueeStart.reset();
            m_DragMoved = false;
        }
        [[nodiscard]] std::optional<StableNodeId> Selection() const noexcept { return m_Selection; }
        [[nodiscard]] std::span<const StableNodeId> Selections() const noexcept { return m_Selections; }
        [[nodiscard]] std::optional<NodeGraphBlockAddress> BlockSelection() const noexcept { return m_BlockSelection; }
        [[nodiscard]] std::optional<StableNodeId> ConnectionSelection() const noexcept { return m_ConnectionSelection; }
        [[nodiscard]] std::optional<StableNodeId> CommentSelection() const noexcept { return m_CommentSelection; }
        [[nodiscard]] bool ConnectionDragActive() const noexcept { return m_DraggingPin.has_value(); }
        [[nodiscard]] Keire::Vector2 Pan() const noexcept { return m_Pan; }
        [[nodiscard]] float Zoom() const noexcept { return m_Zoom; }
        [[nodiscard]] NodeGraphViewport Viewport() const noexcept { return {m_Pan, m_Zoom}; }
        void RestoreViewport(NodeGraphViewport viewport) noexcept;

      private:
        [[nodiscard]] Keire::UiPosition ToScreen(Keire::Vector2 position, Keire::UiItemRect canvas) const noexcept;
        [[nodiscard]] Keire::Vector2 ToGraph(Keire::UiPosition position, Keire::UiItemRect canvas) const noexcept;
        [[nodiscard]] bool IsSelected(StableNodeId node) const noexcept;
        void MakePrimary(StableNodeId node);
        void ClearSelection() noexcept;

        Keire::Vector2 m_Pan;
        float m_Zoom = 1.0F;
        std::optional<StableNodeId> m_Selection;
        std::vector<StableNodeId> m_Selections;
        std::optional<NodeGraphBlockAddress> m_BlockSelection;
        std::optional<StableNodeId> m_ConnectionSelection;
        std::optional<StableNodeId> m_Dragging;
        std::optional<NodeGraphBlockAddress> m_DraggingBlock;
        std::size_t m_BlockDragDestination = 0;
        std::optional<NodeGraphPinAddress> m_DraggingPin;
        std::optional<NodeGraphRerouteAddress> m_RerouteSelection;
        std::optional<NodeGraphRerouteAddress> m_DraggingReroute;
        std::optional<StableNodeId> m_CommentSelection;
        std::optional<StableNodeId> m_DraggingComment;
        std::optional<StableNodeId> m_ResizingComment;
        Keire::Vector2 m_CommentDragPosition;
        Keire::Vector2 m_CommentResize;
        std::vector<std::pair<StableNodeId, Keire::Vector2>> m_CommentMemberPositions;
        std::vector<std::pair<StableNodeId, Keire::Vector2>> m_CommentMemberCommentPositions;
        Keire::Vector2 m_RerouteDragPosition;
        std::vector<std::pair<StableNodeId, Keire::Vector2>> m_DragPositions;
        std::optional<Keire::Vector2> m_MarqueeStart;
        std::vector<StableNodeId> m_MarqueeBase;
        bool m_DragMoved = false;
    };

    void SynchronizeGraphSelection(StableNodeGraphCanvas& canvas,
                                   std::span<const std::pair<StableNodeId, Keire::AssetId>> identities,
                                   std::vector<Keire::AssetId>& selected, std::optional<Keire::AssetId> primary);
    [[nodiscard]] std::vector<Keire::AssetId>
    ResolveGraphSelection(std::span<const StableNodeId> selected,
                          std::span<const std::pair<StableNodeId, Keire::AssetId>> identities);

    /// Shared interaction state for searchable node-creation menus. Identifiers, rather than display indexes, preserve
    /// selection as live search results are rebuilt and let different graph editors keep a small recent-node list.
    class NodeMenuSelection final
    {
      public:
        static constexpr std::size_t RecentCapacity = 6;

        void Open() noexcept;
        [[nodiscard]] bool ConsumeFocusRequest() noexcept;
        void Synchronize(std::span<const std::string_view> visibleIds);
        void MovePrevious(std::span<const std::string_view> visibleIds);
        void MoveNext(std::span<const std::string_view> visibleIds);
        void Remember(std::string_view id);

        [[nodiscard]] std::optional<std::string_view> Selected() const noexcept;
        [[nodiscard]] bool IsSelected(std::string_view id) const noexcept;
        [[nodiscard]] std::span<const std::string> Recent() const noexcept { return m_Recent; }

      private:
        void Move(std::span<const std::string_view> visibleIds, int direction);

        std::string m_Selected;
        std::vector<std::string> m_Recent;
        bool m_FocusRequested = false;
    };

    /// Returns whether two Shader Graph pins can form a directed cable. The arguments may be supplied in either
    /// order; direction and supported Shader Graph numeric coercions determine the output and input endpoints.
    [[nodiscard]] bool ShaderGraphPinsCanConnect(const Keire::ShaderGraphPin& first,
                                                 const Keire::ShaderGraphPin& second) noexcept;
    /// Returns whether at least one pin pair can connect between two Shader Graph nodes.
    [[nodiscard]] bool ShaderGraphNodesCanConnect(const Keire::ShaderGraphNode& first,
                                                  const Keire::ShaderGraphNode& second) noexcept;

    class AuthoringValueEditors final
    {
      public:
        [[nodiscard]] static bool Curve(Keire::UiFrame& ui, std::string_view label, Keire::Curve1D& value,
                                        float minimumTime = 0.0F, float maximumTime = 1.0F);
        [[nodiscard]] static bool Gradient(Keire::UiFrame& ui, std::string_view label, Keire::ColorGradient& value);
    };
} // namespace KeireEditor
