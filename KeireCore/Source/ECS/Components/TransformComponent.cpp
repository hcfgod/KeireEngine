#include "Keire/ECS/Components/TransformComponent.h"

#include "KeireInternal/SceneState.h"

#include <cmath>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T Read(const ComponentPropertyBag& values, const std::string_view key, const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Transform component property has an incompatible type.");
        }
    } // namespace

    TransformComponent::TransformComponent() : Component(StaticType()) {}

    bool TransformComponent::IsValidLocalScale(const Vector3 value) noexcept
    {
        return Math::IsFinite(value) && std::abs(value.X) >= MinimumScaleMagnitude &&
               std::abs(value.Y) >= MinimumScaleMagnitude && std::abs(value.Z) >= MinimumScaleMagnitude;
    }

    Vector3 TransformComponent::LocalEulerAngles() const { return Math::QuaternionToEulerDegrees(m_LocalRotation); }

    void TransformComponent::SetLocalPosition(const Vector3 value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Transform position must be finite.");
        m_LocalPosition = value;
        NotifyChanged();
    }

    void TransformComponent::SetLocalRotation(const Quaternion value)
    {
        m_LocalRotation = Math::Normalize(value);
        NotifyChanged();
    }

    void TransformComponent::SetLocalEulerAngles(const Vector3 value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Transform Euler angles must be finite.");
        SetLocalRotation(Math::EulerDegreesToQuaternion(value));
    }

    void TransformComponent::SetLocalScale(const Vector3 value)
    {
        if (!IsValidLocalScale(value))
            throw std::invalid_argument("Transform scale axes must be finite with a magnitude of at least 0.000001.");
        m_LocalScale = value;
        NotifyChanged();
    }

    Matrix4 TransformComponent::LocalMatrix() const
    {
        return Math::ComposeTransform(m_LocalPosition, m_LocalRotation, m_LocalScale);
    }

    Matrix4 TransformComponent::WorldMatrix() const { return IsAttached() ? OwnerWorldMatrix() : LocalMatrix(); }

    Vector3 TransformComponent::WorldPosition() const
    {
        const auto& values = WorldMatrix().Elements;
        return {values[12], values[13], values[14]};
    }

    Entity TransformComponent::Parent() const noexcept
    {
        const auto owner = Owner();
        return owner ? owner.Parent() : Entity{};
    }

    std::vector<Entity> TransformComponent::Children() const
    {
        const auto owner = Owner();
        return owner ? owner.Children() : std::vector<Entity>{};
    }

    void TransformComponent::SetParent(Entity parent, const bool preserveWorldTransform)
    {
        auto owner = Owner();
        if (!owner)
            throw std::logic_error("A detached Transform cannot be reparented.");
        owner.SetParent(std::move(parent), preserveWorldTransform);
    }

    void TransformComponent::Reset()
    {
        m_LocalPosition = {};
        m_LocalRotation = {};
        m_LocalScale = {1.0F, 1.0F, 1.0F};
        NotifyChanged();
    }

    ComponentRegistration CreateTransformComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = TransformComponent::StaticType();
        result.Name = "Transform";
        result.Category = "Core";
        result.Removable = false;
        result.Properties = {{"position", "Position", "Transform", ComponentPropertyKind::Vector3},
                             {"rotation", "Rotation", "Transform", ComponentPropertyKind::Quaternion},
                             {"scale", "Scale", "Transform", ComponentPropertyKind::Vector3}};
        result.Factory = [] { return Ref<Component>(CreateRef<TransformComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& transform = dynamic_cast<const TransformComponent&>(component);
            return ComponentPropertyBag{{"position", transform.m_LocalPosition},
                                        {"rotation", transform.m_LocalRotation},
                                        {"scale", transform.m_LocalScale}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Transform component schema version.");
            auto& transform = dynamic_cast<TransformComponent&>(component);
            const auto position = Read(values, "position", Vector3{});
            const auto rotation = Math::Normalize(Read(values, "rotation", Quaternion{}));
            const auto scale = Read(values, "scale", Vector3{1.0F, 1.0F, 1.0F});
            if (!Math::IsFinite(position) || !TransformComponent::IsValidLocalScale(scale))
                throw std::invalid_argument("Transform component contains invalid position or scale values.");
            transform.m_LocalPosition = position;
            transform.m_LocalRotation = rotation;
            transform.m_LocalScale = scale;
        };
        return result;
    }
} // namespace Keire
