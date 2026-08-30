#include "KeireInternal/Scripting/ManagedBuiltinComponents.h"

#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/JointComponents.h"
#include "Keire/ECS/Components/LightProbeVolumeComponent.h"
#include "Keire/ECS/Components/ReflectionProbeComponent.h"
#include "Keire/ECS/Components/UiDocumentComponent.h"

#include <Coral/String.hpp>
#include <algorithm>
#include <limits>
#include <optional>
#include <variant>

namespace Keire::Detail
{
    namespace
    {
        thread_local ManagedRuntimeEntityResolver CurrentResolver = nullptr;

        [[nodiscard]] std::optional<ComponentRegistration> Registration(const ComponentTypeId type)
        {
            if (type == ColliderComponent::StaticType())
                return CreateColliderComponentRegistration();
            if (type == ReflectionProbeComponent::StaticType())
                return CreateReflectionProbeComponentRegistration();
            if (type == LightProbeVolumeComponent::StaticType())
                return CreateLightProbeVolumeComponentRegistration();
            if (type == FixedJointComponent::StaticType())
                return CreateFixedJointComponentRegistration();
            if (type == HingeJointComponent::StaticType())
                return CreateHingeJointComponentRegistration();
            if (type == DistanceJointComponent::StaticType())
                return CreateDistanceJointComponentRegistration();
            if (type == SpringJointComponent::StaticType())
                return CreateSpringJointComponentRegistration();
            if (type == UiDocumentComponent::StaticType())
                return CreateUiDocumentComponentRegistration();
            return std::nullopt;
        }

        [[nodiscard]] bool Write(const ComponentPropertyValue& value, NativeBuiltinProperty& destination)
        {
            if (const auto* boolean = std::get_if<bool>(&value))
                destination.Kind = NativeBuiltinPropertyKind::Boolean, destination.Integer = *boolean ? 1 : 0;
            else if (const auto* integer = std::get_if<std::int64_t>(&value))
                destination.Kind = NativeBuiltinPropertyKind::Integer, destination.Integer = *integer;
            else if (const auto* scalar = std::get_if<double>(&value))
                destination.Kind = NativeBuiltinPropertyKind::Scalar, destination.Scalar = *scalar;
            else if (const auto* vector2 = std::get_if<Vector2>(&value))
                destination.Kind = NativeBuiltinPropertyKind::Vector2,
                destination.Vector = Vector4{vector2->X, vector2->Y, 0.0F, 0.0F};
            else if (const auto* vector3 = std::get_if<Vector3>(&value))
                destination.Kind = NativeBuiltinPropertyKind::Vector3,
                destination.Vector = Vector4{vector3->X, vector3->Y, vector3->Z, 0.0F};
            else if (const auto* vector4 = std::get_if<Vector4>(&value))
                destination.Kind = NativeBuiltinPropertyKind::Vector4, destination.Vector = *vector4;
            else if (const auto* color = std::get_if<Color>(&value))
                destination.Kind = NativeBuiltinPropertyKind::Color,
                destination.Vector = Vector4{color->Red, color->Green, color->Blue, color->Alpha};
            else if (const auto* asset = std::get_if<AssetId>(&value))
            {
                destination.Kind = NativeBuiltinPropertyKind::Asset;
                destination.High = asset->High();
                destination.Low = asset->Low();
            }
            else if (const auto* entity = std::get_if<EntityId>(&value))
            {
                destination.Kind = NativeBuiltinPropertyKind::Entity;
                destination.High = entity->Value().High();
                destination.Low = entity->Value().Low();
            }
            else
                return false;
            return true;
        }

        [[nodiscard]] std::optional<ComponentPropertyValue> Read(const NativeBuiltinProperty& value)
        {
            switch (value.Kind)
            {
            case NativeBuiltinPropertyKind::Boolean:
                return value.Integer != 0;
            case NativeBuiltinPropertyKind::Integer:
                return value.Integer;
            case NativeBuiltinPropertyKind::Scalar:
                return value.Scalar;
            case NativeBuiltinPropertyKind::Vector2:
                return Vector2{value.Vector.X, value.Vector.Y};
            case NativeBuiltinPropertyKind::Vector3:
                return Vector3{value.Vector.X, value.Vector.Y, value.Vector.Z};
            case NativeBuiltinPropertyKind::Vector4:
                return value.Vector;
            case NativeBuiltinPropertyKind::Color:
                return Color{value.Vector.X, value.Vector.Y, value.Vector.Z, value.Vector.W};
            case NativeBuiltinPropertyKind::Asset:
                return AssetId(value.High, value.Low);
            case NativeBuiltinPropertyKind::Entity:
                return EntityId(AssetId(value.High, value.Low));
            }
            return std::nullopt;
        }
    } // namespace

    ManagedBuiltinComponentResolverScope::ManagedBuiltinComponentResolverScope(
        const ManagedRuntimeEntityResolver resolver) noexcept
        : m_Previous(CurrentResolver)
    {
        CurrentResolver = resolver;
    }

    ManagedBuiltinComponentResolverScope::~ManagedBuiltinComponentResolverScope() { CurrentResolver = m_Previous; }

    bool GetManagedBuiltinComponentProperty(const Entity& entity, const ComponentTypeId type,
                                            const std::string_view key, NativeBuiltinProperty& destination) noexcept
    {
        try
        {
            const auto registration = Registration(type);
            const auto component = entity ? entity.GetComponent(type) : Ref<Component>{};
            if (!registration || !component)
                return false;
            const auto values = registration->Serialize(*component);
            const auto found = values.find(key);
            return found != values.end() && Write(found->second, destination);
        }
        catch (...)
        {
            return false;
        }
    }

    bool SetManagedBuiltinComponentProperty(const Entity& entity, const ComponentTypeId type,
                                            const std::string_view key, const NativeBuiltinProperty& source) noexcept
    {
        try
        {
            const auto registration = Registration(type);
            const auto component = entity ? entity.GetComponent(type) : Ref<Component>{};
            const auto value = Read(source);
            if (!registration || !component || !value)
                return false;
            auto values = registration->Serialize(*component);
            const auto found = values.find(key);
            if (found == values.end() || found->second.index() != value->index())
                return false;
            found->second = *value;
            registration->Deserialize(*component, values, registration->SchemaVersion);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::uint8_t GetManagedBuiltinComponentPropertyIcall(const std::uint64_t world, const std::uint64_t entityHigh,
                                                         const std::uint64_t entityLow, const std::uint64_t typeHigh,
                                                         const std::uint64_t typeLow, const Coral::String key,
                                                         NativeBuiltinProperty* destination) noexcept
    {
        try
        {
            const auto entity = CurrentResolver ? CurrentResolver(world, entityHigh, entityLow) : Entity{};
            return destination &&
                           GetManagedBuiltinComponentProperty(entity, ComponentTypeId(AssetId(typeHigh, typeLow)),
                                                              static_cast<std::string>(key), *destination)
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    std::uint8_t SetManagedBuiltinComponentPropertyIcall(const std::uint64_t world, const std::uint64_t entityHigh,
                                                         const std::uint64_t entityLow, const std::uint64_t typeHigh,
                                                         const std::uint64_t typeLow, const Coral::String key,
                                                         const NativeBuiltinProperty* source) noexcept
    {
        try
        {
            const auto entity = CurrentResolver ? CurrentResolver(world, entityHigh, entityLow) : Entity{};
            return source && SetManagedBuiltinComponentProperty(entity, ComponentTypeId(AssetId(typeHigh, typeLow)),
                                                                static_cast<std::string>(key), *source)
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    std::int32_t GetManagedBuiltinComponentTextIcall(const std::uint64_t world, const std::uint64_t entityHigh,
                                                     const std::uint64_t entityLow, const std::uint64_t typeHigh,
                                                     const std::uint64_t typeLow, const Coral::String key,
                                                     char* destination, const std::int32_t capacity) noexcept
    {
        try
        {
            const auto entity = CurrentResolver ? CurrentResolver(world, entityHigh, entityLow) : Entity{};
            const auto registration = Registration(ComponentTypeId(AssetId(typeHigh, typeLow)));
            const auto component =
                entity ? entity.GetComponent(ComponentTypeId(AssetId(typeHigh, typeLow))) : Ref<Component>{};
            if (!registration || !component)
                return 0;
            const auto values = registration->Serialize(*component);
            const auto found = values.find(static_cast<std::string>(key));
            const auto* text = found == values.end() ? nullptr : std::get_if<std::string>(&found->second);
            if (!text)
                return 0;
            if (destination && capacity > 0)
                std::copy_n(text->begin(), std::min(text->size(), static_cast<std::size_t>(capacity)), destination);
            return static_cast<std::int32_t>(
                std::min<std::size_t>(text->size(), std::numeric_limits<std::int32_t>::max()));
        }
        catch (...)
        {
            return 0;
        }
    }

    std::uint8_t SetManagedBuiltinComponentTextIcall(const std::uint64_t world, const std::uint64_t entityHigh,
                                                     const std::uint64_t entityLow, const std::uint64_t typeHigh,
                                                     const std::uint64_t typeLow, const Coral::String key,
                                                     const Coral::String value) noexcept
    {
        try
        {
            const auto entity = CurrentResolver ? CurrentResolver(world, entityHigh, entityLow) : Entity{};
            const auto type = ComponentTypeId(AssetId(typeHigh, typeLow));
            const auto registration = Registration(type);
            const auto component = entity ? entity.GetComponent(type) : Ref<Component>{};
            if (!registration || !component)
                return 0;
            auto values = registration->Serialize(*component);
            const auto found = values.find(static_cast<std::string>(key));
            if (found == values.end() || !std::holds_alternative<std::string>(found->second))
                return 0;
            found->second = static_cast<std::string>(value);
            registration->Deserialize(*component, values, registration->SchemaVersion);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }
} // namespace Keire::Detail
