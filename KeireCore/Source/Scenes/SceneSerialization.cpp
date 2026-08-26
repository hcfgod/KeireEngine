#include "KeireInternal/Scenes/SceneSerialization.h"

#include "Keire/Animation/ProceduralMotion.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] Json EncodeProperty(const ComponentPropertyValue& value)
        {
            return std::visit(
                [](const auto& current) -> Json
                {
                    using T = std::remove_cvref_t<decltype(current)>;
                    if constexpr (std::same_as<T, Vector2>)
                        return Json::array({current.X, current.Y});
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json::array({current.X, current.Y, current.Z});
                    else if constexpr (std::same_as<T, Vector4> || std::same_as<T, Quaternion>)
                        return Json::array({current.X, current.Y, current.Z, current.W});
                    else if constexpr (std::same_as<T, Color>)
                        return Json::array({current.Red, current.Green, current.Blue, current.Alpha});
                    else if constexpr (std::same_as<T, AssetId> || std::same_as<T, EntityId>)
                        return current ? Json(current.ToString()) : Json(nullptr);
                    else if constexpr (std::same_as<T, ComponentReferenceValue>)
                        return Json{
                            {"entity", current.Entity ? Json(current.Entity.ToString()) : Json(nullptr)},
                            {"component", current.Component ? Json(current.Component.ToString()) : Json(nullptr)}};
                    else if constexpr (std::same_as<T, ComponentEventValue>)
                    {
                        Json listeners = Json::array();
                        for (const auto& listener : current.Listeners)
                        {
                            listeners.push_back(
                                {{"enabled", listener.Enabled},
                                 {"target", listener.Target ? Json(listener.Target.ToString()) : Json(nullptr)},
                                 {"component",
                                  listener.Component ? Json(listener.Component.ToString()) : Json(nullptr)},
                                 {"method", listener.Method}});
                        }
                        return listeners;
                    }
                    else if constexpr (std::same_as<T, Curve1D>)
                    {
                        Json keys = Json::array();
                        for (const auto& key : current.Keys())
                        {
                            keys.push_back({{"time", key.Time},
                                            {"value", key.Value},
                                            {"inTangent", key.InTangent},
                                            {"outTangent", key.OutTangent},
                                            {"interpolation", static_cast<std::uint8_t>(key.Interpolation)}});
                        }
                        return keys;
                    }
                    else if constexpr (std::same_as<T, ColorGradient>)
                    {
                        Json keys = Json::array();
                        for (const auto& key : current.Keys())
                        {
                            keys.push_back(
                                {{"time", key.Time},
                                 {"color", {key.Value.Red, key.Value.Green, key.Value.Blue, key.Value.Alpha}}});
                        }
                        return Json{{"interpolation", static_cast<std::uint8_t>(current.Interpolation())},
                                    {"keys", std::move(keys)}};
                    }
                    else
                        return Json(current);
                },
                value);
        }

        [[nodiscard]] ComponentPropertyValue DecodeProperty(const Json& value, const ComponentProperty& property)
        {
            const auto requireArray = [&](const std::size_t size)
            {
                if (!value.is_array() || value.size() != size)
                    throw std::invalid_argument("Component vector property has an invalid shape.");
            };
            switch (property.Kind)
            {
            case ComponentPropertyKind::Boolean:
                return value.get<bool>();
            case ComponentPropertyKind::Integer:
                return value.get<std::int64_t>();
            case ComponentPropertyKind::Scalar:
                return value.get<double>();
            case ComponentPropertyKind::Text:
            case ComponentPropertyKind::ManagedReferenceGraph:
                return value.get<std::string>();
            case ComponentPropertyKind::Vector2:
                requireArray(2);
                return Vector2{value[0].get<float>(), value[1].get<float>()};
            case ComponentPropertyKind::Vector3:
                requireArray(3);
                return Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            case ComponentPropertyKind::Vector4:
                requireArray(4);
                return Vector4{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                               value[3].get<float>()};
            case ComponentPropertyKind::Quaternion:
                requireArray(4);
                return Quaternion{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                                  value[3].get<float>()};
            case ComponentPropertyKind::Color:
                requireArray(4);
                return Color{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                             value[3].get<float>()};
            case ComponentPropertyKind::Asset:
                return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
            case ComponentPropertyKind::Entity:
                if (property.ReferenceKind == ManagedReferenceKind::Component ||
                    property.ReferenceKind == ManagedReferenceKind::Behaviour)
                {
                    if (value.is_null())
                        return ComponentReferenceValue{};
                    if (!value.is_object())
                        return ComponentReferenceValue{EntityId::Parse(value.get<std::string>()), {}};
                    ComponentReferenceValue result;
                    if (const auto entity = value.find("entity"); entity != value.end() && !entity->is_null())
                        result.Entity = EntityId::Parse(entity->get<std::string>());
                    if (const auto component = value.find("component");
                        component != value.end() && !component->is_null())
                    {
                        result.Component = ComponentTypeId::Parse(component->get<std::string>());
                    }
                    return result;
                }
                return value.is_null() ? EntityId{} : EntityId::Parse(value.get<std::string>());
            case ComponentPropertyKind::Event:
            {
                if (!value.is_array())
                    throw std::runtime_error("Component event property must be an array.");
                ComponentEventValue result;
                result.Listeners.reserve(value.size());
                for (const auto& serialized : value)
                {
                    if (!serialized.is_object())
                        throw std::runtime_error("Component event listener must be an object.");
                    ComponentEventListener listener;
                    listener.Enabled = serialized.value("enabled", true);
                    if (const auto found = serialized.find("target"); found != serialized.end() && !found->is_null())
                        listener.Target = EntityId::Parse(found->get<std::string>());
                    if (const auto found = serialized.find("component"); found != serialized.end() && !found->is_null())
                        listener.Component = ComponentTypeId::Parse(found->get<std::string>());
                    listener.Method = serialized.value("method", std::string{});
                    result.Listeners.push_back(std::move(listener));
                }
                return result;
            }
            case ComponentPropertyKind::Curve:
            {
                if (!value.is_array())
                    throw std::runtime_error("Component curve property must be an array.");
                std::vector<CurveKey> keys;
                keys.reserve(value.size());
                for (const auto& serialized : value)
                {
                    if (!serialized.is_object())
                        throw std::runtime_error("Component curve key must be an object.");
                    const auto interpolation = serialized.value("interpolation", std::uint8_t{1});
                    if (interpolation > static_cast<std::uint8_t>(CurveInterpolation::Cubic))
                        throw std::runtime_error("Component curve interpolation is invalid.");
                    keys.push_back({serialized.at("time").get<float>(), serialized.at("value").get<float>(),
                                    serialized.value("inTangent", 0.0F), serialized.value("outTangent", 0.0F),
                                    static_cast<CurveInterpolation>(interpolation)});
                }
                return Curve1D(std::move(keys));
            }
            case ComponentPropertyKind::Gradient:
            {
                if (!value.is_object())
                    throw std::runtime_error("Component gradient property must be an object.");
                const auto interpolation = value.value("interpolation", std::uint8_t{1});
                if (interpolation > static_cast<std::uint8_t>(GradientInterpolation::Linear))
                    throw std::runtime_error("Component gradient interpolation is invalid.");
                const auto& serializedKeys = value.at("keys");
                if (!serializedKeys.is_array())
                    throw std::runtime_error("Component gradient keys must be an array.");
                std::vector<ColorGradientKey> keys;
                keys.reserve(serializedKeys.size());
                for (const auto& serialized : serializedKeys)
                {
                    if (!serialized.is_object())
                        throw std::runtime_error("Component gradient key must be an object.");
                    const auto& color = serialized.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Component gradient color must contain four channels.");
                    keys.push_back(
                        {serialized.at("time").get<float>(),
                         {color[0].get<float>(), color[1].get<float>(), color[2].get<float>(), color[3].get<float>()}});
                }
                return ColorGradient(std::move(keys), static_cast<GradientInterpolation>(interpolation));
            }
            }
            throw std::invalid_argument("Unsupported component property kind.");
        }

        [[nodiscard]] std::optional<AssetId> TaggedEntityId(const Json& value)
        {
            if (!value.is_object())
                return std::nullopt;
            if (const auto nested = value.find("entity"); nested != value.end())
                return TaggedEntityId(*nested);
            if (const auto nested = value.find("Entity"); nested != value.end())
                return TaggedEntityId(*nested);
            if (const auto nested = value.find("Id"); nested != value.end() && nested->is_object())
                return TaggedEntityId(*nested);
            if (const auto nested = value.find("id"); nested != value.end() && nested->is_object())
                return TaggedEntityId(*nested);
            const auto high = value.find(value.contains("High") ? "High" : "high");
            const auto low = value.find(value.contains("Low") ? "Low" : "low");
            if (high == value.end() || low == value.end() || !high->is_number_unsigned() || !low->is_number_unsigned())
                return std::nullopt;
            return AssetId(high->get<std::uint64_t>(), low->get<std::uint64_t>());
        }

        bool ReplaceTaggedEntityId(Json& value, const AssetId replacement)
        {
            if (!value.is_object())
                return false;
            for (const auto key : {"entity", "Entity", "Id", "id"})
            {
                if (auto nested = value.find(key); nested != value.end() && nested->is_object())
                    if (ReplaceTaggedEntityId(*nested, replacement))
                        return true;
            }
            auto high = value.find(value.contains("High") ? "High" : "high");
            auto low = value.find(value.contains("Low") ? "Low" : "low");
            if (high == value.end() || low == value.end())
                return false;
            *high = replacement.High();
            *low = replacement.Low();
            return true;
        }

        void RemapManagedReferences(Json& value, const std::unordered_map<EntityId, EntityId>& remapped)
        {
            if (value.is_array())
            {
                for (auto& child : value)
                    RemapManagedReferences(child, remapped);
                return;
            }
            if (!value.is_object())
                return;
            const auto tag = value.find("$ref");
            const bool taggedEntity =
                tag != value.end() && tag->is_string() &&
                (tag->get_ref<const std::string&>() == "entity" || tag->get_ref<const std::string&>() == "component");
            const bool legacyEntity =
                (value.contains("World") || value.contains("world")) && (value.contains("Id") || value.contains("id"));
            if (taggedEntity || legacyEntity)
            {
                if (const auto source = TaggedEntityId(value))
                {
                    const auto found = remapped.find(EntityId(*source));
                    if (found != remapped.end())
                        (void)ReplaceTaggedEntityId(value, found->second.Value());
                }
            }
            for (auto& [key, child] : value.items())
            {
                (void)key;
                RemapManagedReferences(child, remapped);
            }
        }
    } // namespace

    bool IsMeshMaterialSlotKey(const std::string_view key) noexcept
    {
        constexpr std::string_view prefix = "material.";
        if (!key.starts_with(prefix) || key.size() == prefix.size())
            return false;

        std::size_t slot = 0;
        for (const char character : key.substr(prefix.size()))
        {
            if (character < '0' || character > '9')
                return false;
            const auto digit = static_cast<std::size_t>(character - '0');
            if (slot > (std::numeric_limits<std::size_t>::max() - digit) / 10U)
                return false;
            slot = slot * 10U + digit;
        }
        return slot > 0U && slot < 256U;
    }

    std::string EncodeComponentPropertyBag(const ComponentPropertyBag& bag)
    {
        Json object = Json::object();
        for (const auto& [key, value] : bag)
            object[key] = EncodeProperty(value);
        return object.dump();
    }

    ComponentPropertyBag DecodeComponentPropertyBag(const std::string_view data,
                                                    const ComponentRegistration& registration)
    {
        const auto object = Json::parse(data);
        if (!object.is_object())
            throw std::invalid_argument("Component data must be a JSON object.");
        ComponentPropertyBag result;
        for (const auto& property : registration.Properties)
        {
            if (const auto found = object.find(property.Key); found != object.end())
                result.emplace(property.Key, DecodeProperty(*found, property));
        }
        if (registration.Type == MeshRendererComponent::StaticType())
        {
            const auto material = std::ranges::find(registration.Properties, "material", &ComponentProperty::Key);
            if (material == registration.Properties.end())
                throw std::logic_error("Mesh Renderer registration is missing its material property.");
            for (const auto& [key, value] : object.items())
            {
                if (IsMeshMaterialSlotKey(key))
                    result.emplace(key, DecodeProperty(value, *material));
            }
        }
        if (const auto found = object.find("managedState"); found != object.end())
        {
            if (!found->is_string())
                throw std::invalid_argument("Managed component state must be text.");
            result.insert_or_assign("managedState", found->get<std::string>());
        }
        return result;
    }

    std::string EncodeLegacyTransform(const SceneTransform& transform)
    {
        return EncodeComponentPropertyBag(
            {{"position", transform.Position}, {"rotation", transform.Rotation}, {"scale", transform.Scale}});
    }

    std::string RemapManagedStateReferences(const std::string_view state,
                                            const std::unordered_map<EntityId, EntityId>& remapped)
    {
        auto document = Json::parse(state);
        RemapManagedReferences(document, remapped);
        return document.dump();
    }
} // namespace Keire::Detail
