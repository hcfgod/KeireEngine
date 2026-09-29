#pragma once

#include "KeireClientInternal/Editor/AnimatorControllerPreviewInternal.h"

#include "Keire/Ui.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <chrono>

namespace KeireEditor
{
    // Shares one preview owner with controller playback so closing either view restores the pose.
    [[nodiscard]] inline bool DrawStandaloneClipPreview(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                                        SceneDocument& sceneDocument,
                                                        const AnimatorControllerDocument& clip,
                                                        AnimatorControllerPreviewState& preview)
    {
        ui.TextColoredWrapped(theme.Accent, "ANIMATION CLIP PREVIEW");
        ui.TextWrapped(Keire::Detail::PathToUtf8(clip.SourcePath()));
        ui.TextWrapped("Select a scene entity with an Animator and skinned mesh, then press Play. "
                       "Preview speed is independent of the entity's Animator Speed. "
                       "Preview does not change its assigned controller or save changes to the clip.");
        const auto session = sceneDocument.PlaySession();
        const bool running = session && session->State() != Keire::ScenePlayState::Stopped;
        if (running)
            ui.TextColoredWrapped(theme.Warning, "Stop scene Play Mode to preview this clip.");
        if (auto disabled = ui.BeginDisabled(running); disabled)
        {
            const float width = std::max(1.0F, ui.ContentAvailable().Width);
            const bool stacked = width < 240.0F;
            const Keire::UiSize buttonSize{stacked ? width : 0.0F, 0.0F};
            if (ui.Button(preview.Playing ? "Pause" : "Play", buttonSize))
            {
                preview.Active = true;
                preview.Playing = !preview.Playing;
                preview.LastTick = std::chrono::steady_clock::now();
            }
            if (!stacked)
                ui.SameLine();
            if (ui.Button("Restart", buttonSize))
                preview.Restart();
            if (!stacked)
                ui.SameLine();
            if (ui.Button("Stop", buttonSize))
                preview.Stop();
            float timeline = preview.NormalizedTime;
            ui.Text("Timeline");
            ui.SetNextItemWidth(width);
            if (ui.SliderFloat("##Timeline", timeline, 0.0F, 1.0F))
                preview.Seek(timeline);
            (void)ui.SliderFloat("Preview Speed", preview.PlaybackSpeed, 0.1F, 3.0F);
        }
        if (!preview.Diagnostic.empty())
            ui.TextColoredWrapped(theme.Warning, preview.Diagnostic);
        if (ui.Button("Back to Controller", {std::max(1.0F, ui.ContentAvailable().Width), 0.0F}))
        {
            preview.Stop();
            return true;
        }
        return false;
    }
} // namespace KeireEditor
