#pragma once

#include "Keire/Ui.h"
#include "KeireClientInternal/Editor/EnvironmentAuthoringPresets.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"
#include "KeireClientInternal/Editor/EnvironmentTimeline.h"

#include <algorithm>
#include <string>

namespace KeireEditor::Detail
{
    inline void DrawEnvironmentTimeline(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                        const Keire::Detail::EnvironmentLightingProfile& profile,
                                        EnvironmentPreviewClock* preview, std::size_t& selectedKey)
    {
        ui.Text("24-hour lighting timeline");
        const auto origin = ui.CursorScreenPosition();
        const float width = std::max(1.0F, ui.ContentAvailable().Width);
        constexpr float height = 156.0F;
        const float left = origin.X + std::min(12.0F, width * 0.1F);
        const float plotWidth = std::max(1.0F, width - 2.0F * (left - origin.X));
        const float top = origin.Y + 12.0F;
        const float bottom = origin.Y + height - 30.0F;
        auto disabled = ui.BeginDisabled(preview == nullptr);
        (void)ui.InvisibleButton("EnvironmentTimeline", {width, height});
        const auto item = ui.LastItemState();
        const auto& keys = profile.Keyframes();
        selectedKey = keys.empty() ? 0 : std::min(selectedKey, keys.size() - 1);
        if (item.Activated && !keys.empty())
        {
            const double phase = TimelinePhase(ui.PointerState().Position.X, left, plotWidth, keys, false);
            if (const auto hit = TimelineKeyAtPhase(phase, plotWidth, keys))
            {
                selectedKey = *hit;
            }
        }
        if (preview != nullptr && item.Active && ui.PointerState().LeftDown)
        {
            const double phase =
                TimelinePhase(ui.PointerState().Position.X, left, plotWidth, profile.Keyframes(), !ui.ShiftDown());
            preview->Seek(preview->State().Day, phase);
            preview->SetPlaying(false);
        }
        ui.DrawFilledRectangle({origin, {origin.X + width, origin.Y + height}}, theme.Canvas, 4.0F);
        for (int hour = 0; hour <= 24; hour += 6)
        {
            const float x = left + plotWidth * static_cast<float>(hour) / 24.0F;
            ui.DrawLine({x, top}, {x, bottom}, theme.Border);
            const float labelX = hour == 24 ? x - 32.0F : x;
            ui.DrawOverlayText({labelX, bottom + 8.0F}, theme.MutedText,
                               hour == 0 ? "00:00" : std::to_string(hour) + ":00");
        }
        double maximumIntensity = 1.0;
        for (const auto& key : profile.Keyframes())
        {
            maximumIntensity = std::max(maximumIntensity, key.Intensity);
        }
        const auto point = [&](const double phase, const double intensity)
        {
            return Keire::UiPosition{left + plotWidth * static_cast<float>(phase),
                                     bottom - (bottom - top) * static_cast<float>(intensity / maximumIntensity)};
        };
        // Sample the actual cyclic evaluator, including the midnight seam, rather than inventing a UI curve.
        const auto phases = TimelineIntensityKnots(keys);
        Keire::UiPosition previous{};
        for (std::size_t index = 0; index < phases.size(); ++index)
        {
            const double phase = phases[index];
            const auto sample = profile.Evaluate({0, phase == 1.0 ? 0.0 : phase, 86400.0});
            if (!sample)
            {
                break;
            }
            const auto current = point(phase, sample->Intensity);
            if (index > 0)
            {
                ui.DrawLine(previous, current, theme.Accent, 2.0F);
            }
            previous = current;
        }
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            const auto& key = keys[index];
            const auto position = point(key.DayFraction, key.Intensity);
            ui.DrawFilledCircle(position, 4.0F, theme.Warning);
            if (index == selectedKey)
            {
                ui.DrawCircle(position, 7.0F, theme.Text, 1.5F);
            }
        }
        if (preview != nullptr)
        {
            const float x = point(preview->State().DayFraction, 0.0).X;
            ui.DrawLine({x, top}, {x, bottom}, theme.Text, 2.0F);
            ui.DrawFilledTriangle({x - 5.0F, top}, {x + 5.0F, top}, {x, top + 7.0F}, theme.Text);
        }
        if (item.Hovered)
        {
            const double phase =
                TimelinePhase(ui.PointerState().Position.X, left, plotWidth, profile.Keyframes(), !ui.ShiftDown());
            ui.SetTooltip(EnvironmentClockLabel(phase) + " - drag to preview; Shift disables key snapping");
        }
        ui.TextColoredWrapped(
            theme.MutedText, "Directional intensity (auto-scaled). Gold dots are lighting keys; white is preview time. "
                             "Click near a key to select its controls. Drag to scrub without saving. "
                             "Shift bypasses snapping. Numeric preview remains above.");
    }
} // namespace KeireEditor::Detail
