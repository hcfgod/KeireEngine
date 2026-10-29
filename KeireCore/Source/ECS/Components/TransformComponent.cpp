#include "Keire/ECS/Components/TransformComponent.h"

#include "KeireInternal/SceneState.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

        [[nodiscard]] Quaternion RestoreSerializedRotation(const Quaternion value)
        {
            const double squaredLength =
                static_cast<double>(value.X) * value.X + static_cast<double>(value.Y) * value.Y +
                static_cast<double>(value.Z) * value.Z + static_cast<double>(value.W) * value.W;
            constexpr double unitRoundoffTolerance = 4.0 * std::numeric_limits<float>::epsilon();
            // Re-normalizing stored unit floats can oscillate between adjacent values and dirty an Undo savepoint.
            if (std::isfinite(squaredLength) && std::abs(squaredLength - 1.0) <= unitRoundoffTolerance)
            {
                return value;
            }
            return Math::Normalize(value);
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

        [[nodiscard]] Vector3 LegacyPosition(const Vector3d value)
        {
            return Math::RelativeFloat(value, {}, std::numeric_limits<double>::max());
        }

        // Apply only the linear part; absolute translations must never pass through a float matrix.
        [[nodiscard]] Vector3d TransformOffset(const Matrix4& matrix, const Vector3d offset)
        {
            const auto& values = matrix.Elements;
            return {values[0] * offset.X + values[4] * offset.Y + values[8] * offset.Z,
                    values[1] * offset.X + values[5] * offset.Y + values[9] * offset.Z,
                    values[2] * offset.X + values[6] * offset.Y + values[10] * offset.Z};
        }
    } // namespace

    TransformComponent::TransformComponent() : Component(StaticType()) {}

    bool TransformComponent::IsValidLocalScale(const Vector3 value) noexcept
    {
        return Math::IsFinite(value) && std::abs(value.X) >= MinimumScaleMagnitude &&
               std::abs(value.Y) >= MinimumScaleMagnitude && std::abs(value.Z) >= MinimumScaleMagnitude;
    }

    Vector3 TransformComponent::LocalEulerAngles() const { return Math::QuaternionToEulerDegrees(m_LocalRotation); }

    Vector3 TransformComponent::LocalPosition() const { return LegacyPosition(m_LocalPosition); }

    void TransformComponent::SetLocalPosition(const Vector3 value) { SetLocalPositionPrecise(Math::Widen(value)); }

    void TransformComponent::SetLocalPositionPrecise(const Vector3d value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Transform position must be finite.");
        if (m_LocalPosition == value)
            return;
        m_LocalPosition = value;
        NotifyChanged();
    }

    void TransformComponent::SetLocalRotation(const Quaternion value)
    {
        if (m_LocalRotation == value)
            return;
        const auto normalized = Math::Normalize(value);
        if (m_LocalRotation == normalized)
            return;
        m_LocalRotation = normalized;
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
        if (m_LocalScale == value)
            return;
        m_LocalScale = value;
        NotifyChanged();
    }

    Matrix4 TransformComponent::LocalMatrix() const
    {
        return Math::ComposeTransform(LocalPosition(), m_LocalRotation, m_LocalScale);
    }

    Matrix4 TransformComponent::WorldMatrix() const { return IsAttached() ? OwnerWorldMatrix() : LocalMatrix(); }

    Matrix4 TransformComponent::WorldRelativeMatrix(const Vector3d origin, const double maximumError) const
    {
        auto result = Math::ComposeTransform({}, m_LocalRotation, m_LocalScale);
        auto parent = Parent();
        while (parent)
        {
            const auto transform = parent.GetComponent<TransformComponent>();
            if (!transform)
            {
                throw std::logic_error("Transform parent has no transform component.");
            }
            result =
                Math::Multiply(Math::ComposeTransform({}, transform->LocalRotation(), transform->LocalScale()), result);
            parent = transform->Parent();
        }
        if (!Math::IsFinite(result))
        {
            throw std::out_of_range("Transform hierarchy exceeds its linear coordinate range.");
        }
        const auto relative = Math::RelativeFloat(WorldPositionPrecise(), origin, maximumError);
        result.Elements[12] = relative.X;
        result.Elements[13] = relative.Y;
        result.Elements[14] = relative.Z;
        return result;
    }

    TransformComponent::PrecisePresentationTransform TransformComponent::BuildPresentationTransform() const
    {
        if (m_HasPresentationWorldMatrix)
        {
            auto linear = m_PresentationWorldMatrix;
            const auto position = m_PresentationWorldPosition;
            if (m_PresentationPositionOnly)
            {
                linear = Math::ComposeTransform({}, m_LocalRotation, m_LocalScale);
                auto ancestor = Parent();
                while (ancestor)
                {
                    const auto transform = ancestor.GetComponent<TransformComponent>();
                    if (!transform)
                    {
                        throw std::logic_error("Transform parent has no transform component.");
                    }
                    linear = Math::Multiply(
                        Math::ComposeTransform({}, transform->LocalRotation(), transform->LocalScale()), linear);
                    ancestor = transform->Parent();
                }
            }
            linear.Elements[12] = 0.0F;
            linear.Elements[13] = 0.0F;
            linear.Elements[14] = 0.0F;
            return {linear, position};
        }

        const auto parent = Parent();
        const bool interpolated = m_HasFixedPresentation && m_FixedParent == parent.Id();
        PrecisePresentationTransform result{interpolated ? m_FixedPresentationLocal
                                                         : Math::ComposeTransform({}, m_LocalRotation, m_LocalScale),
                                            interpolated ? m_FixedPresentationPosition : m_LocalPosition};
        if (parent)
        {
            const auto transform = parent.GetComponent<TransformComponent>();
            if (!transform)
            {
                throw std::logic_error("Transform parent has no transform component.");
            }
            const auto ancestor = transform->BuildPresentationTransform();
            const auto offset = TransformOffset(ancestor.Linear, result.Position);
            result.Position = {ancestor.Position.X + offset.X, ancestor.Position.Y + offset.Y,
                               ancestor.Position.Z + offset.Z};
            result.Linear = Math::Multiply(ancestor.Linear, result.Linear);
        }
        if (!Math::IsFinite(result.Position) || !Math::IsFinite(result.Linear))
        {
            throw std::out_of_range("Presentation hierarchy exceeds its coordinate range.");
        }
        return result;
    }

    Matrix4 TransformComponent::PresentationWorldMatrix() const
    {
        return PresentationRelativeMatrix({}, std::numeric_limits<double>::max());
    }

    Vector3d TransformComponent::PresentationWorldPositionPrecise() const
    {
        return BuildPresentationTransform().Position;
    }

    Matrix4 TransformComponent::PresentationRelativeMatrix(const Vector3d origin, const double maximumError) const
    {
        auto result = BuildPresentationTransform();
        const auto relative = Math::RelativeFloat(result.Position, origin, maximumError);
        result.Linear.Elements[12] = relative.X;
        result.Linear.Elements[13] = relative.Y;
        result.Linear.Elements[14] = relative.Z;
        return result.Linear;
    }

    Vector3 TransformComponent::WorldPosition() const { return LegacyPosition(WorldPositionPrecise()); }

    Vector3d TransformComponent::WorldPositionPrecise() const
    {
        auto position = m_LocalPosition;
        auto parent = Parent();
        while (parent)
        {
            const auto transform = parent.GetComponent<TransformComponent>();
            if (!transform)
            {
                throw std::logic_error("Transform parent has no transform component.");
            }
            const auto linear = Math::ComposeTransform({}, transform->LocalRotation(), transform->LocalScale());
            const auto offset = TransformOffset(linear, position);
            const auto translation = transform->LocalPositionPrecise();
            position = {translation.X + offset.X, translation.Y + offset.Y, translation.Z + offset.Z};
            if (!Math::IsFinite(position))
            {
                throw std::out_of_range("Transform hierarchy exceeds the precise position range.");
            }
            parent = transform->Parent();
        }
        return position;
    }

    Vector3d TransformComponent::InverseTransformPointPrecise(const Vector3d value) const
    {
        if (!Math::IsFinite(value))
        {
            throw std::invalid_argument("Transform world position must be finite.");
        }
        std::vector<Ref<TransformComponent>> ancestors;
        auto parent = Parent();
        while (parent)
        {
            const auto transform = parent.GetComponent<TransformComponent>();
            if (!transform)
            {
                throw std::logic_error("Transform parent has no transform component.");
            }
            ancestors.push_back(transform);
            parent = transform->Parent();
        }

        auto position = value;
        for (auto ancestor = ancestors.rbegin(); ancestor != ancestors.rend(); ++ancestor)
        {
            const auto translation = (*ancestor)->LocalPositionPrecise();
            const Vector3d offset{position.X - translation.X, position.Y - translation.Y, position.Z - translation.Z};
            const auto linear = Math::ComposeTransform({}, (*ancestor)->LocalRotation(), (*ancestor)->LocalScale());
            position = TransformOffset(Math::Inverse(linear), offset);
        }
        const Vector3d offset{position.X - m_LocalPosition.X, position.Y - m_LocalPosition.Y,
                              position.Z - m_LocalPosition.Z};
        const auto linear = Math::ComposeTransform({}, m_LocalRotation, m_LocalScale);
        const auto local = TransformOffset(Math::Inverse(linear), offset);
        if (!Math::IsFinite(local))
        {
            throw std::out_of_range("Transform hierarchy exceeds the precise position range.");
        }
        return local;
    }

    void TransformComponent::SetWorldPositionPrecise(const Vector3d value)
    {
        const auto parent = Parent();
        const auto transform = parent ? parent.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        SetLocalPositionPrecise(transform ? transform->InverseTransformPointPrecise(value) : value);
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
        return Math::DecomposeTransform(BuildPresentationTransform().Linear, position, rotation, scale)
                   ? rotation
                   : WorldRotation();
    }

    void TransformComponent::SetWorldPosition(const Vector3 value) { SetWorldPositionPrecise(Math::Widen(value)); }

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
        SetRuntimePresentationWorldMatrix(value, {value.Elements[12], value.Elements[13], value.Elements[14]});
    }

    void TransformComponent::SetRuntimePresentationWorldMatrix(const Matrix4 value, const Vector3d position)
    {
        if (!Math::IsFinite(value) || !Math::IsFinite(position))
        {
            throw std::invalid_argument("Transform presentation matrix and position must be finite.");
        }
        m_PresentationWorldMatrix = value;
        m_PresentationWorldMatrix.Elements[12] = 0.0F;
        m_PresentationWorldMatrix.Elements[13] = 0.0F;
        m_PresentationWorldMatrix.Elements[14] = 0.0F;
        m_PresentationWorldPosition = position;
        m_HasPresentationWorldMatrix = true;
        m_PresentationPositionOnly = false;
    }

    void TransformComponent::SetRuntimePresentationWorldPosition(const Vector3 value)
    {
        SetRuntimePresentationWorldPositionPrecise(Math::Widen(value));
    }

    void TransformComponent::SetRuntimePresentationWorldPositionPrecise(const Vector3d value)
    {
        if (!Math::IsFinite(value))
        {
            throw std::invalid_argument("Transform presentation position must be finite.");
        }
        m_PresentationWorldPosition = value;
        m_HasPresentationWorldMatrix = true;
        m_PresentationPositionOnly = true;
    }

    void TransformComponent::SetFixedPresentationInterpolation(const bool enabled)
    {
        const auto owner = Owner();
        if (const auto state = owner.m_State.Lock())
            state->SetFixedPresentationInterpolation(owner.Id(), enabled);
        if (m_FixedPresentationInterpolation == enabled)
            return;
        m_FixedPresentationInterpolation = enabled;
        ClearRuntimeFixedPresentation();
    }

    void TransformComponent::ClearRuntimeFixedPresentation() noexcept
    {
        m_HasFixedSamples = false;
        m_HasFixedPresentation = false;
    }

    void TransformComponent::BeginRuntimeFixedPresentationSample()
    {
        const auto parent = Parent().Id();
        if (!m_HasFixedSamples || m_FixedParent != parent)
        {
            m_CurrentFixedPosition = m_LocalPosition;
            m_CurrentFixedRotation = m_LocalRotation;
            m_FixedParent = parent;
            m_HasFixedSamples = true;
        }
        m_PreviousFixedPosition = m_CurrentFixedPosition;
        m_PreviousFixedRotation = m_CurrentFixedRotation;
    }

    void TransformComponent::EndRuntimeFixedPresentationSample()
    {
        if (!m_HasFixedSamples || m_FixedParent != Parent().Id())
            BeginRuntimeFixedPresentationSample();
        m_CurrentFixedPosition = m_LocalPosition;
        m_CurrentFixedRotation = m_LocalRotation;
    }

    void TransformComponent::ApplyRuntimeFixedPresentation(const float alpha)
    {
        if (!std::isfinite(alpha) || alpha < 0.0F || alpha > 1.0F)
            throw std::invalid_argument("Fixed presentation alpha must be finite and in the range 0..1.");
        if (!m_FixedPresentationInterpolation || !m_HasFixedSamples || m_FixedParent != Parent().Id())
        {
            m_HasFixedPresentation = false;
            return;
        }
        const Vector3d position{
            std::lerp(m_PreviousFixedPosition.X, m_CurrentFixedPosition.X, static_cast<double>(alpha)),
            std::lerp(m_PreviousFixedPosition.Y, m_CurrentFixedPosition.Y, static_cast<double>(alpha)),
            std::lerp(m_PreviousFixedPosition.Z, m_CurrentFixedPosition.Z, static_cast<double>(alpha))};
        auto current = m_CurrentFixedRotation;
        const auto previous = m_PreviousFixedRotation;
        if (previous.X * current.X + previous.Y * current.Y + previous.Z * current.Z + previous.W * current.W < 0.0F)
            current = {-current.X, -current.Y, -current.Z, -current.W};
        const auto rotation = Math::Normalize(
            {previous.X + (current.X - previous.X) * alpha, previous.Y + (current.Y - previous.Y) * alpha,
             previous.Z + (current.Z - previous.Z) * alpha, previous.W + (current.W - previous.W) * alpha});
        // Keep current scale: interpolating signed scales can create singular transforms.
        m_FixedPresentationLocal = Math::ComposeTransform({}, rotation, m_LocalScale);
        m_FixedPresentationPosition = position;
        m_HasFixedPresentation = true;
    }

    void TransformComponent::ResetPresentationInterpolation() noexcept
    {
        m_HasPresentationWorldMatrix = false;
        ClearRuntimeFixedPresentation();
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
        result.SchemaVersion = 2;
        result.Properties = {{"position", "Position", "Transform", ComponentPropertyKind::Vector3d},
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
        result.Migrate = [](ComponentPropertyBag values, const std::uint32_t version)
        {
            if (version != 1 && version != 2)
            {
                throw std::invalid_argument("Unsupported Transform component schema version.");
            }
            const auto position = values.find("position");
            if (position != values.end())
            {
                if (const auto* legacy = std::get_if<Vector3>(&position->second))
                {
                    position->second = Math::Widen(*legacy);
                }
            }
            return values;
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1 && version != 2)
                throw std::invalid_argument("Unsupported Transform component schema version.");
            auto& transform = dynamic_cast<TransformComponent&>(component);
            Vector3d position;
            const auto storedPosition = values.find("position");
            if (storedPosition != values.end())
            {
                if (const auto* precise = std::get_if<Vector3d>(&storedPosition->second))
                {
                    position = *precise;
                }
                else if (const auto* legacy = std::get_if<Vector3>(&storedPosition->second))
                {
                    position = Math::Widen(*legacy);
                }
                else
                {
                    throw std::invalid_argument("Transform position has an incompatible type.");
                }
            }
            const auto rotation = RestoreSerializedRotation(Read(values, "rotation", Quaternion{}));
            const auto scale = Read(values, "scale", Vector3{1.0F, 1.0F, 1.0F});
            if (!Math::IsFinite(position) || !TransformComponent::IsValidLocalScale(scale))
                throw std::invalid_argument("Transform component contains invalid position or scale values.");
            transform.m_LocalPosition = position;
            transform.m_LocalRotation = rotation;
            transform.m_LocalScale = scale;
            transform.m_HasPresentationWorldMatrix = false;
            transform.ClearRuntimeFixedPresentation();
        };
        return result;
    }
} // namespace Keire
