#include "KeireClient/Editor/ShaderGraphPanel.h"

#include <stdexcept>

namespace KeireEditor
{
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
} // namespace KeireEditor
