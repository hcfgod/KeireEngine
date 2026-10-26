#pragma once

#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Environment/EnvironmentCloudPhase.h"
#include "KeireInternal/Environment/EnvironmentStarPhase.h"

namespace Keire::Detail
{
    // Snapshot validation occurs at request acceptance. Only the immutable packet's copy is changed.
    inline void ApplyEnvironment(RenderEnvironmentSettings& settings,
                                 std::array<std::array<double, 2>, MaximumCloudLayers>& cloudPhase, double& starPhase,
                                 const std::optional<EnvironmentLightingSnapshot>& snapshot)
    {
        cloudPhase = {};
        starPhase = 0.0;
        if (snapshot && snapshot->Time)
        {
            const auto& time = *snapshot->Time;
            cloudPhase =
                EvaluateEnvironmentCloudPhase(settings.Clouds, {time.Day, time.DayFraction, time.SecondsPerDay});
            starPhase = EvaluateEnvironmentStarPhase(settings.Stars.TwinkleFrequency,
                                                     {time.Day, time.DayFraction, time.SecondsPerDay});
        }
        if (!snapshot || !snapshot->Ambient)
        {
            return;
        }
        const auto& ambient = *snapshot->Ambient;
        settings.AmbientColor = {ambient.LinearColor[0], ambient.LinearColor[1], ambient.LinearColor[2], 1.0F};
        settings.AmbientIntensity = ambient.Intensity;
    }
} // namespace Keire::Detail
