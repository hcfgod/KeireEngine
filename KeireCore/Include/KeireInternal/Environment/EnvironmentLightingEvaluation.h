#pragma once

#include "KeireInternal/Environment/EnvironmentLightingProfile.h"
#include "KeireInternal/Rendering/EnvironmentLightingValidation.h"

#include <cstddef>
#include <cstdint>

namespace Keire::Detail
{
    // Local east/up/north frame only. Planetary observers must transform the evaluated direction separately.
    [[nodiscard]] inline EnvironmentLightingSnapshot
    EvaluateEnvironmentLighting(const EnvironmentLightingProfile& profile, const EnvironmentClockState clock,
                                const AssetId source, const std::uint64_t revision)
    {
        EnvironmentLightingSnapshot result{source, revision, std::nullopt};
        result.Time = EnvironmentTimeSample{clock.Day, clock.DayFraction, clock.SecondsPerDay};
        if (const auto evaluated = profile.Evaluate(clock))
        {
            EnvironmentDirectionalLight light;
            for (std::size_t axis = 0; axis < light.Direction.size(); ++axis)
            {
                light.Direction[axis] = static_cast<float>(evaluated->Direction[axis]);
                light.LinearColor[axis] = static_cast<float>(evaluated->LinearColor[axis]);
            }
            light.Intensity = static_cast<float>(evaluated->Intensity);
            light.Shadows = profile.Shadows();
            result.Directional = light;
            result.Ambient = evaluated->Ambient;
        }
        ValidateEnvironmentLighting(result);
        return result;
    }
} // namespace Keire::Detail
