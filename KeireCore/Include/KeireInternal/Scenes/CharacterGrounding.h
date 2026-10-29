#pragma once

#include "Keire/Math/Math.h"

#include <cstdint>

namespace Keire::Detail
{
    [[nodiscard]] inline Vector3 ResolveCharacterCollisionRemainder(const Vector3 movement, const Vector3 surfaceNormal,
                                                                    const bool slideAlongSurface) noexcept
    {
        if (!slideAlongSurface)
            return {};
        const auto intoSurface =
            movement.X * surfaceNormal.X + movement.Y * surfaceNormal.Y + movement.Z * surfaceNormal.Z;
        if (intoSurface >= 0.0F)
            return movement;
        return {movement.X - surfaceNormal.X * intoSurface, movement.Y - surfaceNormal.Y * intoSurface,
                movement.Z - surfaceNormal.Z * intoSurface};
    }

    [[nodiscard]] inline bool ShouldSnapCharacterToGround(const bool wasGrounded,
                                                          const float requestedVerticalDisplacement,
                                                          const Vector3 surfaceNormal,
                                                          const float minimumWalkableNormal,
                                                          const Vector3 up = {0.0F, 1.0F, 0.0F}) noexcept
    {
        const auto alignment = surfaceNormal.X * up.X + surfaceNormal.Y * up.Y + surfaceNormal.Z * up.Z;
        return wasGrounded && requestedVerticalDisplacement <= 0.0F && alignment >= minimumWalkableNormal;
    }

    [[nodiscard]] inline bool ResolveCharacterGrounded(const bool hasWalkableHit, const bool wasGrounded,
                                                       const float requestedVerticalDisplacement,
                                                       std::uint32_t& missedWalkableFrames,
                                                       const std::uint32_t maximumMissedWalkableFrames = 3U) noexcept
    {
        if (requestedVerticalDisplacement > 0.0F)
        {
            missedWalkableFrames = 0;
            return false;
        }
        if (hasWalkableHit)
        {
            missedWalkableFrames = 0;
            return true;
        }
        if (requestedVerticalDisplacement <= 0.0F && wasGrounded && missedWalkableFrames < maximumMissedWalkableFrames)
        {
            ++missedWalkableFrames;
            return true;
        }
        missedWalkableFrames = 0;
        return false;
    }
} // namespace Keire::Detail
