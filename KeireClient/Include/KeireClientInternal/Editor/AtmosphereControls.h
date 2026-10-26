#pragma once

#include "Keire/Rendering/Atmosphere.h"
#include "Keire/Ui.h"
#include "Keire/UiWorkspace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string_view>

namespace KeireEditor::Detail
{
    inline bool DrawAtmosphereScalar(Keire::UiFrame& ui, const std::string_view label, double& value,
                                     const double speed, const double minimum, const double maximum, bool& commit)
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

    inline bool DrawAtmosphereSpectrum(Keire::UiFrame& ui, const std::string_view label, std::array<double, 3>& values,
                                       const double speed, const double maximum, bool& commit)
    {
        auto id = ui.PushId(label);
        ui.Text(label);
        constexpr std::array channels{"Red", "Green", "Blue"};
        bool changed = false;
        for (std::size_t channel = 0; channel < values.size(); ++channel)
        {
            changed |= DrawAtmosphereScalar(ui, channels[channel], values[channel], speed, 0.0, maximum, commit);
        }
        return changed;
    }

    inline bool DrawAtmosphereControls(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                       Keire::AtmosphereSettings& settings, bool& commit)
    {
        auto section = ui.BeginTreeNode("Planetary Atmosphere (Experimental)");
        if (!section)
        {
            return false;
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Single-scattering sky follows the selected directional light, including daily "
                              "lighting. Sky visibility still applies. Environment reflections, ambient and baked "
                              "lighting remain independent. This is not volumetric clouds or aerial perspective.");
        bool changed = ui.Checkbox("Enable Atmosphere Sky", settings.Enabled);
        commit |= changed;
        if (ui.Button("Reset Earth-like Atmosphere"))
        {
            const bool enabled = settings.Enabled;
            settings = {};
            settings.Enabled = enabled;
            changed = true;
            commit = true;
        }
        ui.TextColoredWrapped(theme.MutedText,
                              "Reset preserves enabled state and places the surface at local Y = 0. "
                              "All distances are metres; Ctrl-click numeric controls to enter exact values.");
        auto disabled = ui.BeginDisabled(!settings.Enabled);
        if (auto planet = ui.BeginTreeNode("Planet and Density"); planet)
        {
            constexpr std::array centerLabels{"Planet Center X (m)", "Planet Center Y (m)", "Planet Center Z (m)"};
            for (std::size_t axis = 0; axis < settings.PlanetCenter.size(); ++axis)
            {
                changed |= DrawAtmosphereScalar(ui, centerLabels[axis], settings.PlanetCenter[axis], 100.0, -1.0e15,
                                                1.0e15, commit);
            }
            changed |=
                DrawAtmosphereScalar(ui, "Planet Radius (m)", settings.PlanetRadius, 100.0, 1'000.0, 1.0e9, commit);
            // Preserve validity when a smaller planet or atmosphere reduces dependent ranges.
            settings.Height = std::min(settings.Height, settings.PlanetRadius);
            changed |= DrawAtmosphereScalar(ui, "Atmosphere Thickness (m)", settings.Height, 100.0, 100.0,
                                            settings.PlanetRadius, commit);
            settings.RayleighScaleHeight = std::min(settings.RayleighScaleHeight, settings.Height);
            settings.MieScaleHeight = std::min(settings.MieScaleHeight, settings.Height);
            settings.AbsorptionPeakAltitude = std::min(settings.AbsorptionPeakAltitude, settings.Height);
            settings.AbsorptionHalfWidth = std::min(settings.AbsorptionHalfWidth, settings.Height);
            changed |= DrawAtmosphereScalar(ui, "Rayleigh Scale Height (m)", settings.RayleighScaleHeight, 10.0, 1.0,
                                            settings.Height, commit);
            changed |= DrawAtmosphereScalar(ui, "Mie Scale Height (m)", settings.MieScaleHeight, 10.0, 1.0,
                                            settings.Height, commit);
            ui.TextColoredWrapped(theme.MutedText,
                                  "Center uses the scene coordinate frame. Changing radius does not move the center. "
                                  "Reducing radius/thickness clamps dependent density and absorption heights.");
        }
        if (auto scattering = ui.BeginTreeNode("Scattering and Absorption"); scattering)
        {
            changed |= DrawAtmosphereSpectrum(ui, "Rayleigh Scattering (1/m)", settings.RayleighScattering, 1.0e-7,
                                              0.01, commit);
            changed |=
                DrawAtmosphereScalar(ui, "Mie Scattering (1/m)", settings.MieScattering, 1.0e-7, 0.0, 0.01, commit);
            settings.MieExtinction = std::max(settings.MieExtinction, settings.MieScattering);
            changed |= DrawAtmosphereScalar(ui, "Mie Extinction (1/m)", settings.MieExtinction, 1.0e-7,
                                            settings.MieScattering, 0.01, commit);
            changed |= DrawAtmosphereScalar(ui, "Mie Anisotropy", settings.MieAnisotropy, 0.01, -0.95, 0.95, commit);
            changed |= DrawAtmosphereSpectrum(ui, "Absorption Extinction (1/m)", settings.AbsorptionExtinction, 1.0e-7,
                                              0.01, commit);
            changed |= DrawAtmosphereScalar(ui, "Absorption Peak Altitude (m)", settings.AbsorptionPeakAltitude, 100.0,
                                            0.0, settings.Height, commit);
            changed |= DrawAtmosphereScalar(ui, "Absorption Half-width (m)", settings.AbsorptionHalfWidth, 100.0, 1.0,
                                            settings.Height, commit);
            ui.TextColoredWrapped(
                theme.MutedText,
                "Coefficients are inverse metres, not colors. Extinction includes scattering "
                "and cannot be lower than Mie scattering. Positive anisotropy favors forward scattering.");
        }
        if (auto sunlight = ui.BeginTreeNode("Sun and Quality Budget"); sunlight)
        {
            changed |= DrawAtmosphereSpectrum(ui, "Top-of-atmosphere Solar Irradiance", settings.SolarIrradiance, 0.1,
                                              100'000.0, commit);
            constexpr double diameterDegreesPerRadiusRadian = 360.0 / std::numbers::pi_v<double>;
            constexpr double minimumSunRadius = 0.00001;
            constexpr double maximumSunRadius = 0.05;
            double diameterDegrees = settings.SunAngularRadius * diameterDegreesPerRadiusRadian;
            if (DrawAtmosphereScalar(ui, "Sun Angular Diameter (degrees)", diameterDegrees, 0.01,
                                     minimumSunRadius * diameterDegreesPerRadiusRadian,
                                     maximumSunRadius * diameterDegreesPerRadiusRadian, commit))
            {
                settings.SunAngularRadius =
                    std::clamp(diameterDegrees / diameterDegreesPerRadiusRadian, minimumSunRadius, maximumSunRadius);
                changed = true;
            }
            if (ui.Button("Reset Earth-like Sun Size"))
            {
                settings.SunAngularRadius = Keire::AtmosphereSettings{}.SunAngularRadius;
                changed = true;
                commit = true;
            }
            ui.TextColoredWrapped(theme.MutedText,
                                  "Diameter is the full visible angular width, not a world-space size. The Earth-like "
                                  "default is about 0.536 degrees. Increasing size enlarges the disk, not its glare.");
            auto viewSamples = static_cast<std::int32_t>(settings.ViewSamples);
            auto lightSamples = static_cast<std::int32_t>(settings.LightSamples);
            if (ui.SliderInt("View Samples", viewSamples, 4, 64))
            {
                settings.ViewSamples = static_cast<std::uint32_t>(std::clamp(viewSamples, 4, 64));
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
            if (ui.SliderInt("Light Samples", lightSamples, 2, 32))
            {
                settings.LightSamples = static_cast<std::uint32_t>(std::clamp(lightSamples, 2, 32));
                changed = true;
            }
            commit |= ui.LastItemState().DeactivatedAfterEdit;
            ui.TextColoredWrapped(
                theme.MutedText,
                "Solar irradiance controls atmosphere brightness independently of the light intensity. "
                "Higher sample counts reduce integration artifacts but multiply GPU work. "
                "Start with the defaults before increasing either budget.");
        }
        return changed;
    }
} // namespace KeireEditor::Detail
