#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphSourceView.h"

#include <algorithm>
#include <limits>
#include <ranges>

namespace KeireEditor
{
    void ShaderGraphPanel::DrawGeneratedSource(Keire::UiFrame& ui)
    {
        auto tree = ui.BeginTreeNode("Generated Source");
        if (!tree)
            return;
        const auto& document = m_Controller.ShaderGraphState();
        const auto* compilation = &document.Compilation();
        if (compilation->Variants.empty() && document.LastGoodCompilation())
        {
            compilation = &*document.LastGoodCompilation();
            ui.TextWrapped("Showing the last successful generated source; the current graph has no generated variant.");
        }
        else if (document.CompilationPending())
            ui.TextWrapped("Compilation pending. This source belongs to the previous generation.");
        if (compilation->Variants.empty())
        {
            ui.TextWrapped("No generated source is available. Reusable graphs generate code when used by a shader.");
            return;
        }
        m_SourceVariant = std::min(m_SourceVariant, compilation->Variants.size() - 1);
        const auto label = [](const Keire::ShaderGraphShaderVariant& variant)
        {
            std::string result;
            for (const auto& keyword : variant.Keywords)
            {
                if (!result.empty())
                    result += ", ";
                result += keyword;
            }
            return result.empty() ? std::string("Default") : result;
        };
        if (auto combo = ui.BeginCombo("Source Variant", label(compilation->Variants[m_SourceVariant])); combo)
            for (std::size_t index = 0; index < compilation->Variants.size(); ++index)
            {
                auto id = ui.PushId(std::to_string(index));
                if (ui.Selectable(label(compilation->Variants[index]), index == m_SourceVariant))
                {
                    m_SourceVariant = index;
                    m_SourceLine = 1;
                }
            }
        const auto& variant = compilation->Variants[m_SourceVariant];
        ui.TextWrapped(variant.GeneratedSource.generic_string());
        if (ui.Button("Copy HLSL"))
            m_Controller.SetGraphClipboard(variant.Hlsl);
        if (ui.Button("Copy Reflection Manifest"))
            m_Controller.SetGraphClipboard(variant.Manifest);
        if (m_SelectedNode)
        {
            const auto& nodes = document.Definition().Nodes;
            const auto node = std::ranges::find(nodes, *m_SelectedNode, &Keire::ShaderGraphNode::Id);
            if (node != nodes.end() && !node->Symbol.empty() && ui.Button("Find Selected Symbol"))
            {
                auto line = FindShaderGraphSourceSymbol(variant.Hlsl, "_KeireMaterial_" + node->Symbol);
                if (!line)
                    line = FindShaderGraphSourceSymbol(variant.Hlsl, "_KeireVertexMaterial_" + node->Symbol);
                if (!line)
                    line = FindShaderGraphSourceSymbol(variant.Hlsl, node->Symbol);
                if (line)
                    m_SourceLine = static_cast<int>(std::min(*line, std::size_t{std::numeric_limits<int>::max()}));
                else
                    m_Message = "The selected symbol is absent from this generated variant.";
            }
        }
        const auto lineCount = std::min<std::size_t>(
            1 + static_cast<std::size_t>(std::ranges::count(variant.Hlsl, '\n')), std::numeric_limits<int>::max());
        (void)ui.SliderInt("First Source Line", m_SourceLine, 1, static_cast<int>(lineCount));
        m_SourceLine = std::max(1, m_SourceLine);
        ui.TextWrapped("Read-only generated HLSL. Showing up to 80 lines; edit the graph to change this source.");
        if (auto source = ui.BeginChild("GeneratedHlsl", {0.0F, 260.0F}, true); source)
        {
            const auto page = ShaderGraphSourcePage(variant.Hlsl, static_cast<std::size_t>(m_SourceLine));
            if (page.empty())
                ui.Text("No source at this line.");
            for (const auto& line : page)
                ui.Text(std::to_string(line.Number) + "  " + std::string(line.Text));
        }
    }
} // namespace KeireEditor
