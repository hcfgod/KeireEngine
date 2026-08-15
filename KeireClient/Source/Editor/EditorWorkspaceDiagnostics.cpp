#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/ConsolePanel.h"
#include "KeireClient/Editor/DiagnosticsPanel.h"

#include <string>
#include <utility>

void EditorWorkspaceLayer::AddConsoleMessage(std::string category, std::string message, const Keire::UiColor color,
                                             const Keire::LogLevel level) noexcept
{
    m_ConsolePanel->LogAndCapture(std::move(category), std::move(message), color, Owner().GetTime().FrameCount(),
                                  m_Theme, level);
}

void EditorWorkspaceLayer::ReportError(std::string category, std::string message) noexcept
{
    AddConsoleMessage(std::move(category), std::move(message), m_Theme.Error, Keire::LogLevel::Error);
}

void EditorWorkspaceLayer::SetAssetError(std::string message) noexcept
{
    try
    {
        m_AssetStatus = message;
    }
    catch (...)
    {
    }
    ReportError("Assets", std::move(message));
}

void EditorWorkspaceLayer::DrawConsole(Keire::UiFrame& ui) { m_ConsolePanel->Draw(ui, m_Theme); }

void EditorWorkspaceLayer::DrawDiagnostics(Keire::UiFrame& ui)
{
    const auto& time = Owner().GetTime();
    const auto size = Owner().MainWindow()->LogicalSize();
    m_DiagnosticsPanel->Draw(ui, m_Theme, time.FrameCount(), time.UnscaledDeltaTime().Milliseconds(),
                             {static_cast<float>(size.Width), static_cast<float>(size.Height)}, Owner().UiCapture(),
                             Owner().DiagnosticDefinitions(), Owner().DiagnosticReports(), Owner().Windows());
}
