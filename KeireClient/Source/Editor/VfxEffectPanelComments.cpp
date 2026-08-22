#include "KeireClient/Editor/VfxEffectPanel.h"

#include "KeireClient/Editor/VfxEffectDocument.h"

#include <algorithm>
#include <stdexcept>

namespace KeireEditor
{
    void
    VfxEffectPanel::CreateGraphComment(Keire::UiFrame& ui, const Keire::AssetId system,
                                       const std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
                                       const std::span<const NodeGraphNode> nodes, const Keire::Vector2 position,
                                       const bool selection)
    {
        const auto& document = m_Controller.VfxEffectState();
        const auto graph = std::ranges::find(document.Definition().Systems, system, &Keire::VfxGraphSystem::Id);
        if (graph == document.Definition().Systems.end())
            return;
        NodeGraphCanvasResult canvas;
        canvas.CreateCommentRequested =
            selection ? StableNodeGraphCanvas::CommentFromSelection(nodes, m_GraphCanvas.Selections(), position)
                      : NodeGraphCommentCreateRequest{.Position = position};
        const auto comments = BuildNodeGraphCommentModel(graph->Authoring, nodeIdentities);
        DrawGraphComments(ui, system, nodeIdentities, nodes, comments, canvas);
    }

    void
    VfxEffectPanel::DrawGraphComments(Keire::UiFrame& ui, const Keire::AssetId system,
                                      const std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
                                      const std::span<const NodeGraphNode> nodes, const NodeGraphCommentModel& comments,
                                      const NodeGraphCanvasResult& canvas)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto graph = std::ranges::find(document.Definition().Systems, system, &Keire::VfxGraphSystem::Id);
        if (graph == document.Definition().Systems.end())
            return;
        const auto edit = DrawGraphCommentEditor(ui, "VfxGraphCommentEditor", graph->Authoring, nodeIdentities, nodes,
                                                 comments, canvas, m_CommentEditor);
        if (!edit)
            return;

        const auto name = edit->Kind == GraphCommentCanvasEditKind::Create   ? "Created VFX graph comment"
                          : edit->Kind == GraphCommentCanvasEditKind::Delete ? "Deleted VFX graph comment"
                                                                             : "Edited VFX graph comment";
        if (ApplyEdit(name,
                      [system, edit = *edit](Keire::VfxEffectDefinition& definition)
                      {
                          auto target = std::ranges::find(definition.Systems, system, &Keire::VfxGraphSystem::Id);
                          if (target == definition.Systems.end())
                              throw std::invalid_argument("VFX graph system is unavailable.");
                          ApplyGraphCommentCanvasEdit(target->Authoring, edit);
                          for (const auto& [id, position] : edit.MovedNodes)
                              if (auto node = std::ranges::find(target->Nodes, id, &Keire::VfxGraphNode::Id);
                                  node != target->Nodes.end())
                                  node->EditorPosition = position;
                      }) &&
            edit->Kind == GraphCommentCanvasEditKind::Delete)
            m_CommentEditor.Clear();
    }
} // namespace KeireEditor
