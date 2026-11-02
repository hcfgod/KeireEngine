#pragma once

#include "KeireInternal/Spatial/PlanetCharacterFramePath.h"
#include "KeireInternal/Spatial/PlanetCharacterRelativeInterval.h"

#include <optional>
#include <span>

namespace Keire::Detail
{
    struct PlanetCharacterMovingObstacle
    {
        PhysicsBodyId Body;
        PlanetCharacterFramePath Path;
        // Collision queries use the installed shape pose, which can precede a staged movement phase.
        std::optional<PhysicsCapsuleOverlapQuery> NativeReference;
    };

    // Candidate bodies must enumerate every independently moving obstacle. The supplied paths must bound
    // actual solver motion; speculative dynamic velocity alone is insufficient after impulses or correction.
    [[nodiscard]] inline bool
    PlanetCharacterMovingClearance(const PhysicsWorld& world, const PlanetCharacterFramePath& character,
                                   const std::span<const PlanetCharacterMovingObstacle> obstacles,
                                   const std::span<const PhysicsBodyId> movingBodies,
                                   std::size_t remainingQueries = 4095)
    {
        if (remainingQueries == 0 || remainingQueries > 4095 || movingBodies.size() != obstacles.size())
        {
            throw std::invalid_argument("Character moving clearance requires a bounded complete body list.");
        }
        for (std::size_t index = 0; index < obstacles.size(); ++index)
        {
            if (!obstacles[index].Body || obstacles[index].Body != movingBodies[index])
            {
                throw std::invalid_argument("Character moving obstacle identities are not paired.");
            }
        }
        const auto fixedClear = [&](const PhysicsCapsuleOverlapQuery& envelope)
        {
            if (remainingQueries == 0)
            {
                return false;
            }
            --remainingQueries;
            return !PhysicsFrameAccess::HasCapsuleContact(world, envelope, std::nullopt, movingBodies);
        };
        if (!PlanetCharacterPathClear(character, fixedClear))
        {
            return false;
        }
        for (const auto& obstacle : obstacles)
        {
            if (obstacle.Body == character.Start.IgnoreBody)
            {
                continue;
            }
            const auto relativePath = [&](const double begin, const double end)
            {
                const auto& native = obstacle.NativeReference ? *obstacle.NativeReference : obstacle.Path.Start;
                return PlanetCharacterRelativeInterval(character(begin, end), obstacle.Path(begin, end), native.Center,
                                                       native.Rotation);
            };
            const auto pairClear = [&](const PhysicsCapsuleOverlapQuery& envelope)
            {
                if (remainingQueries == 0)
                {
                    return false;
                }
                --remainingQueries;
                return !PhysicsFrameAccess::HasCapsuleContact(world, envelope, obstacle.Body, {});
            };
            if (!PlanetCharacterPathClear(relativePath, pairClear))
            {
                return false;
            }
        }
        return true;
    }
} // namespace Keire::Detail
