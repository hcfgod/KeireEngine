#pragma once

#include "Keire/Rendering/Cloud.h"
#include "Keire/Ui.h"
#include "Keire/UiWorkspace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace KeireEditor::Detail
{
    inline bool DrawCloudScalar(Keire::UiFrame& ui, const std::string_view label, double& value, const double speed,
                                const double minimum, const double maximum, bool& commit)
    {
        auto id = ui.PushId(label);
        ui.TextWrapped(label);
        ui.SetNextItemWidth(-1.0F);
        double candidate = value;
        const bool edited = ui.DragScalar("##Value", candidate, speed, minimum, maximum);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        if (!edited || !std::isfinite(candidate))
        {
            return false;
        }
        candidate = std::clamp(candidate, minimum, maximum);
        if (candidate == value)
        {
            return false;
        }
        value = candidate;
        return true;
    }

    inline bool DrawCloudLayerControls(Keire::UiFrame& ui, Keire::CloudLayer& layer, bool& commit)
    {
        bool changed = ui.Checkbox("Enable Layer", layer.Enabled);
        commit |= changed;
        if (ui.Button("Reset Layer"))
        {
            const bool enabled = layer.Enabled;
            layer = {};
            layer.Enabled = enabled;
            changed = true;
            commit = true;
        }
        auto disabled = ui.BeginDisabled(!layer.Enabled);
        constexpr std::array appearances{Keire::CloudAppearance::NaturalLit, Keire::CloudAppearance::LegacyUnlit};
        constexpr std::array appearanceNames{"Natural lit", "Legacy unlit"};
        const char* selectedAppearance =
            layer.Appearance == Keire::CloudAppearance::NaturalLit ? appearanceNames[0] : appearanceNames[1];
        if (auto appearance = ui.BeginCombo("Appearance", selectedAppearance); appearance)
        {
            for (std::size_t index = 0; index < appearances.size(); ++index)
            {
                if (ui.Selectable(appearanceNames[index], layer.Appearance == appearances[index]))
                {
                    layer.Appearance = appearances[index];
                    changed = true;
                    commit = true;
                }
            }
        }
        {
            auto seedDisabled = ui.BeginDisabled(layer.Appearance == Keire::CloudAppearance::LegacyUnlit);
            std::uint64_t seed = layer.Seed;
            constexpr std::uint64_t maximumSeed = std::numeric_limits<std::uint32_t>::max();
            if (ui.DragUnsignedInteger("Pattern Seed", seed, 1.0, std::uint64_t{0}, maximumSeed))
            {
                layer.Seed = static_cast<std::uint32_t>(std::min(seed, maximumSeed));
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
        }
        changed |= DrawCloudScalar(ui, "Altitude (m)", layer.AltitudeMeters, 10.0, -10'000.0, 100'000.0, commit);
        changed |=
            DrawCloudScalar(ui, "Pattern Repeat Size (m)", layer.PatternSizeMeters, 100.0, 100.0, 1'000'000.0, commit);
        changed |= DrawCloudScalar(ui, "Coverage", layer.Coverage, 0.01, 0.0, 1.0, commit);
        changed |= DrawCloudScalar(ui, "Opacity", layer.Opacity, 0.01, 0.0, 1.0, commit);
        changed |= DrawCloudScalar(ui, "Edge Softness", layer.EdgeSoftness, 0.01, 0.001, 1.0, commit);
        changed |= DrawCloudScalar(ui, "Horizon Fade (cosine)", layer.HorizonFade, 0.01, 0.01, 1.0, commit);
        changed |=
            DrawCloudScalar(ui, "East / X Wind (m/s)", layer.WindMetersPerSecond[0], 0.1, -1'000.0, 1'000.0, commit);
        changed |=
            DrawCloudScalar(ui, "North / Z Wind (m/s)", layer.WindMetersPerSecond[1], 0.1, -1'000.0, 1'000.0, commit);
        constexpr std::array channels{"Linear Red", "Linear Green", "Linear Blue"};
        for (std::size_t channel = 0; channel < layer.LinearColor.size(); ++channel)
        {
            changed |= DrawCloudScalar(ui, channels[channel], layer.LinearColor[channel], 0.01, 0.0, 16.0, commit);
        }
        return changed;
    }

    inline bool DrawCloudControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                  Keire::CloudSettings& settings, bool& commit)
    {
        auto section = ui.BeginSection("Layered Clouds (Experimental)");
        if (!section)
        {
            return false;
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Two artistic horizontal sheets, not volumetric clouds. Altitude is local Y relative "
                              "to the world-zero pattern anchor; wind uses X/Z metres per second. These sheets do not "
                              "follow planetary curvature or cast ground shadows. Sky visibility still applies.");
        ui.TextColoredWrapped(theme.MutedText,
                              "Natural lit uses the selected sun and evaluated ambient lighting; linear RGB is its "
                              "tint. Legacy unlit preserves the old pattern and fixed color. Older layers keep legacy "
                              "appearance until you select Natural lit; Undo restores the previous choice.");
        ui.TextColoredWrapped(theme.MutedText,
                              "Pattern seed changes the natural pattern, not wind speed. Legacy mode ignores it. "
                              "Use the environment preview above to pause, scrub or reset time. Weather does not "
                              "automatically change these layers. Ctrl-click numeric controls for exact values.");
        bool changed = false;
        for (std::size_t index = 0; index < settings.Layers.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(index));
            auto layer = ui.BeginSection("Layer " + std::to_string(index + 1));
            if (layer)
            {
                changed |= DrawCloudLayerControls(ui, settings.Layers[index], commit);
            }
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Reset restores one layer's defaults, including Natural lit and seed 1, while preserving "
                              "its enabled state. A larger "
                              "horizon-fade value hides more grazing-angle coverage. Sheets fade at long distance "
                              "and have no interior; separate their altitudes to avoid overlapping identical layers.");
        return changed;
    }
} // namespace KeireEditor::Detail
