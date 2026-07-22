#include "KeireClient/Editor/DiagnosticsPanel.h"

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
        }
    }
} // namespace KeireEditor
