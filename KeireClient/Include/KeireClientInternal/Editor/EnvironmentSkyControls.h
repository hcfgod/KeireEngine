#pragma once

#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/AtmosphereControls.h"
#include "KeireClientInternal/Editor/EnvironmentAuthoringPresets.h"
#include "KeireClientInternal/Editor/EnvironmentSkyEditing.h"
#include "KeireClientInternal/Editor/EnvironmentSkyTimeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace KeireEditor::Detail
{
    inline bool DrawSkyTrack(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme, const std::string_view name,
                             Keire::Detail::EnvironmentColorTrack& track, EnvironmentPreviewClock* preview,
                             std::size_t& selected, bool& commit)
    {
        auto id = ui.PushId(name);
        auto section = ui.BeginSection(name);
        if (!section)
        {
            return false;
        }
        selected = std::min(selected, track.Keys().size() - 1);
        DrawSkyColorTimeline(ui, theme, track, preview, selected);
        if (auto combo = ui.BeginCombo("Selected key", EnvironmentClockLabel(track.Keys()[selected].DayFraction));
            combo)
        {
            for (std::size_t index = 0; index < track.Keys().size(); ++index)
            {
                auto keyId = ui.PushId(std::to_string(index));
                if (ui.Selectable(EnvironmentClockLabel(track.Keys()[index].DayFraction), index == selected))
                {
                    selected = index;
                }
            }
        }
        {
            auto disabled = ui.BeginDisabled(preview == nullptr);
            if (ui.Button("Preview selected key") && preview != nullptr)
            {
                preview->Seek(preview->State().Day, track.Keys()[selected].DayFraction);
                preview->SetPlaying(false);
            }
        }
        {
            const auto existing =
                preview == nullptr ? std::nullopt : FindSkyColorKeyAtPhase(track, preview->State().DayFraction);
            auto disabled = ui.BeginDisabled(
                preview == nullptr ||
                (!existing && track.Keys().size() >= Keire::Detail::EnvironmentColorTrack::MaximumKeys));
            const auto label = existing ? "Select key at preview time" : "Add key at preview time";
            if (ui.Button(label) && preview != nullptr)
            {
                if (existing)
                {
                    selected = *existing;
                    return false;
                }
                const double phase = preview->State().DayFraction;
                track = InsertSkyColorKey(track, phase);
                selected = *FindSkyColorKeyAtPhase(track, phase);
                commit = true;
                return true;
            }
        }
        auto keys = track.Keys();
        auto& key = keys[selected];
        double hours = key.DayFraction * 24.0;
        bool changed =
            DrawAtmosphereScalar(ui, "Key time (hours)", hours, 1.0 / 60.0, 0.0, std::nextafter(24.0, 0.0), commit);
        if (changed)
        {
            key.DayFraction = SkyColorKeyPhase(track, selected, hours);
        }
        std::array<float, 3> pickerColor{static_cast<float>(key.LinearColor[0]), static_cast<float>(key.LinearColor[1]),
                                         static_cast<float>(key.LinearColor[2])};
        ui.TextWrapped("Linear HDR color");
        ui.SetNextItemWidth(-1.0F);
        if (ui.ColorEditHdrRgb("##SkyKeyColor", pickerColor))
        {
            for (std::size_t channel = 0; channel < pickerColor.size(); ++channel)
            {
                key.LinearColor[channel] = std::clamp(static_cast<double>(pickerColor[channel]), 0.0,
                                                      Keire::Detail::EnvironmentColorTrack::MaximumRadiance);
            }
            changed = true;
        }
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        constexpr std::array channels{"Linear HDR Red", "Linear HDR Green", "Linear HDR Blue"};
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
        {
            changed |= DrawAtmosphereScalar(ui, channels[channel], key.LinearColor[channel], 0.01, 0.0,
                                            Keire::Detail::EnvironmentColorTrack::MaximumRadiance, commit);
        }
        constexpr std::array interpolationNames{"Linear", "Smooth", "Hold"};
        if (auto combo = ui.BeginCombo("To next key", interpolationNames[static_cast<std::size_t>(key.Interpolation)]);
            combo)
        {
            for (std::size_t index = 0; index < interpolationNames.size(); ++index)
            {
                if (ui.Selectable(interpolationNames[index], index == static_cast<std::size_t>(key.Interpolation)))
                {
                    key.Interpolation = static_cast<Keire::Detail::EnvironmentKeyInterpolation>(index);
                    changed = true;
                    commit = true;
                }
            }
        }
        {
            auto disabled = ui.BeginDisabled(keys.size() <= 1);
            if (ui.Button("Remove selected key"))
            {
                keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(selected));
                selected = std::min(selected, keys.size() - 1);
                changed = true;
                commit = true;
            }
        }
        if (changed)
        {
            track = Keire::Detail::EnvironmentColorTrack(std::move(keys));
        }
        return changed;
    }

    inline bool DrawSkyColorControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                     ProjectSettingsDocument& document, EnvironmentPreviewClock* preview,
                                     std::array<std::size_t, 2>& selected)
    {
        auto section = ui.BeginSection("Sky Color Timeline (In Development)");
        if (!section)
        {
            return false;
        }
        const auto profile = document.EnvironmentLighting();
        auto sky = profile.Sky().value_or(DaylightSkyPreset());
        if (!profile.Sky())
        {
            sky.Enabled = false;
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Independent horizon and zenith colors in linear HDR. Sun, stars and clouds retain "
                              "their own appearance. During Play, edits apply to the next session.");
        bool changed = ui.Checkbox("Use artistic sky colors", sky.Enabled);
        bool commit = changed;
        if (ui.Button("Apply Daylight Sky Preset"))
        {
            document.UpdateEnvironmentLighting(ReplaceSkyProfile(profile, DaylightSkyPreset()));
            selected = {};
            return true;
        }
        if (ui.Button("Reset to Physical Sky"))
        {
            document.UpdateEnvironmentLighting(ReplaceSkyProfile(profile, std::nullopt));
            selected = {};
            return true;
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Preset/reset affect only these sky tracks. Disable keeps your keys; reset removes "
                              "them. Undo restores the previous profile. Drag strips to preview without saving time.");
        changed |= DrawAtmosphereScalar(ui, "Artistic blend", sky.BlendWeight, 0.01, 0.0, 1.0, commit);
        changed |= DrawAtmosphereScalar(ui, "Horizon gradient exponent", sky.GradientExponent, 0.01, 0.1, 16.0, commit);
        changed |= DrawSkyTrack(ui, theme, "Horizon", sky.Horizon, preview, selected[0], commit);
        changed |= DrawSkyTrack(ui, theme, "Zenith", sky.Zenith, preview, selected[1], commit);
        if (changed)
        {
            document.UpdateEnvironmentLighting(ReplaceSkyProfile(profile, std::move(sky)));
        }
        return commit;
    }
} // namespace KeireEditor::Detail
