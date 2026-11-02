#pragma once

#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/EnvironmentAuthoringPresets.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"

#include <cmath>
#include <format>
#include <string>

namespace KeireEditor::Detail
{
    inline void DrawEnvironmentPreviewControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                               EnvironmentPreviewClock& preview, ProjectSettingsDocument& document)
    {
        preview.SynchronizeAuthored(document.EnvironmentTime());
        auto state = preview.State();
        ui.TextColored(theme.Accent, "Preview Clock: " + EnvironmentClockLabel(state.DayFraction));
        ui.TextColoredWrapped(theme.MutedText, preview.Playing() ? "Playing - temporary edit-mode preview"
                                                                 : "Paused - temporary edit-mode preview");
        const bool wideTransport = ui.ContentAvailable().Width >= 440.0F;
        if (auto help = ui.BeginSection("How time preview works"); help)
        {
            ui.TextColoredWrapped(
                theme.MutedText,
                "Edit-mode preview time is transient. Enabled daily lighting and layered cloud wind follow this clock. "
                "Atmosphere follows the selected directional light; enable daily lighting to move its sun with time.");
            ui.TextColoredWrapped(
                theme.MutedText,
                "Apply Time saves only the preview day and time of day for the next Play session and "
                "builds. Preview speed and playback are not saved; authored rate and pause stay unchanged.");
            ui.TextColoredWrapped(
                theme.MutedText,
                "Reset returns to authored time, stops playback and restores preview speed to 1. "
                "Sky textures, environment reflections and baked lighting do not animate with this clock. "
                "Clock shortcuts seek within the current preview day without saving.");
        }
        if (ui.Button(preview.Playing() ? "Pause Preview" : "Play Preview"))
        {
            preview.SetPlaying(!preview.Playing());
        }
        if (wideTransport)
        {
            ui.SameLine();
        }
        if (ui.Button("Reset Preview"))
        {
            preview.Reset();
        }
        state = preview.State();
        constexpr const char* timeLabels[] = {"Midnight (00:00)", "Dawn (06:00)", "Noon (12:00)", "Dusk (18:00)"};
        if (wideTransport)
        {
            ui.SameLine();
        }
        ui.SetNextItemWidth(170.0F);
        if (auto presets = ui.BeginCombo("##PreviewTimePreset", "Jump to time..."); presets)
        {
            for (std::size_t index = 0; index < 4; ++index)
            {
                if (ui.Selectable(timeLabels[index]))
                {
                    preview.Seek(state.Day, static_cast<double>(index) / 4.0);
                    state = preview.State();
                }
            }
        }
        ui.Text("Time of Day (hours)");
        ui.SetNextItemWidth(-1.0F);
        double hours = state.DayFraction * 24.0;
        if (ui.SliderScalar("##TimeOfDayHours", hours, 0.0, std::nextafter(24.0, 0.0)))
        {
            preview.Seek(state.Day, hours / 24.0);
        }
        if (auto advanced = ui.BeginSection("Day and playback settings"); advanced)
        {
            ui.Text("Preview Day");
            ui.SetNextItemWidth(-1.0F);
            if (ui.DragInteger("##Preview Day", state.Day))
            {
                preview.Seek(state.Day, state.DayFraction);
            }
            ui.Text("Preview Speed");
            ui.SetNextItemWidth(-1.0F);
            if (ui.DragScalar("##Preview Speed", state.Rate, 0.1, -64.0, 64.0))
            {
                preview.SetSpeed(state.Rate);
            }
            ui.TextColoredWrapped(theme.MutedText, std::format("Day length: {:.1f} seconds.", state.SecondsPerDay));
            ui.TextColoredWrapped(theme.MutedText, "Negative speed runs backward. These controls affect preview only; "
                                                   "Apply Time preserves the project's authored playback rate.");
        }
        if (ui.Button("Apply Time to Project"))
        {
            document.UpdateEnvironmentTime(preview.ApplyTimeCandidate());
            document.CommitEdit("Apply Environment Time");
            document.Save();
            preview.SynchronizeAuthored(document.EnvironmentTime());
        }
        ui.Separator();
    }
} // namespace KeireEditor::Detail
