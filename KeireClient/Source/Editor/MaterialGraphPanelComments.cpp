#include "KeireClient/Editor/MaterialGraphPanel.h"

#include <algorithm>

namespace KeireEditor
{
    void MaterialGraphPanel::CreateComment(Keire::UiFrame& ui, MaterialGraphDocument& document,
                                           const MaterialGraphCanvasModel& model, const Keire::Vector2 position,
                                           const bool selection)
    {
        NodeGraphCanvasResult canvas;
        canvas.CreateCommentRequested =
            selection ? StableNodeGraphCanvas::CommentFromSelection(model.Nodes, m_Canvas.Selections(), position)
                      : NodeGraphCommentCreateRequest{.Position = position};
        const auto comments = BuildNodeGraphCommentModel(document.Definition().Authoring, model.NodeIdentities);
        DrawComments(ui, document, model, comments, canvas);
    }

    void MaterialGraphPanel::DrawComments(Keire::UiFrame& ui, MaterialGraphDocument& document,
                                          const MaterialGraphCanvasModel& model, const NodeGraphCommentModel& comments,
                                          const NodeGraphCanvasResult& canvas)
    {
        const auto edit = DrawGraphCommentEditor(ui, "MaterialGraphCommentEditor", document.Definition().Authoring,
                                                 model.NodeIdentities, model.Nodes, comments, canvas, m_CommentEditor);
        if (!edit)
            return;

        try
        {
            const auto name = edit->Kind == GraphCommentCanvasEditKind::Create   ? "Created Material Graph comment"
                              : edit->Kind == GraphCommentCanvasEditKind::Delete ? "Deleted Material Graph comment"
                                                                                 : "Edited Material Graph comment";
            (void)document.Edit(
                name,
                [edit = *edit](Keire::MaterialGraphDefinition& definition)
                {
                    ApplyGraphCommentCanvasEdit(definition.Authoring, edit);
                    for (const auto& [id, position] : edit.MovedNodes)
                    {
                        if (id == definition.OutputNode)
                            definition.OutputPosition = position;
                        if (auto value = std::ranges::find(definition.Nodes, id, &Keire::MaterialGraphValueNode::Id);
                            value != definition.Nodes.end())
                            value->EditorPosition = position;
                        if (auto expression =
                                std::ranges::find(definition.SurfaceGraph.Nodes, id, &Keire::ShaderGraphNode::Id);
                            expression != definition.SurfaceGraph.Nodes.end())
                            expression->EditorPosition = position;
                    }
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
