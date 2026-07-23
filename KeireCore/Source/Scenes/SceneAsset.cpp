#include "Keire/Scenes/SceneAsset.h"

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <ranges>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumDocumentBytes = 64U * 1024U * 1024U;
        constexpr std::size_t MaximumObjects = 100'000;
        constexpr std::size_t MaximumComponentsPerEntity = 1024;
        constexpr std::size_t MaximumComponentDataBytes = 4U * 1024U * 1024U;
        constexpr std::size_t MaximumHierarchyDepth = 512;
        constexpr std::size_t MaximumNameBytes = 256;

        [[nodiscard]] std::vector<AssetId> RenderingDependencies(const SceneDefinition& definition)
        {
            std::set<AssetId> unique;
            for (const auto& instance : definition.PrefabInstances)
                unique.insert(instance.Prefab);
            const auto collectOverrideDependencies = [&](const std::vector<PrefabOverrideDefinition>& overrides)
            {
                for (const auto& overrideValue : overrides)
                {
                    if (overrideValue.Kind == PrefabOverrideKind::SetComponentProperty &&
                        std::holds_alternative<AssetId>(overrideValue.Value))
                    {
                        const auto dependency = std::get<AssetId>(overrideValue.Value);
                        if (dependency)
                            unique.insert(dependency);
                    }
                }
            };
            collectOverrideDependencies(definition.PrefabOverrides);
            for (const auto& instance : definition.PrefabInstances)
                collectOverrideDependencies(instance.Overrides);
            for (const auto& object : definition.Objects)
            {
                for (const auto& component : object.Components)
                {
                    if (component.Type != MeshRendererComponent::StaticType())
                        continue;
                    const auto data = Json::parse(component.Data);
                    for (const auto* key : {"mesh", "material"})
                    {
                        if (!data.contains(key) || data.at(key).is_null())
                            continue;
                        const auto dependency = AssetId::Parse(data.at(key).get<std::string>());
                        if (dependency && dependency != MeshAsset::CubeId() && dependency != MeshAsset::ErrorId())
                            unique.insert(dependency);
                    }
                }
            }
            return {unique.begin(), unique.end()};
        }

        [[nodiscard]] SceneVector3 ParseVector3(const Json& value)
        {
            if (!value.is_array() || value.size() != 3)
                throw std::runtime_error("Scene vector must contain exactly three numbers.");
            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }

        [[nodiscard]] SceneQuaternion ParseQuaternion(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::runtime_error("Scene quaternion must contain exactly four numbers.");
            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
        }

        [[nodiscard]] Json EncodeVector3(const SceneVector3& value) { return Json::array({value.X, value.Y, value.Z}); }

        [[nodiscard]] Json EncodeQuaternion(const SceneQuaternion& value)
        {
            return Json::array({value.X, value.Y, value.Z, value.W});
        }

        [[nodiscard]] Json TransformData(const SceneTransform& transform)
        {
            return {{"position", EncodeVector3(transform.Position)},
                    {"rotation", EncodeQuaternion(transform.Rotation)},
                    {"scale", EncodeVector3(transform.Scale)}};
        }

        [[nodiscard]] SceneTransform ParseTransformData(const Json& data)
        {
            if (!data.is_object())
                throw std::runtime_error("Transform component data must be an object.");
            return {ParseVector3(data.at("position")), ParseQuaternion(data.at("rotation")),
                    ParseVector3(data.at("scale"))};
        }

        [[nodiscard]] SceneComponentDefinition MakeTransformDefinition(const SceneTransform& transform)
        {
            return {TransformComponent::StaticType(), 1, true, TransformData(transform).dump()};
        }

        [[nodiscard]] std::size_t ApproximateResidentBytes(const SceneDefinition& definition) noexcept
        {
            std::size_t result = definition.Name.size() + definition.Objects.size() * sizeof(SceneObjectDefinition);
            for (const auto& object : definition.Objects)
            {
                result += object.Name.size();
                for (const auto& component : object.Components)
                    result += component.Data.size() + sizeof(SceneComponentDefinition);
            }
            return result;
        }

        [[nodiscard]] SceneObjectDefinition DecodeEntity(const Json& value)
        {
            SceneObjectDefinition object;
            object.Id = AssetId::Parse(value.at("id").get<std::string>());
            if (value.contains("parent") && !value["parent"].is_null())
                object.Parent = AssetId::Parse(value["parent"].get<std::string>());
            object.Name = value.at("name").get<std::string>();
            object.Active = value.value("active", true);
            const auto& components = value.at("components");
            if (!components.is_array() || components.size() > MaximumComponentsPerEntity)
                throw std::runtime_error("Scene entity components must be a bounded array.");
            object.Components.reserve(components.size());
            for (const auto& serialized : components)
            {
                if (!serialized.is_object())
                    throw std::runtime_error("Scene component record must be an object.");
                SceneComponentDefinition component;
                component.Type = ComponentTypeId::Parse(serialized.at("type").get<std::string>());
                component.SchemaVersion = serialized.at("version").get<std::uint32_t>();
                component.Enabled = serialized.value("enabled", true);
                component.Data = serialized.at("data").dump();
                if (component.Type == TransformComponent::StaticType())
                    object.Transform = ParseTransformData(serialized.at("data"));
                object.Components.push_back(std::move(component));
            }
            return object;
        }

        [[nodiscard]] Json EncodePropertyValue(const ComponentPropertyValue& value)
        {
            return std::visit(
                [](const auto& current) -> Json
                {
                    using T = std::remove_cvref_t<decltype(current)>;
                    if constexpr (std::same_as<T, Vector2>)
                        return Json::array({current.X, current.Y});
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json::array({current.X, current.Y, current.Z});
                    else if constexpr (std::same_as<T, Vector4>)
                        return Json::array({current.X, current.Y, current.Z, current.W});
                    else if constexpr (std::same_as<T, Quaternion>)
                        return Json::array({current.X, current.Y, current.Z, current.W});
                    else if constexpr (std::same_as<T, Color>)
                        return Json::array({current.Red, current.Green, current.Blue, current.Alpha});
                    else if constexpr (std::same_as<T, AssetId> || std::same_as<T, EntityId>)
                        return current ? Json(current.ToString()) : Json(nullptr);
                    else
                        return Json(current);
                },
                value);
        }

        [[nodiscard]] ComponentPropertyValue DecodePropertyValue(const std::size_t type, const Json& value)
        {
            const auto array = [&](const std::size_t count)
            {
                if (!value.is_array() || value.size() != count)
                    throw std::runtime_error("Prefab override value has an invalid shape.");
            };
            switch (type)
            {
            case 0:
                return value.get<bool>();
            case 1:
                return value.get<std::int64_t>();
            case 2:
                return value.get<double>();
            case 3:
                return value.get<std::string>();
            case 4:
                array(2);
                return Vector2{value[0].get<float>(), value[1].get<float>()};
            case 5:
                array(3);
                return Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            case 6:
                array(4);
                return Vector4{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                               value[3].get<float>()};
            case 7:
                array(4);
                return Quaternion{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                                  value[3].get<float>()};
            case 8:
                array(4);
                return Color{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                             value[3].get<float>()};
            case 9:
                return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
            case 10:
                return value.is_null() ? EntityId{} : EntityId::Parse(value.get<std::string>());
            default:
                throw std::runtime_error("Prefab override value uses an unsupported type.");
            }
        }

        [[nodiscard]] Json EncodeEntity(const SceneObjectDefinition& object)
        {
            Json components = Json::array();
            bool hasTransform = false;
            for (const auto& component : object.Components)
            {
                components.push_back({{"type", component.Type.ToString()},
                                      {"version", component.SchemaVersion},
                                      {"enabled", component.Enabled},
                                      {"data", Json::parse(component.Data)}});
                hasTransform |= component.Type == TransformComponent::StaticType();
            }
            if (!hasTransform)
            {
                const auto transform = MakeTransformDefinition(object.Transform);
                Json serializedTransform{{"type", transform.Type.ToString()},
                                         {"version", transform.SchemaVersion},
                                         {"enabled", transform.Enabled},
                                         {"data", Json::parse(transform.Data)}};
                components.insert(components.begin(), std::move(serializedTransform));
            }
            Json result{{"id", object.Id.ToString()},
                        {"name", object.Name},
                        {"active", object.Active},
                        {"components", std::move(components)}};
            result["parent"] = object.Parent ? Json(object.Parent.ToString()) : Json(nullptr);
            return result;
        }

        [[nodiscard]] Json EncodeOverride(const PrefabOverrideDefinition& value)
        {
            Json result{{"kind", static_cast<std::uint8_t>(value.Kind)},
                        {"object", value.Object ? Json(value.Object.ToString()) : Json(nullptr)}};
            switch (value.Kind)
            {
            case PrefabOverrideKind::RenameObject:
                result["name"] = value.Name;
                break;
            case PrefabOverrideKind::SetObjectActive:
                result["active"] = value.Active;
                break;
            case PrefabOverrideKind::SetObjectTransform:
                result["transform"] = TransformData(value.Transform);
                break;
            case PrefabOverrideKind::SetComponentProperty:
                result["component"] = value.Component.ToString();
                result["property"] = value.Property;
                result["value"] = {{"type", value.Value.index()}, {"data", EncodePropertyValue(value.Value)}};
                break;
            case PrefabOverrideKind::AddComponent:
                result["componentValue"] = {{"type", value.AddedComponent->Type.ToString()},
                                            {"version", value.AddedComponent->SchemaVersion},
                                            {"enabled", value.AddedComponent->Enabled},
                                            {"data", Json::parse(value.AddedComponent->Data)}};
                break;
            case PrefabOverrideKind::RemoveComponent:
                result["component"] = value.Component.ToString();
                break;
            case PrefabOverrideKind::AddObject:
                result["objectValue"] = EncodeEntity(*value.AddedObject);
                break;
            case PrefabOverrideKind::RemoveObject:
                break;
            }
            return result;
        }

        [[nodiscard]] PrefabOverrideDefinition DecodeOverride(const Json& value)
        {
            PrefabOverrideDefinition result;
            result.Kind = static_cast<PrefabOverrideKind>(value.at("kind").get<std::uint8_t>());
            if (value.contains("object") && !value.at("object").is_null())
                result.Object = AssetId::Parse(value.at("object").get<std::string>());
            switch (result.Kind)
            {
            case PrefabOverrideKind::RenameObject:
                result.Name = value.at("name").get<std::string>();
                break;
            case PrefabOverrideKind::SetObjectActive:
                result.Active = value.at("active").get<bool>();
                break;
            case PrefabOverrideKind::SetObjectTransform:
                result.Transform = ParseTransformData(value.at("transform"));
                break;
            case PrefabOverrideKind::SetComponentProperty:
                result.Component = ComponentTypeId::Parse(value.at("component").get<std::string>());
                result.Property = value.at("property").get<std::string>();
                result.Value =
                    DecodePropertyValue(value.at("value").at("type").get<std::size_t>(), value.at("value").at("data"));
                break;
            case PrefabOverrideKind::AddComponent:
            {
                const auto& component = value.at("componentValue");
                result.AddedComponent =
                    SceneComponentDefinition{ComponentTypeId::Parse(component.at("type").get<std::string>()),
                                             component.at("version").get<std::uint32_t>(),
                                             component.value("enabled", true), component.at("data").dump()};
                break;
            }
            case PrefabOverrideKind::RemoveComponent:
                result.Component = ComponentTypeId::Parse(value.at("component").get<std::string>());
                break;
            case PrefabOverrideKind::AddObject:
                result.AddedObject = DecodeEntity(value.at("objectValue"));
                break;
            case PrefabOverrideKind::RemoveObject:
                break;
            default:
                throw std::runtime_error("Prefab override uses an unsupported operation.");
            }
            return result;
        }

        [[nodiscard]] Json EncodeInstance(const PrefabInstanceDefinition& instance)
        {
            Json mappings = Json::array();
            for (const auto& mapping : instance.Objects)
                mappings.push_back({{"source", mapping.Source.ToString()}, {"instance", mapping.Instance.ToString()}});
            Json overrides = Json::array();
            for (const auto& value : instance.Overrides)
                overrides.push_back(EncodeOverride(value));
            return {{"prefab", instance.Prefab.ToString()},
                    {"root", instance.Root.ToString()},
                    {"objects", std::move(mappings)},
                    {"overrides", std::move(overrides)}};
        }

        [[nodiscard]] PrefabInstanceDefinition DecodeInstance(const Json& value)
        {
            PrefabInstanceDefinition result;
            result.Prefab = AssetId::Parse(value.at("prefab").get<std::string>());
            result.Root = AssetId::Parse(value.at("root").get<std::string>());
            for (const auto& mapping : value.at("objects"))
                result.Objects.push_back({AssetId::Parse(mapping.at("source").get<std::string>()),
                                          AssetId::Parse(mapping.at("instance").get<std::string>())});
            for (const auto& overrideValue : value.value("overrides", Json::array()))
                result.Overrides.push_back(DecodeOverride(overrideValue));
            return result;
        }

        [[nodiscard]] SceneDefinition DecodeVersionOne(const Json& document)
        {
            SceneDefinition definition{.SchemaVersion = 3, .Name = document.at("name").get<std::string>()};
            const auto& objects = document.at("objects");
            if (!objects.is_array())
                throw std::runtime_error("Scene objects must be an array.");
            definition.Objects.reserve(objects.size());
            for (const auto& value : objects)
            {
                SceneObjectDefinition object;
                object.Id = AssetId::Parse(value.at("id").get<std::string>());
                if (value.contains("parent") && !value["parent"].is_null())
                    object.Parent = AssetId::Parse(value["parent"].get<std::string>());
                object.Name = value.at("name").get<std::string>();
                object.Active = value.value("active", true);
                const auto& transform = value.at("transform");
                object.Transform = {ParseVector3(transform.at("position")), ParseQuaternion(transform.at("rotation")),
                                    ParseVector3(transform.at("scale"))};
                object.Components.push_back(MakeTransformDefinition(object.Transform));
                definition.Objects.push_back(std::move(object));
            }
            return definition;
        }
    } // namespace

    SceneAsset::SceneAsset(SceneDefinition definition) : m_Definition(std::move(definition))
    {
        if (!m_Definition.Name.empty() || !m_Definition.Objects.empty())
            Validate(m_Definition);
        m_ResidentBytes = ApproximateResidentBytes(m_Definition);
    }

    std::size_t SceneAsset::ResidentBytes() const noexcept { return m_ResidentBytes; }

    const SceneObjectDefinition* SceneAsset::FindObject(const AssetId id) const noexcept
    {
        const auto found = std::ranges::find(m_Definition.Objects, id, &SceneObjectDefinition::Id);
        return found == m_Definition.Objects.end() ? nullptr : &*found;
    }

    Ref<SceneAsset> SceneAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::runtime_error("Scene asset is empty or exceeds the supported size limit.");
        const auto* characters = reinterpret_cast<const char*>(bytes.data());
        const auto document = Json::parse(characters, characters + bytes.size());
        if (!document.is_object())
            throw std::runtime_error("Scene asset root must be an object.");

        const auto version = document.at("schemaVersion").get<std::uint32_t>();
        SceneDefinition definition;
        if (version == 1)
        {
            definition = DecodeVersionOne(document);
        }
        else if (version == 2 || version == 3)
        {
            definition.SchemaVersion = 3;
            definition.Name = document.at("name").get<std::string>();
            const auto& entities = document.at("entities");
            if (!entities.is_array())
                throw std::runtime_error("Scene entities must be an array.");
            definition.Objects.reserve(entities.size());
            for (const auto& value : entities)
                definition.Objects.push_back(DecodeEntity(value));
            if (version == 3)
            {
                for (const auto& instance : document.value("prefabInstances", Json::array()))
                    definition.PrefabInstances.push_back(DecodeInstance(instance));
                for (const auto& overrideValue : document.value("prefabOverrides", Json::array()))
                    definition.PrefabOverrides.push_back(DecodeOverride(overrideValue));
            }
        }
        else
        {
            throw std::runtime_error("Scene asset uses an unsupported schema version.");
        }
        Validate(definition);
        return CreateRef<SceneAsset>(std::move(definition));
    }

    std::vector<std::byte> SceneAsset::Encode(const SceneDefinition& definition)
    {
        Validate(definition);
        Json entities = Json::array();
        for (const auto& object : definition.Objects)
            entities.push_back(EncodeEntity(object));
        Json instances = Json::array();
        for (const auto& instance : definition.PrefabInstances)
            instances.push_back(EncodeInstance(instance));
        Json overrides = Json::array();
        for (const auto& overrideValue : definition.PrefabOverrides)
            overrides.push_back(EncodeOverride(overrideValue));
        const Json document{{"schemaVersion", 3},
                            {"name", definition.Name},
                            {"entities", std::move(entities)},
                            {"prefabInstances", std::move(instances)},
                            {"prefabOverrides", std::move(overrides)}};
        const auto text = document.dump(2) + '\n';
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }

    SceneDefinition SceneAsset::EmptyDefinition(std::string name)
    {
        return {.SchemaVersion = 3, .Name = std::move(name)};
    }

    SceneDefinition SceneAsset::SampleDefinition()
    {
        return SampleDefinition(AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000002"));
    }

    SceneDefinition SceneAsset::SampleDefinition(const AssetId material)
    {
        if (!material)
            throw std::invalid_argument("Sample scene material identity must be valid.");
        SceneDefinition result = EmptyDefinition("SampleScene");
        SceneObjectDefinition camera{AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000001"),
                                     {},
                                     "Main Camera",
                                     true,
                                     {{0.0F, 1.0F, -10.0F}, {}, {1.0F, 1.0F, 1.0F}}};
        camera.Components.push_back(MakeTransformDefinition(camera.Transform));
        camera.Components.push_back({CameraComponent::StaticType(), 1, true,
                                     Json({{"projection", 0},
                                           {"clearMode", 0},
                                           {"primary", true},
                                           {"priority", 0},
                                           {"fieldOfView", 60.0},
                                           {"orthographicSize", 10.0},
                                           {"nearPlane", 0.1},
                                           {"farPlane", 1000.0},
                                           {"clearColor", Json::array({0.10F, 0.12F, 0.16F, 1.0F})}})
                                         .dump()});
        result.Objects.push_back(std::move(camera));

        SceneObjectDefinition cube{AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000003"),
                                   {},
                                   "Cube",
                                   true,
                                   {{0.0F, 0.5F, 0.0F}, {}, {2.0F, 2.0F, 2.0F}}};
        cube.Components.push_back(MakeTransformDefinition(cube.Transform));
        cube.Components.push_back({MeshRendererComponent::StaticType(), 1, true,
                                   Json({{"mesh", MeshAsset::CubeId().ToString()},
                                         {"material", material.ToString()},
                                         {"tint", Json::array({0.25F, 0.55F, 1.0F, 1.0F})},
                                         {"visible", true}})
                                       .dump()});
        result.Objects.push_back(std::move(cube));

        SceneObjectDefinition light{AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000002"),
                                    {},
                                    "Directional Light",
                                    true,
                                    {{0.0F, 3.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
        light.Components.push_back(MakeTransformDefinition(light.Transform));
        light.Components.push_back({DirectionalLightComponent::StaticType(), 1, true,
                                    Json({{"color", Json::array({1.0F, 1.0F, 1.0F, 1.0F})},
                                          {"intensity", 1.0},
                                          {"useTemperature", false},
                                          {"temperature", 6500.0},
                                          {"shadows", 2},
                                          {"shadowStrength", 1.0},
                                          {"shadowBias", 0.005}})
                                        .dump()});
        result.Objects.push_back(std::move(light));
        return result;
    }

    void SceneAsset::Validate(const SceneDefinition& definition)
    {
        if (definition.SchemaVersion != 3)
            throw std::invalid_argument("Scene definition must use canonical schema version 3.");
        if (definition.Name.empty() || definition.Name.size() > MaximumNameBytes)
            throw std::invalid_argument("Scene name is empty or exceeds 256 UTF-8 bytes.");
        if (definition.Objects.size() > MaximumObjects)
            throw std::invalid_argument("Scene exceeds the supported entity limit.");

        std::unordered_map<AssetId, std::size_t> depths;
        depths.reserve(definition.Objects.size());
        for (const auto& object : definition.Objects)
        {
            if (!object.Id || object.Name.empty() || object.Name.size() > MaximumNameBytes ||
                depths.contains(object.Id))
                throw std::invalid_argument("Scene entity has an invalid ID or name, or duplicates another entity.");
            std::size_t depth = 1;
            if (object.Parent)
            {
                const auto parent = depths.find(object.Parent);
                if (parent == depths.end())
                    throw std::invalid_argument("Scene parents must exist and precede their children.");
                depth = parent->second + 1;
            }
            if (depth > MaximumHierarchyDepth)
                throw std::invalid_argument("Scene hierarchy exceeds the supported depth limit.");
            if (object.Components.size() > MaximumComponentsPerEntity)
                throw std::invalid_argument("Scene entity exceeds the supported component limit.");

            std::size_t transformCount = 0;
            for (const auto& component : object.Components)
            {
                if (!component.Type || component.SchemaVersion == 0 || component.Data.empty() ||
                    component.Data.size() > MaximumComponentDataBytes)
                    throw std::invalid_argument("Scene component record is incomplete or exceeds its size limit.");
                const auto data = Json::parse(component.Data);
                if (!data.is_object())
                    throw std::invalid_argument("Scene component data must be an object.");
                if (component.Type == TransformComponent::StaticType())
                {
                    ++transformCount;
                    const auto transform = ParseTransformData(data);
                    if (!Math::IsFinite(transform.Position) || !Math::IsFinite(transform.Rotation) ||
                        !Math::IsFinite(transform.Scale) || std::abs(Math::Length(transform.Rotation) - 1.0F) > 0.001F)
                        throw std::invalid_argument("Scene Transform contains invalid or non-normalized values.");
                }
            }
            if (transformCount > 1)
                throw std::invalid_argument("Scene entity contains more than one Transform component.");

            if (!Math::IsFinite(object.Transform.Position) || !Math::IsFinite(object.Transform.Rotation) ||
                !Math::IsFinite(object.Transform.Scale) ||
                std::abs(Math::Length(object.Transform.Rotation) - 1.0F) > 0.001F)
                throw std::invalid_argument("Scene compatibility transform contains invalid values.");
            depths.emplace(object.Id, depth);
        }

        for (const auto& instance : definition.PrefabInstances)
        {
            if (!instance.Prefab || !instance.Root || !depths.contains(instance.Root) || instance.Objects.empty())
                throw std::invalid_argument("Prefab instance is incomplete or references a missing scene root.");
            std::set<AssetId> sources;
            std::set<AssetId> instances;
            for (const auto& mapping : instance.Objects)
            {
                if (!mapping.Source || !mapping.Instance || !depths.contains(mapping.Instance) ||
                    !sources.insert(mapping.Source).second || !instances.insert(mapping.Instance).second)
                    throw std::invalid_argument("Prefab instance object mapping is invalid.");
            }
            if (!instances.contains(instance.Root))
                throw std::invalid_argument("Prefab instance root must participate in its object mapping.");
        }

        const auto validateOverrides = [](const std::vector<PrefabOverrideDefinition>& overrides)
        {
            for (const auto& value : overrides)
            {
                switch (value.Kind)
                {
                case PrefabOverrideKind::RenameObject:
                    if (!value.Object || value.Name.empty() || value.Name.size() > MaximumNameBytes)
                        throw std::invalid_argument("Prefab rename override is invalid.");
                    break;
                case PrefabOverrideKind::SetObjectActive:
                    if (!value.Object)
                        throw std::invalid_argument("Prefab active override has no target.");
                    break;
                case PrefabOverrideKind::SetObjectTransform:
                    if (!value.Object || !Math::IsFinite(value.Transform.Position) ||
                        !Math::IsFinite(value.Transform.Rotation) || !Math::IsFinite(value.Transform.Scale) ||
                        std::abs(Math::Length(value.Transform.Rotation) - 1.0F) > 0.001F)
                        throw std::invalid_argument("Prefab transform override is invalid.");
                    break;
                case PrefabOverrideKind::SetComponentProperty:
                    if (!value.Object || !value.Component || value.Property.empty() || value.Property.size() > 256)
                        throw std::invalid_argument("Prefab component property override is invalid.");
                    break;
                case PrefabOverrideKind::AddComponent:
                    if (!value.Object || !value.AddedComponent || !value.AddedComponent->Type ||
                        value.AddedComponent->SchemaVersion == 0 || value.AddedComponent->Data.empty() ||
                        !Json::parse(value.AddedComponent->Data).is_object())
                        throw std::invalid_argument("Prefab add-component override is invalid.");
                    break;
                case PrefabOverrideKind::RemoveComponent:
                    if (!value.Object || !value.Component || value.Component == TransformComponent::StaticType())
                        throw std::invalid_argument("Prefab remove-component override is invalid.");
                    break;
                case PrefabOverrideKind::AddObject:
                    if (!value.AddedObject || !value.AddedObject->Id || value.AddedObject->Name.empty())
                        throw std::invalid_argument("Prefab add-object override is invalid.");
                    break;
                case PrefabOverrideKind::RemoveObject:
                    if (!value.Object)
                        throw std::invalid_argument("Prefab remove-object override has no target.");
                    break;
                default:
                    throw std::invalid_argument("Prefab override uses an unsupported operation.");
                }
            }
        };
        validateOverrides(definition.PrefabOverrides);
        for (const auto& instance : definition.PrefabInstances)
            validateOverrides(instance.Overrides);
    }

    AssetDecoderRegistration CreateSceneAssetDecoder()
    {
        return {SceneAsset::StaticType(), CreateRef<SceneAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return SceneAsset::Decode(bytes); }};
    }

    AssetImporterRegistration CreateSceneAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.Scene";
        result.Version = 3;
        result.Type = SceneAsset::StaticType();
        result.Extensions = {".keirescene"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto parsed = SceneAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = SceneAsset::Encode(parsed->Definition());
            output.AssetDependencies = RenderingDependencies(parsed->Definition());
            return output;
        };
        return result;
    }
} // namespace Keire
