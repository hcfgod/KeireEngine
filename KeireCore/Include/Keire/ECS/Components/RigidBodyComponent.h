#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Physics/PhysicsSystem.h"

namespace Keire
{
    class KEIRE_API RigidBodyComponent final : public Component
    {
      public:
        RigidBodyComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245524947ULL, 0x4944424f44590001ULL));
        }

        [[nodiscard]] PhysicsMotionType Motion() const noexcept { return m_Motion; }
        [[nodiscard]] float Mass() const noexcept { return m_Mass; }
        [[nodiscard]] Vector3 LinearVelocity() const noexcept { return m_LinearVelocity; }
        [[nodiscard]] bool Continuous() const noexcept { return m_Continuous; }
        [[nodiscard]] bool UseGravity() const noexcept { return m_UseGravity; }

        void SetMotion(PhysicsMotionType value);
        void SetMass(float value);
        void SetLinearVelocity(Vector3 value);
        void SetContinuous(bool value);
        void SetUseGravity(bool value);

      private:
        friend ComponentRegistration CreateRigidBodyComponentRegistration();
        PhysicsMotionType m_Motion = PhysicsMotionType::Dynamic;
        float m_Mass = 1.0F;
        Vector3 m_LinearVelocity;
        bool m_Continuous = false;
        bool m_UseGravity = true;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateRigidBodyComponentRegistration();
} // namespace Keire
