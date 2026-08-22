#include "KeireClient/Editor/GraphComments.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace KeireEditor
{
    namespace
    {
        constexpr float HeaderHeight = 34.0F;
        constexpr float ResizeHandleSize = 16.0F;
        constexpr float CollapseArrowSize = 10.0F;

        [[nodiscard]] Keire::UiPosition ToScreen(const Keire::Vector2 position, const Keire::UiItemRect canvas,
                                                 const Keire::Vector2 pan, const float zoom) noexcept
        {
            return {canvas.Minimum.X + (position.X + pan.X) * zoom, canvas.Minimum.Y + (position.Y + pan.Y) * zoom};
        }

        [[nodiscard]] Keire::UiColor Opaque(const Keire::UiColor color, const float alpha) noexcept
        {
            return {color.Red, color.Green, color.Blue, alpha};
        }

    } // namespace

    std::optional<Keire::AssetId> NodeGraphCommentModel::Asset(const StableNodeId canvas) const noexcept
    {
        const auto found = std::ranges::find(Identities, canvas, &decltype(Identities)::value_type::first);
        return found == Identities.end() ? std::nullopt : std::optional(found->second);
    }

    void GraphCommentEditorState::Select(const Keire::GraphComment& comment)
    {
        if (m_Selection == comment.Id)
            return;
        m_Selection = comment.Id;
        m_Title = comment.Title;
        m_Description = comment.Description;
        m_Color = {comment.Tint.Red, comment.Tint.Green, comment.Tint.Blue, comment.Tint.Alpha};
        m_FontSize = comment.FontSize;
        m_MoveContents = comment.MoveMode == Keire::GraphCommentMoveMode::Group;
        m_Collapsed = comment.Collapsed;
    }

    GraphCommentEditorAction GraphCommentEditorState::Draw(Keire::UiFrame& ui, Keire::GraphComment& value)
    {
        Select(value);
        ui.TextColored({0.3F, 0.78F, 1.0F, 1.0F}, "GRAPH COMMENT");
        bool changed = ui.InputText("Title", m_Title);
        changed = ui.InputTextMultiline("Description", m_Description, 4) || changed;
        changed = ui.ColorEdit("Color / Alpha", m_Color) || changed;
        changed = ui.DragScalar("Font Size", m_FontSize, 0.5, 8.0, 96.0) || changed;
        changed = ui.Checkbox("Move Contents", m_MoveContents) || changed;
        changed = ui.Checkbox("Collapsed", m_Collapsed) || changed;
        if (changed)
        {
            value.Title = m_Title.empty() ? "Comment" : m_Title;
            value.Description = m_Description;
            value.Tint = {m_Color.Red, m_Color.Green, m_Color.Blue, m_Color.Alpha};
            value.FontSize = static_cast<float>(m_FontSize);
            value.MoveMode =
                m_MoveContents ? Keire::GraphCommentMoveMode::Group : Keire::GraphCommentMoveMode::CommentOnly;
            value.Collapsed = m_Collapsed;
            return GraphCommentEditorAction::Apply;
        }
        return ui.Button("Delete Comment") ? GraphCommentEditorAction::Delete : GraphCommentEditorAction::None;
    }

    NodeGraphCommentModel
    BuildNodeGraphCommentModel(const Keire::GraphAuthoringMetadata& metadata,
                               const std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities)
    {
        NodeGraphCommentModel result;
        StableNodeGraphIdMap ids;
        for (const auto& [canvas, asset] : nodeIdentities)
            (void)ids.Assign(asset, canvas);
        for (const auto& comment : metadata.Comments)
        {
            auto preferred = comment.Id.High() ^ comment.Id.Low() ^ 0x434f4d4d454e5401ULL;
            const auto canvas = ids.Assign(comment.Id, preferred == 0 ? 1 : preferred);
            result.Identities.emplace_back(canvas, comment.Id);
        }
        for (const auto& comment : metadata.Comments)
        {
            NodeGraphComment canvas;
            canvas.Id =
                std::ranges::find(result.Identities, comment.Id, &std::pair<StableNodeId, Keire::AssetId>::second)
                    ->first;
            canvas.Title = comment.Title;
            canvas.Description = comment.Description;
            canvas.Position = comment.Position;
            canvas.Size = comment.Size;
            canvas.Color = {comment.Tint.Red, comment.Tint.Green, comment.Tint.Blue, comment.Tint.Alpha};
            canvas.FontSize = comment.FontSize;
            canvas.MoveContents = comment.MoveMode == Keire::GraphCommentMoveMode::Group;
            canvas.Collapsed = comment.Collapsed;
            if (comment.Parent)
                canvas.Parent = ids.Find(comment.Parent);
            for (const auto member : comment.Members)
            {
                const auto node =
                    std::ranges::find(nodeIdentities, member, &std::pair<StableNodeId, Keire::AssetId>::second);
                if (node != nodeIdentities.end())
                    canvas.Members.push_back(node->first);
                else if (const auto nested = std::ranges::find(result.Identities, member,
                                                               &std::pair<StableNodeId, Keire::AssetId>::second);
                         nested != result.Identities.end())
                    canvas.Members.push_back(nested->first);
            }
            result.Comments.push_back(std::move(canvas));
        }
        return result;
    }

    void ApplyNodeGraphAnnotations(const Keire::GraphAuthoringMetadata& metadata,
                                   const std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
                                   const std::span<NodeGraphNode> nodes)
    {
        for (const auto& annotation : metadata.NodeAnnotations)
        {
            if (!annotation.Visible)
                continue;
            const auto identity =
                std::ranges::find(nodeIdentities, annotation.Node, &std::pair<StableNodeId, Keire::AssetId>::second);
            if (identity == nodeIdentities.end())
                continue;
            if (const auto node = std::ranges::find(nodes, identity->first, &NodeGraphNode::Id); node != nodes.end())
            {
                node->Comment = annotation.Text;
                node->CommentPinned = annotation.Pinned;
            }
        }
    }

    void SetGraphNodeAnnotation(Keire::GraphAuthoringMetadata& metadata, const Keire::AssetId node, std::string text,
                                const bool pinned)
    {
        const auto found = std::ranges::find(metadata.NodeAnnotations, node, &Keire::GraphNodeAnnotation::Node);
        if (text.empty())
        {
            if (found != metadata.NodeAnnotations.end())
                metadata.NodeAnnotations.erase(found);
            return;
        }
        if (found == metadata.NodeAnnotations.end())
        {
            metadata.NodeAnnotations.push_back({node, std::move(text), true, pinned});
            return;
        }
        found->Text = std::move(text);
        found->Visible = true;
        found->Pinned = pinned;
    }

    Keire::GraphComment
    CreateGraphComment(const NodeGraphCommentCreateRequest& request,
                       const std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities)
    {
        Keire::GraphComment result;
        result.Id = Keire::AssetId::Generate();
        result.Title = request.Members.empty() ? "Comment" : "Selection";
        result.Position = request.Position;
        result.Size = request.Size;
        for (const auto member : request.Members)
            if (const auto found =
                    std::ranges::find(nodeIdentities, member, &std::pair<StableNodeId, Keire::AssetId>::first);
                found != nodeIdentities.end())
                result.Members.push_back(found->second);
        return result;
    }

    std::optional<GraphCommentCanvasEdit>
    DrawGraphCommentEditor(Keire::UiFrame& ui, const std::string_view popupId,
                           const Keire::GraphAuthoringMetadata& metadata,
                           const std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
                           const std::span<const NodeGraphNode> nodes, const NodeGraphCommentModel& comments,
                           const NodeGraphCanvasResult& canvas, GraphCommentEditorState& editor)
    {
        if (canvas.CreateCommentRequested)
            return GraphCommentCanvasEdit{GraphCommentCanvasEditKind::Create,
                                          CreateGraphComment(*canvas.CreateCommentRequested, nodeIdentities)};

        const auto editComment = [&](const StableNodeId canvasId) -> std::optional<GraphCommentCanvasEdit>
        {
            const auto asset = comments.Asset(canvasId);
            const auto presentation = std::ranges::find(comments.Comments, canvasId, &NodeGraphComment::Id);
            const auto source = asset ? std::ranges::find(metadata.Comments, *asset, &Keire::GraphComment::Id)
                                      : metadata.Comments.end();
            if (!asset || presentation == comments.Comments.end() || source == metadata.Comments.end())
                return std::nullopt;
            GraphCommentCanvasEdit result{GraphCommentCanvasEditKind::Update, *source};
            result.Comment.Position = presentation->Position;
            result.Comment.Size = presentation->Size;
            for (const auto moved : canvas.CommentMemberNodes)
            {
                const auto identity =
                    std::ranges::find(nodeIdentities, moved, &std::pair<StableNodeId, Keire::AssetId>::first);
                const auto node = std::ranges::find(nodes, moved, &NodeGraphNode::Id);
                if (identity != nodeIdentities.end() && node != nodes.end())
                    result.MovedNodes.emplace_back(identity->second, node->Position);
            }
            for (const auto& [moved, position] : canvas.CommentMemberComments)
                if (const auto identity = comments.Asset(moved))
                    result.MovedComments.emplace_back(*identity, position);
            return result;
        };

        if (canvas.MoveCompletedComment)
            return editComment(*canvas.MoveCompletedComment);
        if (canvas.ResizeCompletedComment)
            return editComment(*canvas.ResizeCompletedComment);
        if (canvas.ToggleCommentCollapseRequested)
        {
            auto edit = editComment(*canvas.ToggleCommentCollapseRequested);
            if (edit)
                edit->Comment.Collapsed = !edit->Comment.Collapsed;
            return edit;
        }
        if (canvas.DeleteCommentRequested)
            if (const auto asset = comments.Asset(*canvas.DeleteCommentRequested))
                return GraphCommentCanvasEdit{GraphCommentCanvasEditKind::Delete, {.Id = *asset}};

        auto requestedEditor = canvas.ActivatedComment;
        if (canvas.ContextRequested && canvas.ContextRequested->Kind == NodeGraphContextTargetKind::Comment)
            requestedEditor = canvas.ContextRequested->Comment;
        if (canvas.RenameCommentRequested)
            requestedEditor = canvas.RenameCommentRequested;
        if (requestedEditor)
            if (const auto asset = comments.Asset(*requestedEditor))
                if (const auto source = std::ranges::find(metadata.Comments, *asset, &Keire::GraphComment::Id);
                    source != metadata.Comments.end())
                {
                    editor.Select(*source);
                    if (canvas.RenameCommentRequested ||
                        (canvas.ContextRequested &&
                         canvas.ContextRequested->Kind == NodeGraphContextTargetKind::Comment))
                        ui.OpenPopup(popupId);
                }

        if (auto popup = ui.BeginPopup(popupId); popup)
        {
            const auto selection = editor.Selection();
            const auto source = selection ? std::ranges::find(metadata.Comments, *selection, &Keire::GraphComment::Id)
                                          : metadata.Comments.end();
            if (source == metadata.Comments.end())
            {
                ui.Text("The graph comment is no longer available.");
                return std::nullopt;
            }
            auto value = *source;
            const auto action = editor.Draw(ui, value);
            if (action == GraphCommentEditorAction::Apply)
                return GraphCommentCanvasEdit{GraphCommentCanvasEditKind::Update, std::move(value)};
            if (action == GraphCommentEditorAction::Delete)
                return GraphCommentCanvasEdit{GraphCommentCanvasEditKind::Delete, std::move(value)};
        }
        return std::nullopt;
    }

    void ApplyGraphCommentCanvasEdit(Keire::GraphAuthoringMetadata& metadata, const GraphCommentCanvasEdit& edit)
    {
        if (edit.Kind == GraphCommentCanvasEditKind::Create)
        {
            auto comment = edit.Comment;
            for (auto& candidate : metadata.Comments)
                for (const auto member : comment.Members)
                    std::erase(candidate.Members, member);

            Keire::GraphComment* parent = nullptr;
            float parentArea = std::numeric_limits<float>::max();
            const Keire::Vector2 center{comment.Position.X + comment.Size.X * 0.5F,
                                        comment.Position.Y + comment.Size.Y * 0.5F};
            for (auto& candidate : metadata.Comments)
            {
                const bool contains = center.X >= candidate.Position.X && center.Y >= candidate.Position.Y &&
                                      center.X <= candidate.Position.X + candidate.Size.X &&
                                      center.Y <= candidate.Position.Y + candidate.Size.Y;
                const float area = candidate.Size.X * candidate.Size.Y;
                if (contains && area < parentArea)
                {
                    parent = &candidate;
                    parentArea = area;
                }
            }
            if (parent)
            {
                comment.Parent = parent->Id;
                parent->Members.push_back(comment.Id);
            }
            metadata.Comments.push_back(std::move(comment));
            return;
        }
        if (edit.Kind == GraphCommentCanvasEditKind::Delete)
        {
            Keire::RemoveGraphComment(metadata, edit.Comment.Id);
            return;
        }
        const auto found = std::ranges::find(metadata.Comments, edit.Comment.Id, &Keire::GraphComment::Id);
        if (found == metadata.Comments.end())
            throw std::invalid_argument("Graph comment is unavailable.");
        *found = edit.Comment;
        for (const auto& [id, position] : edit.MovedComments)
            if (auto moved = std::ranges::find(metadata.Comments, id, &Keire::GraphComment::Id);
                moved != metadata.Comments.end())
                moved->Position = position;
    }

    float GraphCommentDisplayHeight(const NodeGraphComment& comment) noexcept
    {
        if (!comment.Collapsed)
            return comment.Size.Y;
        return std::max(HeaderHeight,
                        HeaderHeight +
                            static_cast<float>(std::max(comment.SummaryInputs, comment.SummaryOutputs)) * 20.0F);
    }

    NodeGraphCommentDragFrameResult ApplyGraphCommentDragFrame(NodeGraphComment& comment,
                                                               Keire::Vector2& retainedPosition,
                                                               const Keire::Vector2 pointerDelta, const float zoom,
                                                               const bool pointerDown,
                                                               const bool pointerReleased) noexcept
    {
        if (!pointerDown && !pointerReleased)
            return {};

        const float inverseZoom = zoom > 0.0F ? 1.0F / zoom : 1.0F;
        const Keire::Vector2 delta{pointerDelta.X * inverseZoom, pointerDelta.Y * inverseZoom};
        retainedPosition.X += delta.X;
        retainedPosition.Y += delta.Y;
        comment.Position = retainedPosition;
        return {true, delta};
    }

    std::optional<StableNodeId>
    FindGraphCommentCollapseToggleAtPointer(const std::span<const NodeGraphComment> comments,
                                            const Keire::UiItemRect canvas, const Keire::Vector2 pan, const float zoom,
                                            const Keire::UiPosition pointer)
    {
        for (auto comment = comments.rbegin(); comment != comments.rend(); ++comment)
        {
            const auto minimum = ToScreen(comment->Position, canvas, pan, zoom);
            const float height = GraphCommentDisplayHeight(*comment) * zoom;
            const Keire::UiItemRect rectangle{minimum, {minimum.X + comment->Size.X * zoom, minimum.Y + height}};
            if (!rectangle.Contains(pointer))
                continue;

            const float headerHeight = std::min(HeaderHeight * zoom, height);
            const float arrowHitWidth = std::clamp(28.0F * zoom, 22.0F, 28.0F);
            if (pointer.Y > minimum.Y + headerHeight || pointer.X > minimum.X + arrowHitWidth)
                return std::nullopt;

            return comment->Id;
        }
        return std::nullopt;
    }

    NodeGraphCommentLayerResult DrawNodeGraphComments(Keire::UiFrame& ui,
                                                      const std::span<const NodeGraphComment> comments,
                                                      const Keire::UiItemRect canvas, const Keire::Vector2 pan,
                                                      const float zoom, const Keire::UiPosition pointer)
    {
        for (const auto& comment : comments)
        {
            const auto minimum = ToScreen(comment.Position, canvas, pan, zoom);
            const float height = GraphCommentDisplayHeight(comment) * zoom;
            const Keire::UiItemRect rectangle{minimum, {minimum.X + comment.Size.X * zoom, minimum.Y + height}};
            const Keire::UiItemRect header{minimum,
                                           {rectangle.Maximum.X, minimum.Y + std::min(HeaderHeight * zoom, height)}};
            ui.DrawFilledRectangle(rectangle, Opaque(comment.Color, comment.Collapsed ? 0.72F : comment.Color.Alpha),
                                   6.0F);
            ui.DrawFilledRectangle(header, Opaque(comment.Color, 0.9F), 6.0F);
            ui.DrawRectangle(rectangle, Opaque(comment.Color, 1.0F), 1.5F, 6.0F);
            const float arrowSize = std::clamp(CollapseArrowSize * zoom, 7.0F, CollapseArrowSize);
            const Keire::UiPosition arrowCenter{header.Minimum.X + std::clamp(14.0F * zoom, 10.0F, 14.0F),
                                                header.Minimum.Y + header.Size().Height * 0.5F};
            if (comment.Collapsed)
                ui.DrawFilledTriangle({arrowCenter.X - arrowSize * 0.35F, arrowCenter.Y - arrowSize * 0.5F},
                                      {arrowCenter.X - arrowSize * 0.35F, arrowCenter.Y + arrowSize * 0.5F},
                                      {arrowCenter.X + arrowSize * 0.5F, arrowCenter.Y}, {0.97F, 0.98F, 1.0F, 0.95F});
            else
                ui.DrawFilledTriangle({arrowCenter.X - arrowSize * 0.5F, arrowCenter.Y - arrowSize * 0.35F},
                                      {arrowCenter.X + arrowSize * 0.5F, arrowCenter.Y - arrowSize * 0.35F},
                                      {arrowCenter.X, arrowCenter.Y + arrowSize * 0.5F}, {0.97F, 0.98F, 1.0F, 0.95F});
            ui.DrawOverlayText({header.Minimum.X + std::clamp(28.0F * zoom, 22.0F, 28.0F),
                                header.Minimum.Y + std::clamp(6.0F * zoom, 2.0F, 6.0F)},
                               {0.97F, 0.98F, 1.0F, 1.0F}, comment.Title,
                               std::clamp(comment.FontSize * zoom, 8.0F, comment.FontSize), header);
            if (!comment.Collapsed && !comment.Description.empty() && zoom >= 0.55F)
                ui.DrawOverlayText({rectangle.Minimum.X + 10.0F, header.Maximum.Y + 8.0F}, {0.82F, 0.86F, 0.94F, 0.9F},
                                   comment.Description, std::clamp(12.0F * zoom, 9.0F, 12.0F), rectangle);
            if (!comment.Collapsed)
            {
                const float handleSize = std::clamp(ResizeHandleSize * zoom, 12.0F, ResizeHandleSize);
                const Keire::UiItemRect handle{{rectangle.Maximum.X - handleSize, rectangle.Maximum.Y - handleSize},
                                               rectangle.Maximum};
                ui.DrawFilledTriangle(handle.Minimum, {handle.Maximum.X, handle.Minimum.Y}, handle.Maximum,
                                      Opaque(comment.Color, 0.95F));
                ui.DrawLine({handle.Minimum.X + 3.0F, handle.Maximum.Y - 2.0F},
                            {handle.Maximum.X - 2.0F, handle.Minimum.Y + 3.0F}, {0.96F, 0.98F, 1.0F, 0.9F}, 1.5F);
            }
        }

        NodeGraphCommentLayerResult result;
        for (auto comment = comments.rbegin(); comment != comments.rend(); ++comment)
        {
            const auto minimum = ToScreen(comment->Position, canvas, pan, zoom);
            const float height = GraphCommentDisplayHeight(*comment) * zoom;
            const Keire::UiItemRect rectangle{minimum, {minimum.X + comment->Size.X * zoom, minimum.Y + height}};
            if (!rectangle.Contains(pointer))
                continue;
            result.Hovered = comment->Id;
            result.Header = pointer.Y <= minimum.Y + HeaderHeight * zoom;
            const float arrowHitWidth = std::clamp(28.0F * zoom, 22.0F, 28.0F);
            result.CollapseToggle = result.Header && pointer.X <= rectangle.Minimum.X + arrowHitWidth;
            const float handleSize = std::clamp(ResizeHandleSize * zoom, 12.0F, ResizeHandleSize);
            result.ResizeHandle = !comment->Collapsed && pointer.X >= rectangle.Maximum.X - handleSize &&
                                  pointer.Y >= rectangle.Maximum.Y - handleSize;
            break;
        }
        return result;
    }
} // namespace KeireEditor
