#pragma once

#include "Keire/Ui.h"
#include "Keire/UiWorkspace.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"
#include "KeireClientInternal/Editor/EnvironmentSkyEditing.h"
#include "KeireInternal/Environment/EnvironmentColorTrack.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace KeireEditor::Detail
{
    inline void DrawSkyColorTimeline(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                     const Keire::Detail::EnvironmentColorTrack& track,
                                     EnvironmentPreviewClock* preview, std::size_t& selected)
    {
        const auto origin = ui.CursorScreenPosition();
        const float width = std::max(1.0F, ui.ContentAvailable().Width);
        constexpr float height = 44.0F;
        (void)ui.InvisibleButton("SkyGradientTimeline", {width, height + 22.0F});
        const auto item = ui.LastItemState();
        const double phase = std::clamp(static_cast<double>((ui.PointerState().Position.X - origin.X) / width), 0.0,
                                        std::nextafter(1.0, 0.0));
        if (item.Activated)
        {
            if (const auto key = PickSkyColorKey(track, phase, width))
            {
                selected = *key;
            }
        }
        if (preview != nullptr && item.Active && ui.PointerState().LeftDown)
        {
            preview->Seek(preview->State().Day, phase);
            preview->SetPlaying(false);
        }
        constexpr int samples = 96;
        for (int index = 0; index < samples; ++index)
        {
            const auto linear = track.Evaluate({.DayFraction = (index + 0.5) / samples});
            const auto display = [](const double value)
            { return static_cast<float>(std::pow(value / (1.0 + value), 1.0 / 2.2)); };
            const Keire::UiColor color{display(linear[0]), display(linear[1]), display(linear[2]), 1.0F};
            ui.DrawFilledRectangle({{origin.X + width * index / samples, origin.Y},
                                    {origin.X + width * (index + 1) / samples, origin.Y + height}},
                                   color);
        }
        for (std::size_t index = 0; index < track.Keys().size(); ++index)
        {
            const float x = origin.X + width * static_cast<float>(track.Keys()[index].DayFraction);
            ui.DrawFilledCircle({x, origin.Y + height - 6.0F}, 4.0F, theme.Warning);
            if (index == selected)
            {
                ui.DrawCircle({x, origin.Y + height - 6.0F}, 6.0F, theme.Text);
            }
        }
        if (preview != nullptr)
        {
            const float x = origin.X + width * static_cast<float>(preview->State().DayFraction);
            ui.DrawLine({x, origin.Y}, {x, origin.Y + height}, theme.Text, 2.0F);
        }
        ui.DrawOverlayText({origin.X, origin.Y + height + 3.0F}, theme.MutedText, "00:00");
        ui.DrawOverlayText({origin.X + width - 38.0F, origin.Y + height + 3.0F}, theme.MutedText, "24:00");
        if (item.Hovered)
        {
            ui.SetTooltip("Drag to preview time; click a gold key to select. Numeric controls are below. "
                          "Preview strip compresses HDR; the rendered sky also uses exposure and tone mapping.");
        }
    }
} // namespace KeireEditor::Detail
