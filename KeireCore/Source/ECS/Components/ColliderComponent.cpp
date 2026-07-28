#include "Keire/ECS/Components/ColliderComponent.h"

#include <cmath>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadColliderProperty(const ComponentPropertyBag& values, const std::string_view key,
                                             const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Collider component property has an incompatible type.");
        }

        [[nodiscard]] std::uint32_t ReadUnsigned(const ComponentPropertyBag& values, const std::string_view key,
                                                 const std::uint32_t fallback)
        {
            const auto value = ReadColliderProperty(values, key, static_cast<std::int64_t>(fallback));
            if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
                throw std::invalid_argument("Collider layer value is outside the supported range.");
            return static_cast<std::uint32_t>(value);
        }
    } // namespace

    ColliderComponent::ColliderComponent() : Component(StaticType()) {}

    void ColliderComponent::SetShape(const ColliderShape value)
    {
        m_Shape = value;
        NotifyChanged();
    }

    void ColliderComponent::SetCenter(const Vector3 value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("Collider center must be finite.");
        m_Center = value;
        NotifyChanged();
    }

    void ColliderComponent::SetHalfExtent(const Vector3 value)
    {
        if (!Math::IsFinite(value) || value.X <= 0.0F || value.Y <= 0.0F || value.Z <= 0.0F)
            throw std::invalid_argument("Collider half extent must be finite and positive.");
        m_HalfExtent = value;
        NotifyChanged();
    }

    void ColliderComponent::SetRadius(const float value)
    {
        if (!std::isfinite(value) || value <= 0.0F)
            throw std::invalid_argument("Collider radius must be finite and positive.");
        m_Radius = value;
        NotifyChanged();
    }

    void ColliderComponent::SetHeight(const float value)
    {
        if (!std::isfinite(value) || value <= 0.0F)
            throw std::invalid_argument("Collider height must be finite and positive.");
        m_Height = value;
        NotifyChanged();
    }

    void ColliderComponent::SetLayer(const std::uint32_t value)
    {
        m_Layer = value;
        NotifyChanged();
    }

    void ColliderComponent::SetMask(const std::uint32_t value)
    {
        m_Mask = value;
        NotifyChanged();
    }

    void ColliderComponent::SetTrigger(const bool value)
    {
        m_Trigger = value;
        NotifyChanged();
    }

    ComponentRegistration CreateColliderComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = ColliderComponent::StaticType();
        result.Name = "Collider";
        result.Category = "Physics";
        result.Properties = {
            {"shape", "Shape", "Collider", ComponentPropertyKind::Integer, false, 0.0, 4.0, 1.0},
            {"center", "Center", "Collider", ComponentPropertyKind::Vector3},
            {"halfExtent", "Half Extent", "Collider", ComponentPropertyKind::Vector3},
            {"radius", "Radius", "Collider", ComponentPropertyKind::Scalar, false, 0.001, 100'000.0, 0.05},
            {"height", "Height", "Collider", ComponentPropertyKind::Scalar, false, 0.001, 100'000.0, 0.05},
            {"layer", "Layer", "Filtering", ComponentPropertyKind::Integer},
            {"mask", "Mask", "Filtering", ComponentPropertyKind::Integer},
            {"trigger", "Is Trigger", "Filtering", ComponentPropertyKind::Boolean}};
        result.Factory = [] { return Ref<Component>(CreateRef<ColliderComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& collider = dynamic_cast<const ColliderComponent&>(component);
            return ComponentPropertyBag{{"shape", static_cast<std::int64_t>(collider.m_Shape)},
                                        {"center", collider.m_Center},
                                        {"halfExtent", collider.m_HalfExtent},
                                        {"radius", static_cast<double>(collider.m_Radius)},
                                        {"height", static_cast<double>(collider.m_Height)},
                                        {"layer", static_cast<std::int64_t>(collider.m_Layer)},
                                        {"mask", static_cast<std::int64_t>(collider.m_Mask)},
                                        {"trigger", collider.m_Trigger}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Collider component schema version.");
            auto& collider = dynamic_cast<ColliderComponent&>(component);
            const auto shape = ReadColliderProperty(values, "shape", std::int64_t{0});
            if (shape < 0 || shape > static_cast<std::int64_t>(ColliderShape::TriangleMesh))
                throw std::invalid_argument("Collider shape is outside the supported range.");
            collider.m_Shape = static_cast<ColliderShape>(shape);
            collider.m_Center = ReadColliderProperty(values, "center", Vector3{});
            collider.m_HalfExtent = ReadColliderProperty(values, "halfExtent", Vector3{0.5F, 0.5F, 0.5F});
            collider.m_Radius = static_cast<float>(ReadColliderProperty(values, "radius", 0.5));
            collider.m_Height = static_cast<float>(ReadColliderProperty(values, "height", 1.0));
            collider.m_Layer = ReadUnsigned(values, "layer", 1);
            collider.m_Mask = ReadUnsigned(values, "mask", ~0U);
            collider.m_Trigger = ReadColliderProperty(values, "trigger", false);
            if (!Math::IsFinite(collider.m_Center) || !Math::IsFinite(collider.m_HalfExtent) ||
                collider.m_HalfExtent.X <= 0.0F || collider.m_HalfExtent.Y <= 0.0F || collider.m_HalfExtent.Z <= 0.0F ||
                !std::isfinite(collider.m_Radius) || collider.m_Radius <= 0.0F || !std::isfinite(collider.m_Height) ||
                collider.m_Height <= 0.0F)
            {
                throw std::invalid_argument("Collider component contains invalid geometry.");
            }
        };
        return result;
    }
} // namespace Keire
