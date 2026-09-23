#include "KeireClient/Editor/InspectorComponentUtilities.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace KeireEditor
{
    [[nodiscard]] std::string JoinEntityTags(const std::span<const std::string> tags)
    {
        std::string result;
        for (const auto& tag : tags)
        {
            if (!result.empty())
                result += ", ";
            result += tag;
        }
        return result;
    }

    [[nodiscard]] std::optional<std::vector<std::string>> ParseEntityTags(const std::string_view text)
    {
        std::vector<std::string> result;
        std::unordered_set<std::string> unique;
        std::size_t begin = 0;
        while (begin < text.size())
        {
            const auto separator = text.find(',', begin);
            auto value =
                text.substr(begin, separator == std::string_view::npos ? text.size() - begin : separator - begin);
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return std::nullopt;
            value.remove_prefix(first);
            const auto last = value.find_last_not_of(" \t\r\n");
            value = value.substr(0, last + 1);
            if (!Keire::SceneAsset::IsValidEntityTag(value) || !unique.emplace(value).second ||
                result.size() >= Keire::MaximumEntityTagCount)
                return std::nullopt;
            result.emplace_back(value);
            if (separator == std::string_view::npos)
                break;
            begin = separator + 1;
            if (begin == text.size())
                return std::nullopt;
        }
        return result;
    }

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

    bool HaveUniformComponentValues(const Keire::Ref<Keire::Scene>& scene,
                                    const std::span<const Keire::AssetId> entities,
                                    const Keire::ComponentRegistration& registration,
                                    const Keire::Ref<Keire::Component>& reference, const std::size_t ordinal) noexcept
    {
        if (!scene || !reference)
            return false;
        try
        {
            const auto expected = registration.Serialize(*reference);
            const auto expectedEnabled = reference->Enabled();
            for (const auto id : entities)
            {
                const auto entity = scene->FindEntity(Keire::EntityId(id));
                if (!entity)
                    return false;
                std::size_t currentOrdinal = 0;
                Keire::Ref<Keire::Component> candidate;
                for (const auto& component : entity.GetComponents())
                {
                    if (!component || component->Type() != registration.Type)
                        continue;
                    if (currentOrdinal++ == ordinal)
                    {
                        candidate = component;
                        break;
                    }
                }
                if (!candidate || candidate->Enabled() != expectedEnabled ||
                    registration.Serialize(*candidate) != expected)
                    return false;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool HaveCommonMeshMaterialLayout(const Keire::Ref<Keire::Scene>& scene,
                                      const std::span<const Keire::AssetId> entities,
                                      const Keire::Ref<Keire::MeshRendererComponent>& reference) noexcept
    {
        if (!scene || !reference)
            return false;
        return std::ranges::all_of(entities,
                                   [&](const Keire::AssetId id)
                                   {
                                       const auto entity = scene->FindEntity(Keire::EntityId(id));
                                       const auto renderer =
                                           entity ? entity.GetComponent<Keire::MeshRendererComponent>() : nullptr;
                                       return renderer && renderer->Mesh() == reference->Mesh();
                                   });
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

    bool IsRetiredSceneUiComponent(const Keire::ComponentTypeId type) noexcept
    {
        return Keire::ComponentRegistry::IsReservedType(type);
    }
} // namespace KeireEditor
