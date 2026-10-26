#pragma once

#include "Keire/Rendering/StarField.h"
#include "KeireClientInternal/Editor/AtmosphereControls.h"
#include "KeireInternal/Environment/StarFieldPresets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace KeireEditor::Detail
{
    inline bool DrawStarFieldControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                      Keire::StarFieldSettings& settings, bool& commit)
    {
        auto section = ui.BeginTreeNode("Star Field (Experimental)");
        if (!section)
        {
            return false;
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Seeded artistic stars, not an astronomical catalog. Preview follows the shared clock; "
                              "the selected sun direction controls night visibility. Sky visibility still applies.");
        bool changed = ui.Checkbox("Enable Stars", settings.Enabled);
        commit |= changed;
        auto disabled = ui.BeginDisabled(!settings.Enabled);
        if (ui.Button("Apply Bright Night Appearance"))
        {
            settings = Keire::Detail::MakeBrightNightStarAppearance(settings);
            changed = true;
            commit = true;
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Bright Night adjusts density, brightness, tint and size, and turns twinkle off. "
                              "It preserves seed, rotation and night timing. Preview at midnight; exposure and "
                              "viewport resolution affect apparent brightness. Undo restores your previous look.");
        if (ui.Button("Reset Star Appearance"))
        {
            const bool enabled = settings.Enabled;
            settings = {};
            settings.Enabled = enabled;
            changed = true;
            commit = true;
        }
        std::uint64_t seed = settings.Seed;
        constexpr std::uint64_t maximumSeed = std::numeric_limits<std::uint32_t>::max();
        ui.Text("Star Seed");
        ui.SetNextItemWidth(-1.0F);
        if (ui.DragUnsignedInteger("##StarSeed", seed, 1.0, std::uint64_t{0}, maximumSeed))
        {
            settings.Seed = static_cast<std::uint32_t>(std::min(seed, maximumSeed));
            changed = true;
        }
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= DrawAtmosphereScalar(ui, "Star Density", settings.Density, 0.01, 0.0, 1.0, commit);
        changed |= DrawAtmosphereScalar(ui, "Star HDR Intensity", settings.Intensity, 0.1, 0.0, 1000.0, commit);
        changed |= DrawAtmosphereSpectrum(ui, "Star Tint (linear RGB)", settings.Tint, 0.01, 1.0, commit);
        changed |= DrawAtmosphereScalar(ui, "Star Color Variation", settings.ColorVariation, 0.01, 0.0, 1.0, commit);
        constexpr double diameterScale = 360.0 / std::numbers::pi;
        double diameter = settings.AngularRadius * diameterScale;
        if (DrawAtmosphereScalar(ui, "Star Angular Diameter (degrees)", diameter, 0.001, 0.000001 * diameterScale,
                                 0.01 * diameterScale, commit))
        {
            settings.AngularRadius = std::clamp(diameter / diameterScale, 0.000001, 0.01);
            changed = true;
        }
        changed |=
            DrawAtmosphereScalar(ui, "Star Rotation (degrees)", settings.RotationDegrees, 1.0, 0.0, 360.0, commit);
        if (auto animation = ui.BeginTreeNode("Twinkle and Night Visibility"); animation)
        {
            ui.TextColoredWrapped(theme.MutedText,
                                  "Twinkle is off by default. Zero amplitude prevents flicker. No clock means a "
                                  "stationary pattern. Stars fade out at very coarse pixel footprints.");
            changed |= DrawAtmosphereScalar(ui, "Twinkle Amplitude", settings.TwinkleAmplitude, 0.01, 0.0, 1.0, commit);
            changed |=
                DrawAtmosphereScalar(ui, "Twinkle Frequency (Hz)", settings.TwinkleFrequency, 0.05, 0.0, 10.0, commit);
            changed |= DrawAtmosphereScalar(ui, "Fade Starts at Solar Elevation (degrees)", settings.FadeStartElevation,
                                            0.1, std::nextafter(settings.FullVisibilityElevation, 90.0), 90.0, commit);
            changed |= DrawAtmosphereScalar(ui, "Full Visibility at Solar Elevation (degrees)",
                                            settings.FullVisibilityElevation, 0.1, -90.0,
                                            std::nextafter(settings.FadeStartElevation, -90.0), commit);
        }
        return changed;
    }
} // namespace KeireEditor::Detail
