#pragma once

#include "KeireInternal/Environment/EnvironmentLightingProfile.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireEditor::Detail
{
    // Art-directed defaults, not geographic solar ephemerides or physical illuminance values.
    [[nodiscard]] inline Keire::Detail::EnvironmentLightingProfile
    DaylightPreset(const Keire::EnvironmentShadowSettings shadows,
                   const std::optional<Keire::Detail::EnvironmentSkyProfile>& sky = {})
    {
        const Keire::Detail::EnvironmentLightingProfile directional(
            Keire::Detail::EnvironmentDirectionalOwnership::Environment,
            {{0.0, 0.0, -60.0, {0.15, 0.2, 0.4}, 0.0},
             {0.20, 72.0, -12.0, {0.5, 0.2, 0.1}, 0.0},
             {0.25, 90.0, 0.0, {1.0, 0.35, 0.12}, 0.5},
             {0.33, 120.0, 30.0, {1.0, 0.75, 0.5}, 3.0},
             {0.50, 180.0, 65.0, {1.0, 0.95, 0.85}, 5.0},
             {0.67, 240.0, 30.0, {1.0, 0.7, 0.4}, 3.0},
             {0.75, 270.0, 0.0, {1.0, 0.25, 0.08}, 0.5},
             {0.80, 288.0, -12.0, {0.4, 0.15, 0.2}, 0.0}},
            shadows);
        auto keys = directional.Keyframes();
        for (auto& key : keys)
        {
            const float daylight = static_cast<float>(key.Intensity / 5.0);
            key.Ambient =
                Keire::EnvironmentAmbientLight{{std::lerp(0.08F, 0.30F, daylight), std::lerp(0.12F, 0.35F, daylight),
                                                std::lerp(0.25F, 0.45F, daylight)},
                                               std::lerp(0.08F, 0.75F, daylight)};
        }
        return Keire::Detail::EnvironmentLightingProfile(directional.Ownership(), std::move(keys), shadows, sky);
    }

    // A visibility preset, not a simulated moon: preserve the authored sun and independent sky tracks.
    [[nodiscard]] inline Keire::Detail::EnvironmentLightingProfile
    NightFillPreset(const Keire::Detail::EnvironmentLightingProfile& profile,
                    const Keire::EnvironmentAmbientLight fallbackAmbient)
    {
        auto keys = profile.Keyframes();
        for (auto& key : keys)
        {
            key.Ambient = key.Ambient.value_or(fallbackAmbient);
            if (key.ElevationDegrees >= 0.0)
            {
                continue;
            }
            const float nightWeight = static_cast<float>(std::clamp(-key.ElevationDegrees / 12.0, 0.0, 1.0));
            // Use a stable baseline so repeated preset application cannot accumulate twilight fill.
            const auto original = fallbackAmbient;
            constexpr Keire::EnvironmentAmbientLight nightFill{{0.16F, 0.22F, 0.36F}, 0.18F};
            auto ambient = original;
            for (std::size_t channel = 0; channel < ambient.LinearColor.size(); ++channel)
            {
                ambient.LinearColor[channel] =
                    std::lerp(original.LinearColor[channel], nightFill.LinearColor[channel], nightWeight);
            }
            ambient.Intensity = std::lerp(original.Intensity, nightFill.Intensity, nightWeight);
            key.Ambient = ambient;
        }
        return Keire::Detail::EnvironmentLightingProfile(profile.Ownership(), std::move(keys), profile.Shadows(),
                                                         profile.Sky());
    }

    [[nodiscard]] inline std::string EnvironmentClockLabel(const double fraction)
    {
        if (!std::isfinite(fraction) || fraction < 0.0 || fraction >= 1.0)
        {
            throw std::invalid_argument("Clock display requires a day fraction in [0, 1).");
        }
        constexpr int MinutesPerDay = 24 * 60;
        const double minuteValue = fraction * MinutesPerDay;
        const double nearestMinute = std::round(minuteValue);
        // Hours-to-phase conversion can land a few ULPs below an exact minute. Correct only that
        // representation noise; ordinary sub-minute preview values still display the elapsed minute.
        constexpr double minuteBoundaryTolerance = 1.0e-10;
        const double displayMinute =
            std::abs(minuteValue - nearestMinute) <= minuteBoundaryTolerance ? nearestMinute : minuteValue;
        const int minutes = std::min(MinutesPerDay - 1, static_cast<int>(displayMinute));
        const int hour = minutes / 60;
        const int minute = minutes % 60;
        return std::string(hour < 10 ? "0" : "") + std::to_string(hour) + ":" + (minute < 10 ? "0" : "") +
               std::to_string(minute);
    }
} // namespace KeireEditor::Detail
