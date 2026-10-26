#pragma once

#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Rendering/CloudUniforms.h"
#include "KeireInternal/Rendering/VolumetricCloudUniforms.h"

#include <array>
#include <optional>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline VolumetricCloudUniforms
    CaptureCloudVolume(const RenderCamera& camera, const VolumetricCloudSettings& settings,
                       const std::optional<EnvironmentLightingSnapshot>& environment)
    {
        if (!settings.Enabled)
        {
            return {};
        }
        const auto observer = Math::TransformPoint(Math::Inverse(camera.View), {});
        const std::array<double, 3> observerFromAnchor{observer.X, observer.Y, observer.Z};
        if (!CloudObserverWithinLocalDomain(observerFromAnchor))
        {
            return {};
        }
        Detail::EnvironmentClockState clock;
        if (environment && environment->Time)
        {
            const auto& time = *environment->Time;
            clock = {time.Day, time.DayFraction, time.SecondsPerDay};
        }
        return MakeVolumetricCloudUniforms(settings, observerFromAnchor, clock);
    }
} // namespace Keire::RenderBackend
