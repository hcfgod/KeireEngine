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

        [[nodiscard]] Quaternion MultiplyRotation(const Quaternion left, const Quaternion right)
        {
            return Math::Normalize({left.W * right.X + left.X * right.W + left.Y * right.Z - left.Z * right.Y,
                                    left.W * right.Y - left.X * right.Z + left.Y * right.W + left.Z * right.X,
                                    left.W * right.Z + left.X * right.Y - left.Y * right.X + left.Z * right.W,
                                    left.W * right.W - left.X * right.X - left.Y * right.Y - left.Z * right.Z});
        }

        [[nodiscard]] Quaternion InverseRotation(const Quaternion value)
        {
            const auto rotation = Math::Normalize(value);
            return {-rotation.X, -rotation.Y, -rotation.Z, rotation.W};
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

    Matrix4 TransformComponent::PresentationWorldMatrix() const
    {
        if (m_HasPresentationWorldMatrix)
            return m_PresentationWorldMatrix;
        const auto parent = Parent();
        const auto parentTransform = parent ? parent.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        return parentTransform ? Math::Multiply(parentTransform->PresentationWorldMatrix(), LocalMatrix())
                               : LocalMatrix();
    }

    Vector3 TransformComponent::WorldPosition() const
    {
        const auto& values = WorldMatrix().Elements;
        return {values[12], values[13], values[14]};
    }

    Quaternion TransformComponent::WorldRotation() const
    {
        const auto parent = Parent();
        const auto parentTransform = parent ? parent.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        return parentTransform ? MultiplyRotation(parentTransform->WorldRotation(), m_LocalRotation) : m_LocalRotation;
    }

    Vector3 TransformComponent::PresentationWorldPosition() const
    {
        const auto& values = PresentationWorldMatrix().Elements;
        return {values[12], values[13], values[14]};
    }

    Quaternion TransformComponent::PresentationWorldRotation() const
    {
        Vector3 position;
        Vector3 scale;
        Quaternion rotation;
        return Math::DecomposeTransform(PresentationWorldMatrix(), position, rotation, scale) ? rotation
                                                                                              : WorldRotation();
    }

    void TransformComponent::SetWorldPosition(const Vector3 value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Transform world position must be finite.");
        const auto parent = Parent();
        const auto parentTransform = parent ? parent.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        SetLocalPosition(parentTransform ? Math::TransformPoint(Math::Inverse(parentTransform->WorldMatrix()), value)
                                         : value);
    }

    void TransformComponent::SetWorldRotation(const Quaternion value)
    {
        const auto rotation = Math::Normalize(value);
        const auto parent = Parent();
        const auto parentTransform = parent ? parent.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        if (!parentTransform)
        {
            SetLocalRotation(rotation);
            return;
        }
        SetLocalRotation(MultiplyRotation(InverseRotation(parentTransform->WorldRotation()), rotation));
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

    void TransformComponent::SetRuntimePresentationWorldMatrix(const Matrix4 value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Transform presentation matrix must be finite.");
        m_PresentationWorldMatrix = value;
        m_HasPresentationWorldMatrix = true;
    }

    void TransformComponent::ResetPresentationInterpolation() noexcept
    {
        m_HasPresentationWorldMatrix = false;
        ++m_PresentationResetRevision;
        for (const auto& child : Children())
        {
            if (const auto transform = child.GetComponent<TransformComponent>())
                transform->ResetPresentationInterpolation();
        }
    }

    void TransformComponent::Reset()
    {
        m_LocalPosition = {};
        m_LocalRotation = {};
        m_LocalScale = {1.0F, 1.0F, 1.0F};
        ResetPresentationInterpolation();
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
            transform.m_HasPresentationWorldMatrix = false;
        };
        return result;
    }
} // namespace Keire
