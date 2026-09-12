#include "KeireClient/Editor/ShaderGraphPanel.h"

#include "KeireClient/Editor/ShaderGraphBlackboard.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    void ShaderGraphPanel::DrawBlackboard(Keire::UiFrame& ui)
    {
        auto tree = ui.BeginTreeNode("Blackboard");
        if (!tree)
            return;
        auto& document = m_Controller.ShaderGraphState();
        const auto& theme = m_Controller.ShaderGraphTheme();
        ui.SetNextItemWidth(std::max(1.0F, std::min(280.0F, ui.ContentAvailable().Width)));
        (void)ui.InputTextWithHint("##BlackboardSearch", "Find properties and keywords...", m_BlackboardSearch);
        if (!m_ReadOnly)
            if (auto add = ui.BeginMenu("Add Property"); add)
            {
                constexpr std::array types{
                    Keire::ShaderGraphValueType::Scalar,  Keire::ShaderGraphValueType::Vector2,
                    Keire::ShaderGraphValueType::Vector3, Keire::ShaderGraphValueType::Vector4,
                    Keire::ShaderGraphValueType::Color,   Keire::ShaderGraphValueType::Texture2D};
                constexpr std::array names{"Scalar", "Vector 2", "Vector 3", "Vector 4", "Color", "Texture 2D"};
                for (std::size_t index = 0; index < types.size(); ++index)
                    if (ui.MenuItem(names[index]))
                    {
                        (void)AddNode(Keire::ShaderGraphNodeKind::Parameter, types[index]);
                        return;
                    }
            }
        const auto entries = BuildShaderGraphBlackboard(document.Definition(), m_BlackboardSearch);
        if (entries.empty())
        {
            ui.TextColored(theme.MutedText, "No matching properties or keywords.");
            return;
        }
        if (auto list = ui.BeginChild("BlackboardEntries", {0.0F, 160.0F}, true); list)
        {
            std::string category;
            for (const auto& entry : entries)
            {
                if (category != entry.Category)
                {
                    category = entry.Category;
                    ui.TextColored(theme.Accent, category);
                }
                const auto identity = ui.PushId(entry.Identity);
                const auto disabled = ui.BeginDisabled(m_ReadOnly && !entry.Node);
                auto label = entry.Name;
                if (!entry.Exposed)
                    label += " (hidden)";
                if (!entry.Node)
                    label += " - place node";
                if (ui.Selectable(label, entry.Node && m_SelectedNode == entry.Node))
                {
                    if (entry.Node)
                    {
                        m_SelectedNode = entry.Node;
                        m_SelectedNodes = {*entry.Node};
                        m_SelectedConnection.reset();
                    }
                    else
                        try
                        {
                            auto node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Keyword);
                            node.Symbol = entry.Symbol;
                            node.Name = entry.Name;
                            node.EditorPosition = {-m_Canvas.Pan().X + 280.0F / m_Canvas.Zoom(),
                                                   -m_Canvas.Pan().Y + 180.0F / m_Canvas.Zoom()};
                            (void)CommitCreatedNode(std::move(node), std::nullopt, std::nullopt);
                        }
                        catch (const std::exception& error)
                        {
                            Report(error.what());
                        }
                    m_FrameNode = m_SelectedNode;
                }
                if (ui.LastItemState().Hovered)
                    ui.SetTooltip(entry.Symbol + (entry.Description.empty() ? "" : "\n" + entry.Description));
            }
        }
    }
} // namespace KeireEditor
