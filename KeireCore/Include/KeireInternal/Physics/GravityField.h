#pragma once

#include "Keire/Physics/PhysicsSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>

namespace Keire::Detail
{
    inline void ValidateGravitySource(const PhysicsGravitySource& source)
    {
        if (!Math::IsFinite(source.Center) || !std::isfinite(source.GravitationalParameter) ||
            source.GravitationalParameter < 0.0 || !std::isfinite(source.RadiusMetres) || source.RadiusMetres <= 0.0)
        {
            throw std::invalid_argument(
                "Gravity source requires a finite center, nonnegative parameter and positive radius.");
        }
    }

    [[nodiscard]] inline Vector3d EvaluateGravitySource(const PhysicsGravitySource& source, const Vector3d worldOrigin,
                                                        const Vector3 localPosition)
    {
        const Vector3d toward{(source.Center.X - worldOrigin.X) - localPosition.X,
                              (source.Center.Y - worldOrigin.Y) - localPosition.Y,
                              (source.Center.Z - worldOrigin.Z) - localPosition.Z};
        const double distance = std::hypot(toward.X, toward.Y, toward.Z);
        if (!Math::IsFinite(toward) || !std::isfinite(distance))
        {
            throw std::out_of_range("Gravity source displacement is not representable.");
        }
        if (distance == 0.0 || source.GravitationalParameter == 0.0)
        {
            return {};
        }
        const double radius = std::max(distance, source.RadiusMetres);
        const double magnitude = (source.GravitationalParameter / radius) / radius * (distance / radius);
        const Vector3d acceleration{toward.X / distance * magnitude, toward.Y / distance * magnitude,
                                    toward.Z / distance * magnitude};
        if (!Math::IsFinite(acceleration))
        {
            throw std::out_of_range("Gravity source acceleration is not representable.");
        }
        return acceleration;
    }

    [[nodiscard]] inline Vector3d AccumulateGravitySources(const std::span<const PhysicsGravitySource> sources,
                                                           const Vector3d worldOrigin, const Vector3 localPosition)
    {
        Vector3d acceleration{};
        for (const auto& source : sources)
        {
            const auto contribution = EvaluateGravitySource(source, worldOrigin, localPosition);
            acceleration.X += contribution.X;
            acceleration.Y += contribution.Y;
            acceleration.Z += contribution.Z;
        }
        if (!Math::IsFinite(acceleration))
        {
            throw std::out_of_range("Accumulated gravity acceleration is not representable.");
        }
        return acceleration;
    }

    [[nodiscard]] inline Vector3 CombineGravity(const Vector3d sources, const Vector3 uniform)
    {
        const Vector3d acceleration{sources.X + uniform.X, sources.Y + uniform.Y, sources.Z + uniform.Z};
        constexpr double maximum = std::numeric_limits<float>::max();
        if (!Math::IsFinite(acceleration) || std::abs(acceleration.X) > maximum || std::abs(acceleration.Y) > maximum ||
            std::abs(acceleration.Z) > maximum)
        {
            throw std::out_of_range("Combined gravity acceleration is not representable.");
        }
        return {static_cast<float>(acceleration.X), static_cast<float>(acceleration.Y),
                static_cast<float>(acceleration.Z)};
    }
} // namespace Keire::Detail
