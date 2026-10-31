#pragma once

#include "Keire/Rendering/Atmosphere.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace Keire::Detail
{
    [[nodiscard]] inline bool IsSupportedAtmosphereRadius(const double radius) noexcept
    {
        return std::isfinite(radius) && radius >= 1'000.0 && radius <= 1.0e9;
    }

    inline void ValidateAtmosphere(const AtmosphereSettings& settings, const std::optional<double> physicalRadius = {})
    {
        const auto bounded = [](const double value, const double minimum, const double maximum)
        { return std::isfinite(value) && value >= minimum && value <= maximum; };
        if (!bounded(settings.GroundIntensity, 0.0, 100.0))
        {
            throw std::invalid_argument("Atmosphere ground intensity must be finite and within 0..100.");
        }
        for (const double coordinate : settings.PlanetCenter)
        {
            if (!bounded(coordinate, -1.0e15, 1.0e15))
            {
                throw std::invalid_argument("Atmosphere planet center must be finite and within 1e15 metres.");
            }
        }
        if (physicalRadius && !IsSupportedAtmosphereRadius(*physicalRadius))
        {
            throw std::invalid_argument(
                "Planet atmosphere overrides require a surface radius from 1,000 to 1,000,000,000 metres.");
        }
        const double thicknessLimit = physicalRadius.value_or(settings.PlanetRadius);
        if (!IsSupportedAtmosphereRadius(settings.PlanetRadius) || !bounded(settings.Height, 100.0, thicknessLimit) ||
            !bounded(settings.RayleighScaleHeight, 1.0, settings.Height) ||
            !bounded(settings.MieScaleHeight, 1.0, settings.Height))
        {
            throw std::invalid_argument("Atmosphere radius, thickness or density scale heights are invalid.");
        }
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            if (!bounded(settings.RayleighScattering[channel], 0.0, 0.01) ||
                !bounded(settings.AbsorptionExtinction[channel], 0.0, 0.01) ||
                !bounded(settings.SolarIrradiance[channel], 0.0, 100'000.0) ||
                !bounded(settings.GroundReflectance[channel], 0.0, 1.0))
            {
                throw std::invalid_argument("Atmosphere spectral coefficients or solar irradiance are invalid.");
            }
        }
        if (!bounded(settings.MieScattering, 0.0, 0.01) ||
            !bounded(settings.MieExtinction, settings.MieScattering, 0.01) ||
            !bounded(settings.MieAnisotropy, -0.95, 0.95) ||
            !bounded(settings.AbsorptionPeakAltitude, 0.0, settings.Height) ||
            !bounded(settings.AbsorptionHalfWidth, 1.0, settings.Height) ||
            !bounded(settings.SunAngularRadius, 0.00001, 0.05))
        {
            throw std::invalid_argument("Atmosphere aerosol, absorption or sun parameters are invalid.");
        }
        if (settings.ViewSamples < 4 || settings.ViewSamples > 64 || settings.LightSamples < 2 ||
            settings.LightSamples > 32)
        {
            throw std::invalid_argument("Atmosphere sample budgets must be 4..64 view and 2..32 light samples.");
        }
    }
    [[nodiscard]] inline AtmosphereSettings CreatePlanetAtmosphereOverride(const AtmosphereSettings& inherited,
                                                                           const double physicalRadius)
    {
        ValidateAtmosphere(inherited);
        if (!IsSupportedAtmosphereRadius(physicalRadius))
        {
            throw std::invalid_argument(
                "Planet atmosphere overrides require a surface radius from 1,000 to 1,000,000,000 metres.");
        }
        auto result = inherited;
        // Only initialization of a new override bounds copied appearance; existing profiles are never coerced.
        result.Height = std::min(result.Height, physicalRadius);
        result.RayleighScaleHeight = std::min(result.RayleighScaleHeight, result.Height);
        result.MieScaleHeight = std::min(result.MieScaleHeight, result.Height);
        result.AbsorptionPeakAltitude = std::min(result.AbsorptionPeakAltitude, result.Height);
        result.AbsorptionHalfWidth = std::min(result.AbsorptionHalfWidth, result.Height);
        ValidateAtmosphere(result, physicalRadius);
        return result;
    }
} // namespace Keire::Detail
