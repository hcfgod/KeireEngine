#pragma once

#include <array>
#include <compare>
#include <cstdint>

namespace Keire
{
    /// Art-directed distant stars, not an astronomical catalog. Angles are in degrees unless stated otherwise.
    struct StarFieldSettings
    {
        bool Enabled = false;
        std::uint32_t Seed = 1;
        /// Fraction of candidate stars retained; zero is an empty field.
        double Density = 0.35;
        /// Linear HDR radiance before scene exposure. Tint is linear RGB.
        double Intensity = 1.0;
        std::array<double, 3> Tint{1.0, 1.0, 1.0};
        double ColorVariation = 0.5;
        /// Apparent angular radius in radians; filtering must preserve subpixel energy.
        double AngularRadius = 0.00015;
        double RotationDegrees = 0.0;
        /// Zero amplitude disables temporal modulation, regardless of frequency.
        double TwinkleAmplitude = 0.0;
        /// Cycles per simulation second; preview pause and time jumps must use the shared clock.
        double TwinkleFrequency = 0.5;
        /// Stars fade in as solar elevation decreases, reaching full visibility at the second threshold.
        double FadeStartElevation = -2.0;
        double FullVisibilityElevation = -12.0;

        auto operator<=>(const StarFieldSettings&) const noexcept = default;
    };
} // namespace Keire
