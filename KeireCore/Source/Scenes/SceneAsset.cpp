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

        [[nodiscard]] SceneDefinition DecodeVersionOne(const Json& document)
        {
            SceneDefinition definition{.SchemaVersion = 2, .Name = document.at("name").get<std::string>()};
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
        else if (version == 2)
        {
            definition.SchemaVersion = 2;
            definition.Name = document.at("name").get<std::string>();
            const auto& entities = document.at("entities");
            if (!entities.is_array())
                throw std::runtime_error("Scene entities must be an array.");
            definition.Objects.reserve(entities.size());
            for (const auto& value : entities)
                definition.Objects.push_back(DecodeEntity(value));
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
        {
            Json components = Json::array();
            bool hasTransform = false;
            for (const auto& component : object.Components)
            {
                const auto data = Json::parse(component.Data);
                components.push_back({{"type", component.Type.ToString()},
                                      {"version", component.SchemaVersion},
                                      {"enabled", component.Enabled},
                                      {"data", data}});
                hasTransform |= component.Type == TransformComponent::StaticType();
            }
            if (!hasTransform)
            {
                const auto transform = MakeTransformDefinition(object.Transform);
                components.insert(components.begin(), {{"type", transform.Type.ToString()},
                                                       {"version", transform.SchemaVersion},
                                                       {"enabled", transform.Enabled},
                                                       {"data", Json::parse(transform.Data)}});
            }
            Json entity{{"id", object.Id.ToString()},
                        {"name", object.Name},
                        {"active", object.Active},
                        {"components", std::move(components)}};
            entity["parent"] = object.Parent ? Json(object.Parent.ToString()) : Json(nullptr);
            entities.push_back(std::move(entity));
        }
        const Json document{{"schemaVersion", 2}, {"name", definition.Name}, {"entities", std::move(entities)}};
        const auto text = document.dump(2) + '\n';
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }

    SceneDefinition SceneAsset::EmptyDefinition(std::string name)
    {
        return {.SchemaVersion = 2, .Name = std::move(name)};
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
        if (definition.SchemaVersion != 2)
            throw std::invalid_argument("Scene definition must use canonical schema version 2.");
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
        result.Version = 2;
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
