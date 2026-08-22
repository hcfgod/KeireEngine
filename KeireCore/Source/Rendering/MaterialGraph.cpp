#include "Keire/Rendering/MaterialGraph.h"

#include "Keire/Rendering/ShaderGraph.h"
#include "KeireInternal/Authoring/GraphAuthoringSerialization.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumMaterialGraphBytes = std::size_t{4} * 1024U * 1024U;
        constexpr std::size_t MaximumMaterialProperties = 80;
        constexpr std::size_t MaximumMaterialKeywords = 16;
        constexpr std::size_t MaximumMaterialGraphNodes = 256;
        constexpr std::size_t MaximumMaterialGraphConnections = 256;
        constexpr std::size_t MaximumGraphRoutingPointsPerConnection = 64;
        constexpr std::size_t MaximumMaterialInstanceDepth = 16;

        [[nodiscard]] std::string MaterialGraphVariantKey(const std::string_view target,
                                                          const std::span<const std::string> keywords)
        {
            return "material-graph/" + MakeShaderGraphVariantSubAssetKey(target, keywords);
        }

        [[nodiscard]] AssetId StableGraphId(const MaterialShaderReference& shader, const std::string_view role,
                                            const AssetId property = {}, const std::string_view name = {}) noexcept
        {
            std::uint64_t high = 14695981039346656037ULL;
            std::uint64_t low = 1099511628211ULL;
            const auto mix = [&](const std::uint8_t value)
            {
                high = (high ^ value) * 1099511628211ULL;
                low ^= static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ULL + (low << 6U) + (low >> 2U);
            };
            const auto mixText = [&](const std::string_view value)
            {
                for (const char character : value)
                    mix(static_cast<std::uint8_t>(character));
            };
            const auto mixInteger = [&](const std::uint64_t value)
            {
                for (std::size_t shift = 0; shift < 64; shift += 8)
                    mix(static_cast<std::uint8_t>(value >> shift));
            };
            mixInteger(shader.Asset.High());
            mixInteger(shader.Asset.Low());
            mixText(shader.Target);
            mixText(role);
            mixInteger(property.High());
            mixInteger(property.Low());
            mixText(name);
            high = (high & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
            low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
            return {high, low};
        }

        struct ResolvedShaderGraphVariant final
        {
            AssetId Owner;
            std::vector<std::string> Keywords;
        };

        [[nodiscard]] ResolvedShaderGraphVariant ResolveShaderGraphVariant(const AssetImportContext& context,
                                                                           const MaterialShaderReference& reference)
        {
            if (context.ProjectRoot.empty() || context.SourceRoot.empty() || !context.ReadProjectFile ||
                !context.ResolveAssetSource)
            {
                throw std::invalid_argument("Shader Graph material bindings require source and cross-asset resolvers.");
            }
            const auto source = context.ResolveAssetSource(reference.Asset);
            if (!source || source->Type != ShaderGraphAsset::StaticType())
                throw std::runtime_error("Material references a Shader Graph that is not present in the source index.");
            const auto sourcePrefix = std::filesystem::relative(context.SourceRoot, context.ProjectRoot);
            const auto graph =
                ShaderGraphAsset::DecodeSource(context.ReadProjectFile(sourcePrefix / source->RelativePath));
            ShaderGraphInstanceDefinition selection;
            selection.Parent = reference.Asset;
            selection.KeywordOverrides = reference.Keywords;
            const std::array ancestry{selection};
            auto resolved = ResolveShaderGraphInstance(graph, ancestry);
            return {graph.GeneratedAssetOwner ? graph.GeneratedAssetOwner : reference.Asset,
                    std::move(resolved.Keywords)};
        }

        [[nodiscard]] std::string Text(const std::span<const std::byte> bytes)
        {
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }

        [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
        {
            return {reinterpret_cast<const std::byte*>(text.data()),
                    reinterpret_cast<const std::byte*>(text.data() + text.size())};
        }

        [[nodiscard]] std::vector<std::uint8_t> Unsigned(const std::span<const std::byte> values)
        {
            std::vector<std::uint8_t> result(values.size());
            std::ranges::transform(values, result.begin(),
                                   [](const std::byte value) { return std::to_integer<std::uint8_t>(value); });
            return result;
        }

        [[nodiscard]] std::vector<std::byte> Bytes(const std::vector<std::uint8_t>& values)
        {
            std::vector<std::byte> result(values.size());
            std::ranges::transform(values, result.begin(), [](const std::uint8_t value) { return std::byte(value); });
            return result;
        }

        [[nodiscard]] bool ValidIdentifier(const std::string_view value)
        {
            if (value.empty() || value.size() > 128 ||
                !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
                return false;
            return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_'; });
        }

        [[nodiscard]] bool ValidTarget(const std::string_view value)
        {
            return !value.empty() && value.size() <= 64 &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_' || character == '-'; });
        }

        [[nodiscard]] Json EncodeValue(const MaterialPropertyValue& value)
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
                    else
                        return typed ? Json(typed.ToString()) : Json(nullptr);
                },
                value);
        }

        [[nodiscard]] MaterialPropertyValue DecodeValue(const Json& source, const std::size_t type)
        {
            const auto component = [&](const std::size_t index)
            {
                if (!source.is_array() || source.size() <= index)
                    throw std::invalid_argument("Material Graph vector value has an invalid component count.");
                return source[index].get<float>();
            };
            switch (type)
            {
            case 0:
                return source.get<float>();
            case 1:
                return Vector2{component(0), component(1)};
            case 2:
                return Vector3{component(0), component(1), component(2)};
            case 3:
                return Vector4{component(0), component(1), component(2), component(3)};
            case 4:
                return Color{component(0), component(1), component(2), component(3)};
            case 5:
                return source.is_null() ? AssetId{} : AssetId::Parse(source.get<std::string>());
            default:
                throw std::invalid_argument("Material Graph property type is unsupported.");
            }
        }

        void ValidateFiniteValue(const MaterialPropertyValue& value)
        {
            std::visit(
                [](const auto& typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (!std::same_as<T, AssetId>)
                    {
                        const bool finite = [&]
                        {
                            if constexpr (std::same_as<T, float>)
                                return std::isfinite(typed);
                            else if constexpr (std::same_as<T, Vector2>)
                                return Math::IsFinite(Vector4{typed.X, typed.Y, 0.0F, 0.0F});
                            else if constexpr (std::same_as<T, Vector3>)
                                return Math::IsFinite(Vector4{typed.X, typed.Y, typed.Z, 0.0F});
                            else if constexpr (std::same_as<T, Vector4>)
                                return Math::IsFinite(typed);
                            else
                                return Math::IsFinite(Vector4{typed.Red, typed.Green, typed.Blue, typed.Alpha});
                        }();
                        if (!finite)
                            throw std::invalid_argument("Material values must be finite.");
                    }
                },
                value);
        }

        [[nodiscard]] Json EncodeShaderReference(const MaterialShaderReference& reference)
        {
            const auto kind = reference.Kind == MaterialShaderSourceKind::Builtin       ? "builtin"
                              : reference.Kind == MaterialShaderSourceKind::ShaderGraph ? "graph"
                                                                                        : "asset";
            Json keywords = Json::object();
            for (const auto& [name, option] : reference.Keywords)
                keywords[name] = option;
            return {{"kind", kind},
                    {"asset", reference.Asset ? Json(reference.Asset.ToString()) : Json(nullptr)},
                    {"target", reference.Target},
                    {"keywords", std::move(keywords)}};
        }

        [[nodiscard]] MaterialShaderReference DecodeShaderReference(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Material Graph shader reference must be an object.");
            MaterialShaderReference result;
            const auto kind = source.at("kind").get<std::string>();
            if (kind == "builtin")
                result.Kind = MaterialShaderSourceKind::Builtin;
            else if (kind == "asset")
                result.Kind = MaterialShaderSourceKind::ShaderAsset;
            else if (kind == "graph")
                result.Kind = MaterialShaderSourceKind::ShaderGraph;
            else
                throw std::invalid_argument("Material Graph shader reference kind is unsupported.");
            result.Asset =
                source.at("asset").is_null() ? AssetId{} : AssetId::Parse(source.at("asset").get<std::string>());
            result.Target = source.value("target", std::string("default"));
            const auto& keywords = source.value("keywords", Json::object());
            if (!keywords.is_object() || keywords.size() > MaximumMaterialKeywords)
                throw std::invalid_argument("Material Graph shader keywords must be a bounded object.");
            for (const auto& [name, option] : keywords.items())
                result.Keywords.emplace(name, option.get<std::string>());
            return result;
        }

        [[nodiscard]] ShaderGraphDefinition CreateStableMaterialSurfaceGraph(const MaterialShaderReference& shader);

        [[nodiscard]] Json EncodeDefinition(const MaterialGraphDefinition& definition)
        {
            Json properties = Json::array();
            for (const auto& property : definition.Properties)
            {
                properties.push_back({{"id", property.Property ? Json(property.Property.ToString()) : Json(nullptr)},
                                      {"name", property.Name},
                                      {"type", static_cast<std::uint8_t>(property.Type)},
                                      {"pin", property.Pin.ToString()},
                                      {"value", EncodeValue(property.Value)}});
            }
            Json nodes = Json::array();
            for (const auto& node : definition.Nodes)
            {
                nodes.push_back({{"id", node.Id.ToString()},
                                 {"name", node.Name},
                                 {"position", Json::array({node.EditorPosition.X, node.EditorPosition.Y})},
                                 {"type", static_cast<std::uint8_t>(node.Type)},
                                 {"outputPin", node.OutputPin.ToString()},
                                 {"value", EncodeValue(node.Value)}});
            }
            Json connections = Json::array();
            for (const auto& connection : definition.Connections)
            {
                Json routing = Json::array();
                for (const auto point : connection.RoutingPoints)
                    routing.push_back({point.X, point.Y});
                connections.push_back(
                    {{"id", connection.Id.ToString()},
                     {"output",
                      {{"node", connection.Output.Node.ToString()}, {"pin", connection.Output.Pin.ToString()}}},
                     {"input", {{"node", connection.Input.Node.ToString()}, {"pin", connection.Input.Pin.ToString()}}},
                     {"routing", std::move(routing)}});
            }
            const auto surfaceGraphSource = ShaderGraphAsset::EncodeSource(definition.SurfaceGraph);
            const auto surfaceGraph = Json::parse(Text(surfaceGraphSource));
            return {{"schemaVersion", definition.SchemaVersion},
                    {"shader", EncodeShaderReference(definition.Shader)},
                    {"surface",
                     {{"alphaMode", static_cast<std::uint8_t>(definition.Surface.AlphaMode)},
                      {"alphaCutoff", definition.Surface.AlphaCutoff},
                      {"doubleSided", definition.Surface.DoubleSided}}},
                    {"bakedLighting",
                     {{"contributeEmission", definition.ContributeEmissionToGI},
                      {"emissiveIntensity", definition.EmissiveGIIntensity}}},
                    {"output",
                     {{"node", definition.OutputNode.ToString()},
                      {"position", Json::array({definition.OutputPosition.X, definition.OutputPosition.Y})}}},
                    {"properties", std::move(properties)},
                    {"nodes", std::move(nodes)},
                    {"connections", std::move(connections)},
                    {"surfaceGraph", surfaceGraph},
                    {"authoring", Detail::EncodeGraphAuthoringMetadata(definition.Authoring)}};
        }

        [[nodiscard]] MaterialGraphDefinition DecodeDefinition(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Material Graph source must be an object.");
            MaterialGraphDefinition result;
            const auto sourceSchema = source.value("schemaVersion", 0U);
            if (sourceSchema == 0 || sourceSchema > MaterialGraphSourceSchemaVersion)
                throw std::invalid_argument("Material Graph source schema is unsupported.");
            result.SchemaVersion = MaterialGraphSourceSchemaVersion;
            result.Shader = DecodeShaderReference(source.at("shader"));
            const auto& surface = source.value("surface", Json::object());
            result.Surface.AlphaMode =
                static_cast<MaterialAlphaMode>(surface.value("alphaMode", static_cast<std::uint8_t>(0)));
            result.Surface.AlphaCutoff = surface.value("alphaCutoff", 0.5F);
            result.Surface.DoubleSided = surface.value("doubleSided", false);
            const auto& lighting = source.value("bakedLighting", Json::object());
            result.ContributeEmissionToGI = lighting.value("contributeEmission", true);
            result.EmissiveGIIntensity = lighting.value("emissiveIntensity", 1.0F);
            const auto& properties = source.value("properties", Json::array());
            if (!properties.is_array() || properties.size() > MaximumMaterialProperties)
                throw std::invalid_argument("Material Graph properties must be a bounded array.");
            for (const auto& property : properties)
            {
                MaterialGraphPropertyBinding binding;
                binding.Property =
                    property.at("id").is_null() ? AssetId{} : AssetId::Parse(property.at("id").get<std::string>());
                binding.Name = property.at("name").get<std::string>();
                const auto type = property.at("type").get<std::size_t>();
                binding.Type = static_cast<ShaderPropertyType>(type);
                binding.Pin = sourceSchema == 1
                                  ? StableGraphId(result.Shader, "property-input", binding.Property, binding.Name)
                                  : AssetId::Parse(property.at("pin").get<std::string>());
                binding.Value = DecodeValue(property.at("value"), type);
                result.Properties.push_back(std::move(binding));
            }
            if (sourceSchema == 1)
            {
                result.OutputNode = StableGraphId(result.Shader, "material-output");
            }
            else
            {
                const auto& output = source.at("output");
                result.OutputNode = AssetId::Parse(output.at("node").get<std::string>());
                const auto& position = output.at("position");
                if (!position.is_array() || position.size() != 2)
                    throw std::invalid_argument("Material Graph output position is invalid.");
                result.OutputPosition = {position[0].get<float>(), position[1].get<float>()};
                const auto& nodes = source.value("nodes", Json::array());
                const auto& connections = source.value("connections", Json::array());
                if (!nodes.is_array() || nodes.size() > MaximumMaterialGraphNodes || !connections.is_array() ||
                    connections.size() > MaximumMaterialGraphConnections)
                    throw std::invalid_argument("Material Graph topology exceeds its portable bounds.");
                for (const auto& encoded : nodes)
                {
                    MaterialGraphValueNode node;
                    node.Id = AssetId::Parse(encoded.at("id").get<std::string>());
                    node.Name = encoded.at("name").get<std::string>();
                    const auto& nodePosition = encoded.at("position");
                    if (!nodePosition.is_array() || nodePosition.size() != 2)
                        throw std::invalid_argument("Material Graph node position is invalid.");
                    node.EditorPosition = {nodePosition[0].get<float>(), nodePosition[1].get<float>()};
                    node.Type = static_cast<ShaderPropertyType>(encoded.at("type").get<std::size_t>());
                    node.OutputPin = AssetId::Parse(encoded.at("outputPin").get<std::string>());
                    node.Value = DecodeValue(encoded.at("value"), static_cast<std::size_t>(node.Type));
                    result.Nodes.push_back(std::move(node));
                }
                for (const auto& encoded : connections)
                {
                    MaterialGraphConnection connection;
                    connection.Id = AssetId::Parse(encoded.at("id").get<std::string>());
                    connection.Output.Node = AssetId::Parse(encoded.at("output").at("node").get<std::string>());
                    connection.Output.Pin = AssetId::Parse(encoded.at("output").at("pin").get<std::string>());
                    connection.Input.Node = AssetId::Parse(encoded.at("input").at("node").get<std::string>());
                    connection.Input.Pin = AssetId::Parse(encoded.at("input").at("pin").get<std::string>());
                    const auto& routing = encoded.value("routing", Json::array());
                    if (!routing.is_array() || routing.size() > MaximumGraphRoutingPointsPerConnection)
                        throw std::invalid_argument("Material Graph cable routing points exceed their bounds.");
                    for (const auto& point : routing)
                    {
                        if (!point.is_array() || point.size() != 2)
                            throw std::invalid_argument("Material Graph cable routing point is invalid.");
                        connection.RoutingPoints.push_back({point.at(0).get<float>(), point.at(1).get<float>()});
                    }
                    result.Connections.push_back(std::move(connection));
                }
            }
            if (sourceSchema >= 3)
            {
                const auto encodedSurfaceGraph = source.at("surfaceGraph").dump();
                result.SurfaceGraph = ShaderGraphAsset::DecodeSource(Bytes(encodedSurfaceGraph));
            }
            else
                result.SurfaceGraph = CreateStableMaterialSurfaceGraph(result.Shader);
            if (sourceSchema >= 4)
            {
                std::vector<AssetId> nodeIds{result.OutputNode};
                nodeIds.reserve(result.Nodes.size() + result.SurfaceGraph.Nodes.size() + 1U);
                std::ranges::transform(result.Nodes, std::back_inserter(nodeIds), &MaterialGraphValueNode::Id);
                std::ranges::transform(result.SurfaceGraph.Nodes, std::back_inserter(nodeIds), &ShaderGraphNode::Id);
                result.Authoring =
                    Detail::DecodeGraphAuthoringMetadata(source.value("authoring", Json::object()), nodeIds);
            }
            ValidateMaterialGraph(result);
            return result;
        }

        [[nodiscard]] Json EncodeInstanceDefinition(const MaterialInstanceDefinition& definition)
        {
            Json properties = Json::array();
            for (const auto& [name, value] : definition.Properties)
                properties.push_back({{"name", name}, {"type", value.index()}, {"value", EncodeValue(value)}});
            Json surface = nullptr;
            if (definition.Surface)
                surface = {{"alphaMode", static_cast<std::uint8_t>(definition.Surface->AlphaMode)},
                           {"alphaCutoff", definition.Surface->AlphaCutoff},
                           {"doubleSided", definition.Surface->DoubleSided}};
            return {{"schemaVersion", definition.SchemaVersion},
                    {"parent", definition.Parent ? Json(definition.Parent.ToString()) : Json(nullptr)},
                    {"surface", std::move(surface)},
                    {"contributeEmissionToGI",
                     definition.ContributeEmissionToGI ? Json(*definition.ContributeEmissionToGI) : Json(nullptr)},
                    {"emissiveGIIntensity",
                     definition.EmissiveGIIntensity ? Json(*definition.EmissiveGIIntensity) : Json(nullptr)},
                    {"properties", std::move(properties)},
                    {"keywords", definition.KeywordOverrides}};
        }

        [[nodiscard]] MaterialInstanceDefinition DecodeInstanceDefinition(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Material Instance source must be an object.");
            MaterialInstanceDefinition result;
            const auto sourceSchema = source.value("schemaVersion", 0U);
            if (sourceSchema != 1 && sourceSchema != MaterialInstanceSourceSchemaVersion)
                throw std::invalid_argument("Material Instance source schema is unsupported.");
            result.SchemaVersion = MaterialInstanceSourceSchemaVersion;
            result.Parent =
                source.at("parent").is_null() ? AssetId{} : AssetId::Parse(source.at("parent").get<std::string>());
            const auto& surface = source.value("surface", Json(nullptr));
            if (!surface.is_null())
            {
                MaterialSurfaceState state;
                state.AlphaMode =
                    static_cast<MaterialAlphaMode>(surface.value("alphaMode", static_cast<std::uint8_t>(0)));
                state.AlphaCutoff = surface.value("alphaCutoff", 0.5F);
                state.DoubleSided = surface.value("doubleSided", false);
                result.Surface = state;
            }
            const auto& contribute = source.value("contributeEmissionToGI", Json(nullptr));
            if (!contribute.is_null())
                result.ContributeEmissionToGI = contribute.get<bool>();
            const auto& intensity = source.value("emissiveGIIntensity", Json(nullptr));
            if (!intensity.is_null())
                result.EmissiveGIIntensity = intensity.get<float>();
            const auto& properties = source.value("properties", Json::array());
            if (!properties.is_array() || properties.size() > MaximumMaterialProperties)
                throw std::invalid_argument("Material Instance properties exceed their portable bound.");
            for (const auto& property : properties)
            {
                const auto name = property.at("name").get<std::string>();
                if (!result.Properties
                         .emplace(name, DecodeValue(property.at("value"), property.at("type").get<std::size_t>()))
                         .second)
                    throw std::invalid_argument("Material Instance properties must have unique names.");
            }
            if (sourceSchema >= 2)
                result.KeywordOverrides = source.value("keywords", std::map<std::string, std::string, std::less<>>{});
            ValidateMaterialInstance(result);
            return result;
        }

        [[nodiscard]] bool ValueMatches(const MaterialPropertyValue& value, const ShaderPropertyType type)
        {
            return (type == ShaderPropertyType::Scalar && std::holds_alternative<float>(value)) ||
                   (type == ShaderPropertyType::Vector2 && std::holds_alternative<Vector2>(value)) ||
                   (type == ShaderPropertyType::Vector3 && std::holds_alternative<Vector3>(value)) ||
                   (type == ShaderPropertyType::Vector4 && std::holds_alternative<Vector4>(value)) ||
                   (type == ShaderPropertyType::Color && std::holds_alternative<Color>(value)) ||
                   (type == ShaderPropertyType::Texture2D && std::holds_alternative<AssetId>(value));
        }

        [[nodiscard]] ShaderGraphValue ToShaderGraphValue(const MaterialPropertyValue& value)
        {
            return std::visit([](const auto& typed) -> ShaderGraphValue { return typed; }, value);
        }

        [[nodiscard]] bool EquivalentSurfacePin(const std::string_view left, const std::string_view right) noexcept
        {
            return left == right || (left == "BaseColor" && right == "Color") ||
                   (left == "Color" && right == "BaseColor");
        }

        [[nodiscard]] ShaderGraphDefinition CreateStableMaterialSurfaceGraph(const MaterialShaderReference& shader)
        {
            auto result = CreateDefaultShaderGraph();
            for (auto& node : result.Nodes)
            {
                node.Id = StableGraphId(shader, "surface-expression", {}, node.TypeId);
                for (auto& pin : node.Pins)
                    pin.Id = StableGraphId(shader, "surface-expression-pin", node.Id, pin.Name);
            }
            return result;
        }

        [[nodiscard]] bool HasMaterialSurfaceExpressions(const MaterialGraphDefinition& definition)
        {
            const auto master =
                std::ranges::find(definition.SurfaceGraph.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
            return master != definition.SurfaceGraph.Nodes.end() &&
                   std::ranges::any_of(definition.SurfaceGraph.Connections, [&](const ShaderGraphConnection& connection)
                                       { return connection.Input.Node == master->Id; });
        }

        void PruneUnreachableShaderNodes(ShaderGraphDefinition& definition)
        {
            const auto master =
                std::ranges::find(definition.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
            if (master == definition.Nodes.end())
                throw std::invalid_argument("Composed material shader has no output node.");

            std::set<AssetId> reachable{master->Id};
            std::vector<AssetId> pending{master->Id};
            while (!pending.empty())
            {
                const auto input = pending.back();
                pending.pop_back();
                for (const auto& connection : definition.Connections)
                    if (connection.Input.Node == input && reachable.insert(connection.Output.Node).second)
                        pending.push_back(connection.Output.Node);
            }
            std::erase_if(definition.Nodes, [&](const ShaderGraphNode& node) { return !reachable.contains(node.Id); });
            std::erase_if(
                definition.Connections, [&](const ShaderGraphConnection& connection)
                { return !reachable.contains(connection.Output.Node) || !reachable.contains(connection.Input.Node); });
        }
    } // namespace

    MaterialPropertyValue DefaultMaterialGraphValue(const ShaderPropertyDefinition& property)
    {
        switch (property.Type)
        {
        case ShaderPropertyType::Scalar:
            return property.DefaultValue.X;
        case ShaderPropertyType::Vector2:
            return Vector2{property.DefaultValue.X, property.DefaultValue.Y};
        case ShaderPropertyType::Vector3:
            return Vector3{property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z};
        case ShaderPropertyType::Vector4:
            return property.DefaultValue;
        case ShaderPropertyType::Color:
            return Color{property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z,
                         property.DefaultValue.W};
        case ShaderPropertyType::Texture2D:
            return property.DefaultTexture;
        }
        throw std::invalid_argument("Shader property type cannot be represented by a Material Graph.");
    }

    MaterialGraphDefinition CreateMaterialGraph(MaterialShaderReference shader,
                                                const ShaderInterfaceDefinition& interfaceDefinition)
    {
        MaterialGraphDefinition result;
        result.Shader = std::move(shader);
        result.OutputNode = StableGraphId(result.Shader, "material-output");
        result.SurfaceGraph = CreateStableMaterialSurfaceGraph(result.Shader);
        SynchronizeMaterialGraphInterface(result, interfaceDefinition);
        ValidateMaterialGraph(result);
        return result;
    }

    ShaderGraphDefinition CreateMaterialSurfaceGraph(const ShaderGraphDefinition& shaderTemplate)
    {
        ValidateShaderGraph(shaderTemplate);
        const auto templateOutput =
            std::ranges::find(shaderTemplate.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
        if (templateOutput == shaderTemplate.Nodes.end())
            throw std::invalid_argument("Shader Graph template has no output contract for a Material Graph.");

        ShaderGraphDefinition result;
        result.Output = shaderTemplate.Output;
        result.Nodes.push_back(*templateOutput);
        result.Nodes.front().Id = AssetId::Generate();
        result.Nodes.front().Name = "Material Output";
        result.Nodes.front().EditorPosition = {760.0F, 180.0F};
        for (auto& pin : result.Nodes.front().Pins)
            pin.Id = AssetId::Generate();
        ValidateShaderGraph(result);
        return result;
    }

    void SynchronizeMaterialGraphInterface(MaterialGraphDefinition& definition,
                                           const ShaderInterfaceDefinition& interfaceDefinition)
    {
        if (interfaceDefinition.SchemaVersion != 1 || interfaceDefinition.AbiVersion == 0 ||
            interfaceDefinition.Properties.size() > MaximumMaterialProperties)
            throw std::invalid_argument("Shader interface cannot be used by a Material Graph.");
        if (!definition.OutputNode)
            definition.OutputNode = StableGraphId(definition.Shader, "material-output");

        std::vector<MaterialGraphPropertyBinding> synchronized;
        synchronized.reserve(interfaceDefinition.Properties.size());
        for (const auto& property : interfaceDefinition.Properties)
        {
            auto existing = definition.Properties.end();
            if (property.Id)
                existing =
                    std::ranges::find(definition.Properties, property.Id, &MaterialGraphPropertyBinding::Property);
            if (existing == definition.Properties.end())
                existing = std::ranges::find(definition.Properties, property.Name, &MaterialGraphPropertyBinding::Name);

            MaterialGraphPropertyBinding binding;
            binding.Property = property.Id;
            binding.Name = property.Name;
            binding.Type = property.Type;
            binding.Pin = existing != definition.Properties.end() && existing->Pin
                              ? existing->Pin
                              : StableGraphId(definition.Shader, "property-input", property.Id, property.Name);
            binding.Value = existing != definition.Properties.end() && ValueMatches(existing->Value, property.Type)
                                ? existing->Value
                                : DefaultMaterialGraphValue(property);
            synchronized.push_back(std::move(binding));
        }
        definition.Properties = std::move(synchronized);

        std::erase_if(definition.Connections,
                      [&](const MaterialGraphConnection& connection)
                      {
                          const auto property = std::ranges::find(definition.Properties, connection.Input.Pin,
                                                                  &MaterialGraphPropertyBinding::Pin);
                          const auto node =
                              std::ranges::find(definition.Nodes, connection.Output.Node, &MaterialGraphValueNode::Id);
                          return connection.Input.Node != definition.OutputNode ||
                                 property == definition.Properties.end() || node == definition.Nodes.end() ||
                                 node->OutputPin != connection.Output.Pin || node->Type != property->Type;
                      });
    }

    MaterialGraphValueNode CreateMaterialGraphValueNode(const ShaderPropertyType type, MaterialPropertyValue value,
                                                        const Vector2 position)
    {
        if (!ValueMatches(value, type))
            throw std::invalid_argument("Material Graph value node does not match its declared type.");
        ValidateFiniteValue(value);
        MaterialGraphValueNode result;
        result.Id = AssetId::Generate();
        result.Name = type == ShaderPropertyType::Texture2D ? "Texture" : "Value";
        result.EditorPosition = position;
        result.Type = type;
        result.OutputPin = AssetId::Generate();
        result.Value = value;
        return result;
    }

    std::map<std::string, MaterialPropertyValue, std::less<>>
    EvaluateMaterialGraphProperties(const MaterialGraphDefinition& definition)
    {
        ValidateMaterialGraph(definition);
        std::map<std::string, MaterialPropertyValue, std::less<>> result;
        for (const auto& property : definition.Properties)
        {
            auto value = property.Value;
            const auto connection = std::ranges::find_if(
                definition.Connections, [&](const MaterialGraphConnection& candidate)
                { return candidate.Input.Node == definition.OutputNode && candidate.Input.Pin == property.Pin; });
            if (connection != definition.Connections.end())
            {
                const auto node =
                    std::ranges::find(definition.Nodes, connection->Output.Node, &MaterialGraphValueNode::Id);
                if (node == definition.Nodes.end() || node->OutputPin != connection->Output.Pin)
                    throw std::invalid_argument("Material Graph connection source is unavailable.");
                value = node->Value;
            }
            result.emplace(property.Name, value);
        }
        return result;
    }

    ShaderGraphDefinition ComposeMaterialGraphShader(const MaterialGraphDefinition& definition,
                                                     const ShaderGraphDefinition& shaderTemplate)
    {
        ValidateMaterialGraph(definition);
        ValidateShaderGraph(shaderTemplate);
        if (definition.Shader.Kind != MaterialShaderSourceKind::ShaderGraph)
            throw std::invalid_argument("Surface expressions require a Shader Graph template.");

        auto result = shaderTemplate;
        result.GeneratedAssetOwner = {};
        const auto values = EvaluateMaterialGraphProperties(definition);
        for (const auto& binding : definition.Properties)
        {
            auto parameter = result.Nodes.end();
            if (binding.Property)
                parameter = std::ranges::find(result.Nodes, binding.Property, &ShaderGraphNode::Id);
            if (parameter == result.Nodes.end())
                parameter = std::ranges::find(result.Nodes, binding.Name, &ShaderGraphNode::Symbol);
            if (parameter == result.Nodes.end() || parameter->Kind != ShaderGraphNodeKind::Parameter)
                throw std::invalid_argument("Material Graph template parameter is unavailable: " + binding.Name);
            const auto value = values.find(binding.Name);
            if (value == values.end())
                throw std::invalid_argument("Material Graph output value is unavailable: " + binding.Name);
            parameter->Value = ToShaderGraphValue(value->second);
        }

        const auto materialMaster =
            std::ranges::find(definition.SurfaceGraph.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
        auto templateMaster = std::ranges::find(result.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
        if (materialMaster == definition.SurfaceGraph.Nodes.end() || templateMaster == result.Nodes.end())
            throw std::invalid_argument("Material Graph composition requires material and template output nodes.");

        std::set<AssetId> nodeIds;
        std::set<AssetId> pinIds;
        std::set<AssetId> connectionIds;
        for (const auto& node : result.Nodes)
        {
            nodeIds.insert(node.Id);
            for (const auto& pin : node.Pins)
                pinIds.insert(pin.Id);
        }
        for (const auto& connection : result.Connections)
            connectionIds.insert(connection.Id);

        for (const auto& node : definition.SurfaceGraph.Nodes)
        {
            if (node.Kind == ShaderGraphNodeKind::Master)
                continue;
            if (!nodeIds.insert(node.Id).second)
                throw std::invalid_argument("Material Graph expression node collides with its Shader Graph template.");
            for (const auto& pin : node.Pins)
                if (!pinIds.insert(pin.Id).second)
                    throw std::invalid_argument(
                        "Material Graph expression pin collides with its Shader Graph template.");
            result.Nodes.push_back(node);
        }
        templateMaster = std::ranges::find(result.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);

        for (const auto& connection : definition.SurfaceGraph.Connections)
        {
            auto composed = connection;
            if (connection.Input.Node == materialMaster->Id)
            {
                const auto materialPin =
                    std::ranges::find(materialMaster->Pins, connection.Input.Pin, &ShaderGraphPin::Id);
                if (materialPin == materialMaster->Pins.end())
                    throw std::invalid_argument("Material Graph output connection targets an unavailable pin.");
                const auto targetPin =
                    std::ranges::find_if(templateMaster->Pins,
                                         [&](const ShaderGraphPin& pin)
                                         {
                                             return pin.Direction == ShaderGraphPinDirection::Input &&
                                                    pin.Type == materialPin->Type &&
                                                    EquivalentSurfacePin(pin.Name, materialPin->Name);
                                         });
                if (targetPin == templateMaster->Pins.end())
                    throw std::invalid_argument("Shader Graph template does not support material output: " +
                                                materialPin->Name);
                std::erase_if(result.Connections,
                              [&](const ShaderGraphConnection& existing)
                              {
                                  if (existing.Input != ShaderGraphEndpoint{templateMaster->Id, targetPin->Id})
                                      return false;
                                  connectionIds.erase(existing.Id);
                                  return true;
                              });
                composed.Input = {templateMaster->Id, targetPin->Id};
            }
            if (!connectionIds.insert(composed.Id).second)
                throw std::invalid_argument("Material Graph expression connection collides with its template.");
            result.Connections.push_back(std::move(composed));
        }

        for (const auto& keyword : definition.SurfaceGraph.Keywords)
        {
            const auto existing = std::ranges::find(result.Keywords, keyword.Name, &ShaderGraphKeyword::Name);
            if (existing == result.Keywords.end())
                result.Keywords.push_back(keyword);
            else if (*existing != keyword)
                throw std::invalid_argument("Material Graph keyword conflicts with its template: " + keyword.Name);
        }
        for (const auto& root : definition.SurfaceGraph.IncludeRoots)
            if (std::ranges::find(result.IncludeRoots, root) == result.IncludeRoots.end())
                result.IncludeRoots.push_back(root);

        PruneUnreachableShaderNodes(result);
        ValidateShaderGraph(result);
        return result;
    }

    MaterialGraphAsset::MaterialGraphAsset(MaterialGraphDefinition definition) : m_Definition(std::move(definition)) {}

    std::size_t MaterialGraphAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this) + m_Definition.Shader.Target.size();
        for (const auto& [name, option] : m_Definition.Shader.Keywords)
            result += name.size() + option.size();
        for (const auto& property : m_Definition.Properties)
            result += sizeof(property) + property.Name.size();
        for (const auto& node : m_Definition.Nodes)
            result += sizeof(node) + node.Name.size();
        result += m_Definition.Connections.size() * sizeof(MaterialGraphConnection);
        for (const auto& connection : m_Definition.Connections)
            result += connection.RoutingPoints.capacity() * sizeof(Vector2);
        result += m_Definition.SurfaceGraph.Nodes.size() * sizeof(ShaderGraphNode);
        result += m_Definition.SurfaceGraph.Connections.size() * sizeof(ShaderGraphConnection);
        for (const auto& connection : m_Definition.SurfaceGraph.Connections)
            result += connection.RoutingPoints.capacity() * sizeof(Vector2);
        for (const auto& annotation : m_Definition.Authoring.NodeAnnotations)
            result += sizeof(annotation) + annotation.Text.capacity();
        for (const auto& comment : m_Definition.Authoring.Comments)
            result += sizeof(comment) + comment.Title.capacity() + comment.Description.capacity() +
                      comment.Members.capacity() * sizeof(AssetId);
        return result;
    }

    Ref<MaterialGraphAsset> MaterialGraphAsset::Decode(const std::span<const std::byte> bytes)
    {
        try
        {
            if (bytes.size() > MaximumMaterialGraphBytes)
                throw std::invalid_argument("Material Graph cooked data exceeds its byte limit.");
            return CreateRef<MaterialGraphAsset>(DecodeDefinition(Json::from_cbor(Unsigned(bytes))));
        }
        catch (const std::exception& error)
        {
            throw std::invalid_argument(std::string("Material Graph asset decode failed: ") + error.what());
        }
    }

    std::vector<std::byte> MaterialGraphAsset::Encode(const MaterialGraphDefinition& definition)
    {
        ValidateMaterialGraph(definition);
        return Bytes(Json::to_cbor(EncodeDefinition(definition)));
    }

    MaterialGraphDefinition MaterialGraphAsset::DecodeSource(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > MaximumMaterialGraphBytes)
            throw std::invalid_argument("Material Graph source exceeds its byte limit.");
        return DecodeDefinition(Json::parse(Text(bytes)));
    }

    std::vector<std::byte> MaterialGraphAsset::EncodeSource(const MaterialGraphDefinition& definition)
    {
        ValidateMaterialGraph(definition);
        const auto text = EncodeDefinition(definition).dump(2) + '\n';
        return Bytes(text);
    }

    Ref<MaterialGraphAsset> MaterialGraphAsset::Error() { return CreateRef<MaterialGraphAsset>(); }

    MaterialInstanceAsset::MaterialInstanceAsset(MaterialInstanceDefinition definition)
        : m_Definition(std::move(definition))
    {
    }

    std::size_t MaterialInstanceAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& [name, value] : m_Definition.Properties)
        {
            (void)value;
            result += name.size() + sizeof(MaterialPropertyValue);
        }
        return result;
    }

    Ref<MaterialInstanceAsset> MaterialInstanceAsset::Decode(const std::span<const std::byte> bytes)
    {
        try
        {
            if (bytes.size() > MaximumMaterialGraphBytes)
                throw std::invalid_argument("Material Instance cooked data exceeds its byte limit.");
            return CreateRef<MaterialInstanceAsset>(DecodeInstanceDefinition(Json::from_cbor(Unsigned(bytes))));
        }
        catch (const std::exception& error)
        {
            throw std::invalid_argument(std::string("Material Instance asset decode failed: ") + error.what());
        }
    }

    std::vector<std::byte> MaterialInstanceAsset::Encode(const MaterialInstanceDefinition& definition)
    {
        ValidateMaterialInstance(definition);
        return Bytes(Json::to_cbor(EncodeInstanceDefinition(definition)));
    }

    MaterialInstanceDefinition MaterialInstanceAsset::DecodeSource(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > MaximumMaterialGraphBytes)
            throw std::invalid_argument("Material Instance source exceeds its byte limit.");
        return DecodeInstanceDefinition(Json::parse(Text(bytes)));
    }

    std::vector<std::byte> MaterialInstanceAsset::EncodeSource(const MaterialInstanceDefinition& definition)
    {
        ValidateMaterialInstance(definition);
        return Bytes(EncodeInstanceDefinition(definition).dump(2) + '\n');
    }

    Ref<MaterialInstanceAsset> MaterialInstanceAsset::Error() { return CreateRef<MaterialInstanceAsset>(); }

    void ValidateMaterialGraph(const MaterialGraphDefinition& definition)
    {
        if (definition.SchemaVersion != MaterialGraphSourceSchemaVersion ||
            definition.Shader.Kind > MaterialShaderSourceKind::ShaderGraph ||
            definition.Properties.size() > MaximumMaterialProperties ||
            definition.Nodes.size() > MaximumMaterialGraphNodes ||
            definition.Connections.size() > MaximumMaterialGraphConnections || !definition.OutputNode ||
            definition.Shader.Keywords.size() > MaximumMaterialKeywords ||
            definition.Surface.AlphaMode > MaterialAlphaMode::AlphaHoldout ||
            !std::isfinite(definition.Surface.AlphaCutoff) || definition.Surface.AlphaCutoff < 0.0F ||
            definition.Surface.AlphaCutoff > 1.0F || !std::isfinite(definition.EmissiveGIIntensity) ||
            definition.EmissiveGIIntensity < 0.0F || definition.EmissiveGIIntensity > 100'000.0F ||
            !Math::IsFinite(definition.OutputPosition))
            throw std::invalid_argument("Material Graph definition is invalid or exceeds a portable bound.");
        if (!definition.Shader.Asset ||
            (definition.Shader.Kind == MaterialShaderSourceKind::ShaderGraph && !ValidTarget(definition.Shader.Target)))
            throw std::invalid_argument("Material Graph requires a valid shader or Shader Graph target.");
        if (definition.Shader.Kind != MaterialShaderSourceKind::ShaderGraph &&
            (!definition.Shader.Keywords.empty() || definition.Shader.Target != "default"))
            throw std::invalid_argument("Only Shader Graph references may select targets or keywords.");
        for (const auto& [name, option] : definition.Shader.Keywords)
            if (!ValidIdentifier(name) || (option != "true" && option != "false" && !ValidIdentifier(option)))
                throw std::invalid_argument("Material Graph keyword selection is invalid.");
        ValidateShaderGraph(definition.SurfaceGraph);

        std::set<AssetId> propertyIds;
        std::set<std::string, std::less<>> propertyNames;
        std::set<AssetId> identities{definition.OutputNode};
        for (const auto& property : definition.Properties)
        {
            if (!ValidIdentifier(property.Name) || !propertyNames.insert(property.Name).second ||
                property.Type > ShaderPropertyType::Texture2D || !property.Pin ||
                !identities.insert(property.Pin).second || !ValueMatches(property.Value, property.Type) ||
                (property.Property && !propertyIds.insert(property.Property).second))
                throw std::invalid_argument("Material Graph property bindings must have unique valid identities.");
            ValidateFiniteValue(property.Value);
        }

        std::set<AssetId> nodeIds;
        for (const auto& node : definition.Nodes)
        {
            if (!node.Id || !nodeIds.insert(node.Id).second || !identities.insert(node.Id).second ||
                node.Name.empty() || node.Name.size() > 128 || node.Type > ShaderPropertyType::Texture2D ||
                !node.OutputPin || !identities.insert(node.OutputPin).second || !ValueMatches(node.Value, node.Type) ||
                !Math::IsFinite(node.EditorPosition))
                throw std::invalid_argument("Material Graph value node is invalid or duplicated.");
            ValidateFiniteValue(node.Value);
        }

        std::set<AssetId> connectionIds;
        std::set<AssetId> connectedInputs;
        for (const auto& connection : definition.Connections)
        {
            if (!connection.Id || !connectionIds.insert(connection.Id).second ||
                connection.Input.Node != definition.OutputNode ||
                !connectedInputs.insert(connection.Input.Pin).second ||
                connection.RoutingPoints.size() > MaximumGraphRoutingPointsPerConnection ||
                std::ranges::any_of(connection.RoutingPoints,
                                    [](const Vector2 point) { return !Math::IsFinite(point); }))
                throw std::invalid_argument("Material Graph connections require unique output inputs.");
            const auto property =
                std::ranges::find(definition.Properties, connection.Input.Pin, &MaterialGraphPropertyBinding::Pin);
            const auto node = std::ranges::find(definition.Nodes, connection.Output.Node, &MaterialGraphValueNode::Id);
            if (property == definition.Properties.end() || node == definition.Nodes.end() ||
                node->OutputPin != connection.Output.Pin || node->Type != property->Type)
                throw std::invalid_argument("Material Graph connection endpoints or value types are incompatible.");
        }

        std::vector<AssetId> authoringNodeIds{definition.OutputNode};
        authoringNodeIds.reserve(definition.Nodes.size() + definition.SurfaceGraph.Nodes.size() + 1U);
        std::ranges::transform(definition.Nodes, std::back_inserter(authoringNodeIds), &MaterialGraphValueNode::Id);
        std::ranges::transform(definition.SurfaceGraph.Nodes, std::back_inserter(authoringNodeIds),
                               &ShaderGraphNode::Id);
        ValidateGraphAuthoringMetadata(definition.Authoring, authoringNodeIds);
    }

    std::vector<MaterialGraphDiagnostic>
    ValidateMaterialGraphAgainstInterface(const MaterialGraphDefinition& definition,
                                          const ShaderInterfaceDefinition& interfaceDefinition)
    {
        ValidateMaterialGraph(definition);
        if (interfaceDefinition.SchemaVersion != 1 || interfaceDefinition.AbiVersion == 0)
            throw std::invalid_argument("Shader interface schema or ABI version is unsupported.");
        std::vector<MaterialGraphDiagnostic> diagnostics;
        for (const auto& binding : definition.Properties)
        {
            auto found = interfaceDefinition.Properties.end();
            if (binding.Property)
                found =
                    std::ranges::find(interfaceDefinition.Properties, binding.Property, &ShaderPropertyDefinition::Id);
            if (found == interfaceDefinition.Properties.end())
                found =
                    std::ranges::find(interfaceDefinition.Properties, binding.Name, &ShaderPropertyDefinition::Name);
            if (found == interfaceDefinition.Properties.end())
                diagnostics.push_back({MaterialGraphDiagnosticSeverity::Error,
                                       "MAT1001",
                                       "Property is not exposed by the selected shader: " + binding.Name,
                                       binding.Property,
                                       {},
                                       binding.Pin});
            else if (!ValueMatches(binding.Value, found->Type))
                diagnostics.push_back({MaterialGraphDiagnosticSeverity::Error,
                                       "MAT1002",
                                       "Property type does not match the selected shader: " + binding.Name,
                                       binding.Property,
                                       {},
                                       binding.Pin});
            else if (binding.Property && found->Name != binding.Name)
                diagnostics.push_back({MaterialGraphDiagnosticSeverity::Info,
                                       "MAT1003",
                                       "Property was renamed to " + found->Name + "; its stable identity was retained.",
                                       binding.Property,
                                       {},
                                       binding.Pin});
        }
        for (const auto& node : definition.Nodes)
            if (std::ranges::none_of(definition.Connections, [&](const MaterialGraphConnection& connection)
                                     { return connection.Output.Node == node.Id; }))
                diagnostics.push_back({MaterialGraphDiagnosticSeverity::Warning,
                                       "MAT1004",
                                       "Value node is not connected to Material Output: " + node.Name,
                                       {},
                                       node.Id,
                                       node.OutputPin});
        return diagnostics;
    }

    MaterialAssetDefinition
    BakeMaterialGraph(const MaterialGraphDefinition& definition,
                      const std::function<AssetId(const MaterialShaderReference&)>& resolveShader)
    {
        ValidateMaterialGraph(definition);
        if (!resolveShader)
            throw std::invalid_argument("Material Graph baking requires a shader resolver.");
        MaterialAssetDefinition result;
        result.Shader = resolveShader(definition.Shader);
        if (!result.Shader)
            throw std::runtime_error("Material Graph selected a shader target that is not published.");
        result.Surface = definition.Surface;
        result.ContributeEmissionToGI = definition.ContributeEmissionToGI;
        result.EmissiveGIIntensity = definition.EmissiveGIIntensity;
        result.Properties = EvaluateMaterialGraphProperties(definition);
        return result;
    }

    void ValidateMaterialInstance(const MaterialInstanceDefinition& definition)
    {
        if (definition.SchemaVersion != MaterialInstanceSourceSchemaVersion || !definition.Parent ||
            definition.Properties.size() > MaximumMaterialProperties)
            throw std::invalid_argument("Material Instance definition is invalid or exceeds a portable bound.");
        if (definition.Surface && (definition.Surface->AlphaMode > MaterialAlphaMode::AlphaHoldout ||
                                   !std::isfinite(definition.Surface->AlphaCutoff) ||
                                   definition.Surface->AlphaCutoff < 0.0F || definition.Surface->AlphaCutoff > 1.0F))
            throw std::invalid_argument("Material Instance surface override is invalid.");
        if (definition.EmissiveGIIntensity &&
            (!std::isfinite(*definition.EmissiveGIIntensity) || *definition.EmissiveGIIntensity < 0.0F ||
             *definition.EmissiveGIIntensity > 100'000.0F))
            throw std::invalid_argument("Material Instance emissive intensity override is invalid.");
        for (const auto& [name, value] : definition.Properties)
        {
            if (!ValidIdentifier(name))
                throw std::invalid_argument("Material Instance property names must be valid shader identifiers.");
            ValidateFiniteValue(value);
        }
        if (definition.KeywordOverrides.size() > 16)
            throw std::invalid_argument("Material Instance static parameter overrides exceed their portable bound.");
        for (const auto& [name, value] : definition.KeywordOverrides)
            if (!ValidIdentifier(name) || (value != "true" && value != "false" && !ValidIdentifier(value)))
                throw std::invalid_argument("Material Instance static parameter override is invalid.");
    }

    MaterialAssetDefinition BakeMaterialInstance(const MaterialAssetDefinition& parent,
                                                 const MaterialInstanceDefinition& instance)
    {
        ValidateMaterialInstance(instance);
        if (!parent.Shader)
            throw std::invalid_argument("Material Instance parent has no resolved shader.");
        auto result = parent;
        if (instance.Surface)
            result.Surface = *instance.Surface;
        if (instance.ContributeEmissionToGI)
            result.ContributeEmissionToGI = *instance.ContributeEmissionToGI;
        if (instance.EmissiveGIIntensity)
            result.EmissiveGIIntensity = *instance.EmissiveGIIntensity;
        for (const auto& [name, value] : instance.Properties)
        {
            const auto inherited = result.Properties.find(name);
            if (inherited == result.Properties.end())
                throw std::invalid_argument("Material Instance property is not exposed by its parent: " + name);
            if (inherited->second.index() != value.index())
                throw std::invalid_argument("Material Instance property override changes the inherited value type: " +
                                            name);
            result.Properties.insert_or_assign(name, value);
        }
        return result;
    }

    AssetImporterRegistration CreateMaterialGraphAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.MaterialGraph";
        result.Version = 5;
        result.Type = MaterialGraphAsset::StaticType();
        result.Extensions = {".keirematerialgraph"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            if (!context.Asset || !context.ResolveSubAssetId)
                throw std::invalid_argument("Material Graph import requires a stable asset and subasset resolver.");
            const auto definition = MaterialGraphAsset::DecodeSource(bytes);
            if (HasMaterialSurfaceExpressions(definition))
            {
                if (definition.Shader.Kind != MaterialShaderSourceKind::ShaderGraph || context.ProjectRoot.empty() ||
                    context.SourceRoot.empty() || !context.ReadProjectFile || !context.ResolveAssetSource)
                    throw std::invalid_argument(
                        "Material surface expressions require a Shader Graph and complete project resolvers.");
                const auto source = context.ResolveAssetSource(definition.Shader.Asset);
                if (!source || source->Type != ShaderGraphAsset::StaticType())
                    throw std::runtime_error(
                        "Material Graph template is not present in the Shader Graph source index.");
                const auto sourcePrefix = std::filesystem::relative(context.SourceRoot, context.ProjectRoot);
                const auto shaderTemplate =
                    ShaderGraphAsset::DecodeSource(context.ReadProjectFile(sourcePrefix / source->RelativePath));
                const auto composed = ComposeMaterialGraphShader(definition, shaderTemplate);
                const auto graphImporter = CreateShaderGraphAssetImporter();
                if (!graphImporter.ContextualImport)
                    throw std::logic_error("Material Graph requires the contextual Shader Graph importer.");
                auto graphContext = context;
                graphContext.ResolveSubAssetId = [resolve = context.ResolveSubAssetId](const std::string_view key)
                { return resolve(key == "material/default" ? key : "material-graph/" + std::string(key)); };
                auto output = graphImporter.ContextualImport(graphContext, ShaderGraphAsset::EncodeSource(composed));
                output.Bytes = MaterialGraphAsset::Encode(definition);
                output.AssetDependencies.push_back(definition.Shader.Asset);

                ShaderGraphInstanceDefinition selection;
                selection.Parent = context.Asset;
                selection.KeywordOverrides = definition.Shader.Keywords;
                const std::array ancestry{selection};
                const auto resolved = ResolveShaderGraphInstance(composed, ancestry);
                const auto selectedShader =
                    context.ResolveSubAssetId(MaterialGraphVariantKey(definition.Shader.Target, resolved.Keywords));
                if (std::ranges::none_of(
                        output.SubAssets, [selectedShader](const AssetGeneratedSubAsset& subAsset)
                        { return subAsset.Id == selectedShader && subAsset.Type == ShaderAsset::StaticType(); }))
                    throw std::runtime_error("Composed Material Graph selected an unpublished shader variant.");
                const auto materialSubAsset = std::ranges::find_if(
                    output.SubAssets, [](const AssetGeneratedSubAsset& subAsset)
                    { return subAsset.Type == MaterialAsset::StaticType() && subAsset.Key == "material/default"; });
                if (materialSubAsset == output.SubAssets.end())
                    throw std::logic_error("Composed Material Graph did not publish its runtime material.");
                auto material = MaterialAsset::Decode(materialSubAsset->Bytes)->Definition();
                material.Shader = selectedShader;
                material.Surface = definition.Surface;
                material.ContributeEmissionToGI = definition.ContributeEmissionToGI;
                material.EmissiveGIIntensity = definition.EmissiveGIIntensity;
                materialSubAsset->Name = "Runtime Material";
                materialSubAsset->Bytes = MaterialAsset::Encode(material);
                materialSubAsset->AssetDependencies.push_back(definition.Shader.Asset);
                materialSubAsset->AssetDependencies.push_back(selectedShader);
                std::ranges::sort(materialSubAsset->AssetDependencies);
                materialSubAsset->AssetDependencies.erase(
                    std::unique(materialSubAsset->AssetDependencies.begin(), materialSubAsset->AssetDependencies.end()),
                    materialSubAsset->AssetDependencies.end());
                std::ranges::sort(output.AssetDependencies);
                output.AssetDependencies.erase(
                    std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                    output.AssetDependencies.end());
                return output;
            }
            const auto material = BakeMaterialGraph(
                definition,
                [&context](const MaterialShaderReference& shader)
                {
                    if (shader.Kind != MaterialShaderSourceKind::ShaderGraph)
                        return shader.Asset;
                    if (!context.ResolveSubAssetIdFor)
                        throw std::invalid_argument(
                            "Shader Graph material bindings require a cross-asset subasset resolver.");
                    const auto variant = ResolveShaderGraphVariant(context, shader);
                    return context.ResolveSubAssetIdFor(
                        variant.Owner, MakeShaderGraphVariantSubAssetKey(shader.Target, variant.Keywords));
                });
            AssetImportOutput output;
            output.Bytes = MaterialGraphAsset::Encode(definition);
            output.AssetDependencies.push_back(definition.Shader.Asset);
            if (material.Shader != definition.Shader.Asset)
                output.AssetDependencies.push_back(material.Shader);
            for (const auto& [name, value] : material.Properties)
            {
                (void)name;
                if (const auto* texture = std::get_if<AssetId>(&value); texture && *texture)
                    output.AssetDependencies.push_back(*texture);
            }
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
            output.SubAssets.push_back({context.ResolveSubAssetId("material/default"), MaterialAsset::StaticType(),
                                        "material/default", "Runtime Material", MaterialAsset::Encode(material),
                                        output.AssetDependencies});
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateMaterialGraphAssetDecoder()
    {
        return {MaterialGraphAsset::StaticType(), MaterialGraphAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return MaterialGraphAsset::Decode(bytes); }};
    }

    AssetImporterRegistration CreateMaterialInstanceAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.MaterialInstance";
        result.Version = 2;
        result.Type = MaterialInstanceAsset::StaticType();
        result.Extensions = {".keirematerialinstance"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            if (!context.Asset || context.ProjectRoot.empty() || context.SourceRoot.empty() ||
                !context.ReadProjectFile || !context.ResolveAssetSource || !context.ResolveSubAssetId ||
                !context.ResolveSubAssetIdFor)
                throw std::invalid_argument(
                    "Material Instance import requires source and stable cross-asset resolvers.");

            const auto definition = MaterialInstanceAsset::DecodeSource(bytes);
            AssetImportOutput output;
            output.Bytes = MaterialInstanceAsset::Encode(definition);
            std::vector<MaterialInstanceDefinition> ancestry{definition};
            std::set<AssetId> visited{context.Asset};
            const auto sourcePrefix = std::filesystem::relative(context.SourceRoot, context.ProjectRoot);
            MaterialAssetDefinition material;
            std::optional<ShaderGraphDefinition> instanceVariantGraph;
            AssetId instanceVariantOwner;
            std::string instanceVariantTarget = "default";
            std::map<std::string, std::string, std::less<>> instanceVariantDefaults;
            AssetId parent = definition.Parent;
            for (std::size_t depth = 0; depth < MaximumMaterialInstanceDepth && parent; ++depth)
            {
                if (!visited.insert(parent).second)
                    throw std::invalid_argument("Material Instance parent chain contains a cycle.");
                output.AssetDependencies.push_back(parent);
                const auto source = context.ResolveAssetSource(parent);
                if (!source)
                    throw std::runtime_error("Material Instance parent is not present in the source index: " +
                                             parent.ToString());
                const auto parentBytes = context.ReadProjectFile(sourcePrefix / source->RelativePath);
                if (source->Type == MaterialInstanceAsset::StaticType())
                {
                    auto parentInstance = MaterialInstanceAsset::DecodeSource(parentBytes);
                    parent = parentInstance.Parent;
                    ancestry.push_back(std::move(parentInstance));
                    continue;
                }

                const auto resolveShader = [&](const MaterialShaderReference& shader)
                {
                    if (shader.Kind != MaterialShaderSourceKind::ShaderGraph)
                        return shader.Asset;
                    const auto variant = ResolveShaderGraphVariant(context, shader);
                    return context.ResolveSubAssetIdFor(
                        variant.Owner, MakeShaderGraphVariantSubAssetKey(shader.Target, variant.Keywords));
                };
                if (source->Type == MaterialGraphAsset::StaticType())
                {
                    const auto graph = MaterialGraphAsset::DecodeSource(parentBytes);
                    if (HasMaterialSurfaceExpressions(graph))
                    {
                        auto parentContext = context;
                        parentContext.Asset = parent;
                        parentContext.RelativePath = source->RelativePath;
                        parentContext.SourcePath = context.SourceRoot / source->RelativePath;
                        parentContext.ResolveSubAssetId =
                            [resolve = context.ResolveSubAssetIdFor, parent](const std::string_view key)
                        { return resolve(parent, key); };
                        const auto imported =
                            CreateMaterialGraphAssetImporter().ContextualImport(parentContext, parentBytes);
                        const auto runtimeMaterial =
                            std::ranges::find_if(imported.SubAssets,
                                                 [](const AssetGeneratedSubAsset& subAsset)
                                                 {
                                                     return subAsset.Type == MaterialAsset::StaticType() &&
                                                            subAsset.Key == "material/default";
                                                 });
                        if (runtimeMaterial == imported.SubAssets.end())
                            throw std::runtime_error("Material Instance parent did not publish a runtime material.");
                        material = MaterialAsset::Decode(runtimeMaterial->Bytes)->Definition();
                        output.AssetDependencies.insert(output.AssetDependencies.end(),
                                                        imported.AssetDependencies.begin(),
                                                        imported.AssetDependencies.end());
                        const auto templateSource = context.ResolveAssetSource(graph.Shader.Asset);
                        if (!templateSource || templateSource->Type != ShaderGraphAsset::StaticType())
                            throw std::runtime_error("Material Instance parent Shader Graph template is unavailable.");
                        const auto shaderTemplate = ShaderGraphAsset::DecodeSource(
                            context.ReadProjectFile(sourcePrefix / templateSource->RelativePath));
                        instanceVariantGraph = ComposeMaterialGraphShader(graph, shaderTemplate);
                        instanceVariantOwner = parent;
                        instanceVariantTarget = graph.Shader.Target;
                        instanceVariantDefaults = graph.Shader.Keywords;
                    }
                    else
                        material = BakeMaterialGraph(graph, resolveShader);
                    output.AssetDependencies.push_back(graph.Shader.Asset);
                    break;
                }
                if (source->Type == MaterialAsset::StaticType())
                {
                    const auto authoring = MaterialAsset::DecodeAuthoringSource(parentBytes);
                    material.Shader = resolveShader(authoring.Shader);
                    material.Surface = authoring.Surface;
                    material.ContributeEmissionToGI = authoring.ContributeEmissionToGI;
                    material.EmissiveGIIntensity = authoring.EmissiveGIIntensity;
                    material.Properties = authoring.Properties;
                    if (authoring.Shader.Asset)
                        output.AssetDependencies.push_back(authoring.Shader.Asset);
                    break;
                }
                throw std::invalid_argument(
                    "Material Instance parent must be a Material, Material Graph, or another Material Instance.");
            }
            if (!material.Shader)
                throw std::invalid_argument(
                    "Material Instance parent chain exceeds 16 entries or has no material root.");

            std::ranges::reverse(ancestry);
            for (const auto& instance : ancestry)
                material = BakeMaterialInstance(material, instance);
            if (instanceVariantGraph)
            {
                ShaderGraphInstanceDefinition selection;
                selection.Parent = instanceVariantOwner;
                selection.KeywordOverrides = std::move(instanceVariantDefaults);
                for (const auto& instance : ancestry)
                    for (const auto& [name, value] : instance.KeywordOverrides)
                        selection.KeywordOverrides.insert_or_assign(name, value);
                const std::array selections{selection};
                const auto resolved = ResolveShaderGraphInstance(*instanceVariantGraph, selections);
                material.Shader = context.ResolveSubAssetIdFor(
                    instanceVariantOwner, MaterialGraphVariantKey(instanceVariantTarget, resolved.Keywords));
                if (!material.Shader)
                    throw std::runtime_error("Material Instance selected an unavailable static-parameter variant.");
            }
            output.AssetDependencies.push_back(material.Shader);
            for (const auto& [name, value] : material.Properties)
            {
                (void)name;
                if (const auto* asset = std::get_if<AssetId>(&value); asset && *asset)
                    output.AssetDependencies.push_back(*asset);
            }
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
            output.SubAssets.push_back({context.ResolveSubAssetId("material/default"), MaterialAsset::StaticType(),
                                        "material/default", "Runtime Material", MaterialAsset::Encode(material),
                                        output.AssetDependencies});
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateMaterialInstanceAssetDecoder()
    {
        return {MaterialInstanceAsset::StaticType(), MaterialInstanceAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return MaterialInstanceAsset::Decode(bytes); }};
    }
} // namespace Keire
