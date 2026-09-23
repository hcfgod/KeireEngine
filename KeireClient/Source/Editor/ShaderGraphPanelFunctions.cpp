#include "KeireClient/Editor/ShaderGraphPanel.h"

#include "KeireClient/Editor/ShaderGraphBlackboard.h"

#include <algorithm>
#include <stdexcept>

namespace KeireEditor
{
    bool ShaderGraphPanel::CanExtractSelection(const Keire::ShaderGraphDefinition& definition) const
    {
        return !m_SelectedNodes.empty() &&
               std::ranges::all_of(m_SelectedNodes,
                                   [&](const Keire::AssetId selected)
                                   {
                                       const auto node =
                                           std::ranges::find(definition.Nodes, selected, &Keire::ShaderGraphNode::Id);
                                       return node != definition.Nodes.end() &&
                                              node->Kind != Keire::ShaderGraphNodeKind::Master &&
                                              node->Kind != Keire::ShaderGraphNodeKind::Parameter;
                                   });
    }

    bool ShaderGraphPanel::AddFunctionNode(const Keire::AssetId asset, const std::string_view name,
                                           const std::optional<Keire::Vector2> graphPosition,
                                           const std::optional<Keire::ShaderGraphEndpoint> anchor,
                                           const std::optional<Keire::AssetId> insertion)
    {
        try
        {
            const auto function = m_Controller.ResolveShaderGraphFunction(asset);
            if (!function)
                throw std::runtime_error("The reusable graph source is unavailable.");
            auto node = Keire::CreateShaderGraphFunctionCallNode(asset, *function);
            node.Name = std::string(name);
            if (graphPosition)
                node.EditorPosition = *graphPosition;
            else
            {
                const Keire::Vector2 preferred{-m_Canvas.Pan().X + 280.0F / m_Canvas.Zoom(),
                                               -m_Canvas.Pan().Y + 180.0F / m_Canvas.Zoom()};
                const Keire::Vector2 nodeSize{220.0F,
                                              std::max(72.0F, 42.0F + static_cast<float>(node.Pins.size()) * 20.0F)};
                node.EditorPosition = ResolveGraphNodePlacement(
                    m_Controller.ShaderGraphState().BuildCanvasModel().Nodes, preferred, nodeSize);
            }
            return CommitCreatedNode(std::move(node), anchor, insertion);
        }
        catch (const std::exception& error)
        {
            Report(error.what());
            return false;
        }
    }

    bool ShaderGraphPanel::CommitCreatedNode(Keire::ShaderGraphNode node,
                                             const std::optional<Keire::ShaderGraphEndpoint> anchor,
                                             const std::optional<Keire::AssetId> insertion)
    {
        const auto id = node.Id;
        auto& document = m_Controller.ShaderGraphState();
        bool changed = false;
        if (insertion)
            changed = document.InsertNode(std::move(node), *insertion);
        else if (anchor)
            changed = document.AddConnectedNode(std::move(node), *anchor);
        else if (node.Kind == Keire::ShaderGraphNodeKind::Keyword)
            changed =
                document.Edit("Add Shader Graph keyword",
                              [node = std::move(node)](auto& definition) mutable
                              {
                                  if (!ShaderGraphHasKeywordToken(definition, node.Symbol))
                                      definition.Keywords.push_back({.Name = node.Symbol, .DefaultOption = "false"});
                                  definition.Nodes.push_back(std::move(node));
                              });
        else
            changed = document.AddNode(std::move(node));
        if (changed)
        {
            m_SelectedNode = id;
            m_SelectedNodes = {id};
            m_SelectedConnection.reset();
            m_FrameNode = id;
            m_FramePin.reset();
        }
        return changed;
    }

    bool ShaderGraphPanel::DrawFunctionExtractionPopup(Keire::UiFrame& ui)
    {
        auto popup = ui.BeginPopupModal("ExtractShaderGraphFunction");
        if (!popup)
            return false;
        ui.Text("Create a reusable Shader Function beside the current graph and replace the selection with its call.");
        (void)ui.InputText("Function Name", m_ExtractionName);
        bool extracted = false;
        if (auto disabled = ui.BeginDisabled(m_ExtractionName.empty()); disabled)
            if (ui.Button("Extract"))
            {
                extracted =
                    m_Controller.ExtractShaderGraphSelectionToFunction(m_FunctionExtractionSelection, m_ExtractionName);
                if (extracted)
                {
                    m_FunctionExtractionSelection.clear();
                    ui.CloseCurrentPopup();
                }
            }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_FunctionExtractionSelection.clear();
            ui.CloseCurrentPopup();
        }
        return extracted;
    }
} // namespace KeireEditor
