#pragma once

#include "KeireInternal/Physics/PhysicsFrameAccess.h"

#include <stdexcept>

namespace Keire::Detail
{
    struct WaterAnchorVelocity
    {
        Vector3d Linear;
        Vector3d Angular;
    };

    // Exclusive owner-thread scene evaluation: no native step or authoring mutation may occur between queries.
    [[nodiscard]] inline WaterAnchorVelocity
    CaptureWaterAnchorVelocity(const PhysicsWorld& world, const PhysicsBodyId body, const Vector3d worldPoint)
    {
        const auto state = world.TryGetBody(body);
        const auto center = PhysicsFrameAccess::TryGetCenterOfMassPosition(world, body);
        if (!state || !center)
        {
            throw std::logic_error("Water motion anchor is not a live native body.");
        }
        const auto origin = world.WorldOrigin();
        const Vector3d offset{(worldPoint.X - origin.X) - center->X, (worldPoint.Y - origin.Y) - center->Y,
                              (worldPoint.Z - origin.Z) - center->Z};
        const auto angular = state->AngularVelocity;
        WaterAnchorVelocity result;
        result.Angular = {angular.X, angular.Y, angular.Z};
        result.Linear = {state->LinearVelocity.X + angular.Y * offset.Z - angular.Z * offset.Y,
                         state->LinearVelocity.Y + angular.Z * offset.X - angular.X * offset.Z,
                         state->LinearVelocity.Z + angular.X * offset.Y - angular.Y * offset.X};
        if (!Math::IsFinite(offset) || !Math::IsFinite(result.Linear) || !Math::IsFinite(result.Angular))
        {
            throw std::out_of_range("Water motion anchor point exceeds the finite range.");
        }
        return result;
    }
} // namespace Keire::Detail
