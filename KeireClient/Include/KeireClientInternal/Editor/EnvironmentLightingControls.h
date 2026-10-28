#pragma once

#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/EnvironmentAuthoringPresets.h"
#include "KeireClientInternal/Editor/EnvironmentLightingEditing.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace KeireEditor::Detail
{
    inline bool DrawEnvironmentShadowControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                              ProjectSettingsDocument& document)
    {
        auto section = ui.BeginSection("Daily Light Shadows");
        if (!section)
        {
            return false;
        }
        const auto profile = document.EnvironmentLighting();
        auto shadows = profile.Shadows();
        constexpr const char* qualities[] = {"Disabled", "Hard", "Soft"};
        constexpr const char* resolutions[] = {"Low", "Medium", "High", "Very High"};
        bool changed = false;
        bool commit = false;
        if (auto combo = ui.BeginCombo("Shadow Quality", qualities[static_cast<std::size_t>(shadows.Quality)]); combo)
        {
            for (std::size_t index = 0; index < 3; ++index)
            {
                const auto quality = static_cast<Keire::ShadowQuality>(index);
                if (ui.Selectable(qualities[index], shadows.Quality == quality))
                {
                    shadows.Quality = quality;
                    changed = true;
                    commit = true;
                }
            }
        }
        {
            auto disabled = ui.BeginDisabled(shadows.Quality == Keire::ShadowQuality::Disabled);
            if (auto combo =
                    ui.BeginCombo("Shadow Resolution Hint", resolutions[static_cast<std::size_t>(shadows.Resolution)]);
                combo)
            {
                for (std::size_t index = 0; index < 4; ++index)
                {
                    const auto resolution = static_cast<Keire::ShadowResolutionHint>(index);
                    if (ui.Selectable(resolutions[index], shadows.Resolution == resolution))
                    {
                        shadows.Resolution = resolution;
                        changed = true;
                        commit = true;
                    }
                }
            }
            changed |= ui.SliderFloat("Shadow Strength", shadows.Strength, 0.0F, 1.0F);
            commit |= ui.LastItemState().DeactivatedAfterEdit;
            double bias = shadows.Bias;
            constexpr double biasDragStep = 0.0001;
            if (ui.DragScalar("Shadow Bias", bias, biasDragStep, 0.0, 1.0))
            {
                shadows.Bias = static_cast<float>(bias);
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
        }
        ui.TextColoredWrapped(theme.MutedText, "Strength 0 removes visible shadowing. Drag bias for fine adjustment "
                                               "or Ctrl-click to type a value. Excessive bias detaches shadows "
                                               "from their casters.");
        ui.TextColoredWrapped(theme.MutedText, "Cascade count, distance and resolution budget are below in "
                                               "Environment > Directional Shadows. These shadows do not change "
                                               "baked lighting.");
        if (changed)
        {
            document.UpdateEnvironmentLighting(
                LightingProfile(profile.Ownership(), profile.Keyframes(), shadows, profile.Sky()));
        }
        if (ui.Button("Reset Daily Shadows"))
        {
            document.UpdateEnvironmentLighting(ResetLightingShadows(document.EnvironmentLighting()));
            return true;
        }
        ui.TextColoredWrapped(theme.MutedText, "Reset disables daily shadows and restores their defaults; "
                                               "lighting keys and project cascade settings are retained.");
        return commit;
    }

    inline bool DrawEnvironmentLightingControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                                ProjectSettingsDocument& document,
                                                EnvironmentPreviewClock* preview = nullptr,
                                                std::size_t* selectedKey = nullptr)
    {
        ui.Text("Daily Directional Lighting");
        if (auto help = ui.BeginSection("Lighting behavior and preset help"); help)
        {
            ui.TextColoredWrapped(theme.MutedText,
                                  "Replaces the authored directional light, with optional ambient animation. Sky and "
                                  "baked lighting remain independent. Configure this light's shadows below.");
            ui.TextColoredWrapped(theme.MutedText,
                                  "During Play, daily-light edits apply to the next session. Stop and start Play "
                                  "to use them; the running world's lighting stays isolated.");
            ui.TextColoredWrapped(theme.MutedText,
                                  "Daylight preset replaces daily keys with directional and ambient "
                                  "sunrise, noon and sunset lighting. Existing shadows are retained. Undo "
                                  "restores your previous keys.");
        }
        auto profile = document.EnvironmentLighting();
        if (ui.Button("Apply Daylight Preset"))
        {
            document.UpdateEnvironmentLighting(DaylightPreset(profile.Shadows(), profile.Sky()));
            return true;
        }
        bool automatic = profile.Ownership() == LightingOwnership::Environment;
        if (ui.Checkbox("Use Daily Lighting", automatic))
        {
            document.UpdateEnvironmentLighting(SetLightingOwnership(profile, automatic));
            return true;
        }
        if (!automatic)
        {
            ui.TextColoredWrapped(theme.MutedText, "Authored scene lights are used. Existing daily keys are retained.");
            return false;
        }
        bool commit = DrawEnvironmentShadowControls(ui, theme, document);
        bool ambient = !profile.Keyframes().empty() && profile.Keyframes().front().Ambient.has_value();
        if (ui.Checkbox("Animate Ambient Lighting", ambient))
        {
            const auto rendering = document.Settings();
            document.UpdateEnvironmentLighting(SetAmbientAnimation(
                document.EnvironmentLighting(), ambient,
                {{rendering.AmbientColor.Red, rendering.AmbientColor.Green, rendering.AmbientColor.Blue},
                 rendering.AmbientIntensity}));
            return true;
        }
        if (auto help = ui.BeginSection("Ambient animation help"); help)
        {
            ui.TextColoredWrapped(theme.MutedText,
                                  "Ambient animation replaces the project's ambient color and intensity "
                                  "while daily lighting is enabled. Sky, reflections and baked lighting "
                                  "remain independent. Disabling clears ambient keys; Undo restores them.");
        }
        auto keys = profile.Keyframes();
        {
            const auto existing =
                preview == nullptr ? std::nullopt : LightingKeyAtPhase(profile, preview->State().DayFraction);
            auto unavailable = ui.BeginDisabled(preview == nullptr ||
                                                (!existing && keys.size() >= LightingProfile::MaximumKeyframes) ||
                                                (existing && selectedKey == nullptr));
            if (existing)
            {
                if (ui.Button("Select Existing Preview Key") && selectedKey != nullptr)
                {
                    *selectedKey = *existing;
                }
            }
            else if (ui.Button("Insert Key at Preview Time") && preview != nullptr)
            {
                const double phase = preview->State().DayFraction;
                auto updated = InsertLightingKeyAtPhase(document.EnvironmentLighting(), phase);
                if (selectedKey != nullptr)
                {
                    const auto& insertedKeys = updated.Keyframes();
                    *selectedKey = static_cast<std::size_t>(
                        std::lower_bound(insertedKeys.begin(), insertedKeys.end(), phase,
                                         [](const auto& key, const double value) { return key.DayFraction < value; }) -
                        insertedKeys.begin());
                }
                document.UpdateEnvironmentLighting(std::move(updated));
                profile = document.EnvironmentLighting();
                keys = profile.Keyframes();
                commit = true;
            }
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Inserts evaluated lighting, or selects the key already at preview time. "
                              "Selection does not save. Undo removes an inserted key.");
        if (selectedKey != nullptr && !keys.empty())
        {
            *selectedKey = std::min(*selectedKey, keys.size() - 1);
            ui.Text("Selected lighting key");
            ui.SetNextItemWidth(-1.0F);
            if (auto combo =
                    ui.BeginCombo("##SelectedLightingKey", "Key " + std::to_string(*selectedKey + 1) + " - " +
                                                               EnvironmentClockLabel(keys[*selectedKey].DayFraction));
                combo)
            {
                for (std::size_t index = 0; index < keys.size(); ++index)
                {
                    if (ui.Selectable("Key " + std::to_string(index + 1) + " - " +
                                          EnvironmentClockLabel(keys[index].DayFraction),
                                      *selectedKey == index))
                    {
                        *selectedKey = index;
                        commit = true;
                    }
                }
            }
        }
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            if (selectedKey != nullptr && index != *selectedKey)
            {
                continue;
            }
            auto id = ui.PushId(std::to_string(index));
            std::optional<Keire::UiTreeNodeScope> node;
            if (selectedKey == nullptr)
            {
                node.emplace(ui.BeginSection("Key " + std::to_string(index + 1) + " - " +
                                             EnvironmentClockLabel(keys[index].DayFraction) + " - phase " +
                                             std::to_string(keys[index].DayFraction) + "###LightingKey"));
            }
            if (node && !*node)
            {
                continue;
            }
            auto& key = keys[index];
            const auto scalar = [&](const std::string_view label, double& value, const double speed,
                                    const double minimum, const double maximum)
            {
                auto fieldId = ui.PushId(label);
                ui.TextWrapped(label);
                ui.SetNextItemWidth(-1.0F);
                const bool changed = ui.DragScalar("##Value", value, speed, minimum, maximum);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
                return changed;
            };
            {
                auto previewDisabled = ui.BeginDisabled(preview == nullptr);
                if (ui.Button("Preview Key Time") && preview != nullptr)
                {
                    preview->Seek(preview->State().Day, key.DayFraction);
                    preview->SetPlaying(false);
                }
            }
            ui.TextColoredWrapped(theme.MutedText,
                                  "Preview pauses at this key on the current preview day without saving. "
                                  "Unavailable during Play Mode.");
            double hours = key.DayFraction * 24.0;
            bool changed = scalar("Time of day (hours)", hours, 1.0 / 60.0, 0.0, std::nextafter(24.0, 0.0));
            if (changed)
            {
                key.DayFraction = LightingKeyPhaseFromHours(profile, index, hours);
            }
            ui.TextColoredWrapped(theme.MutedText, "Clock: " + EnvironmentClockLabel(key.DayFraction) +
                                                       ". Time stays between neighboring keys; 12.5 means 12:30.");
            if (key.Ambient)
            {
                constexpr const char* ambientChannels[] = {"Ambient Red", "Ambient Green", "Ambient Blue"};
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    auto fieldId = ui.PushId(ambientChannels[channel]);
                    ui.TextWrapped(ambientChannels[channel]);
                    ui.SetNextItemWidth(-1.0F);
                    changed |= ui.SliderFloat("##Value", key.Ambient->LinearColor[channel], 0.0F, 1.0F);
                    commit |= ui.LastItemState().DeactivatedAfterEdit;
                }
                ui.Text("Ambient Intensity");
                ui.SetNextItemWidth(-1.0F);
                changed |= ui.SliderFloat("##AmbientIntensity", key.Ambient->Intensity, 0.0F, 16.0F);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
            }
            changed |= scalar("Azimuth (degrees)", key.AzimuthDegrees, 0.5, 0.0, std::nextafter(360.0, 0.0));
            changed |= scalar("Elevation (degrees)", key.ElevationDegrees, 0.5, -90.0, 90.0);
            constexpr const char* channels[] = {"Linear Red", "Linear Green", "Linear Blue"};
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                changed |= scalar(channels[channel], key.LinearColor[channel], 0.01, 0.0, 1.0);
            }
            changed |= scalar("Intensity", key.Intensity, 0.1, 0.0, std::numeric_limits<float>::max());
            if (changed)
            {
                document.UpdateEnvironmentLighting(ReplaceLightingKey(document.EnvironmentLighting(), index, key));
            }
            auto disabled = ui.BeginDisabled(keys.size() <= 2);
            if (ui.Button("Remove Key"))
            {
                document.UpdateEnvironmentLighting(RemoveLightingKey(document.EnvironmentLighting(), index));
                return true;
            }
        }
        ui.TextColoredWrapped(theme.MutedText, "Keys must remain in increasing day-fraction order. New keys inherit "
                                               "the preceding key in the largest time interval.");
        auto disabled = ui.BeginDisabled(keys.size() >= LightingProfile::MaximumKeyframes);
        if (ui.Button("Add Lighting Key"))
        {
            document.UpdateEnvironmentLighting(AddLightingKey(document.EnvironmentLighting()));
            return true;
        }
        return commit;
    }
} // namespace KeireEditor::Detail
