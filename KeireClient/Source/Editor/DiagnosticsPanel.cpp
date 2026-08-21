#include "KeireClient/Editor/DiagnosticsPanel.h"
#include "KeireInternal/Process.h"

#include <filesystem>
#include <sstream>

namespace KeireEditor
{
    void DiagnosticsPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.diagnostics", "Diagnostics"});
    }

    void DiagnosticsPanel::Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                const std::uint64_t frameNumber, const double deltaMilliseconds,
                                const Keire::UiSize windowSize, Keire::UiCaptureState capture)
    {
        Draw(ui, theme, frameNumber, deltaMilliseconds, windowSize, capture, {}, {}, {});
    }

    void DiagnosticsPanel::Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                const std::uint64_t frameNumber, const double deltaMilliseconds,
                                const Keire::UiSize windowSize, Keire::UiCaptureState capture,
                                const Keire::Ref<Keire::DiagnosticCatalog>& catalog,
                                const Keire::Ref<Keire::DiagnosticSink>& reports,
                                const Keire::Ref<Keire::WindowSystem>& windows)
    {
        if (auto diagnostics = ui.BeginPanel(m_Registration); diagnostics)
        {
            std::ostringstream frame;
            frame << "Frame: " << frameNumber;
            ui.Text(frame.str());
            std::ostringstream delta;
            delta << "Delta: " << deltaMilliseconds << " ms";
            ui.Text(delta.str());
            std::ostringstream extent;
            extent << "Window: " << windowSize.Width << 'x' << windowSize.Height << " logical pixels";
            ui.Text(extent.str());
            (void)ui.Checkbox("Pointer capture", capture.Pointer);
            (void)ui.Checkbox("Keyboard capture", capture.Keyboard);
            ui.TextColored(theme.Success, "Docking active; native viewports remain disabled.");
            if (!m_Status.empty())
                ui.TextColored(theme.Warning, m_Status);
            if (!catalog || !reports)
                return;
            ui.Separator();
            const auto snapshot = reports->Snapshot();
            ui.Text(std::to_string(snapshot.size()) + " structured diagnostics");
            if (snapshot.empty())
                ui.TextColored(theme.MutedText, "No structured diagnostics have been reported.");
            for (const auto& diagnostic : snapshot)
            {
                auto id = ui.PushId(std::to_string(diagnostic.Sequence));
                const auto color = diagnostic.Severity == Keire::DiagnosticSeverity::Information ? theme.MutedText
                                   : diagnostic.Severity == Keire::DiagnosticSeverity::Warning   ? theme.Warning
                                                                                                 : theme.Error;
                ui.TextColored(color, std::string(diagnostic.Id.Value()) + ": " + diagnostic.Message);
                if (diagnostic.Location)
                    ui.Text(diagnostic.Location->File.generic_string() + ':' +
                            std::to_string(diagnostic.Location->Line));
                for (const auto& [key, value] : diagnostic.Context)
                {
                    auto context = key;
                    context.append(": ").append(value);
                    ui.TextColored(theme.MutedText, context);
                }
                if (ui.Button("Learn more"))
                {
                    try
                    {
                        std::string failure;
                        const auto local = catalog->LocalDocumentation(diagnostic.Id);
                        if (local && std::filesystem::is_regular_file(*local))
                        {
                            if (!Keire::Detail::OpenInExternalEditor(*local, {}, std::filesystem::current_path(),
                                                                     failure))
                                m_Status = std::move(failure);
                            else
                                m_Status.clear();
                        }
                        else if (const auto online = catalog->OnlineDocumentation(diagnostic.Id); online && windows)
                        {
                            windows->OpenUrl(*online);
                            m_Status.clear();
                        }
                        else
                        {
                            m_Status = "No documentation location is configured for " +
                                       std::string(diagnostic.Id.Value()) + '.';
                        }
                    }
                    catch (const std::exception& exception)
                    {
                        m_Status = exception.what();
                    }
                }
            }
        }
    }
} // namespace KeireEditor
