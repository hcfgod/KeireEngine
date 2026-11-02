#pragma once

#include "KeireInternal/Spatial/PlanetCharacterFramePath.h"

namespace Keire::Detail
{
    // Jolt MoveKinematic translates the center of mass linearly and integrates shortest-arc angular velocity.
    // The tracked capsule center may differ from COM; that offset rotates rather than following a straight line.
    [[nodiscard]] inline PlanetCharacterFramePath
    MakePlanetCharacterKinematicPath(const PhysicsCapsuleOverlapQuery& start, const Vector3 startCenterOfMass,
                                     const Vector3 targetCenterOfMass, const Quaternion targetRotation)
    {
        if (!Math::IsFinite(startCenterOfMass) || !Math::IsFinite(targetCenterOfMass))
        {
            throw std::invalid_argument("Character kinematic path requires finite center-of-mass endpoints.");
        }
        (void)PlanetCharacterPoseEnvelope(start, 0.0, 0.0);
        const auto initial =
            FrameRotation::Normalize(start.Rotation.X, start.Rotation.Y, start.Rotation.Z, start.Rotation.W);
        const auto target =
            FrameRotation::Normalize(targetRotation.X, targetRotation.Y, targetRotation.Z, targetRotation.W);
        PlanetCharacterFramePath result;
        result.Start = start;
        result.Pivot = {startCenterOfMass.X, startCenterOfMass.Y, startCenterOfMass.Z};
        result.PivotDisplacement = {static_cast<double>(targetCenterOfMass.X) - startCenterOfMass.X,
                                    static_cast<double>(targetCenterOfMass.Y) - startCenterOfMass.Y,
                                    static_cast<double>(targetCenterOfMass.Z) - startCenterOfMass.Z};
        result.Rotation = target.Compose(initial.Inverse());
        return result;
    }
} // namespace Keire::Detail
