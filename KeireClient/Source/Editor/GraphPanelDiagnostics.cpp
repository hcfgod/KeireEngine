#include "KeireClient/Editor/MaterialGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"

namespace KeireEditor
{
    void ShaderGraphPanel::DrawDiagnostics(Keire::UiFrame& ui)
    {
        const auto& document = m_Controller.ShaderGraphState();
        const auto& theme = m_Controller.ShaderGraphTheme();
        if (!m_Message.empty())
            ui.TextColored(theme.Warning, m_Message);
        if (document.Compilation().Diagnostics.empty())
        {
            ui.TextColored(theme.Success, document.ReusableGraph() ? "Reusable graph diagnostics: clear."
                                                                   : "Generated shader diagnostics: clear.");
            return;
        }
        ui.TextColored(theme.Warning, std::string(document.ReusableGraph() ? "Reusable graph diagnostics ("
                                                                           : "Generated shader diagnostics (") +
                                          std::to_string(document.Compilation().Diagnostics.size()) + ")");
        for (const auto& diagnostic : document.Compilation().Diagnostics)
        {
            const auto color = diagnostic.Severity == Keire::ShaderGraphDiagnosticSeverity::Error     ? theme.Error
                               : diagnostic.Severity == Keire::ShaderGraphDiagnosticSeverity::Warning ? theme.Warning
                                                                                                      : theme.MutedText;
            std::string text = diagnostic.Code + "  " + diagnostic.Message;
            if (diagnostic.Node)
                text += "  [" + diagnostic.Node.ToString() + "]";
            if (diagnostic.GeneratedLine != 0)
                text += "  line " + std::to_string(diagnostic.GeneratedLine);
            if (diagnostic.Node)
            {
                auto id = ui.PushId(diagnostic.Node.ToString() + diagnostic.Code);
                if (ui.Selectable(text))
                {
                    m_SelectedNode = diagnostic.Node;
                    m_SelectedNodes = {diagnostic.Node};
                    m_FrameNode = diagnostic.Node;
                }
            }
            else
                ui.TextColored(color, text);
        }
    }

    void ShaderGraphPanel::Report(std::string message) noexcept
    {
        m_Message = std::move(message);
        m_Controller.ReportShaderGraphError(m_Message);
    }

    void MaterialGraphPanel::DrawDiagnostics(Keire::UiFrame& ui)
    {
        const auto& theme = m_Controller.MaterialGraphTheme();
        if (!m_Message.empty())
            ui.TextColored(theme.Warning, m_Message);
        const auto diagnostics = m_Controller.MaterialGraphState().Diagnostics();
        if (diagnostics.empty())
        {
            ui.TextColored(theme.Success, "Material Graph diagnostics: clear.");
            return;
        }
        for (const auto& diagnostic : diagnostics)
        {
            const auto color = diagnostic.Severity == Keire::MaterialGraphDiagnosticSeverity::Error ? theme.Error
                               : diagnostic.Severity == Keire::MaterialGraphDiagnosticSeverity::Warning
                                   ? theme.Warning
                                   : theme.MutedText;
            const auto text = diagnostic.Code + "  " + diagnostic.Message;
            if (diagnostic.Node)
            {
                auto id = ui.PushId(diagnostic.Node.ToString() + diagnostic.Code);
                if (ui.Selectable(text))
                {
                    m_SelectedNode = diagnostic.Node;
                    m_SelectedNodes = {diagnostic.Node};
                    m_FrameNode = diagnostic.Node;
                }
            }
            else
                ui.TextColored(color, text);
        }
    }

    void MaterialGraphPanel::Report(std::string message) noexcept
    {
        m_Message = std::move(message);
        m_Controller.ReportMaterialGraphError(m_Message);
    }
} // namespace KeireEditor
