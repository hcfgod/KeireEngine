#include "Keire/ECS/Components/RigidBodyComponent.h"

#include "Keire/ECS/Components/ColliderComponent.h"
#include "KeireInternal/Physics/MotionSettings.h"

#include <cmath>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadRigidBodyProperty(const ComponentPropertyBag& values, const std::string_view key,
                                              const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Rigid Body component property has an incompatible type.");
        }
    } // namespace

    RigidBodyComponent::RigidBodyComponent() : Component(StaticType()) {}

    void RigidBodyComponent::SetMotionSettings(const PhysicsMotionSettings& value)
    {
        if (!Detail::ValidMotionSettings(value))
        {
            throw std::invalid_argument(
                "Rigid Body damping must be finite and nonnegative; speed limits must be finite and positive.");
        }
        m_MotionSettings = value;
        NotifyChanged();
    }

    void RigidBodyComponent::SetMotion(const PhysicsMotionType value)
    {
        if (value < PhysicsMotionType::Static || value > PhysicsMotionType::Kinematic)
            throw std::invalid_argument("Rigid Body motion type is outside the supported range.");
        m_Motion = value;
        NotifyChanged();
    }

    void RigidBodyComponent::SetMass(const float value)
    {
        if (!std::isfinite(value) || value <= 0.0F)
            throw std::invalid_argument("Rigid Body mass must be finite and positive.");
        m_Mass = value;
        NotifyChanged();
    }

    void RigidBodyComponent::SetLinearVelocity(const Vector3 value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Rigid Body velocity must be finite.");
        m_LinearVelocity = value;
        NotifyChanged();
    }

    void RigidBodyComponent::SetContinuous(const bool value)
    {
        m_Continuous = value;
        NotifyChanged();
    }

    void RigidBodyComponent::SetUseGravity(const bool value)
    {
        m_UseGravity = value;
        NotifyChanged();
    }

    ComponentRegistration CreateRigidBodyComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = RigidBodyComponent::StaticType();
        result.Name = "Rigid Body";
        result.Category = "Physics";
        result.RequiredComponents = {ColliderComponent::StaticType()};
        result.Properties = {{"motion", "Motion", "Body", ComponentPropertyKind::Integer, false, 0.0, 2.0, 1.0},
                             {"mass", "Mass", "Body", ComponentPropertyKind::Scalar, false, 0.001, 1'000'000.0, 0.05},
                             {"linearVelocity", "Linear Velocity", "Body", ComponentPropertyKind::Vector3},
                             {"continuous", "Continuous Collision", "Body", ComponentPropertyKind::Boolean},
                             {"useGravity", "Use Gravity", "Body", ComponentPropertyKind::Boolean},
                             {"linearDamping", "Linear Damping (1/s)", "Motion", ComponentPropertyKind::Scalar, false,
                              0.0, 1'000'000.0, 0.01},
                             {"angularDamping", "Angular Damping (1/s)", "Motion", ComponentPropertyKind::Scalar, false,
                              0.0, 1'000'000.0, 0.01},
                             {"maximumLinearSpeed", "Maximum Linear Speed (m/s)", "Motion",
                              ComponentPropertyKind::Scalar, false, 0.001, 1'000'000'000.0, 1.0},
                             {"maximumAngularSpeed", "Maximum Angular Speed (rad/s)", "Motion",
                              ComponentPropertyKind::Scalar, false, 0.001, 1'000'000'000.0, 0.1}};
        result.Factory = [] { return Ref<Component>(CreateRef<RigidBodyComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& body = dynamic_cast<const RigidBodyComponent&>(component);
            return ComponentPropertyBag{
                {"motion", static_cast<std::int64_t>(body.m_Motion)},
                {"mass", static_cast<double>(body.m_Mass)},
                {"linearVelocity", body.m_LinearVelocity},
                {"continuous", body.m_Continuous},
                {"useGravity", body.m_UseGravity},
                {"linearDamping", static_cast<double>(body.m_MotionSettings.LinearDamping)},
                {"angularDamping", static_cast<double>(body.m_MotionSettings.AngularDamping)},
                {"maximumLinearSpeed", static_cast<double>(body.m_MotionSettings.MaximumLinearSpeed)},
                {"maximumAngularSpeed", static_cast<double>(body.m_MotionSettings.MaximumAngularSpeed)}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Rigid Body component schema version.");
            auto& body = dynamic_cast<RigidBodyComponent&>(component);
            const auto motion = ReadRigidBodyProperty(values, "motion", std::int64_t{1});
            if (motion < 0 || motion > static_cast<std::int64_t>(PhysicsMotionType::Kinematic))
                throw std::invalid_argument("Rigid Body motion type is outside the supported range.");
            const auto mass = static_cast<float>(ReadRigidBodyProperty(values, "mass", 1.0));
            const auto velocity = ReadRigidBodyProperty(values, "linearVelocity", Vector3{});
            const auto continuous = ReadRigidBodyProperty(values, "continuous", false);
            const auto useGravity = ReadRigidBodyProperty(values, "useGravity", true);
            const PhysicsMotionSettings defaults;
            const PhysicsMotionSettings settings{
                static_cast<float>(
                    ReadRigidBodyProperty(values, "linearDamping", static_cast<double>(defaults.LinearDamping))),
                static_cast<float>(
                    ReadRigidBodyProperty(values, "angularDamping", static_cast<double>(defaults.AngularDamping))),
                static_cast<float>(ReadRigidBodyProperty(values, "maximumLinearSpeed",
                                                         static_cast<double>(defaults.MaximumLinearSpeed))),
                static_cast<float>(ReadRigidBodyProperty(values, "maximumAngularSpeed",
                                                         static_cast<double>(defaults.MaximumAngularSpeed)))};
            if (!std::isfinite(mass) || mass <= 0.0F || !Math::IsFinite(velocity) ||
                !Detail::ValidMotionSettings(settings))
            {
                throw std::invalid_argument("Rigid Body component contains invalid values.");
            }
            body.m_Motion = static_cast<PhysicsMotionType>(motion);
            body.m_Mass = mass;
            body.m_LinearVelocity = velocity;
            body.m_Continuous = continuous;
            body.m_UseGravity = useGravity;
            body.m_MotionSettings = settings;
        };
        return result;
    }
} // namespace Keire
