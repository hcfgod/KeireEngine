#include "KeireClient/Editor/ShaderGraphPanel.h"

#include <algorithm>

namespace KeireEditor
{
    void ShaderGraphPanel::CreateComment(Keire::UiFrame& ui, ShaderGraphDocument& document,
                                         const ShaderGraphCanvasModel& model, const Keire::Vector2 position,
                                         const bool selection)
    {
        NodeGraphCanvasResult canvas;
        canvas.CreateCommentRequested =
            selection ? StableNodeGraphCanvas::CommentFromSelection(model.Nodes, m_Canvas.Selections(), position)
                      : NodeGraphCommentCreateRequest{.Position = position};
        const auto comments = BuildNodeGraphCommentModel(document.Definition().Authoring, model.NodeIdentities);
        DrawComments(ui, document, model, comments, canvas);
    }

    void ShaderGraphPanel::DrawComments(Keire::UiFrame& ui, ShaderGraphDocument& document,
                                        const ShaderGraphCanvasModel& model, const NodeGraphCommentModel& comments,
                                        const NodeGraphCanvasResult& canvas)
    {
        const auto edit = DrawGraphCommentEditor(ui, "ShaderGraphCommentEditor", document.Definition().Authoring,
                                                 model.NodeIdentities, model.Nodes, comments, canvas, m_CommentEditor);
        if (!edit)
            return;

        try
        {
            const auto name = edit->Kind == GraphCommentCanvasEditKind::Create   ? "Created Shader Graph comment"
                              : edit->Kind == GraphCommentCanvasEditKind::Delete ? "Deleted Shader Graph comment"
                                                                                 : "Edited Shader Graph comment";
            (void)document.Edit(name,
                                [edit = *edit](Keire::ShaderGraphDefinition& definition)
                                {
                                    ApplyGraphCommentCanvasEdit(definition.Authoring, edit);
                                    for (const auto& [id, position] : edit.MovedNodes)
                                        if (auto node =
                                                std::ranges::find(definition.Nodes, id, &Keire::ShaderGraphNode::Id);
                                            node != definition.Nodes.end())
                                            node->EditorPosition = position;
                                });
            if (edit->Kind == GraphCommentCanvasEditKind::Delete)
                m_CommentEditor.Clear();
        }
        catch (const std::exception& error)
        {
            Report(error.what());
        }
    }
} // namespace KeireEditor
