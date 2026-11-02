#pragma once

#include "Keire/Math/Vector3d.h"
#include "KeireInternal/Spatial/FrameRotation.h"
#include "KeireInternal/Spatial/PlanetCharacterPoseClearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace Keire::Detail
{
    // A rigid support path: linear pivot translation plus shortest-arc rotation about that pivot.
    // Coordinates are relative to the physics origin, never absolute planetary float positions.
    struct PlanetCharacterFramePath
    {
        PhysicsCapsuleOverlapQuery Start;
        std::array<double, 3> Pivot{};
        std::array<double, 3> PivotDisplacement{};
        FrameRotation Rotation;

        [[nodiscard]] PlanetCharacterPoseInterval operator()(const double begin, const double end) const
        {
            if (!std::isfinite(begin) || !std::isfinite(end) || begin < 0.0 || end > 1.0 || begin > end)
            {
                throw std::invalid_argument("Character frame path interval is invalid.");
            }
            auto quaternion = Rotation.Components();
            if (quaternion[3] < 0.0)
            {
                for (auto& value : quaternion)
                {
                    value = -value;
                }
            }
            const double sineHalfAngle = std::hypot(quaternion[0], quaternion[1], quaternion[2]);
            const double halfAngle = std::atan2(sineHalfAngle, quaternion[3]);
            const double fraction = (begin + end) * 0.5;
            const double scale = sineHalfAngle > 0.0 ? std::sin(halfAngle * fraction) / sineHalfAngle : 0.0;
            const auto turn = FrameRotation::Normalize(quaternion[0] * scale, quaternion[1] * scale,
                                                       quaternion[2] * scale, std::cos(halfAngle * fraction));
            std::array<double, 3> offset{static_cast<double>(Start.Center.X) - Pivot[0],
                                         static_cast<double>(Start.Center.Y) - Pivot[1],
                                         static_cast<double>(Start.Center.Z) - Pivot[2]};
            auto center = turn.Apply(offset);
            for (std::size_t axis = 0; axis < center.size(); ++axis)
            {
                if (!std::isfinite(Pivot[axis]) || !std::isfinite(PivotDisplacement[axis]))
                {
                    throw std::invalid_argument("Character frame pivot must be finite.");
                }
                center[axis] += Pivot[axis] + PivotDisplacement[axis] * fraction;
            }
            const auto packedCenter = Math::RelativeFloat({center[0], center[1], center[2]}, {}, 0.001);
            const double rounding =
                std::hypot(center[0] - packedCenter.X, center[1] - packedCenter.Y, center[2] - packedCenter.Z);
            const auto orientation = turn.Compose(
                FrameRotation::Normalize(Start.Rotation.X, Start.Rotation.Y, Start.Rotation.Z, Start.Rotation.W));
            const auto& components = orientation.Components();
            PlanetCharacterPoseInterval result;
            result.Midpoint = Start;
            result.Midpoint.Center = packedCenter;
            result.Midpoint.Rotation = {static_cast<float>(components[0]), static_cast<float>(components[1]),
                                        static_cast<float>(components[2]), static_cast<float>(components[3])};
            const double halfInterval = (end - begin) * 0.5;
            // Include float quaternion packing/normalization error in the angular envelope.
            constexpr double orientationAllowance = 64.0 * std::numeric_limits<float>::epsilon();
            result.AngularExcursion = std::min(std::numbers::pi, 2.0 * halfAngle * halfInterval + orientationAllowance);
            const double pivotRadius = std::hypot(offset[0], offset[1], offset[2]);
            const double translation = std::hypot(PivotDisplacement[0], PivotDisplacement[1], PivotDisplacement[2]);
            const double pivotMagnitude = std::hypot(Pivot[0], Pivot[1], Pivot[2]);
            const double arithmeticAllowance =
                64.0 * std::numeric_limits<double>::epsilon() * (pivotMagnitude + pivotRadius + translation + 1.0);
            result.CenterExcursion = rounding + arithmeticAllowance + translation * halfInterval +
                                     2.0 * pivotRadius * std::sin(halfAngle * halfInterval);
            return result;
        }
    };
} // namespace Keire::Detail
