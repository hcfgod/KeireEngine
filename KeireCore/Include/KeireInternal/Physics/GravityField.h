#pragma once

#include "Keire/Physics/PhysicsSystem.h"

#include <algorithm>
#include <cmath>
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
} // namespace Keire::Detail
