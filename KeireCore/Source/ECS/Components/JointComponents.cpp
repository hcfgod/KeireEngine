#include "Keire/ECS/Components/JointComponents.h"

#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/ECS/Entity.h"

#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Keire
{
    namespace
    {
        constexpr std::uint32_t JointSchemaVersion = 2;

        template <typename T>
        [[nodiscard]] T ReadJointProperty(const ComponentPropertyBag& values, const std::string_view key,
                                          const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Joint component property has an incompatible type.");
        }

        [[nodiscard]] float ReadJointScalar(const ComponentPropertyBag& values, const std::string_view key,
                                            const float fallback)
        {
            return static_cast<float>(ReadJointProperty(values, key, static_cast<double>(fallback)));
        }

        [[nodiscard]] std::vector<ComponentProperty> CreateJointProperties()
        {
            return {{"runtimeId", "Runtime ID", "Runtime", ComponentPropertyKind::Asset, true},
                    {"connectedEntity", "Connected Entity", "Connection", ComponentPropertyKind::Entity},
                    {"localAnchor", "Local Anchor", "Connection", ComponentPropertyKind::Vector3},
                    {"connectedAnchor", "Connected Anchor", "Connection", ComponentPropertyKind::Vector3},
                    {"enableCollision", "Enable Collision", "Connection", ComponentPropertyKind::Boolean},
                    {"breakForce",
                     "Break Force",
                     "Breakage",
                     ComponentPropertyKind::Scalar,
                     false,
                     0.0,
                     1'000'000'000.0,
                     1.0,
                     {},
                     "Zero keeps the joint unbreakable by force."},
                    {"breakTorque",
                     "Break Torque",
                     "Breakage",
                     ComponentPropertyKind::Scalar,
                     false,
                     0.0,
                     1'000'000'000.0,
                     1.0,
                     {},
                     "Zero keeps the joint unbreakable by torque."}};
        }

        [[nodiscard]] ComponentPropertyBag SerializeJoint(const JointComponent& joint)
        {
            return {{"runtimeId", joint.RuntimeId()},
                    {"connectedEntity", joint.ConnectedEntity()},
                    {"localAnchor", joint.LocalAnchor()},
                    {"connectedAnchor", joint.ConnectedAnchor()},
                    {"breakForce", static_cast<double>(joint.BreakForce())},
                    {"breakTorque", static_cast<double>(joint.BreakTorque())},
                    {"enableCollision", joint.EnableCollision()}};
        }

        void DeserializeJoint(JointComponent& joint, const ComponentPropertyBag& values)
        {
            joint.SetRuntimeId(ReadJointProperty(values, "runtimeId", AssetId{}));
            joint.SetConnectedEntity(ReadJointProperty(values, "connectedEntity", EntityId{}));
            joint.SetLocalAnchor(ReadJointProperty(values, "localAnchor", Vector3{}));
            joint.SetConnectedAnchor(ReadJointProperty(values, "connectedAnchor", Vector3{}));
            joint.SetBreakForce(ReadJointScalar(values, "breakForce", 0.0F));
            joint.SetBreakTorque(ReadJointScalar(values, "breakTorque", 0.0F));
            joint.SetEnableCollision(ReadJointProperty(values, "enableCollision", false));
        }

        [[nodiscard]] ComponentPropertyBag MigrateJoint(ComponentPropertyBag values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported joint component schema migration.");
            values.emplace("runtimeId", AssetId::Generate());
            values.emplace("breakForce", 0.0);
            values.emplace("breakTorque", 0.0);
            values.emplace("enableCollision", false);
            return values;
        }

        void ConfigureJointRegistration(ComponentRegistration& registration, const ComponentTypeId type,
                                        std::string name)
        {
            registration.Type = type;
            registration.Name = std::move(name);
            registration.Category = "Physics/Joints";
            registration.SchemaVersion = JointSchemaVersion;
            registration.AllowMultiple = true;
            registration.RequiredComponents = {RigidBodyComponent::StaticType()};
            registration.Properties = CreateJointProperties();
        }

        void RequireCurrentSchema(const std::uint32_t version)
        {
            if (version != JointSchemaVersion)
                throw std::invalid_argument("Unsupported joint component schema version.");
        }

        void AppendProperty(std::vector<ComponentProperty>& properties, ComponentProperty property)
        {
            properties.push_back(std::move(property));
        }
    } // namespace

    JointComponent::JointComponent(const ComponentTypeId type) : Component(type) {}

    void JointComponent::SetRuntimeId(const AssetId value)
    {
        if (!value)
            throw std::invalid_argument("Joint runtime ID must not be empty.");
        m_RuntimeId = value;
        NotifyChanged();
    }

    void JointComponent::SetConnectedEntity(const EntityId value)
    {
        const auto owner = Owner();
        if (owner && owner.Id() == value)
            throw std::invalid_argument("A joint cannot connect an entity to itself.");
        m_ConnectedEntity = value;
        NotifyChanged();
    }

    void JointComponent::SetLocalAnchor(const Vector3 value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Joint local anchor must be finite.");
        m_LocalAnchor = value;
        NotifyChanged();
    }

    void JointComponent::SetConnectedAnchor(const Vector3 value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Joint connected anchor must be finite.");
        m_ConnectedAnchor = value;
        NotifyChanged();
    }

    void JointComponent::SetBreakForce(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F)
            throw std::invalid_argument("Joint break force must be finite and non-negative.");
        m_BreakForce = value;
        NotifyChanged();
    }

    void JointComponent::SetBreakTorque(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F)
            throw std::invalid_argument("Joint break torque must be finite and non-negative.");
        m_BreakTorque = value;
        NotifyChanged();
    }

    void JointComponent::SetEnableCollision(const bool value)
    {
        m_EnableCollision = value;
        NotifyChanged();
    }

    FixedJointComponent::FixedJointComponent() : JointComponent(StaticType()) {}

    HingeJointComponent::HingeJointComponent() : JointComponent(StaticType()) {}

    void HingeJointComponent::SetAxis(const Vector3 value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Hinge axis must be finite.");
        const auto lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
        if (lengthSquared <= 1.0e-12F)
            throw std::invalid_argument("Hinge axis must not be zero.");
        const auto inverseLength = 1.0F / std::sqrt(lengthSquared);
        m_Axis = {value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength};
        NotifyChanged();
    }

    void HingeJointComponent::SetLimitsEnabled(const bool value)
    {
        m_LimitsEnabled = value;
        NotifyChanged();
    }

    void HingeJointComponent::SetLimits(const float lowerDegrees, const float upperDegrees)
    {
        if (!std::isfinite(lowerDegrees) || !std::isfinite(upperDegrees) || lowerDegrees > upperDegrees ||
            lowerDegrees < -360.0F || upperDegrees > 360.0F)
        {
            throw std::invalid_argument("Hinge limits must be finite, ordered, and within 360 degrees.");
        }
        m_LowerLimitDegrees = lowerDegrees;
        m_UpperLimitDegrees = upperDegrees;
        NotifyChanged();
    }

    void HingeJointComponent::SetMotorEnabled(const bool value)
    {
        m_MotorEnabled = value;
        NotifyChanged();
    }

    void HingeJointComponent::SetMotor(const float speedDegrees, const float maximumTorque)
    {
        if (!std::isfinite(speedDegrees) || !std::isfinite(maximumTorque) || maximumTorque < 0.0F)
            throw std::invalid_argument("Hinge motor settings must be finite with non-negative torque.");
        m_MotorSpeedDegrees = speedDegrees;
        m_MaximumMotorTorque = maximumTorque;
        NotifyChanged();
    }

    DistanceJointComponent::DistanceJointComponent() : JointComponent(StaticType()) {}

    void DistanceJointComponent::SetDistanceLimits(const float minimum, const float maximum)
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < 0.0F || minimum > maximum)
            throw std::invalid_argument("Distance joint limits must be finite, non-negative, and ordered.");
        m_MinimumDistance = minimum;
        m_MaximumDistance = maximum;
        NotifyChanged();
    }

    SpringJointComponent::SpringJointComponent() : JointComponent(StaticType()) {}

    void SpringJointComponent::SetRestLength(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F)
            throw std::invalid_argument("Spring rest length must be finite and non-negative.");
        m_RestLength = value;
        NotifyChanged();
    }

    void SpringJointComponent::SetStiffness(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F)
            throw std::invalid_argument("Spring stiffness must be finite and non-negative.");
        m_Stiffness = value;
        NotifyChanged();
    }

    void SpringJointComponent::SetDamping(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F)
            throw std::invalid_argument("Spring damping must be finite and non-negative.");
        m_Damping = value;
        NotifyChanged();
    }

    ComponentRegistration CreateFixedJointComponentRegistration()
    {
        ComponentRegistration result;
        ConfigureJointRegistration(result, FixedJointComponent::StaticType(), "Fixed Joint");
        result.Factory = [] { return Ref<Component>(CreateRef<FixedJointComponent>()); };
        result.Serialize = [](const Component& component)
        { return SerializeJoint(dynamic_cast<const FixedJointComponent&>(component)); };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            RequireCurrentSchema(version);
            DeserializeJoint(dynamic_cast<FixedJointComponent&>(component), values);
        };
        result.Migrate = [](const ComponentPropertyBag& values, const std::uint32_t version)
        { return MigrateJoint(values, version); };
        return result;
    }

    ComponentRegistration CreateHingeJointComponentRegistration()
    {
        ComponentRegistration result;
        ConfigureJointRegistration(result, HingeJointComponent::StaticType(), "Hinge Joint");
        AppendProperty(result.Properties, {"axis", "Axis", "Hinge", ComponentPropertyKind::Vector3});
        AppendProperty(result.Properties, {"limitsEnabled", "Enable Limits", "Limits", ComponentPropertyKind::Boolean});
        AppendProperty(result.Properties,
                       {"lowerLimit", "Lower Limit", "Limits", ComponentPropertyKind::Scalar, false, -360.0, 360.0});
        AppendProperty(result.Properties,
                       {"upperLimit", "Upper Limit", "Limits", ComponentPropertyKind::Scalar, false, -360.0, 360.0});
        AppendProperty(result.Properties, {"motorEnabled", "Enable Motor", "Motor", ComponentPropertyKind::Boolean});
        AppendProperty(result.Properties,
                       {"motorSpeed", "Target Speed", "Motor", ComponentPropertyKind::Scalar, false, {}, {}, 1.0});
        AppendProperty(result.Properties, {"maximumMotorTorque", "Maximum Torque", "Motor",
                                           ComponentPropertyKind::Scalar, false, 0.0, 1'000'000'000.0, 1.0});
        result.Factory = [] { return Ref<Component>(CreateRef<HingeJointComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& joint = dynamic_cast<const HingeJointComponent&>(component);
            auto values = SerializeJoint(joint);
            values.emplace("axis", joint.Axis());
            values.emplace("limitsEnabled", joint.LimitsEnabled());
            values.emplace("lowerLimit", static_cast<double>(joint.LowerLimitDegrees()));
            values.emplace("upperLimit", static_cast<double>(joint.UpperLimitDegrees()));
            values.emplace("motorEnabled", joint.MotorEnabled());
            values.emplace("motorSpeed", static_cast<double>(joint.MotorSpeedDegrees()));
            values.emplace("maximumMotorTorque", static_cast<double>(joint.MaximumMotorTorque()));
            return values;
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            RequireCurrentSchema(version);
            auto& joint = dynamic_cast<HingeJointComponent&>(component);
            DeserializeJoint(joint, values);
            joint.SetAxis(ReadJointProperty(values, "axis", Vector3{0.0F, 1.0F, 0.0F}));
            joint.SetLimitsEnabled(ReadJointProperty(values, "limitsEnabled", false));
            joint.SetLimits(ReadJointScalar(values, "lowerLimit", -45.0F),
                            ReadJointScalar(values, "upperLimit", 45.0F));
            joint.SetMotorEnabled(ReadJointProperty(values, "motorEnabled", false));
            joint.SetMotor(ReadJointScalar(values, "motorSpeed", 90.0F),
                           ReadJointScalar(values, "maximumMotorTorque", 100.0F));
        };
        result.Migrate = [](const ComponentPropertyBag& values, const std::uint32_t version)
        {
            auto migrated = MigrateJoint(values, version);
            migrated.emplace("limitsEnabled", false);
            migrated.emplace("lowerLimit", -45.0);
            migrated.emplace("upperLimit", 45.0);
            migrated.emplace("motorEnabled", false);
            migrated.emplace("motorSpeed", 90.0);
            migrated.emplace("maximumMotorTorque", 100.0);
            return migrated;
        };
        return result;
    }

    ComponentRegistration CreateDistanceJointComponentRegistration()
    {
        ComponentRegistration result;
        ConfigureJointRegistration(result, DistanceJointComponent::StaticType(), "Distance Joint");
        AppendProperty(result.Properties, {"minimumDistance", "Minimum Distance", "Limits",
                                           ComponentPropertyKind::Scalar, false, 0.0, 1'000'000.0, 0.05});
        AppendProperty(result.Properties, {"maximumDistance", "Maximum Distance", "Limits",
                                           ComponentPropertyKind::Scalar, false, 0.0, 1'000'000.0, 0.05});
        result.Factory = [] { return Ref<Component>(CreateRef<DistanceJointComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& joint = dynamic_cast<const DistanceJointComponent&>(component);
            auto values = SerializeJoint(joint);
            values.emplace("minimumDistance", static_cast<double>(joint.MinimumDistance()));
            values.emplace("maximumDistance", static_cast<double>(joint.MaximumDistance()));
            return values;
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            RequireCurrentSchema(version);
            auto& joint = dynamic_cast<DistanceJointComponent&>(component);
            DeserializeJoint(joint, values);
            joint.SetDistanceLimits(ReadJointScalar(values, "minimumDistance", 0.0F),
                                    ReadJointScalar(values, "maximumDistance", 1.0F));
        };
        result.Migrate = [](const ComponentPropertyBag& values, const std::uint32_t version)
        {
            auto migrated = MigrateJoint(values, version);
            migrated.emplace("minimumDistance", 0.0);
            migrated.emplace("maximumDistance", 1.0);
            return migrated;
        };
        return result;
    }

    ComponentRegistration CreateSpringJointComponentRegistration()
    {
        ComponentRegistration result;
        ConfigureJointRegistration(result, SpringJointComponent::StaticType(), "Spring Joint");
        AppendProperty(result.Properties, {"restLength", "Rest Length", "Spring", ComponentPropertyKind::Scalar, false,
                                           0.0, 1'000'000.0, 0.05});
        AppendProperty(result.Properties, {"stiffness", "Stiffness", "Spring", ComponentPropertyKind::Scalar, false,
                                           0.0, 1'000'000'000.0, 1.0});
        AppendProperty(result.Properties, {"damping", "Damping", "Spring", ComponentPropertyKind::Scalar, false, 0.0,
                                           1'000'000'000.0, 0.1});
        result.Factory = [] { return Ref<Component>(CreateRef<SpringJointComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& joint = dynamic_cast<const SpringJointComponent&>(component);
            auto values = SerializeJoint(joint);
            values.emplace("restLength", static_cast<double>(joint.RestLength()));
            values.emplace("stiffness", static_cast<double>(joint.Stiffness()));
            values.emplace("damping", static_cast<double>(joint.Damping()));
            return values;
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            RequireCurrentSchema(version);
            auto& joint = dynamic_cast<SpringJointComponent&>(component);
            DeserializeJoint(joint, values);
            joint.SetRestLength(ReadJointScalar(values, "restLength", 1.0F));
            joint.SetStiffness(ReadJointScalar(values, "stiffness", 100.0F));
            joint.SetDamping(ReadJointScalar(values, "damping", 10.0F));
        };
        result.Migrate = [](const ComponentPropertyBag& values, const std::uint32_t version)
        {
            auto migrated = MigrateJoint(values, version);
            migrated.emplace("restLength", 1.0);
            migrated.emplace("stiffness", 100.0);
            migrated.emplace("damping", 10.0);
            return migrated;
        };
        return result;
    }
} // namespace Keire
