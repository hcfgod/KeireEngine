#pragma once

#include "Keire/Rendering/Atmosphere.h"

#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    inline void ValidateAtmosphere(const AtmosphereSettings& settings)
    {
        const auto bounded = [](const double value, const double minimum, const double maximum)
        { return std::isfinite(value) && value >= minimum && value <= maximum; };
        for (const double coordinate : settings.PlanetCenter)
        {
            if (!bounded(coordinate, -1.0e15, 1.0e15))
            {
                throw std::invalid_argument("Atmosphere planet center must be finite and within 1e15 metres.");
            }
        }
        if (!bounded(settings.PlanetRadius, 1'000.0, 1.0e9) ||
            !bounded(settings.Height, 100.0, settings.PlanetRadius) ||
            !bounded(settings.RayleighScaleHeight, 1.0, settings.Height) ||
            !bounded(settings.MieScaleHeight, 1.0, settings.Height))
        {
            throw std::invalid_argument("Atmosphere radius, thickness or density scale heights are invalid.");
        }
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            if (!bounded(settings.RayleighScattering[channel], 0.0, 0.01) ||
                !bounded(settings.AbsorptionExtinction[channel], 0.0, 0.01) ||
                !bounded(settings.SolarIrradiance[channel], 0.0, 100'000.0))
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
} // namespace Keire::Detail
