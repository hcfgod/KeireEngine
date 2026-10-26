#pragma once

#include "Keire/Rendering/Weather.h"

#include <cmath>
#include <cstddef>
#include <optional>

namespace Keire::Detail
{
    inline constexpr double WeatherMaximumWindMetersPerSecond = 200.0;
    inline constexpr double WeatherMaximumPrecipitationMillimetersPerHour = 1000.0;
    inline constexpr double WeatherMaximumEvaluationSeconds = 3155760000.0;

    [[nodiscard]] inline bool IsWeatherRange(double value, double minimum, double maximum) noexcept
    {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    }

    [[nodiscard]] inline bool IsValidWeatherProfile(const WeatherProfile& profile) noexcept
    {
        if (!IsWeatherRange(profile.CloudCoverage, 0.0, 1.0) ||
            !IsWeatherRange(profile.RainMillimetersPerHour, 0.0, WeatherMaximumPrecipitationMillimetersPerHour) ||
            !IsWeatherRange(profile.SnowWaterMillimetersPerHour, 0.0, WeatherMaximumPrecipitationMillimetersPerHour) ||
            !IsWeatherRange(profile.TemperatureCelsius, -273.15, 100.0) ||
            !IsWeatherRange(profile.SurfaceWetness, 0.0, 1.0) || !IsWeatherRange(profile.SurfaceSnowCoverage, 0.0, 1.0))
        {
            return false;
        }

        for (const double component : profile.WindMetersPerSecond)
        {
            if (!IsWeatherRange(component, -WeatherMaximumWindMetersPerSecond, WeatherMaximumWindMetersPerSecond))
            {
                return false;
            }
        }
        return true;
    }

    /// Reject invalid inputs instead of silently clamping authored data. Endpoints preserve exact values.
    [[nodiscard]] inline std::optional<WeatherProfile>
    BlendWeatherProfiles(const WeatherProfile& first, const WeatherProfile& second, double weight) noexcept
    {
        if (!IsValidWeatherProfile(first) || !IsValidWeatherProfile(second) || !IsWeatherRange(weight, 0.0, 1.0))
        {
            return std::nullopt;
        }
        if (weight == 0.0)
        {
            return first;
        }
        if (weight == 1.0)
        {
            return second;
        }

        const auto blend = [weight](double a, double b) { return std::lerp(a, b, weight); };
        WeatherProfile result;
        result.CloudCoverage = blend(first.CloudCoverage, second.CloudCoverage);
        result.RainMillimetersPerHour = blend(first.RainMillimetersPerHour, second.RainMillimetersPerHour);
        result.SnowWaterMillimetersPerHour =
            blend(first.SnowWaterMillimetersPerHour, second.SnowWaterMillimetersPerHour);
        result.TemperatureCelsius = blend(first.TemperatureCelsius, second.TemperatureCelsius);
        result.SurfaceWetness = blend(first.SurfaceWetness, second.SurfaceWetness);
        result.SurfaceSnowCoverage = blend(first.SurfaceSnowCoverage, second.SurfaceSnowCoverage);
        for (std::size_t axis = 0; axis < result.WindMetersPerSecond.size(); ++axis)
        {
            result.WindMetersPerSecond[axis] = blend(first.WindMetersPerSecond[axis], second.WindMetersPerSecond[axis]);
        }
        return result;
    }

    /// Stateless horizontal gust; repeatable for identical inputs on one floating-point implementation.
    /// Standard-library trig does not promise bit-identical results across platforms.
    [[nodiscard]] inline std::optional<std::array<double, 3>>
    EvaluateWeatherWind(const WeatherProfile& profile, const WeatherGustSettings& gust, double elapsedSeconds) noexcept
    {
        if (!IsValidWeatherProfile(profile) ||
            !IsWeatherRange(gust.AmplitudeMetersPerSecond, 0.0, WeatherMaximumWindMetersPerSecond) ||
            !IsWeatherRange(gust.PeriodSeconds, 0.1, 86400.0) ||
            !IsWeatherRange(elapsedSeconds, -WeatherMaximumEvaluationSeconds, WeatherMaximumEvaluationSeconds))
        {
            return std::nullopt;
        }

        constexpr double fullTurn = 6.28318530717958647692;
        const double seedPhase = static_cast<double>(gust.Seed) / 4294967296.0;
        const double phase =
            fullTurn * (std::remainder(elapsedSeconds, gust.PeriodSeconds) / gust.PeriodSeconds + seedPhase);
        auto wind = profile.WindMetersPerSecond;
        wind[0] += gust.AmplitudeMetersPerSecond * std::sin(phase);
        wind[2] += gust.AmplitudeMetersPerSecond * std::cos(phase);
        return wind;
    }
} // namespace Keire::Detail
