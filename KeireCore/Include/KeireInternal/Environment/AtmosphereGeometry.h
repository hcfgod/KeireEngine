#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace Keire::Detail
{
    struct AtmosphereRayInterval
    {
        double Begin = 0.0;
        double End = 0.0;
        bool GroundOccluded = false;
    };

    // The caller subtracts the planet center in double precision before supplying the origin.
    // Reject invalid geometry rather than allowing a NaN to silently produce a visible sky.
    [[nodiscard]] inline std::optional<AtmosphereRayInterval>
    IntersectAtmosphere(const std::array<double, 3>& origin, const std::array<double, 3>& direction,
                        const double groundRadius, const double atmosphereRadius)
    {
        double directionLengthSquared = 0.0;
        double originLengthSquared = 0.0;
        double projection = 0.0;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis]))
            {
                throw std::invalid_argument("Atmosphere ray must be finite.");
            }
            directionLengthSquared += direction[axis] * direction[axis];
            originLengthSquared += origin[axis] * origin[axis];
            projection += origin[axis] * direction[axis];
        }
        if (!std::isfinite(groundRadius) || !std::isfinite(atmosphereRadius) || groundRadius <= 0.0 ||
            atmosphereRadius <= groundRadius || !std::isfinite(atmosphereRadius * atmosphereRadius) ||
            !std::isfinite(originLengthSquared) || !std::isfinite(directionLengthSquared) ||
            std::abs(directionLengthSquared - 1.0) > 1.0e-8)
        {
            throw std::invalid_argument("Atmosphere requires ordered positive radii and a unit ray direction.");
        }
        const double originRadius = std::sqrt(originLengthSquared);
        if (originRadius < groundRadius)
        {
            return AtmosphereRayInterval{0.0, 0.0, true};
        }
        // (r - R)(r + R) retains near-surface height better than r*r - R*R.
        const auto roots = [&](const double radius) -> std::optional<std::array<double, 2>>
        {
            const double constant = (originRadius - radius) * (originRadius + radius);
            const double discriminant = std::fma(projection, projection, -constant);
            // Radius reconstruction and the dot product can put an exact tangent a few ulps below zero.
            // Scale to the operands, not a world-space epsilon, so genuine nearby misses stay misses.
            const double roundoff =
                8.0 * std::numeric_limits<double>::epsilon() * std::max(projection * projection, std::abs(constant));
            if (discriminant < -roundoff)
            {
                return std::nullopt;
            }
            if (discriminant <= 0.0)
            {
                return std::array{-projection, -projection};
            }
            const double radical = std::sqrt(discriminant);
            const double stableRoot = -projection - std::copysign(radical, projection);
            if (stableRoot == 0.0)
            {
                return std::array{0.0, 0.0};
            }
            const double otherRoot = constant / stableRoot;
            return std::array{std::min(stableRoot, otherRoot), std::max(stableRoot, otherRoot)};
        };
        const auto outer = roots(atmosphereRadius);
        if (!outer || (*outer)[1] <= 0.0)
        {
            return std::nullopt;
        }
        AtmosphereRayInterval result{std::max(0.0, (*outer)[0]), (*outer)[1], false};
        if (const auto ground = roots(groundRadius);
            ground && projection < 0.0 && (*ground)[0] >= 0.0 && (*ground)[0] <= result.End)
        {
            result.End = std::max(result.Begin, (*ground)[0]);
            result.GroundOccluded = true;
        }
        return result;
    }
} // namespace Keire::Detail
