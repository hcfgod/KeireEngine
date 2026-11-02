#pragma once

#include "Keire/Math/Vector3d.h"
#include "KeireInternal/Spatial/FrameRotation.h"
#include "KeireInternal/Spatial/PlanetCharacterPoseClearance.h"

#include <algorithm>
#include <limits>
#include <numbers>

namespace Keire::Detail
{
    // Express the entire character interval against the obstacle's unchanged native start pose.
    // Both inputs must enclose their real paths over the SAME time interval. This does not predict
    // impulses or contacts; callers must supply bounds for those motions or reject the move.
    [[nodiscard]] inline PlanetCharacterPoseInterval
    PlanetCharacterRelativeInterval(const PlanetCharacterPoseInterval& character,
                                    const PlanetCharacterPoseInterval& obstacle, const Vector3 obstacleStartCenter,
                                    const Quaternion obstacleStartRotation)
    {
        (void)PlanetCharacterPoseEnvelope(character.Midpoint, character.AngularExcursion, character.CenterExcursion);
        if (!Math::IsFinite(obstacle.Midpoint.Center) || !Math::IsFinite(obstacleStartCenter))
        {
            throw std::invalid_argument("Relative character clearance requires finite obstacle centers.");
        }
        (void)CapsuleRotationInflation(0.0, obstacle.AngularExcursion, obstacle.CenterExcursion);
        const auto asRotation = [](const Quaternion value)
        { return FrameRotation::Normalize(value.X, value.Y, value.Z, value.W); };
        const auto inverseObstacle = asRotation(obstacle.Midpoint.Rotation).Inverse();
        const auto toStart = asRotation(obstacleStartRotation).Compose(inverseObstacle);
        const std::array<double, 3> separation{
            static_cast<double>(character.Midpoint.Center.X) - obstacle.Midpoint.Center.X,
            static_cast<double>(character.Midpoint.Center.Y) - obstacle.Midpoint.Center.Y,
            static_cast<double>(character.Midpoint.Center.Z) - obstacle.Midpoint.Center.Z};
        auto center = toStart.Apply(separation);
        center[0] += obstacleStartCenter.X;
        center[1] += obstacleStartCenter.Y;
        center[2] += obstacleStartCenter.Z;
        const auto packedCenter = Math::RelativeFloat(Vector3d{center[0], center[1], center[2]}, {}, 0.001);
        const double rounding =
            std::hypot(center[0] - packedCenter.X, center[1] - packedCenter.Y, center[2] - packedCenter.Z);
        const double distance = std::hypot(separation[0], separation[1], separation[2]);
        const double obstacleAngle = std::min(obstacle.AngularExcursion, std::numbers::pi);
        const auto orientation = toStart.Compose(asRotation(character.Midpoint.Rotation));
        const auto components = orientation.Components();
        PlanetCharacterPoseInterval result = character;
        result.Midpoint.Center = packedCenter;
        result.Midpoint.Rotation = {static_cast<float>(components[0]), static_cast<float>(components[1]),
                                    static_cast<float>(components[2]), static_cast<float>(components[3])};
        // Triangle inequality separates center excursions from rotation of midpoint separation.
        // Extra arithmetic allowance covers the double transforms and float orientation packing.
        const double arithmeticAllowance = 64.0 * std::numeric_limits<double>::epsilon() *
                                           (distance + std::hypot(center[0], center[1], center[2]) + 1.0);
        result.CenterExcursion = character.CenterExcursion + obstacle.CenterExcursion +
                                 2.0 * distance * std::sin(obstacleAngle * 0.5) + rounding + arithmeticAllowance;
        result.AngularExcursion = std::min(std::numbers::pi, character.AngularExcursion + obstacle.AngularExcursion +
                                                                 64.0 * std::numeric_limits<float>::epsilon());
        return result;
    }
} // namespace Keire::Detail
