#include "KeireClient/Editor/DiagnosticsPanel.h"

#include "KeireClient/EditorWorkspaceLayer.h"

#include <sstream>

namespace KeireEditor
{
    void DiagnosticsPanel::Draw(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
    {
        if (auto diagnostics = ui.BeginPanel(editor.m_Diagnostics); diagnostics)
        {
            const auto& time = editor.Owner().GetTime();
            std::ostringstream frame;
            frame << "Frame: " << time.FrameCount();
            ui.Text(frame.str());
            std::ostringstream delta;
            delta << "Delta: " << time.UnscaledDeltaTime().Milliseconds() << " ms";
            ui.Text(delta.str());
            const auto window = editor.Owner().MainWindow();
            std::ostringstream extent;
            extent << "Window: " << window->LogicalSize().Width << 'x' << window->LogicalSize().Height
                   << " logical pixels";
            ui.Text(extent.str());
            auto capture = editor.Owner().UiCapture();
            (void)ui.Checkbox("Pointer capture", capture.Pointer);
            (void)ui.Checkbox("Keyboard capture", capture.Keyboard);
            ui.TextColored(editor.m_Theme.Success, "Docking active; native viewports remain disabled.");
        }
    }
} // namespace KeireEditor
