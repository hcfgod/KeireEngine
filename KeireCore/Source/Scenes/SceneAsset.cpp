#include "Keire/Scenes/SceneAsset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumDocumentBytes = 64U * 1024U * 1024U;
        constexpr std::size_t MaximumObjects = 100'000;
        constexpr std::size_t MaximumHierarchyDepth = 512;
        constexpr std::size_t MaximumNameBytes = 256;

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

        [[nodiscard]] bool Finite(const SceneVector3& value) noexcept
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

        [[nodiscard]] bool Finite(const SceneQuaternion& value) noexcept
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z) && std::isfinite(value.W);
        }

        [[nodiscard]] Json EncodeVector3(const SceneVector3& value) { return Json::array({value.X, value.Y, value.Z}); }

        [[nodiscard]] Json EncodeQuaternion(const SceneQuaternion& value)
        {
            return Json::array({value.X, value.Y, value.Z, value.W});
        }

        [[nodiscard]] std::size_t ApproximateResidentBytes(const SceneDefinition& definition) noexcept
        {
            std::size_t result = definition.Name.size() + definition.Objects.size() * sizeof(SceneObjectDefinition);
            for (const auto& object : definition.Objects)
                result += object.Name.size();
            return result;
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

        SceneDefinition definition;
        definition.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        definition.Name = document.at("name").get<std::string>();
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
            object.Transform.Position = ParseVector3(transform.at("position"));
            object.Transform.Rotation = ParseQuaternion(transform.at("rotation"));
            object.Transform.Scale = ParseVector3(transform.at("scale"));
            definition.Objects.push_back(std::move(object));
        }
        Validate(definition);
        return CreateRef<SceneAsset>(std::move(definition));
    }

    std::vector<std::byte> SceneAsset::Encode(const SceneDefinition& definition)
    {
        Validate(definition);
        Json objects = Json::array();
        for (const auto& object : definition.Objects)
        {
            Json value{{"id", object.Id.ToString()},
                       {"name", object.Name},
                       {"active", object.Active},
                       {"transform",
                        {{"position", EncodeVector3(object.Transform.Position)},
                         {"rotation", EncodeQuaternion(object.Transform.Rotation)},
                         {"scale", EncodeVector3(object.Transform.Scale)}}}};
            value["parent"] = object.Parent ? Json(object.Parent.ToString()) : Json(nullptr);
            objects.push_back(std::move(value));
        }
        const Json document{
            {"schemaVersion", definition.SchemaVersion}, {"name", definition.Name}, {"objects", std::move(objects)}};
        const auto text = document.dump(2) + '\n';
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }

    SceneDefinition SceneAsset::EmptyDefinition(std::string name)
    {
        return {.SchemaVersion = 1, .Name = std::move(name)};
    }

    SceneDefinition SceneAsset::SampleDefinition()
    {
        SceneDefinition result = EmptyDefinition("SampleScene");
        result.Objects.push_back({AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000001"),
                                  {},
                                  "Main Camera",
                                  true,
                                  {{0.0F, 1.0F, -10.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}}});
        result.Objects.push_back({AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000002"),
                                  {},
                                  "Directional Light",
                                  true,
                                  {{0.0F, 3.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}}});
        return result;
    }

    void SceneAsset::Validate(const SceneDefinition& definition)
    {
        if (definition.SchemaVersion != 1)
            throw std::invalid_argument("Scene asset uses an unsupported schema version.");
        if (definition.Name.empty() || definition.Name.size() > MaximumNameBytes)
            throw std::invalid_argument("Scene name is empty or exceeds 256 UTF-8 bytes.");
        if (definition.Objects.size() > MaximumObjects)
            throw std::invalid_argument("Scene exceeds the supported object limit.");

        std::unordered_map<AssetId, std::size_t> depths;
        depths.reserve(definition.Objects.size());
        for (const auto& object : definition.Objects)
        {
            if (!object.Id || object.Name.empty() || object.Name.size() > MaximumNameBytes ||
                depths.contains(object.Id))
                throw std::invalid_argument("Scene object has an invalid ID or name, or duplicates another object.");
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
            const auto& transform = object.Transform;
            if (!Finite(transform.Position) || !Finite(transform.Rotation) || !Finite(transform.Scale))
                throw std::invalid_argument("Scene transform contains a non-finite value.");
            const auto magnitude =
                std::sqrt(transform.Rotation.X * transform.Rotation.X + transform.Rotation.Y * transform.Rotation.Y +
                          transform.Rotation.Z * transform.Rotation.Z + transform.Rotation.W * transform.Rotation.W);
            if (magnitude < 0.0001F || std::abs(magnitude - 1.0F) > 0.001F)
                throw std::invalid_argument("Scene rotations must be normalized quaternions.");
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
        return {"Keire.Scene",
                1,
                SceneAsset::StaticType(),
                {".keirescene"},
                [](const std::span<const std::byte> bytes)
                {
                    const auto parsed = SceneAsset::Decode(bytes);
                    return SceneAsset::Encode(parsed->Definition());
                }};
    }
} // namespace Keire
