#pragma once

#include <array>
#include <cstdint>

namespace Keire
{
    /// Numerical weather inputs. Surface values are material/gameplay hooks, not accumulated simulation state.
    struct WeatherProfile
    {
        double CloudCoverage = 0.0;
        double RainMillimetersPerHour = 0.0;
        /// Liquid-water-equivalent snowfall rate, not snow depth.
        double SnowWaterMillimetersPerHour = 0.0;
        double TemperatureCelsius = 15.0;
        double SurfaceWetness = 0.0;
        double SurfaceSnowCoverage = 0.0;
        /// Local east, up, north components, each bounded to +/-200 m/s (not a speed-magnitude bound).
        /// The caller owns the local-frame binding.
        std::array<double, 3> WindMetersPerSecond{};

        bool operator==(const WeatherProfile&) const noexcept = default;
    };

    struct WeatherGustSettings
    {
        /// Selects a phase on the periodic gust, without mutable random-number state.
        std::uint32_t Seed = 0;
        /// Added horizontal-vector magnitude; effective wind components can reach +/-400 m/s.
        double AmplitudeMetersPerSecond = 0.0;
        double PeriodSeconds = 10.0;

        bool operator==(const WeatherGustSettings&) const noexcept = default;
    };
} // namespace Keire
