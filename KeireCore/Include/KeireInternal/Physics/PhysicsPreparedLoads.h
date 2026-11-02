#pragma once

#include "Keire/Physics/PhysicsSystem.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Keire::Detail
{
    enum class PhysicsLoadSource : std::uint8_t
    {
        Water
    };

    struct PhysicsLoadCheckpoint
    {
        std::uint64_t Tick = 0;
        std::optional<std::uint64_t> AcceptedWaterTick;
        float AcceptedWaterDeltaSeconds = 0.0F;
    };

    struct PhysicsLoadFrameContext
    {
        std::uint64_t TopologyRevision = 0;
        std::uint64_t FrameEpoch = 0;
        float DeltaSeconds = 0.0F;

        bool operator==(const PhysicsLoadFrameContext&) const noexcept = default;
    };

    struct PhysicsLoadBodySnapshot
    {
        PhysicsBodyState State;
        // COM in the physics world's local simulation axes; add WorldOrigin only in double precision.
        Vector3d CenterOfMass;
        float InverseMass = 0.0F;
        Vector3 InversePrincipalInertia;
        // Principal inertia axes -> body-local axes: I_body^-1 = R * diagonal * transpose(R).
        Quaternion InertiaRotation;
        // Column-major world/simulation-axis tensor after native angular degree-of-freedom masking.
        // Authoritative when any angular axis is locked; do not reconstruct without applying that mask.
        std::array<double, 9> WorldInverseInertia{};
        PhysicsMotionSettings MotionSettings;
        bool UseGravity = false;
    };

    class PhysicsFrameAccess;

    // Returned as shared_ptr<const>; worker calculations borrow no native world/body pointers.
    class PhysicsLoadCapture final
    {
      public:
        [[nodiscard]] PhysicsLoadSource Source() const noexcept { return m_Source; }

        [[nodiscard]] PhysicsLoadFrameContext Context() const noexcept { return m_Context; }

        [[nodiscard]] std::uint64_t PhysicsTick() const noexcept { return m_PhysicsTick; }

        [[nodiscard]] Vector3d WorldOrigin() const noexcept { return m_WorldOrigin; }

        [[nodiscard]] Vector3 UniformGravity() const noexcept { return m_UniformGravity; }

        [[nodiscard]] std::span<const PhysicsGravitySource> GravitySources() const noexcept { return m_GravitySources; }

        [[nodiscard]] std::span<const PhysicsLoadBodySnapshot> Bodies() const noexcept { return m_Bodies; }

      private:
        friend class PhysicsFrameAccess;
        PhysicsLoadCapture() = default;

        WeakRef<PhysicsWorld> m_World;
        PhysicsLoadSource m_Source = PhysicsLoadSource::Water;
        PhysicsLoadFrameContext m_Context;
        std::uint64_t m_PhysicsTick = 0;
        std::uint64_t m_WorldRevision = 0;
        Vector3d m_WorldOrigin;
        Vector3 m_UniformGravity;
        std::vector<PhysicsGravitySource> m_GravitySources;
        std::vector<PhysicsLoadBodySnapshot> m_Bodies;
    };

    struct PreparedPhysicsBodyLoad
    {
        PhysicsBodyId Body;
        // Newtons and Newton-metres in simulation axes, torque about the captured COM.
        // These are additive contributions, never replacement accumulator values.
        Vector3d Force;
        Vector3d Torque;
    };

    using ImmutablePhysicsLoadCapture = std::shared_ptr<const PhysicsLoadCapture>;
} // namespace Keire::Detail
