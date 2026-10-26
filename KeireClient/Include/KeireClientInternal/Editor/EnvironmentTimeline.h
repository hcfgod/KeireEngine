#pragma once

#include "KeireInternal/Environment/EnvironmentLightingProfile.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

namespace KeireEditor::Detail
{
    [[nodiscard]] inline std::vector<double>
    TimelineIntensityKnots(const std::span<const Keire::Detail::EnvironmentLightingKeyframe> keys)
    {
        // Intensity interpolation is piecewise linear. Include every authored knot so narrow peaks
        // cannot disappear between fixed-rate samples; the final endpoint closes the cyclic seam.
        std::vector<double> phases;
        phases.reserve(keys.size() + 2);
        phases.push_back(0.0);
        for (const auto& key : keys)
        {
            if (key.DayFraction > 0.0)
            {
                phases.push_back(key.DayFraction);
            }
        }
        phases.push_back(1.0);
        return phases;
    }

    [[nodiscard]] inline std::optional<std::size_t>
    TimelineKeyAtPhase(const double phase, const double width,
                       const std::span<const Keire::Detail::EnvironmentLightingKeyframe> keys)
    {
        if (!std::isfinite(phase) || !std::isfinite(width) || width <= 0.0)
        {
            return std::nullopt;
        }
        std::optional<std::size_t> selected;
        double closest = 8.0;
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            const double distance = std::abs(keys[index].DayFraction - phase) * width;
            if (distance < closest)
            {
                closest = distance;
                selected = index;
            }
        }
        return selected;
    }

    // Pixel-based snapping stays usable at narrow widths without quantizing authored key times.
    [[nodiscard]] inline double TimelinePhase(const double pointerX, const double left, const double width,
                                              const std::span<const Keire::Detail::EnvironmentLightingKeyframe> keys,
                                              const bool snap)
    {
        if (!std::isfinite(pointerX) || !std::isfinite(left) || !std::isfinite(width) || width <= 0.0)
        {
            return 0.0;
        }
        const double phase = std::clamp((pointerX - left) / width, 0.0, std::nextafter(1.0, 0.0));
        double result = phase;
        double closestPixels = 8.0;
        if (snap)
        {
            for (const auto& key : keys)
            {
                const double distance = std::abs(key.DayFraction - phase) * width;
                if (distance < closestPixels)
                {
                    closestPixels = distance;
                    result = key.DayFraction;
                }
            }
        }
        return result;
    }
} // namespace KeireEditor::Detail
