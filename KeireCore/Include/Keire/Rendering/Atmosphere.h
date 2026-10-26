#pragma once

#include <array>
#include <compare>
#include <cstdint>

namespace Keire
{
    /// Spherical atmosphere in metres; scattering/extinction coefficients are inverse metres.
    /// PlanetCenter uses the submitted scene's coordinate frame. This does not change Transform precision.
    struct AtmosphereSettings
    {
        bool Enabled = false;
        std::array<double, 3> PlanetCenter{0.0, -6'360'000.0, 0.0};
        double PlanetRadius = 6'360'000.0;
        double Height = 100'000.0;
        double RayleighScaleHeight = 8'000.0;
        double MieScaleHeight = 1'200.0;
        std::array<double, 3> RayleighScattering{5.802e-6, 13.558e-6, 33.1e-6};
        double MieScattering = 3.996e-6;
        double MieExtinction = 4.44e-6;
        double MieAnisotropy = 0.8;
        std::array<double, 3> AbsorptionExtinction{0.65e-6, 1.881e-6, 0.085e-6};
        double AbsorptionPeakAltitude = 25'000.0;
        double AbsorptionHalfWidth = 15'000.0;
        /// Independent top-of-atmosphere illumination; daily light direction supplies the sun position.
        std::array<double, 3> SolarIrradiance{20.0, 20.0, 20.0};
        double SunAngularRadius = 0.004675;
        std::uint32_t ViewSamples = 24;
        std::uint32_t LightSamples = 8;

        auto operator<=>(const AtmosphereSettings&) const noexcept = default;
    };
} // namespace Keire
