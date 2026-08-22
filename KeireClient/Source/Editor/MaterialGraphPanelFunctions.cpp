#include "KeireClient/Editor/MaterialGraphPanel.h"

#include <algorithm>
#include <stdexcept>

namespace KeireEditor
{
    bool MaterialGraphPanel::AddFunctionNode(const Keire::AssetId asset, const std::string_view name,
                                             const std::optional<Keire::Vector2> position)
    {
        try
        {
            const auto function = m_Controller.ResolveMaterialGraphFunction(asset);
            if (!function)
                throw std::runtime_error("The reusable material graph source is unavailable.");
            auto node = Keire::CreateShaderGraphFunctionCallNode(asset, *function);
            node.Name = std::string(name);
            node.EditorPosition = position.value_or(Keire::Vector2{120.0F, 120.0F});
            const auto id = node.Id;
            if (!m_Controller.MaterialGraphState().AddExpressionNode(std::move(node)))
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

    void MaterialGraphPanel::DrawFunctionExtractionContextMenu(Keire::UiFrame& ui,
                                                               const Keire::MaterialGraphDefinition& definition)
    {
        const bool extractable =
            !m_SelectedNodes.empty() &&
            std::ranges::all_of(m_SelectedNodes,
                                [&](const Keire::AssetId selected)
                                {
                                    const auto candidate = std::ranges::find(definition.SurfaceGraph.Nodes, selected,
                                                                             &Keire::ShaderGraphNode::Id);
                                    return candidate != definition.SurfaceGraph.Nodes.end() &&
                                           candidate->Kind != Keire::ShaderGraphNodeKind::Master &&
                                           candidate->Kind != Keire::ShaderGraphNodeKind::Parameter;
                                });
        if (ui.MenuItem("Extract Selection to Material Function...", false, extractable))
        {
            m_ExtractionName = "ExtractedMaterialFunction";
            ui.OpenPopup("ExtractMaterialGraphFunction");
        }
    }

    bool MaterialGraphPanel::DrawFunctionExtractionPopup(Keire::UiFrame& ui)
    {
        auto popup = ui.BeginPopupModal("ExtractMaterialGraphFunction");
        if (!popup)
            return false;
        ui.Text(
            "Create a reusable Material Function beside the current graph and replace the selection with its call.");
        (void)ui.InputText("Function Name", m_ExtractionName);
        bool extracted = false;
        if (auto disabled = ui.BeginDisabled(m_ExtractionName.empty()); disabled)
            if (ui.Button("Extract"))
            {
                extracted = m_Controller.ExtractMaterialGraphSelectionToFunction(m_SelectedNodes, m_ExtractionName);
                if (extracted)
                    ui.CloseCurrentPopup();
            }
        ui.SameLine();
        if (ui.Button("Cancel"))
            ui.CloseCurrentPopup();
        return extracted;
    }
} // namespace KeireEditor
