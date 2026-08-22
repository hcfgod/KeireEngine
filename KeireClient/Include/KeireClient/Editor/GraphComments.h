#pragma once

#include "KeireClient/Editor/AuthoringWidgets.h"

namespace KeireEditor
{
    struct NodeGraphCommentLayerResult
    {
        std::optional<StableNodeId> Hovered;
        bool Header = false;
        bool CollapseToggle = false;
        bool ResizeHandle = false;
    };

    struct NodeGraphCommentDragFrameResult
    {
        bool Active = false;
        Keire::Vector2 Delta;
    };

    struct NodeGraphCommentModel
    {
        std::vector<NodeGraphComment> Comments;
        std::vector<std::pair<StableNodeId, Keire::AssetId>> Identities;

        [[nodiscard]] std::optional<Keire::AssetId> Asset(StableNodeId canvas) const noexcept;
    };

    enum class GraphCommentEditorAction : std::uint8_t
    {
        None,
        Apply,
        Delete
    };

    class GraphCommentEditorState final
    {
      public:
        void Select(const Keire::GraphComment& comment);
        [[nodiscard]] GraphCommentEditorAction Draw(Keire::UiFrame& ui, Keire::GraphComment& value);
        [[nodiscard]] std::optional<Keire::AssetId> Selection() const noexcept { return m_Selection; }
        void Clear() noexcept { m_Selection.reset(); }

      private:
        std::optional<Keire::AssetId> m_Selection;
        std::string m_Title;
        std::string m_Description;
        Keire::UiColor m_Color;
        double m_FontSize = 18.0;
        bool m_MoveContents = true;
        bool m_Collapsed = false;
    };

    enum class GraphCommentCanvasEditKind : std::uint8_t
    {
        Create,
        Update,
        Delete
    };

    struct GraphCommentCanvasEdit
    {
        GraphCommentCanvasEditKind Kind = GraphCommentCanvasEditKind::Update;
        Keire::GraphComment Comment;
        std::vector<std::pair<Keire::AssetId, Keire::Vector2>> MovedNodes;
        std::vector<std::pair<Keire::AssetId, Keire::Vector2>> MovedComments;
    };

    [[nodiscard]] NodeGraphCommentModel
    BuildNodeGraphCommentModel(const Keire::GraphAuthoringMetadata& metadata,
                               std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities);
    void ApplyNodeGraphAnnotations(const Keire::GraphAuthoringMetadata& metadata,
                                   std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
                                   std::span<NodeGraphNode> nodes);
    void SetGraphNodeAnnotation(Keire::GraphAuthoringMetadata& metadata, Keire::AssetId node, std::string text,
                                bool pinned);
    void DrawNodeGraphAnnotation(Keire::UiFrame& ui, const NodeGraphNode& node, Keire::UiItemRect rectangle,
                                 float zoom);
    [[nodiscard]] Keire::GraphComment
    CreateGraphComment(const NodeGraphCommentCreateRequest& request,
                       std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities);
    [[nodiscard]] std::optional<GraphCommentCanvasEdit>
    DrawGraphCommentEditor(Keire::UiFrame& ui, std::string_view popupId, const Keire::GraphAuthoringMetadata& metadata,
                           std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
                           std::span<const NodeGraphNode> nodes, const NodeGraphCommentModel& comments,
                           const NodeGraphCanvasResult& canvas, GraphCommentEditorState& editor);
    void ApplyGraphCommentCanvasEdit(Keire::GraphAuthoringMetadata& metadata, const GraphCommentCanvasEdit& edit);

    [[nodiscard]] float GraphCommentDisplayHeight(const NodeGraphComment& comment) noexcept;

    [[nodiscard]] NodeGraphCommentDragFrameResult
    ApplyGraphCommentDragFrame(NodeGraphComment& comment, Keire::Vector2& retainedPosition, Keire::Vector2 pointerDelta,
                               float zoom, bool pointerDown, bool pointerReleased) noexcept;

    [[nodiscard]] std::optional<StableNodeId>
    FindGraphCommentCollapseToggleAtPointer(std::span<const NodeGraphComment> comments, Keire::UiItemRect canvas,
                                            Keire::Vector2 pan, float zoom, Keire::UiPosition pointer);

    [[nodiscard]] NodeGraphCommentLayerResult DrawNodeGraphComments(Keire::UiFrame& ui,
                                                                    std::span<const NodeGraphComment> comments,
                                                                    Keire::UiItemRect canvas, Keire::Vector2 pan,
                                                                    float zoom, Keire::UiPosition pointer);
} // namespace KeireEditor
