#pragma once

#include "Keire/ECS/Component.h"

namespace Keire
{
    class KEIRE_API JointComponent : public Component
    {
      public:
        [[nodiscard]] AssetId RuntimeId() const noexcept { return m_RuntimeId; }
        [[nodiscard]] EntityId ConnectedEntity() const noexcept { return m_ConnectedEntity; }
        [[nodiscard]] Vector3 LocalAnchor() const noexcept { return m_LocalAnchor; }
        [[nodiscard]] Vector3 ConnectedAnchor() const noexcept { return m_ConnectedAnchor; }
        [[nodiscard]] float BreakForce() const noexcept { return m_BreakForce; }
        [[nodiscard]] float BreakTorque() const noexcept { return m_BreakTorque; }
        [[nodiscard]] bool EnableCollision() const noexcept { return m_EnableCollision; }

        void SetRuntimeId(AssetId value);
        void SetConnectedEntity(EntityId value);
        void SetLocalAnchor(Vector3 value);
        void SetConnectedAnchor(Vector3 value);
        void SetBreakForce(float value);
        void SetBreakTorque(float value);
        void SetEnableCollision(bool value);

      protected:
        explicit JointComponent(ComponentTypeId type);

      private:
        AssetId m_RuntimeId = AssetId::Generate();
        EntityId m_ConnectedEntity;
        Vector3 m_LocalAnchor;
        Vector3 m_ConnectedAnchor;
        float m_BreakForce = 0.0F;
        float m_BreakTorque = 0.0F;
        bool m_EnableCollision = false;
    };

    class KEIRE_API FixedJointComponent final : public JointComponent
    {
      public:
        FixedJointComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245464958ULL, 0x45444a4f494e5401ULL));
        }
    };

    class KEIRE_API HingeJointComponent final : public JointComponent
    {
      public:
        HingeJointComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b4549524548494eULL, 0x47454a4f494e5401ULL));
        }

        [[nodiscard]] Vector3 Axis() const noexcept { return m_Axis; }
        [[nodiscard]] bool LimitsEnabled() const noexcept { return m_LimitsEnabled; }
        [[nodiscard]] float LowerLimitDegrees() const noexcept { return m_LowerLimitDegrees; }
        [[nodiscard]] float UpperLimitDegrees() const noexcept { return m_UpperLimitDegrees; }
        [[nodiscard]] bool MotorEnabled() const noexcept { return m_MotorEnabled; }
        [[nodiscard]] float MotorSpeedDegrees() const noexcept { return m_MotorSpeedDegrees; }
        [[nodiscard]] float MaximumMotorTorque() const noexcept { return m_MaximumMotorTorque; }

        void SetAxis(Vector3 value);
        void SetLimitsEnabled(bool value);
        void SetLimits(float lowerDegrees, float upperDegrees);
        void SetMotorEnabled(bool value);
        void SetMotor(float speedDegrees, float maximumTorque);

      private:
        Vector3 m_Axis{0.0F, 1.0F, 0.0F};
        bool m_LimitsEnabled = false;
        float m_LowerLimitDegrees = -45.0F;
        float m_UpperLimitDegrees = 45.0F;
        bool m_MotorEnabled = false;
        float m_MotorSpeedDegrees = 90.0F;
        float m_MaximumMotorTorque = 100.0F;
    };

    class KEIRE_API DistanceJointComponent final : public JointComponent
    {
      public:
        DistanceJointComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245444953ULL, 0x544a4f494e540001ULL));
        }

        [[nodiscard]] float MinimumDistance() const noexcept { return m_MinimumDistance; }
        [[nodiscard]] float MaximumDistance() const noexcept { return m_MaximumDistance; }

        void SetDistanceLimits(float minimum, float maximum);

      private:
        float m_MinimumDistance = 0.0F;
        float m_MaximumDistance = 1.0F;
    };

    class KEIRE_API SpringJointComponent final : public JointComponent
    {
      public:
        SpringJointComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245535052ULL, 0x494e474a4f494e01ULL));
        }

        [[nodiscard]] float RestLength() const noexcept { return m_RestLength; }
        [[nodiscard]] float Stiffness() const noexcept { return m_Stiffness; }
        [[nodiscard]] float Damping() const noexcept { return m_Damping; }

        void SetRestLength(float value);
        void SetStiffness(float value);
        void SetDamping(float value);

      private:
        float m_RestLength = 1.0F;
        float m_Stiffness = 100.0F;
        float m_Damping = 10.0F;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateFixedJointComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateHingeJointComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateDistanceJointComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateSpringJointComponentRegistration();
} // namespace Keire
