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
        if (!settings.Environment)
        {
            // The built-in sky is a fallback lighting template, not a separately authored HDR environment.
            // Its energy follows evaluated ambient light so it cannot illuminate a zero-light night.
            const float ambientLuminance =
                0.2126F * ambient.LinearColor[0] + 0.7152F * ambient.LinearColor[1] + 0.0722F * ambient.LinearColor[2];
            const float fallbackEnergy = ambient.Intensity * ambientLuminance;
            settings.EnvironmentDiffuseIntensity *= fallbackEnergy;
            settings.EnvironmentSpecularIntensity *= fallbackEnergy;
        }
    }
} // namespace Keire::Detail
