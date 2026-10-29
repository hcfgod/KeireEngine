#pragma once

#include "Keire/Math/Math.h"
#include "Keire/Rendering/Cloud.h"
#include "KeireInternal/Environment/EnvironmentCloudPhase.h"
#include "KeireInternal/Spatial/PlanetFrameResolution.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace Keire::RenderBackend
{
    struct PlanetaryCloudUniforms final
    {
        Vector4 ObserverRadialAltitude;
        Vector4 RadiusGeometry;
        Vector4 ObserverToPlanetX;
        Vector4 ObserverToPlanetY;
        Vector4 ObserverToPlanetZ;
        std::array<std::uint32_t, 4> Controls{};

        bool operator==(const PlanetaryCloudUniforms&) const noexcept = default;
    };

    static_assert(sizeof(PlanetaryCloudUniforms) == 96);

    [[nodiscard]] inline PlanetaryCloudUniforms MakePlanetaryCloudUniforms(const Detail::ResolvedPlanetFrame& frame,
                                                                           const double baseHeight)
    {
        const auto& observer = frame.ObserverFromCenterPlanetMetres;
        const double distance = std::hypot(observer[0], observer[1], observer[2]);
        if (!std::isfinite(distance) || distance <= 0.0 || !std::isfinite(frame.RadiusMetres) ||
            frame.RadiusMetres <= 0.0 || !std::isfinite(baseHeight) ||
            distance > std::sqrt(static_cast<double>(std::numeric_limits<float>::max())) * 0.125 ||
            frame.RadiusMetres > std::sqrt(static_cast<double>(std::numeric_limits<float>::max())) * 0.125 ||
            std::abs(baseHeight) > static_cast<double>(std::numeric_limits<float>::max()))
        {
            throw std::invalid_argument("Planetary clouds require a finite resolved observer and positive radius.");
        }
        const auto radial =
            frame.PlanetToObserver.Apply({observer[0] / distance, observer[1] / distance, observer[2] / distance});
        // Rows of the inverse are columns of the forward orthonormal transform.
        const auto x = frame.PlanetToObserver.Apply({1.0, 0.0, 0.0});
        const auto y = frame.PlanetToObserver.Apply({0.0, 1.0, 0.0});
        const auto z = frame.PlanetToObserver.Apply({0.0, 0.0, 1.0});
        const auto vector = [](const std::array<double, 3>& value)
        {
            return Vector4{static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2]),
                           0.0F};
        };
        PlanetaryCloudUniforms result;
        result.ObserverRadialAltitude = vector(radial);
        result.ObserverRadialAltitude.W = static_cast<float>(distance - frame.RadiusMetres);
        result.RadiusGeometry = {static_cast<float>(frame.RadiusMetres), static_cast<float>(baseHeight), 0.0F, 0.0F};
        result.ObserverToPlanetX = vector(x);
        result.ObserverToPlanetY = vector(y);
        result.ObserverToPlanetZ = vector(z);
        result.Controls[0] = 1U;
        return result;
    }

    [[nodiscard]] inline std::array<float, 3> MakePlanetaryCloudPhase(const Detail::ResolvedPlanetFrame& frame,
                                                                      const double patternSize,
                                                                      const std::array<double, 3>& wind)
    {
        (void)Detail::EnvironmentClock(frame.Clock);
        if (!std::isfinite(patternSize) || patternSize <= 0.0)
        {
            throw std::invalid_argument("Planetary cloud pattern size must be positive and finite.");
        }
        std::array<float, 3> result{};
        const double repeat = patternSize * 256.0;
        if (!std::isfinite(repeat))
        {
            throw std::invalid_argument("Planetary cloud pattern repeat exceeds the coordinate domain.");
        }
        for (std::size_t axis = 0; axis < result.size(); ++axis)
        {
            if (!std::isfinite(frame.ObserverFromCenterPlanetMetres[axis]) || !std::isfinite(wind[axis]))
            {
                throw std::invalid_argument("Planetary cloud coordinates and wind must be finite.");
            }
            const double daily =
                Detail::CloudPhaseDetail::DurationPhase(frame.Clock.SecondsPerDay, wind[axis], repeat, 1.0);
            const double fractional = Detail::CloudPhaseDetail::DurationPhase(frame.Clock.SecondsPerDay, wind[axis],
                                                                              repeat, frame.Clock.DayFraction);
            const double advection = Detail::CloudPhaseDetail::Wrap(
                Detail::CloudPhaseDetail::WholeDayPhase(daily, frame.Clock.Day) + fractional);
            const double phase = std::fmod(frame.ObserverFromCenterPlanetMetres[axis], repeat) / repeat;
            result[axis] = static_cast<float>(Detail::CloudPhaseDetail::Wrap(phase - advection) * 256.0);
            if (result[axis] >= 256.0F)
            {
                result[axis] = 0.0F;
            }
        }
        return result;
    }
} // namespace Keire::RenderBackend
