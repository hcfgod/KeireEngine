#pragma once

#include "Keire/Math/Vector3d.h"
#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Rendering/CloudUniforms.h"
#include "KeireInternal/Rendering/EnvironmentViewCoordinates.h"
#include "KeireInternal/Rendering/PlanetaryCloudUniforms.h"
#include "KeireInternal/Rendering/VolumetricCloudUniforms.h"

#include <array>
#include <optional>
#include <stdexcept>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline VolumetricCloudUniforms
    CaptureCloudVolume(const RenderCamera& camera, const VolumetricCloudSettings& settings,
                       const std::optional<EnvironmentLightingSnapshot>& environment, const bool atCloudBase = false,
                       const Vector3d renderOrigin = {}, const Vector3d environmentAnchor = {},
                       const Detail::ResolvedPlanetFrame* planetFrame = nullptr)
    {
        if (!settings.Enabled)
        {
            return {};
        }
        if (planetFrame)
        {
            auto result = MakeVolumetricCloudUniforms(settings, {}, planetFrame->Clock);
            const auto phase =
                MakePlanetaryCloudPhase(*planetFrame, settings.PatternSizeMeters, settings.WindMetersPerSecond);
            for (std::size_t axis = 0; axis < phase.size(); ++axis)
            {
                result.PatternOrigin[axis] = phase[axis];
            }
            result.Geometry[0] = static_cast<float>(settings.BaseHeightMeters);
            return result;
        }
        const auto observer = Math::TransformPoint(Math::Inverse(camera.View), {});
        auto observerFromAnchor = EnvironmentRelativeObserver(observer, renderOrigin, environmentAnchor);
        if (atCloudBase)
        {
            observerFromAnchor[1] = settings.BaseHeightMeters;
        }
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
