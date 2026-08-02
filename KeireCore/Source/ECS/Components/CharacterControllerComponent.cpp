#include "Keire/ECS/Components/CharacterControllerComponent.h"

#include "Keire/ECS/Entity.h"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace Keire
{
    namespace
    {
        constexpr std::uint32_t CharacterControllerSchemaVersion = 2;
        constexpr float MaximumMovementComponent = 1'000'000.0F;

        template <typename T>
        [[nodiscard]] T ReadControllerProperty(const ComponentPropertyBag& values, const std::string_view key,
                                               const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Character Controller property has an incompatible type.");
        }

        [[nodiscard]] float ReadControllerScalar(const ComponentPropertyBag& values, const std::string_view key,
                                                 const float fallback)
        {
            return static_cast<float>(ReadControllerProperty(values, key, static_cast<double>(fallback)));
        }

        [[nodiscard]] std::uint32_t ReadControllerUnsigned(const ComponentPropertyBag& values,
                                                           const std::string_view key, const std::uint32_t fallback)
        {
            const auto value = ReadControllerProperty(values, key, static_cast<std::int64_t>(fallback));
            if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
                throw std::invalid_argument("Character Controller layer value is outside the supported range.");
            return static_cast<std::uint32_t>(value);
        }
    } // namespace

    CharacterControllerComponent::CharacterControllerComponent() : Component(StaticType()) {}

    void CharacterControllerComponent::SetRuntimeId(const AssetId value)
    {
        if (!value)
            throw std::invalid_argument("Character Controller runtime ID must not be empty.");
        m_RuntimeId = value;
        NotifyChanged();
    }

    void CharacterControllerComponent::Resize(const float radius, const float height)
    {
        ConfigureCapsule(radius, height, m_StepHeight, m_SkinWidth);
    }

    void CharacterControllerComponent::ConfigureCapsule(const float radius, const float height, const float stepHeight,
                                                        const float skinWidth)
    {
        if (!std::isfinite(radius) || !std::isfinite(height) || !std::isfinite(stepHeight) ||
            !std::isfinite(skinWidth) || radius <= 0.0F || height < radius * 2.0F || stepHeight < 0.0F ||
            stepHeight > height || skinWidth <= 0.0F || skinWidth >= radius)
        {
            throw std::invalid_argument(
                "Character Controller dimensions must form a valid capsule and contain its skin and step.");
        }
        m_Radius = radius;
        m_Height = height;
        m_StepHeight = stepHeight;
        m_SkinWidth = skinWidth;
        NotifyChanged();
    }

    void CharacterControllerComponent::SetMaximumSlopeDegrees(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value >= 90.0F)
            throw std::invalid_argument("Character Controller slope must be finite and in [0, 90) degrees.");
        m_MaximumSlopeDegrees = value;
        NotifyChanged();
    }

    void CharacterControllerComponent::SetStepHeight(const float value)
    {
        ConfigureCapsule(m_Radius, m_Height, value, m_SkinWidth);
    }

    void CharacterControllerComponent::SetSkinWidth(const float value)
    {
        ConfigureCapsule(m_Radius, m_Height, m_StepHeight, value);
    }

    void CharacterControllerComponent::SetLayer(const std::uint32_t value)
    {
        if (!std::has_single_bit(value))
            throw std::invalid_argument("Character Controller layer must select exactly one collision layer.");
        if (IsAttached())
        {
            Owner().SetLayer(std::countr_zero(value));
            return;
        }
        m_Layer = value;
        NotifyChanged();
    }

    void CharacterControllerComponent::SetMask(const std::uint32_t value)
    {
        m_Mask = value;
        NotifyChanged();
    }

    bool CharacterControllerComponent::QueueDesiredMovement(const Vector3 displacement)
    {
        if (!Math::IsFinite(displacement) || std::abs(displacement.X) > MaximumMovementComponent ||
            std::abs(displacement.Y) > MaximumMovementComponent || std::abs(displacement.Z) > MaximumMovementComponent)
        {
            throw std::invalid_argument("Character Controller movement must be finite and within the supported range.");
        }
        if (m_MovementCount == m_MovementCommands.size())
        {
            ++m_RuntimeState.DroppedMovementCommands;
            return false;
        }
        m_MovementCommands[m_MovementCount++] = displacement;
        return true;
    }

    Vector3 CharacterControllerComponent::ConsumeDesiredMovement() noexcept
    {
        Vector3 result;
        for (std::size_t index = 0; index < m_MovementCount; ++index)
        {
            result.X += m_MovementCommands[index].X;
            result.Y += m_MovementCommands[index].Y;
            result.Z += m_MovementCommands[index].Z;
        }
        m_MovementCount = 0;
        return result;
    }

    void CharacterControllerComponent::ClearDesiredMovement() noexcept { m_MovementCount = 0; }

    void CharacterControllerComponent::ApplyRuntimeState(const std::uint32_t generation, const bool grounded,
                                                         const Vector3 groundNormal, const Vector3 velocity)
    {
        if (generation == 0 || !Math::IsFinite(groundNormal) || !Math::IsFinite(velocity))
            throw std::invalid_argument("Character Controller runtime state is invalid.");

        auto normalizedGroundNormal = groundNormal;
        if (grounded)
        {
            const auto lengthSquared =
                groundNormal.X * groundNormal.X + groundNormal.Y * groundNormal.Y + groundNormal.Z * groundNormal.Z;
            if (lengthSquared <= 1.0e-12F)
                throw std::invalid_argument("A grounded Character Controller requires a non-zero ground normal.");
            const auto inverseLength = 1.0F / std::sqrt(lengthSquared);
            normalizedGroundNormal = {groundNormal.X * inverseLength, groundNormal.Y * inverseLength,
                                      groundNormal.Z * inverseLength};
        }

        m_RuntimeState.Generation = generation;
        m_RuntimeState.Grounded = grounded;
        m_RuntimeState.GroundNormal = normalizedGroundNormal;
        m_RuntimeState.Velocity = velocity;
    }

    void CharacterControllerComponent::ResetRuntimeState() noexcept
    {
        m_RuntimeState = {};
        ClearDesiredMovement();
    }

    ComponentRegistration CreateCharacterControllerComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = CharacterControllerComponent::StaticType();
        result.Name = "Character Controller";
        result.Category = "Physics";
        result.SchemaVersion = CharacterControllerSchemaVersion;
        result.Properties = {
            {"radius", "Radius", "Capsule", ComponentPropertyKind::Scalar, false, 0.001, 100'000.0, 0.05},
            {"height", "Height", "Capsule", ComponentPropertyKind::Scalar, false, 0.002, 100'000.0, 0.05},
            {"maximumSlope", "Maximum Slope", "Movement", ComponentPropertyKind::Scalar, false, 0.0, 89.9, 1.0},
            {"stepHeight", "Step Height", "Movement", ComponentPropertyKind::Scalar, false, 0.0, 100'000.0, 0.05},
            {"skinWidth", "Skin Width", "Movement", ComponentPropertyKind::Scalar, false, 0.0001, 100'000.0, 0.005},
            {"mask", "Mask", "Filtering", ComponentPropertyKind::Integer}};
        result.Factory = [] { return Ref<Component>(CreateRef<CharacterControllerComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& controller = dynamic_cast<const CharacterControllerComponent&>(component);
            return ComponentPropertyBag{{"runtimeId", controller.RuntimeId()},
                                        {"radius", static_cast<double>(controller.Radius())},
                                        {"height", static_cast<double>(controller.Height())},
                                        {"maximumSlope", static_cast<double>(controller.MaximumSlopeDegrees())},
                                        {"stepHeight", static_cast<double>(controller.StepHeight())},
                                        {"skinWidth", static_cast<double>(controller.SkinWidth())},
                                        {"layer", static_cast<std::int64_t>(controller.Layer())},
                                        {"mask", static_cast<std::int64_t>(controller.Mask())}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != CharacterControllerSchemaVersion)
                throw std::invalid_argument("Unsupported Character Controller component schema version.");
            auto& controller = dynamic_cast<CharacterControllerComponent&>(component);
            const auto runtimeId = ReadControllerProperty(values, "runtimeId", controller.RuntimeId());
            controller.SetRuntimeId(runtimeId ? runtimeId : controller.RuntimeId());
            controller.ConfigureCapsule(
                ReadControllerScalar(values, "radius", 0.5F), ReadControllerScalar(values, "height", 2.0F),
                ReadControllerScalar(values, "stepHeight", 0.3F), ReadControllerScalar(values, "skinWidth", 0.05F));
            controller.SetMaximumSlopeDegrees(ReadControllerScalar(values, "maximumSlope", 45.0F));
            controller.SetLayer(ReadControllerUnsigned(values, "layer", 1));
            controller.SetMask(ReadControllerUnsigned(values, "mask", ~0U));
        };
        result.Migrate = [](const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Character Controller component schema migration.");
            auto migrated = values;
            if (!ReadControllerProperty(migrated, "runtimeId", AssetId{}))
                migrated.insert_or_assign("runtimeId", AssetId::Generate());
            migrated.emplace("skinWidth", 0.05);
            migrated.emplace("layer", std::int64_t{1});
            migrated.emplace("mask", static_cast<std::int64_t>(~0U));
            return migrated;
        };
        return result;
    }
} // namespace Keire
