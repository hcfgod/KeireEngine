#pragma once

#include <cstdint>

namespace Keire::Detail
{
    [[nodiscard]] inline bool ResolveCharacterGrounded(const bool hasWalkableHit, const bool wasGrounded,
                                                       const float requestedVerticalDisplacement,
                                                       std::uint32_t& missedWalkableFrames,
                                                       const std::uint32_t maximumMissedWalkableFrames = 3U) noexcept
    {
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
