#include "Keire/Rendering/ShaderGraph.h"

#include "KeireInternal/Authoring/GraphAuthoringSerialization.h"
#include "KeireInternal/Rendering/ShaderGraphCompilerInternal.h"
#include "KeireInternal/Rendering/ShaderGraphIdentity.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Keire
{
    namespace Detail
    {
        void ValidateShaderGraphInstanceDefinition(const ShaderGraphInstanceDefinition& definition)
        {
            if (definition.SchemaVersion != 1 || !definition.Parent ||
                definition.Properties.size() > MaximumShaderGraphProperties ||
                definition.KeywordOverrides.size() > MaximumShaderGraphKeywords)
                throw std::invalid_argument("Shader Graph instance schema, parent, or collection bounds are invalid.");
            for (const auto& [name, value] : definition.Properties)
            {
                if (!IsValidShaderGraphIdentifier(name))
                    throw std::invalid_argument("Shader Graph instance property names must be identifiers.");
                ValidateFiniteShaderGraphValue(value);
            }
            for (const auto& [name, value] : definition.KeywordOverrides)
                if (!IsValidShaderGraphIdentifier(name) ||
                    (value != "true" && value != "false" && !IsValidShaderGraphIdentifier(value)))
                    throw std::invalid_argument("Shader Graph instance keyword overrides are invalid.");
        }
    } // namespace Detail

    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] std::string Text(const std::span<const std::byte> bytes)
        {
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }

        [[nodiscard]] std::vector<std::uint8_t> ToUnsigned(const std::span<const std::byte> values)
        {
            std::vector<std::uint8_t> result(values.size());
            std::ranges::transform(values, result.begin(),
                                   [](const std::byte value) { return std::to_integer<std::uint8_t>(value); });
            return result;
        }

        [[nodiscard]] std::vector<std::byte> ToBytes(const std::vector<std::uint8_t>& values)
        {
            std::vector<std::byte> result(values.size());
            std::ranges::transform(values, result.begin(), [](const std::uint8_t value) { return std::byte(value); });
            return result;
        }

        template <typename Variant> [[nodiscard]] Json EncodeValue(const Variant& value)
        {
            return std::visit(
                [](const auto& typed) -> Json
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, float>)
                        return typed;
                    else if constexpr (std::same_as<T, Vector2>)
                        return Json::array({typed.X, typed.Y});
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json::array({typed.X, typed.Y, typed.Z});
                    else if constexpr (std::same_as<T, Vector4>)
                        return Json::array({typed.X, typed.Y, typed.Z, typed.W});
                    else if constexpr (std::same_as<T, Color>)
                        return Json::array({typed.Red, typed.Green, typed.Blue, typed.Alpha});
                    else if constexpr (std::same_as<T, AssetId>)
                        return typed ? Json(typed.ToString()) : Json(nullptr);
                    else
                        return Json(nullptr);
                },
                value);
        }

        [[nodiscard]] ShaderGraphValue DecodeValue(const Json& value, const ShaderGraphValueType type)
        {
            const auto finite = [](const float candidate)
            {
                if (!std::isfinite(candidate))
                    throw std::invalid_argument("Shader Graph values must be finite.");
                return candidate;
            };
            if (type == ShaderGraphValueType::Texture2D)
                return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
            if (type == ShaderGraphValueType::MaterialAttributes)
                return ShaderGraphMaterialAttributesValue{};
            if (type == ShaderGraphValueType::Bsdf)
                return ShaderGraphBsdfValue{};
            if (type == ShaderGraphValueType::Scalar)
                return finite(value.get<float>());
            const std::size_t count = type == ShaderGraphValueType::Vector2   ? 2U
                                      : type == ShaderGraphValueType::Vector3 ? 3U
                                                                              : 4U;
            if (!value.is_array() || value.size() != count)
                throw std::invalid_argument("Shader Graph vector value has the wrong component count.");
            std::array<float, 4> components{};
            for (std::size_t index = 0; index < count; ++index)
                components[index] = finite(value[index].get<float>());
            if (type == ShaderGraphValueType::Vector2)
                return Vector2{components[0], components[1]};
            if (type == ShaderGraphValueType::Vector3)
                return Vector3{components[0], components[1], components[2]};
            if (type == ShaderGraphValueType::Color)
                return Color{components[0], components[1], components[2], components[3]};
            return Vector4{components[0], components[1], components[2], components[3]};
        }

        [[nodiscard]] Json EncodeGraphJson(const ShaderGraphDefinition& definition)
        {
            Json nodes = Json::array();
            for (const auto& node : definition.Nodes)
            {
                Json pins = Json::array();
                for (const auto& pin : node.Pins)
                    pins.push_back({{"id", pin.Id.ToString()},
                                    {"name", pin.Name},
                                    {"type", static_cast<std::uint8_t>(pin.Type)},
                                    {"direction", static_cast<std::uint8_t>(pin.Direction)},
                                    {"default", EncodeValue(pin.DefaultValue)}});
                Json metadata{{"description", node.ParameterMetadata.Description},
                              {"category", node.ParameterMetadata.Category},
                              {"sortPriority", node.ParameterMetadata.SortPriority}};
                if (node.ParameterMetadata.Minimum)
                    metadata["minimum"] = *node.ParameterMetadata.Minimum;
                if (node.ParameterMetadata.Maximum)
                    metadata["maximum"] = *node.ParameterMetadata.Maximum;
                if (node.ParameterMetadata.Step)
                    metadata["step"] = *node.ParameterMetadata.Step;
                nodes.push_back(
                    {{"id", node.Id.ToString()},
                     {"typeId", node.TypeId.empty() ? ShaderGraphNodeTypeId(node.Kind) : node.TypeId},
                     {"kind", static_cast<std::uint8_t>(node.Kind)},
                     {"name", node.Name},
                     {"position", {node.EditorPosition.X, node.EditorPosition.Y}},
                     {"valueType", static_cast<std::uint8_t>(node.ValueType)},
                     {"value", EncodeValue(node.Value)},
                     {"textureSemantic", static_cast<std::uint8_t>(node.TextureSemantic)},
                     {"symbol", node.Symbol},
                     {"include", node.Include.generic_string()},
                     {"function", node.Function},
                     {"referencedAsset", node.ReferencedAsset ? Json(node.ReferencedAsset.ToString()) : Json(nullptr)},
                     {"parameterMetadata", std::move(metadata)},
                     {"pins", std::move(pins)}});
            }
            Json connections = Json::array();
            for (const auto& connection : definition.Connections)
            {
                Json routing = Json::array();
                for (const auto point : connection.RoutingPoints)
                    routing.push_back({point.X, point.Y});
                connections.push_back(
                    {{"id", connection.Id.ToString()},
                     {"output", {connection.Output.Node.ToString(), connection.Output.Pin.ToString()}},
                     {"input", {connection.Input.Node.ToString(), connection.Input.Pin.ToString()}},
                     {"routing", std::move(routing)}});
            }
            Json keywords = Json::array();
            for (const auto& keyword : definition.Keywords)
                keywords.push_back({{"name", keyword.Name},
                                    {"options", keyword.Options},
                                    {"default", keyword.DefaultOption},
                                    {"exposed", keyword.Exposed}});
            Json roots = Json::array();
            for (const auto& root : definition.IncludeRoots)
                roots.push_back(root.generic_string());
            Json result{
                {"schemaVersion", ShaderGraphSourceSchemaVersion},
                {"purpose", static_cast<std::uint8_t>(definition.Purpose)},
                {"output", static_cast<std::uint8_t>(definition.Output)},
                {"nodes", std::move(nodes)},
                {"connections", std::move(connections)},
                {"keywords", std::move(keywords)},
                {"includeRoots", std::move(roots)},
                {"maximumWorldPositionDisplacementRadius", definition.MaximumWorldPositionDisplacementRadius},
                {"resources", Json::parse(Text(EncodeShaderGraphResources(definition.Resources))).at("resources")},
                {"authoring", Detail::EncodeGraphAuthoringMetadata(definition.Authoring)}};
            if (definition.GeneratedAssetOwner)
                result["generatedAssetOwner"] = definition.GeneratedAssetOwner.ToString();
            return result;
        }

        [[nodiscard]] ShaderGraphDefinition DecodeGraphJson(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Shader Graph data must be an object.");
            const auto sourceSchemaVersion = source.value("schemaVersion", 0U);
            if (sourceSchemaVersion == 0U)
                throw std::invalid_argument("Shader Graph schema version is missing or unsupported.");
            if (sourceSchemaVersion > ShaderGraphSourceSchemaVersion)
                throw std::invalid_argument("Shader Graph schema version " + std::to_string(sourceSchemaVersion) +
                                            " is newer than the supported version " +
                                            std::to_string(ShaderGraphSourceSchemaVersion) + '.');
            const auto& nodes = source.at("nodes");
            const auto& connections = source.at("connections");
            const auto& keywords = source.value("keywords", Json::array());
            const auto& includeRoots = source.value("includeRoots", Json::array({"Assets"}));
            if (!nodes.is_array() || nodes.empty() || nodes.size() > Detail::MaximumShaderGraphNodes ||
                !connections.is_array() || connections.size() > Detail::MaximumShaderGraphConnections ||
                !keywords.is_array() || keywords.size() > Detail::MaximumShaderGraphKeywords ||
                !includeRoots.is_array() || includeRoots.empty() ||
                includeRoots.size() > Detail::MaximumShaderGraphIncludeRoots)
                throw std::invalid_argument("Shader Graph source collections exceed their bounds.");
            ShaderGraphDefinition result;
            result.SchemaVersion = sourceSchemaVersion;
            result.Purpose = sourceSchemaVersion >= 3U
                                 ? static_cast<ShaderGraphPurpose>(
                                       source.value("purpose", static_cast<std::uint8_t>(ShaderGraphPurpose::Shader)))
                                 : ShaderGraphPurpose::Shader;
            result.Output = static_cast<ShaderGraphOutput>(source.value("output", static_cast<std::uint8_t>(0)));
            result.MaximumWorldPositionDisplacementRadius =
                sourceSchemaVersion >= 5U ? source.at("maximumWorldPositionDisplacementRadius").get<float>() : 0.0F;
            if (source.contains("generatedAssetOwner"))
                result.GeneratedAssetOwner = AssetId::Parse(source.at("generatedAssetOwner").get<std::string>());
            result.IncludeRoots.clear();
            for (const auto& root : includeRoots)
                result.IncludeRoots.emplace_back(root.get<std::string>());
            for (const auto& encoded : nodes)
            {
                ShaderGraphNode node;
                node.Id = AssetId::Parse(encoded.at("id").get<std::string>());
                if (sourceSchemaVersion >= 2U)
                {
                    node.TypeId = encoded.at("typeId").get<std::string>();
                    const auto* descriptor = FindShaderGraphNodeDescriptor(node.TypeId);
                    if (!descriptor)
                        throw std::invalid_argument("Shader Graph contains an unknown node type ID: " + node.TypeId +
                                                    '.');
                    node.Kind = descriptor->Kind;
                    if (encoded.contains("kind") &&
                        static_cast<ShaderGraphNodeKind>(encoded.at("kind").get<std::uint8_t>()) != node.Kind)
                        throw std::invalid_argument("Shader Graph node type ID does not match its legacy kind.");
                }
                else
                {
                    node.Kind = static_cast<ShaderGraphNodeKind>(encoded.at("kind").get<std::uint8_t>());
                    node.TypeId = ShaderGraphNodeTypeId(node.Kind);
                    if (node.TypeId.empty())
                        throw std::invalid_argument("Shader Graph contains an unknown legacy node kind.");
                }
                node.Name = encoded.value("name", std::string{});
                const auto& position = encoded.at("position");
                node.EditorPosition = {position.at(0).get<float>(), position.at(1).get<float>()};
                node.ValueType =
                    static_cast<ShaderGraphValueType>(encoded.value("valueType", static_cast<std::uint8_t>(0)));
                node.Value = DecodeValue(encoded.at("value"), node.ValueType);
                node.TextureSemantic = static_cast<ShaderTextureSemantic>(
                    encoded.value("textureSemantic", static_cast<std::uint8_t>(ShaderTextureSemantic::Generic)));
                node.Symbol = encoded.value("symbol", std::string{});
                node.Include = encoded.value("include", std::string{});
                node.Function = encoded.value("function", std::string{});
                if (sourceSchemaVersion >= 3U && encoded.contains("referencedAsset") &&
                    !encoded.at("referencedAsset").is_null())
                    node.ReferencedAsset = AssetId::Parse(encoded.at("referencedAsset").get<std::string>());
                if (const auto metadata = encoded.find("parameterMetadata"); metadata != encoded.end())
                {
                    node.ParameterMetadata.Description = metadata->value("description", std::string{});
                    node.ParameterMetadata.Category = metadata->value("category", std::string{});
                    node.ParameterMetadata.SortPriority = metadata->value("sortPriority", 0);
                    if (metadata->contains("minimum"))
                        node.ParameterMetadata.Minimum = metadata->at("minimum").get<float>();
                    if (metadata->contains("maximum"))
                        node.ParameterMetadata.Maximum = metadata->at("maximum").get<float>();
                    if (metadata->contains("step"))
                        node.ParameterMetadata.Step = metadata->at("step").get<float>();
                }
                const auto& pins = encoded.at("pins");
                if (!pins.is_array() || pins.empty() || pins.size() > Detail::MaximumShaderGraphPinsPerNode)
                    throw std::invalid_argument("Shader Graph node pins exceed their bounds.");
                for (const auto& encodedPin : pins)
                {
                    ShaderGraphPin pin;
                    pin.Id = AssetId::Parse(encodedPin.at("id").get<std::string>());
                    pin.Name = encodedPin.at("name").get<std::string>();
                    pin.Type = static_cast<ShaderGraphValueType>(encodedPin.at("type").get<std::uint8_t>());
                    pin.Direction =
                        static_cast<ShaderGraphPinDirection>(encodedPin.at("direction").get<std::uint8_t>());
                    pin.DefaultValue = DecodeValue(encodedPin.at("default"), pin.Type);
                    node.Pins.push_back(std::move(pin));
                }
                result.Nodes.push_back(std::move(node));
            }
            for (const auto& encoded : connections)
            {
                const auto& output = encoded.at("output");
                const auto& input = encoded.at("input");
                ShaderGraphConnection connection{
                    AssetId::Parse(encoded.at("id").get<std::string>()),
                    {AssetId::Parse(output.at(0).get<std::string>()), AssetId::Parse(output.at(1).get<std::string>())},
                    {AssetId::Parse(input.at(0).get<std::string>()), AssetId::Parse(input.at(1).get<std::string>())}};
                const auto& routing = encoded.value("routing", Json::array());
                if (!routing.is_array() || routing.size() > Detail::MaximumShaderGraphRoutingPointsPerConnection)
                    throw std::invalid_argument("Shader Graph cable routing points exceed their bounds.");
                for (const auto& point : routing)
                {
                    if (!point.is_array() || point.size() != 2)
                        throw std::invalid_argument("Shader Graph cable routing point is invalid.");
                    connection.RoutingPoints.push_back({point.at(0).get<float>(), point.at(1).get<float>()});
                }
                result.Connections.push_back(std::move(connection));
            }
            for (const auto& encoded : keywords)
                result.Keywords.push_back({encoded.at("name").get<std::string>(),
                                           encoded.value("options", std::vector<std::string>{}),
                                           encoded.value("default", std::string{}), encoded.value("exposed", true)});
            const auto resourceText = Json{{"schemaVersion", ShaderGraphResourceContractSchemaVersion},
                                           {"resources", source.value("resources", Json::array())}}
                                          .dump();
            result.Resources = DecodeShaderGraphResources(std::as_bytes(std::span(resourceText)));
            if (sourceSchemaVersion >= 4U)
            {
                std::vector<AssetId> nodeIds;
                nodeIds.reserve(result.Nodes.size());
                std::ranges::transform(result.Nodes, std::back_inserter(nodeIds), &ShaderGraphNode::Id);
                result.Authoring =
                    Detail::DecodeGraphAuthoringMetadata(source.value("authoring", Json::object()), nodeIds);
            }
            if (result.Purpose == ShaderGraphPurpose::Shader)
            {
                const auto canonicalDefinition = CreateDefaultShaderGraph(result.Output);
                const auto& canonicalMaster = canonicalDefinition.Nodes.front();
                const auto master =
                    std::ranges::find(result.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
                if (master != result.Nodes.end())
                {
                    std::vector<ShaderGraphPin> migratedPins;
                    migratedPins.reserve(canonicalMaster.Pins.size());
                    for (const auto& expected : canonicalMaster.Pins)
                    {
                        const auto existing = std::ranges::find_if(
                            master->Pins, [&](const ShaderGraphPin& pin)
                            { return pin.Name == expected.Name && pin.Direction == expected.Direction; });
                        if (existing != master->Pins.end())
                            migratedPins.push_back(*existing);
                        else
                        {
                            auto migrated = expected;
                            migrated.Id =
                                Detail::StableMigratedShaderPinId(master->Id, expected.Name, expected.Direction);
                            migratedPins.push_back(std::move(migrated));
                        }
                    }
                    master->Pins = std::move(migratedPins);
                }
                if (sourceSchemaVersion < 2U)
                    for (auto& node : result.Nodes)
                    {
                        if (node.Kind == ShaderGraphNodeKind::Master || node.Kind == ShaderGraphNodeKind::Custom)
                            continue;
                        const auto canonical = CreateShaderGraphNode(node.Kind, node.ValueType);
                        std::vector<ShaderGraphPin> migratedPins;
                        migratedPins.reserve(canonical.Pins.size());
                        for (const auto& expected : canonical.Pins)
                        {
                            const auto existing = std::ranges::find_if(
                                node.Pins, [&](const ShaderGraphPin& pin)
                                { return pin.Name == expected.Name && pin.Direction == expected.Direction; });
                            if (existing != node.Pins.end())
                                migratedPins.push_back(*existing);
                            else
                            {
                                auto migrated = expected;
                                migrated.Id =
                                    Detail::StableMigratedShaderPinId(node.Id, expected.Name, expected.Direction);
                                migratedPins.push_back(std::move(migrated));
                            }
                        }
                        node.Pins = std::move(migratedPins);
                    }
            }
            ValidateShaderGraph(result);
            result.SchemaVersion = ShaderGraphSourceSchemaVersion;
            return result;
        }

        [[nodiscard]] Json EncodeInstanceJson(const ShaderGraphInstanceDefinition& definition)
        {
            Json properties = Json::object();
            for (const auto& [name, value] : definition.Properties)
                properties[name] = {{"type", value.index()}, {"value", EncodeValue(value)}};
            Json keywords = Json::object();
            for (const auto& [name, value] : definition.KeywordOverrides)
                keywords[name] = value;
            return {{"schemaVersion", definition.SchemaVersion},
                    {"parent", definition.Parent.ToString()},
                    {"properties", std::move(properties)},
                    {"keywords", std::move(keywords)}};
        }

        [[nodiscard]] ShaderGraphInstanceDefinition DecodeInstanceJson(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Shader Graph instance source must be an object.");
            ShaderGraphInstanceDefinition result;
            result.SchemaVersion = source.value("schemaVersion", 0U);
            result.Parent = AssetId::Parse(source.at("parent").get<std::string>());
            const auto& properties = source.value("properties", Json::object());
            const auto& keywords = source.value("keywords", Json::object());
            if (!properties.is_object() || properties.size() > Detail::MaximumShaderGraphProperties ||
                !keywords.is_object() || keywords.size() > Detail::MaximumShaderGraphKeywords)
                throw std::invalid_argument("Shader Graph instance properties and keywords must be objects.");
            for (const auto& [name, encoded] : properties.items())
            {
                const auto type = static_cast<ShaderGraphValueType>(encoded.at("type").get<std::uint8_t>());
                if (type > ShaderGraphValueType::Texture2D)
                    throw std::invalid_argument("Shader Graph instance property type is invalid.");
                const auto decoded = DecodeValue(encoded.at("value"), type);
                result.Properties.emplace(name, Detail::ToMaterialPropertyValue(decoded));
            }
            for (const auto& [name, encoded] : keywords.items())
                result.KeywordOverrides.emplace(name, encoded.get<std::string>());
            Detail::ValidateShaderGraphInstanceDefinition(result);
            return result;
        }
    } // namespace

    Ref<ShaderGraphAsset> ShaderGraphAsset::Decode(const std::span<const std::byte> bytes)
    {
        try
        {
            if (bytes.size() > Detail::MaximumShaderGraphAssetBytes)
                throw std::invalid_argument("Shader Graph cooked data exceeds its byte limit.");
            return CreateRef<ShaderGraphAsset>(DecodeGraphJson(Json::from_cbor(ToUnsigned(bytes))));
        }
        catch (const std::exception& error)
        {
            throw std::invalid_argument(std::string("Shader Graph asset decode failed: ") + error.what());
        }
    }

    std::vector<std::byte> ShaderGraphAsset::Encode(const ShaderGraphDefinition& definition)
    {
        ValidateShaderGraph(definition);
        return ToBytes(Json::to_cbor(EncodeGraphJson(definition)));
    }

    ShaderGraphDefinition ShaderGraphAsset::DecodeSource(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > Detail::MaximumShaderGraphAssetBytes)
            throw std::invalid_argument("Shader Graph source exceeds its byte limit.");
        return DecodeGraphJson(Json::parse(Text(bytes)));
    }

    std::vector<std::byte> ShaderGraphAsset::EncodeSource(const ShaderGraphDefinition& definition)
    {
        ValidateShaderGraph(definition);
        const auto text = EncodeGraphJson(definition).dump(2) + '\n';
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<ShaderGraphAsset> ShaderGraphAsset::Error()
    {
        return CreateRef<ShaderGraphAsset>(CreateDefaultShaderGraph(ShaderGraphOutput::Unlit));
    }

    Ref<ShaderGraphInstanceAsset> ShaderGraphInstanceAsset::Decode(const std::span<const std::byte> bytes)
    {
        try
        {
            if (bytes.size() > Detail::MaximumShaderGraphAssetBytes)
                throw std::invalid_argument("Shader Graph instance cooked data exceeds its byte limit.");
            return CreateRef<ShaderGraphInstanceAsset>(DecodeInstanceJson(Json::from_cbor(ToUnsigned(bytes))));
        }
        catch (const std::exception& error)
        {
            throw std::invalid_argument(std::string("Shader Graph instance asset decode failed: ") + error.what());
        }
    }

    std::vector<std::byte> ShaderGraphInstanceAsset::Encode(const ShaderGraphInstanceDefinition& definition)
    {
        Detail::ValidateShaderGraphInstanceDefinition(definition);
        return ToBytes(Json::to_cbor(EncodeInstanceJson(definition)));
    }

    ShaderGraphInstanceDefinition ShaderGraphInstanceAsset::DecodeSource(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > Detail::MaximumShaderGraphAssetBytes)
            throw std::invalid_argument("Shader Graph instance source exceeds its byte limit.");
        return DecodeInstanceJson(Json::parse(Text(bytes)));
    }

    std::vector<std::byte> ShaderGraphInstanceAsset::EncodeSource(const ShaderGraphInstanceDefinition& definition)
    {
        Detail::ValidateShaderGraphInstanceDefinition(definition);
        const auto text = EncodeInstanceJson(definition).dump(2) + '\n';
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<ShaderGraphInstanceAsset> ShaderGraphInstanceAsset::Error() { return CreateRef<ShaderGraphInstanceAsset>(); }
} // namespace Keire
