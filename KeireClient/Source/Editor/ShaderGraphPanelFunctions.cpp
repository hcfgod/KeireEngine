#include "KeireClient/Editor/ShaderGraphPanel.h"

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
                                           const std::optional<Keire::Vector2> graphPosition)
    {
        try
        {
            const auto function = m_Controller.ResolveShaderGraphFunction(asset);
            if (!function)
                throw std::runtime_error("The reusable graph source is unavailable.");
            auto node = Keire::CreateShaderGraphFunctionCallNode(asset, *function);
            node.Name = std::string(name);
            node.EditorPosition = graphPosition.value_or(Keire::Vector2{-m_Canvas.Pan().X + 280.0F / m_Canvas.Zoom(),
                                                                        -m_Canvas.Pan().Y + 180.0F / m_Canvas.Zoom()});
            const auto id = node.Id;
            if (!m_Controller.ShaderGraphState().AddNode(std::move(node)))
                return false;
            m_SelectedNode = id;
            return true;
        }
        catch (const std::exception& error)
        {
            Report(error.what());
            return false;
        }
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
