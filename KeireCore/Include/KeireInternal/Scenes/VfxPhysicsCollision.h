#pragma once

#include "Keire/Math/Vector3d.h"
#include "Keire/Physics/PhysicsSystem.h"
#include "Keire/Vfx/VfxSystem.h"

#include <cmath>
#include <optional>
#include <stdexcept>

namespace Keire::Detail
{
    [[nodiscard]] inline std::optional<VfxCollisionHit> QueryVfxPhysicsCollision(const PhysicsWorld& physics,
                                                                                 const Vector3d simulationOrigin,
                                                                                 const Vector3 start, const Vector3 end)
    {
        if (!Math::IsFinite(simulationOrigin) || !Math::IsFinite(start) || !Math::IsFinite(end))
        {
            throw std::invalid_argument("VFX collision coordinates must be finite.");
        }
        const Vector3 delta{end.X - start.X, end.Y - start.Y, end.Z - start.Z};
        const auto distance = std::sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
        if (!std::isfinite(distance))
        {
            throw std::invalid_argument("VFX collision segment exceeds the local simulation range.");
        }
        constexpr float minimumSegmentLength = 0.000001F;
        if (distance <= minimumSegmentLength)
        {
            return std::nullopt;
        }
        constexpr double maximumPositionErrorMetres = 0.001;
        const auto physicsOrigin = physics.WorldOrigin();
        // Subtract origins before adding local offsets: an absolute intermediate loses small offsets.
        const Vector3d offset{simulationOrigin.X - physicsOrigin.X, simulationOrigin.Y - physicsOrigin.Y,
                              simulationOrigin.Z - physicsOrigin.Z};
        const auto localStart = Math::RelativeFloat({offset.X + start.X, offset.Y + start.Y, offset.Z + start.Z}, {},
                                                    maximumPositionErrorMetres);
        const Vector3 direction{delta.X / distance, delta.Y / distance, delta.Z / distance};
        const auto hits = physics.RayCast({localStart, direction, distance, ~0U, true, 1});
        if (hits.empty())
        {
            return std::nullopt;
        }
        const auto& hit = hits.front();
        const auto simulationPosition =
            Math::RelativeFloat({hit.Position.X - offset.X, hit.Position.Y - offset.Y, hit.Position.Z - offset.Z}, {},
                                maximumPositionErrorMetres);
        return VfxCollisionHit{simulationPosition, hit.Normal};
    }
} // namespace Keire::Detail
