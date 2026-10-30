#pragma once

#include "Keire/Physics/PhysicsSystem.h"

#include <optional>

namespace Keire::Detail
{
    class PhysicsFrameAccess final
    {
      public:
        // Owner-thread query in the world's local solver coordinates. LinearVelocity in PhysicsBodyState
        // belongs to this point, not necessarily to the exposed object-origin Position.
        [[nodiscard]] static KEIRE_API std::optional<Vector3> TryGetCenterOfMassPosition(const PhysicsWorld& world,
                                                                                         PhysicsBodyId body);
    };
} // namespace Keire::Detail
