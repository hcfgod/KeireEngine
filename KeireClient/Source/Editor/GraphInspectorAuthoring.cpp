#include "KeireClient/Editor/MaterialGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::string ShaderKindLabel(const Keire::ShaderGraphNodeKind kind)
        {
            switch (kind)
            {
            case Keire::ShaderGraphNodeKind::Constant:
                return "Constant";
            case Keire::ShaderGraphNodeKind::Parameter:
                return "Parameter";
            case Keire::ShaderGraphNodeKind::Add:
                return "Add";
            case Keire::ShaderGraphNodeKind::Multiply:
                return "Multiply";
            case Keire::ShaderGraphNodeKind::Reroute:
                return "Reroute";
            case Keire::ShaderGraphNodeKind::Keyword:
                return "Keyword";
            case Keire::ShaderGraphNodeKind::Custom:
                return "Custom Function";
            case Keire::ShaderGraphNodeKind::Master:
                return "Output";
            case Keire::ShaderGraphNodeKind::FunctionCall:
                return "Function Call";
            default:
                break;
            }
            return "Kind " + std::to_string(static_cast<unsigned int>(kind));
        }

        [[nodiscard]] std::string_view VfxKindLabel(const Keire::VfxGraphNodeKind kind) noexcept
        {
            switch (kind)
            {
            case Keire::VfxGraphNodeKind::Context:
                return "Context";
            case Keire::VfxGraphNodeKind::Module:
                return "Module";
            case Keire::VfxGraphNodeKind::Parameter:
                return "Parameter";
            case Keire::VfxGraphNodeKind::CustomHlsl:
                return "Custom HLSL";
            case Keire::VfxGraphNodeKind::Operator:
                return "Operator";
            case Keire::VfxGraphNodeKind::Attribute:
                return "Attribute";
            case Keire::VfxGraphNodeKind::Subgraph:
                return "Subgraph";
            }
            return "Unknown";
        }

        template <typename Range, typename Projection>
        [[nodiscard]] std::optional<std::string> CommonString(const Range& values, Projection projection)
        {
            if (values.empty())
                return std::nullopt;
            const auto projected = std::invoke(projection, values.front());
            const std::string first(projected);
            if (!std::ranges::all_of(values,
                                     [&](const auto& value) { return std::invoke(projection, value) == first; }))
                return std::nullopt;
            return first;
        }
    } // namespace

    bool ShaderGraphPanel::DrawMultiSelectionInspector(Keire::UiFrame& ui)
    {
        if (m_SelectedNodes.size() < 2)
            return false;

        const auto& definition = m_Controller.ShaderGraphState().Definition();
        std::vector<const Keire::ShaderGraphNode*> nodes;
        nodes.reserve(m_SelectedNodes.size());
        for (const auto id : m_SelectedNodes)
        {
            const auto found = std::ranges::find(definition.Nodes, id, &Keire::ShaderGraphNode::Id);
            if (found != definition.Nodes.end())
                nodes.push_back(&*found);
        }

        ui.TextColored(m_Controller.ShaderGraphTheme().Accent, "MULTI-SELECTION");
        ui.Text(std::to_string(nodes.size()) + " nodes selected");
        if (const auto kind = CommonString(nodes, [](const auto* node) { return ShaderKindLabel(node->Kind); }))
            ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, "Common type: " + *kind);
        else
            ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, "Common type: Mixed");
        if (const auto name = CommonString(nodes, [](const auto* node) -> std::string_view { return node->Name; }))
            ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, "Common name: " + *name);
        ui.TextColored(m_Controller.ShaderGraphTheme().MutedText,
                       "Arrange, copy, duplicate, frame, or delete the selection from the graph.");
        return true;
    }

    bool MaterialGraphPanel::DrawMultiSelectionInspector(Keire::UiFrame& ui)
    {
        if (m_SelectedNodes.size() < 2)
            return false;

        const auto& definition = m_Controller.MaterialGraphState().Definition();
        std::vector<std::string> kinds;
        std::vector<std::string> names;
        kinds.reserve(m_SelectedNodes.size());
        names.reserve(m_SelectedNodes.size());
        for (const auto id : m_SelectedNodes)
        {
            if (id == definition.OutputNode)
            {
                kinds.emplace_back("Output");
                names.emplace_back("Material Output");
                continue;
            }
            if (const auto expression =
                    std::ranges::find(definition.SurfaceGraph.Nodes, id, &Keire::ShaderGraphNode::Id);
                expression != definition.SurfaceGraph.Nodes.end())
            {
                kinds.emplace_back(ShaderKindLabel(expression->Kind));
                names.push_back(expression->Name);
                continue;
            }
            if (const auto value = std::ranges::find(definition.Nodes, id, &Keire::MaterialGraphValueNode::Id);
                value != definition.Nodes.end())
            {
                kinds.emplace_back("Material Value");
                names.push_back(value->Name);
            }
        }

        const auto& theme = m_Controller.MaterialGraphTheme();
        ui.TextColored(theme.Accent, "MULTI-SELECTION");
        ui.Text(std::to_string(kinds.size()) + " nodes selected");
        if (const auto kind = CommonString(kinds, [](const auto& value) -> std::string_view { return value; }))
            ui.TextColored(theme.MutedText, "Common type: " + *kind);
        else
            ui.TextColored(theme.MutedText, "Common type: Mixed");
        if (const auto name = CommonString(names, [](const auto& value) -> std::string_view { return value; }))
            ui.TextColored(theme.MutedText, "Common name: " + *name);
        ui.TextColored(theme.MutedText, "Arrange, copy, duplicate, frame, or delete the selection from the graph.");
        return true;
    }

    bool MaterialGraphPanel::DrawNodeCommentInspector(Keire::UiFrame& ui)
    {
        if (!m_SelectedNode)
            return false;

        auto& document = m_Controller.MaterialGraphState();
        const auto id = *m_SelectedNode;
        if (m_InspectorCommentNode != id)
        {
            m_InspectorCommentNode = id;
            const auto annotation = std::ranges::find(document.Definition().Authoring.NodeAnnotations, id,
                                                      &Keire::GraphNodeAnnotation::Node);
            m_InspectorComment =
                annotation == document.Definition().Authoring.NodeAnnotations.end() ? std::string{} : annotation->Text;
            m_InspectorCommentPinned =
                annotation != document.Definition().Authoring.NodeAnnotations.end() && annotation->Pinned;
        }

        const bool textChanged = ui.InputTextMultiline("Node Comment", m_InspectorComment, 3);
        const bool pinnedChanged = ui.Checkbox("Pin Comment Bubble", m_InspectorCommentPinned);
        if (!textChanged && !pinnedChanged)
            return false;

        try
        {
            (void)document.Edit("Edit Material Graph node comment",
                                [id, text = m_InspectorComment, pinned = m_InspectorCommentPinned](auto& definition)
                                { SetGraphNodeAnnotation(definition.Authoring, id, text, pinned); });
        }
        catch (const std::exception& error)
        {
            Report(error.what());
        }
        return true;
    }

    bool VfxEffectPanel::DrawGraphMultiSelectionInspector(Keire::UiFrame& ui)
    {
        if (m_SelectedNodes.size() < 2)
            return false;

        const auto& definition = m_Controller.VfxEffectState().Definition();
        const auto system = std::ranges::find(definition.Systems, m_SelectedSystem, &Keire::VfxGraphSystem::Id);
        if (system == definition.Systems.end())
            return false;
        std::vector<const Keire::VfxGraphNode*> nodes;
        nodes.reserve(m_SelectedNodes.size());
        for (const auto id : m_SelectedNodes)
        {
            const auto found = std::ranges::find(system->Nodes, id, &Keire::VfxGraphNode::Id);
            if (found != system->Nodes.end())
                nodes.push_back(&*found);
        }

        const auto& theme = m_Controller.VfxEffectTheme();
        ui.TextColored(theme.Accent, "MULTI-SELECTION");
        ui.Text(std::to_string(nodes.size()) + " nodes selected");
        if (const auto kind = CommonString(nodes, [](const auto* node) { return VfxKindLabel(node->Kind); }))
            ui.TextColored(theme.MutedText, "Common type: " + *kind);
        else
            ui.TextColored(theme.MutedText, "Common type: Mixed");
        if (const auto name = CommonString(nodes, [](const auto* node) -> std::string_view { return node->Type; }))
            ui.TextColored(theme.MutedText, "Common name: " + *name);
        ui.TextColored(theme.MutedText, "Arrange, copy, duplicate, frame, or delete the selection from the graph.");
        return true;
    }

    bool VfxEffectPanel::DrawGraphNodeComment(Keire::UiFrame& ui, const Keire::AssetId system,
                                              const Keire::VfxGraphNode& node)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto graph = std::ranges::find(document.Definition().Systems, system, &Keire::VfxGraphSystem::Id);
        if (graph == document.Definition().Systems.end())
            return false;
        if (m_InspectorCommentNode != node.Id)
        {
            m_InspectorCommentNode = node.Id;
            const auto annotation =
                std::ranges::find(graph->Authoring.NodeAnnotations, node.Id, &Keire::GraphNodeAnnotation::Node);
            m_InspectorComment =
                annotation == graph->Authoring.NodeAnnotations.end() ? std::string{} : annotation->Text;
            m_InspectorCommentPinned = annotation != graph->Authoring.NodeAnnotations.end() && annotation->Pinned;
        }

        const bool textChanged = ui.InputTextMultiline("Node Comment", m_InspectorComment, 3);
        const bool pinnedChanged = ui.Checkbox("Pin Comment Bubble", m_InspectorCommentPinned);
        if (!textChanged && !pinnedChanged)
            return false;

        return ApplyEdit("Edit VFX graph node comment",
                         [system, node = node.Id, text = m_InspectorComment,
                          pinned = m_InspectorCommentPinned](Keire::VfxEffectDefinition& definition)
                         {
                             const auto target =
                                 std::ranges::find(definition.Systems, system, &Keire::VfxGraphSystem::Id);
                             if (target == definition.Systems.end())
                                 throw std::invalid_argument("VFX graph system is unavailable.");
                             SetGraphNodeAnnotation(target->Authoring, node, text, pinned);
                         });
    }
} // namespace KeireEditor
