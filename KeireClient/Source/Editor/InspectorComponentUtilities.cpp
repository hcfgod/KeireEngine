#include "KeireClient/Editor/InspectorComponentUtilities.h"

#include <sstream>

namespace KeireEditor
{
    std::string EncodeComponentOrderPayload(const ComponentOrderPayload& value)
    {
        return value.Entity.ToString() + '\n' + value.Type.ToString() + '\n' + std::to_string(value.Ordinal);
    }

    std::optional<ComponentOrderPayload> DecodeComponentOrderPayload(const std::span<const std::byte> bytes) noexcept
    {
        try
        {
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            std::istringstream stream(text);
            std::string entity;
            std::string type;
            std::string ordinal;
            if (!std::getline(stream, entity) || !std::getline(stream, type) || !std::getline(stream, ordinal))
                return std::nullopt;
            std::size_t consumed = 0;
            const auto parsedOrdinal = std::stoull(ordinal, &consumed);
            if (consumed != ordinal.size())
                return std::nullopt;
            return ComponentOrderPayload{Keire::EntityId(Keire::AssetId::Parse(entity)),
                                         Keire::ComponentTypeId(Keire::AssetId::Parse(type)), parsedOrdinal};
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::size_t ComponentOrdinal(const std::span<const Keire::Ref<Keire::Component>> components,
                                 const Keire::Ref<Keire::Component>& component) noexcept
    {
        std::size_t ordinal = 0;
        for (const auto& candidate : components)
        {
            if (candidate == component)
                return ordinal;
            if (candidate && component && candidate->Type() == component->Type())
                ++ordinal;
        }
        return ordinal;
    }

    Keire::Ref<Keire::Component>
    ResolveComponentOrderPayload(const std::span<const Keire::Ref<Keire::Component>> components,
                                 const Keire::EntityId expectedEntity, const ComponentOrderPayload& payload) noexcept
    {
        if (payload.Entity != expectedEntity)
            return {};
        std::size_t ordinal = 0;
        for (const auto& candidate : components)
        {
            if (!candidate || candidate->Type() != payload.Type)
                continue;
            if (ordinal++ == payload.Ordinal)
                return candidate;
        }
        return {};
    }

    bool ColliderPropertyVisible(const Keire::ColliderShape shape, const std::string_view property) noexcept
    {
        return (property != "halfExtent" || shape == Keire::ColliderShape::Box) &&
               (property != "radius" || shape == Keire::ColliderShape::Sphere ||
                shape == Keire::ColliderShape::Capsule) &&
               (property != "height" || shape == Keire::ColliderShape::Capsule) &&
               (property != "collisionMesh" || shape == Keire::ColliderShape::ConvexMesh ||
                shape == Keire::ColliderShape::TriangleMesh);
    }
} // namespace KeireEditor
