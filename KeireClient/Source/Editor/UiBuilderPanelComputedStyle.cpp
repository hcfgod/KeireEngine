#include "KeireClient/Editor/UiBuilderPanel.h"

#include <string>

namespace KeireEditor
{
    void UiBuilderPanel::DrawStyleComputed(Keire::UiFrame& ui)
    {
        const auto& theme = m_Controller.UiBuilderTheme();
        RefreshPreviewSnapshot();
        if (!m_PreviewSnapshot || !m_PreviewSnapshot->SelectedState)
        {
            ui.TextColoredWrapped(theme.MutedText, "Select an element to inspect its resolved style and layout.");
            return;
        }
        const auto& state = *m_PreviewSnapshot->SelectedState;
        ui.TextColored(theme.Accent, "COMPUTED STYLE");
        ui.Text("x " + std::to_string(state.Rect.X) + "  y " + std::to_string(state.Rect.Y));
        ui.Text("width " + std::to_string(state.Rect.Width) + "  height " + std::to_string(state.Rect.Height));
        ui.Separator();
        ui.Text("Opacity  " + std::to_string(state.Style.Opacity));
        ui.Text("Font size  " + std::to_string(state.Style.FontSize) + " px");
        ui.Text("Border  " + std::to_string(state.Style.BorderWidth) + " px");
        ui.Text("Radius  " + std::to_string(state.Style.CornerRadius) + " px");
        ui.Separator();
        ui.TextColored(theme.Accent, "PROVENANCE");
        if (m_PreviewSnapshot->SelectedStyleTrace.empty())
            ui.TextColoredWrapped(theme.MutedText, "No stylesheet rule matches the selected element.");
        for (const auto& trace : m_PreviewSnapshot->SelectedStyleTrace)
        {
            ui.Text(trace.Selector + "  | specificity " + std::to_string(trace.Specificity) + " | order " +
                    std::to_string(trace.SourceOrder));
            for (const auto& property : trace.AppliedProperties)
                ui.TextColored(theme.MutedText, "  " + property);
        }
        ui.Text("Style passes  " + std::to_string(m_PreviewSnapshot->Statistics.StylePasses));
        ui.Text("Layout passes  " + std::to_string(m_PreviewSnapshot->Statistics.LayoutPasses));
        ui.Text("Repaint passes  " + std::to_string(m_PreviewSnapshot->Statistics.RepaintPasses));
    }
} // namespace KeireEditor
