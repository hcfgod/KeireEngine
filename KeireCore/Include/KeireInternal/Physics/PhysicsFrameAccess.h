#pragma once

#include "Keire/Physics/PhysicsSystem.h"

#include <optional>

namespace Keire::Detail
{
    class PhysicsFrameAccess final
    {
      public:
        // Validate every target in a batch before changing any body velocity.
        static KEIRE_API void ValidateKinematicMove(const PhysicsWorld& world, PhysicsBodyId body, Vector3 position,
                                                    Quaternion rotation, float deltaSeconds);

        // Sets solver velocity to reach the target over the next step; does not teleport the current pose.
        // Submit once before Step with the same duration. Only existing kinematic bodies are accepted.
        static KEIRE_API void MoveKinematic(PhysicsWorld& world, PhysicsBodyId body, Vector3 position,
                                            Quaternion rotation, float deltaSeconds);

        // Owner-thread query in the world's local solver coordinates. LinearVelocity in PhysicsBodyState
        // belongs to this point, not necessarily to the exposed object-origin Position.
        [[nodiscard]] static KEIRE_API std::optional<Vector3> TryGetCenterOfMassPosition(const PhysicsWorld& world,
                                                                                         PhysicsBodyId body);
    };
} // namespace Keire::Detail
