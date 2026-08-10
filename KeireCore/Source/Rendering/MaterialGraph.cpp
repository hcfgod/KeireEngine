#include "Keire/Rendering/MaterialGraph.h"
#include "KeireInternal/Rendering/MaterialGraphManifest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <deque>
#include <iomanip>
#include <limits>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumGraphNodes = 1024;
        constexpr std::size_t MaximumGraphConnections = 4096;
        constexpr std::size_t MaximumGraphKeywords = 16;
        constexpr std::size_t MaximumGraphProperties = 80;
        constexpr std::size_t MaximumGraphPinsPerNode = 32;
        constexpr std::size_t MaximumGraphIncludeRoots = 16;
        constexpr std::size_t MaximumGraphText = 128;
        constexpr std::size_t MaximumGraphPath = 1024;
        constexpr std::size_t MaximumGraphAssetBytes = std::size_t{32} * 1024U * 1024U;

        struct EndpointHash final
        {
            [[nodiscard]] std::size_t operator()(const MaterialGraphEndpoint& value) const noexcept
            {
                auto result = std::hash<AssetId>{}(value.Node);
                result ^= std::hash<AssetId>{}(value.Pin) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
                return result;
            }
        };

        struct Expression final
        {
            std::string Code;
            MaterialGraphValueType Type = MaterialGraphValueType::Scalar;
        };

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

        [[nodiscard]] std::vector<std::byte> TextBytes(const std::string_view value)
        {
            const auto bytes = std::as_bytes(std::span(value.data(), value.size()));
            return {bytes.begin(), bytes.end()};
        }

        [[nodiscard]] bool ValidIdentifier(const std::string_view value)
        {
            if (value.empty() || value.size() > MaximumGraphText ||
                !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
                return false;
            return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_'; });
        }

        [[nodiscard]] bool SafeRelativePath(const std::filesystem::path& value)
        {
            if (value.empty() || value.is_absolute())
                return false;
            const auto normalized = value.lexically_normal().generic_string();
            return !normalized.empty() && normalized.size() <= MaximumGraphPath && normalized != "." &&
                   !normalized.starts_with("..") && normalized.find(':') == std::string::npos;
        }

        [[nodiscard]] AssetId StableMigratedPinId(const AssetId node, const std::string_view name,
                                                  const MaterialGraphPinDirection direction) noexcept
        {
            const auto hash = [name, direction](std::uint64_t value)
            {
                value ^= static_cast<std::uint8_t>(direction);
                value *= 1099511628211ULL;
                for (const char input : name)
                {
                    const auto character = static_cast<unsigned char>(input);
                    value ^= character;
                    value *= 1099511628211ULL;
                }
                return value;
            };
            auto high = hash(node.High() ^ 0x4d4750494e484947ULL);
            auto low = hash(node.Low() ^ 0x4d4750494e4c4f57ULL);
            if ((high | low) == 0U)
                low = 1U;
            return {high, low};
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

        [[nodiscard]] MaterialGraphValue DecodeValue(const Json& value, const MaterialGraphValueType type)
        {
            const auto finite = [](const float candidate)
            {
                if (!std::isfinite(candidate))
                    throw std::invalid_argument("Material Graph values must be finite.");
                return candidate;
            };
            if (type == MaterialGraphValueType::Texture2D)
                return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
            if (type == MaterialGraphValueType::MaterialAttributes)
                return MaterialGraphMaterialAttributesValue{};
            if (type == MaterialGraphValueType::Bsdf)
                return MaterialGraphBsdfValue{};
            if (type == MaterialGraphValueType::Scalar)
                return finite(value.get<float>());
            const std::size_t count = type == MaterialGraphValueType::Vector2   ? 2U
                                      : type == MaterialGraphValueType::Vector3 ? 3U
                                                                                : 4U;
            if (!value.is_array() || value.size() != count)
                throw std::invalid_argument("Material Graph vector value has the wrong component count.");
            std::array<float, 4> components{};
            for (std::size_t index = 0; index < count; ++index)
                components[index] = finite(value[index].get<float>());
            if (type == MaterialGraphValueType::Vector2)
                return Vector2{components[0], components[1]};
            if (type == MaterialGraphValueType::Vector3)
                return Vector3{components[0], components[1], components[2]};
            if (type == MaterialGraphValueType::Color)
                return Color{components[0], components[1], components[2], components[3]};
            return Vector4{components[0], components[1], components[2], components[3]};
        }

        [[nodiscard]] Json EncodeGraphJson(const MaterialGraphDefinition& definition)
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
                nodes.push_back({{"id", node.Id.ToString()},
                                 {"typeId", node.TypeId.empty() ? MaterialGraphNodeTypeId(node.Kind) : node.TypeId},
                                 {"kind", static_cast<std::uint8_t>(node.Kind)},
                                 {"name", node.Name},
                                 {"position", {node.EditorPosition.X, node.EditorPosition.Y}},
                                 {"valueType", static_cast<std::uint8_t>(node.ValueType)},
                                 {"value", EncodeValue(node.Value)},
                                 {"textureSemantic", static_cast<std::uint8_t>(node.TextureSemantic)},
                                 {"symbol", node.Symbol},
                                 {"include", node.Include.generic_string()},
                                 {"function", node.Function},
                                 {"parameterMetadata", std::move(metadata)},
                                 {"pins", std::move(pins)}});
            }
            Json connections = Json::array();
            for (const auto& connection : definition.Connections)
                connections.push_back(
                    {{"id", connection.Id.ToString()},
                     {"output", {connection.Output.Node.ToString(), connection.Output.Pin.ToString()}},
                     {"input", {connection.Input.Node.ToString(), connection.Input.Pin.ToString()}}});
            Json keywords = Json::array();
            for (const auto& keyword : definition.Keywords)
                keywords.push_back({{"name", keyword.Name},
                                    {"options", keyword.Options},
                                    {"default", keyword.DefaultOption},
                                    {"exposed", keyword.Exposed}});
            Json roots = Json::array();
            for (const auto& root : definition.IncludeRoots)
                roots.push_back(root.generic_string());
            return {{"schemaVersion", MaterialGraphSourceSchemaVersion},
                    {"output", static_cast<std::uint8_t>(definition.Output)},
                    {"nodes", std::move(nodes)},
                    {"connections", std::move(connections)},
                    {"keywords", std::move(keywords)},
                    {"includeRoots", std::move(roots)}};
        }

        [[nodiscard]] MaterialGraphDefinition DecodeGraphJson(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Material Graph data must be an object.");
            const auto sourceSchemaVersion = source.value("schemaVersion", 0U);
            if (sourceSchemaVersion == 0U)
                throw std::invalid_argument("Material Graph schema version is missing or unsupported.");
            if (sourceSchemaVersion > MaterialGraphSourceSchemaVersion)
                throw std::invalid_argument("Material Graph schema version " + std::to_string(sourceSchemaVersion) +
                                            " is newer than the supported version " +
                                            std::to_string(MaterialGraphSourceSchemaVersion) + '.');
            const auto& nodes = source.at("nodes");
            const auto& connections = source.at("connections");
            const auto& keywords = source.value("keywords", Json::array());
            const auto& includeRoots = source.value("includeRoots", Json::array({"Assets"}));
            if (!nodes.is_array() || nodes.empty() || nodes.size() > MaximumGraphNodes || !connections.is_array() ||
                connections.size() > MaximumGraphConnections || !keywords.is_array() ||
                keywords.size() > MaximumGraphKeywords || !includeRoots.is_array() || includeRoots.empty() ||
                includeRoots.size() > MaximumGraphIncludeRoots)
                throw std::invalid_argument("Material Graph source collections exceed their bounds.");
            MaterialGraphDefinition result;
            result.SchemaVersion = sourceSchemaVersion;
            result.Output = static_cast<MaterialGraphOutput>(source.value("output", static_cast<std::uint8_t>(0)));
            result.IncludeRoots.clear();
            for (const auto& root : includeRoots)
                result.IncludeRoots.emplace_back(root.get<std::string>());
            for (const auto& encoded : nodes)
            {
                MaterialGraphNode node;
                node.Id = AssetId::Parse(encoded.at("id").get<std::string>());
                if (sourceSchemaVersion >= 2U)
                {
                    node.TypeId = encoded.at("typeId").get<std::string>();
                    const auto* descriptor = FindMaterialGraphNodeDescriptor(node.TypeId);
                    if (!descriptor)
                        throw std::invalid_argument("Material Graph contains an unknown node type ID: " + node.TypeId +
                                                    '.');
                    node.Kind = descriptor->Kind;
                    if (encoded.contains("kind") &&
                        static_cast<MaterialGraphNodeKind>(encoded.at("kind").get<std::uint8_t>()) != node.Kind)
                        throw std::invalid_argument("Material Graph node type ID does not match its legacy kind.");
                }
                else
                {
                    node.Kind = static_cast<MaterialGraphNodeKind>(encoded.at("kind").get<std::uint8_t>());
                    node.TypeId = MaterialGraphNodeTypeId(node.Kind);
                    if (node.TypeId.empty())
                        throw std::invalid_argument("Material Graph contains an unknown legacy node kind.");
                }
                node.Name = encoded.value("name", std::string{});
                const auto& position = encoded.at("position");
                node.EditorPosition = {position.at(0).get<float>(), position.at(1).get<float>()};
                node.ValueType =
                    static_cast<MaterialGraphValueType>(encoded.value("valueType", static_cast<std::uint8_t>(0)));
                node.Value = DecodeValue(encoded.at("value"), node.ValueType);
                node.TextureSemantic = static_cast<ShaderTextureSemantic>(
                    encoded.value("textureSemantic", static_cast<std::uint8_t>(ShaderTextureSemantic::Generic)));
                node.Symbol = encoded.value("symbol", std::string{});
                node.Include = encoded.value("include", std::string{});
                node.Function = encoded.value("function", std::string{});
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
                if (!pins.is_array() || pins.empty() || pins.size() > MaximumGraphPinsPerNode)
                    throw std::invalid_argument("Material Graph node pins exceed their bounds.");
                for (const auto& encodedPin : pins)
                {
                    MaterialGraphPin pin;
                    pin.Id = AssetId::Parse(encodedPin.at("id").get<std::string>());
                    pin.Name = encodedPin.at("name").get<std::string>();
                    pin.Type = static_cast<MaterialGraphValueType>(encodedPin.at("type").get<std::uint8_t>());
                    pin.Direction =
                        static_cast<MaterialGraphPinDirection>(encodedPin.at("direction").get<std::uint8_t>());
                    pin.DefaultValue = DecodeValue(encodedPin.at("default"), pin.Type);
                    node.Pins.push_back(std::move(pin));
                }
                result.Nodes.push_back(std::move(node));
            }
            for (const auto& encoded : connections)
            {
                const auto& output = encoded.at("output");
                const auto& input = encoded.at("input");
                result.Connections.push_back(
                    {AssetId::Parse(encoded.at("id").get<std::string>()),
                     {AssetId::Parse(output.at(0).get<std::string>()), AssetId::Parse(output.at(1).get<std::string>())},
                     {AssetId::Parse(input.at(0).get<std::string>()), AssetId::Parse(input.at(1).get<std::string>())}});
            }
            for (const auto& encoded : keywords)
                result.Keywords.push_back({encoded.at("name").get<std::string>(),
                                           encoded.value("options", std::vector<std::string>{}),
                                           encoded.value("default", std::string{}), encoded.value("exposed", true)});
            {
                const auto canonicalDefinition = CreateDefaultMaterialGraph(result.Output);
                const auto& canonicalMaster = canonicalDefinition.Nodes.front();
                const auto master =
                    std::ranges::find(result.Nodes, MaterialGraphNodeKind::Master, &MaterialGraphNode::Kind);
                if (master != result.Nodes.end())
                {
                    std::vector<MaterialGraphPin> migratedPins;
                    migratedPins.reserve(canonicalMaster.Pins.size());
                    for (const auto& expected : canonicalMaster.Pins)
                    {
                        const auto existing = std::ranges::find_if(
                            master->Pins, [&](const MaterialGraphPin& pin)
                            { return pin.Name == expected.Name && pin.Direction == expected.Direction; });
                        if (existing != master->Pins.end())
                            migratedPins.push_back(*existing);
                        else
                        {
                            auto migrated = expected;
                            migrated.Id = StableMigratedPinId(master->Id, expected.Name, expected.Direction);
                            migratedPins.push_back(std::move(migrated));
                        }
                    }
                    master->Pins = std::move(migratedPins);
                }
                if (sourceSchemaVersion < 2U)
                    for (auto& node : result.Nodes)
                    {
                        if (node.Kind == MaterialGraphNodeKind::Master || node.Kind == MaterialGraphNodeKind::Custom)
                            continue;
                        const auto canonical = CreateMaterialGraphNode(node.Kind, node.ValueType);
                        std::vector<MaterialGraphPin> migratedPins;
                        migratedPins.reserve(canonical.Pins.size());
                        for (const auto& expected : canonical.Pins)
                        {
                            const auto existing = std::ranges::find_if(
                                node.Pins, [&](const MaterialGraphPin& pin)
                                { return pin.Name == expected.Name && pin.Direction == expected.Direction; });
                            if (existing != node.Pins.end())
                                migratedPins.push_back(*existing);
                            else
                            {
                                auto migrated = expected;
                                migrated.Id = StableMigratedPinId(node.Id, expected.Name, expected.Direction);
                                migratedPins.push_back(std::move(migrated));
                            }
                        }
                        node.Pins = std::move(migratedPins);
                    }
            }
            ValidateMaterialGraph(result);
            result.SchemaVersion = MaterialGraphSourceSchemaVersion;
            return result;
        }

        [[nodiscard]] MaterialGraphValue DefaultValue(const MaterialGraphValueType type)
        {
            switch (type)
            {
            case MaterialGraphValueType::Scalar:
                return 0.0F;
            case MaterialGraphValueType::Vector2:
                return Vector2{};
            case MaterialGraphValueType::Vector3:
                return Vector3{};
            case MaterialGraphValueType::Vector4:
                return Vector4{};
            case MaterialGraphValueType::Color:
                return Color{};
            case MaterialGraphValueType::Texture2D:
                return AssetId{};
            case MaterialGraphValueType::MaterialAttributes:
                return MaterialGraphMaterialAttributesValue{};
            case MaterialGraphValueType::Bsdf:
                return MaterialGraphBsdfValue{};
            }
            return 0.0F;
        }

        [[nodiscard]] MaterialGraphValue UnitValue(const MaterialGraphValueType type)
        {
            switch (type)
            {
            case MaterialGraphValueType::Scalar:
                return 1.0F;
            case MaterialGraphValueType::Vector2:
                return Vector2{1.0F, 1.0F};
            case MaterialGraphValueType::Vector3:
                return Vector3{1.0F, 1.0F, 1.0F};
            case MaterialGraphValueType::Vector4:
                return Vector4{1.0F, 1.0F, 1.0F, 1.0F};
            case MaterialGraphValueType::Color:
                return Color{1.0F, 1.0F, 1.0F, 1.0F};
            case MaterialGraphValueType::Texture2D:
                return AssetId{};
            case MaterialGraphValueType::MaterialAttributes:
                return MaterialGraphMaterialAttributesValue{};
            case MaterialGraphValueType::Bsdf:
                return MaterialGraphBsdfValue{};
            }
            return 1.0F;
        }

        void AddPin(MaterialGraphNode& node, std::string name, const MaterialGraphValueType type,
                    const MaterialGraphPinDirection direction, MaterialGraphValue value)
        {
            node.Pins.push_back({AssetId::Generate(), std::move(name), type, direction, value});
        }

        [[nodiscard]] const MaterialGraphPin* FindPin(const MaterialGraphNode& node, const std::string_view name,
                                                      const MaterialGraphPinDirection direction)
        {
            const auto found = std::ranges::find_if(node.Pins, [name, direction](const MaterialGraphPin& pin)
                                                    { return pin.Name == name && pin.Direction == direction; });
            return found == node.Pins.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool Compatible(const MaterialGraphValueType output, const MaterialGraphValueType input)
        {
            return output == input ||
                   ((output == MaterialGraphValueType::Color && input == MaterialGraphValueType::Vector4) ||
                    (output == MaterialGraphValueType::Vector4 && input == MaterialGraphValueType::Color) ||
                    ((output == MaterialGraphValueType::Vector4 || output == MaterialGraphValueType::Color) &&
                     input == MaterialGraphValueType::Vector3) ||
                    (output == MaterialGraphValueType::Vector3 &&
                     (input == MaterialGraphValueType::Vector4 || input == MaterialGraphValueType::Color))) ||
                   (output == MaterialGraphValueType::Scalar && input != MaterialGraphValueType::Texture2D &&
                    input != MaterialGraphValueType::MaterialAttributes && input != MaterialGraphValueType::Bsdf);
        }

        [[nodiscard]] bool NumericNode(const MaterialGraphNodeKind kind) noexcept
        {
            switch (kind)
            {
            case MaterialGraphNodeKind::Add:
            case MaterialGraphNodeKind::Multiply:
            case MaterialGraphNodeKind::Lerp:
            case MaterialGraphNodeKind::OneMinus:
            case MaterialGraphNodeKind::Clamp:
            case MaterialGraphNodeKind::StaticSwitch:
            case MaterialGraphNodeKind::Subtract:
            case MaterialGraphNodeKind::Divide:
            case MaterialGraphNodeKind::Power:
            case MaterialGraphNodeKind::Minimum:
            case MaterialGraphNodeKind::Maximum:
            case MaterialGraphNodeKind::Absolute:
            case MaterialGraphNodeKind::Floor:
            case MaterialGraphNodeKind::Ceiling:
            case MaterialGraphNodeKind::Fraction:
            case MaterialGraphNodeKind::Sine:
            case MaterialGraphNodeKind::Cosine:
            case MaterialGraphNodeKind::Normalize:
            case MaterialGraphNodeKind::Remap:
            case MaterialGraphNodeKind::SmoothStep:
            case MaterialGraphNodeKind::Step:
            case MaterialGraphNodeKind::Posterize:
            case MaterialGraphNodeKind::Round:
            case MaterialGraphNodeKind::Truncate:
            case MaterialGraphNodeKind::Sign:
            case MaterialGraphNodeKind::Modulo:
            case MaterialGraphNodeKind::SquareRoot:
            case MaterialGraphNodeKind::ReciprocalSquareRoot:
            case MaterialGraphNodeKind::Exponential2:
            case MaterialGraphNodeKind::Logarithm2:
            case MaterialGraphNodeKind::Tangent:
            case MaterialGraphNodeKind::ArcSine:
            case MaterialGraphNodeKind::ArcCosine:
            case MaterialGraphNodeKind::ArcTangent2:
            case MaterialGraphNodeKind::DerivativeX:
            case MaterialGraphNodeKind::DerivativeY:
            case MaterialGraphNodeKind::FilterWidth:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] std::size_t EstimatedNodeCost(const MaterialGraphNodeKind kind) noexcept
        {
            switch (kind)
            {
            case MaterialGraphNodeKind::Master:
                return 48;
            case MaterialGraphNodeKind::TextureSample:
            case MaterialGraphNodeKind::TextureSampleLevel:
                return 4;
            case MaterialGraphNodeKind::TriplanarSample:
                return 18;
            case MaterialGraphNodeKind::NormalMap:
                return 14;
            case MaterialGraphNodeKind::DetailNormal:
                return 10;
            case MaterialGraphNodeKind::Parallax:
                return 16;
            case MaterialGraphNodeKind::Divide:
            case MaterialGraphNodeKind::Power:
            case MaterialGraphNodeKind::Normalize:
            case MaterialGraphNodeKind::Length:
            case MaterialGraphNodeKind::Fresnel:
            case MaterialGraphNodeKind::Refract:
                return 8;
            case MaterialGraphNodeKind::SimpleNoise:
                return 28;
            case MaterialGraphNodeKind::VoronoiNoise:
                return 36;
            case MaterialGraphNodeKind::GradientNoise:
                return 30;
            case MaterialGraphNodeKind::RotateUV:
            case MaterialGraphNodeKind::Desaturate:
            case MaterialGraphNodeKind::Remap:
            case MaterialGraphNodeKind::SmoothStep:
                return 6;
            case MaterialGraphNodeKind::Sine:
            case MaterialGraphNodeKind::Cosine:
            case MaterialGraphNodeKind::Posterize:
            case MaterialGraphNodeKind::SquareRoot:
            case MaterialGraphNodeKind::ReciprocalSquareRoot:
                return 4;
            case MaterialGraphNodeKind::HueShift:
                return 12;
            case MaterialGraphNodeKind::Checkerboard:
                return 8;
            case MaterialGraphNodeKind::Parameter:
            case MaterialGraphNodeKind::Constant:
            case MaterialGraphNodeKind::UV:
            case MaterialGraphNodeKind::Keyword:
            case MaterialGraphNodeKind::VertexColor:
            case MaterialGraphNodeKind::WorldPosition:
            case MaterialGraphNodeKind::WorldNormal:
            case MaterialGraphNodeKind::ViewDirection:
                return 0;
            default:
                return 2;
            }
        }

        [[nodiscard]] MaterialGraphStatistics AnalyzeGraph(const MaterialGraphDefinition& definition)
        {
            MaterialGraphStatistics result;
            result.NodeCount = definition.Nodes.size();
            result.ConnectionCount = definition.Connections.size();
            const auto master =
                std::ranges::find(definition.Nodes, MaterialGraphNodeKind::Master, &MaterialGraphNode::Kind);
            if (master == definition.Nodes.end())
                return result;
            std::unordered_set<AssetId> reachable{master->Id};
            std::vector<AssetId> pending{master->Id};
            while (!pending.empty())
            {
                const auto inputNode = pending.back();
                pending.pop_back();
                for (const auto& connection : definition.Connections)
                    if (connection.Input.Node == inputNode && reachable.insert(connection.Output.Node).second)
                        pending.push_back(connection.Output.Node);
            }
            result.ReachableNodeCount = reachable.size();
            result.UnusedNodeCount = result.NodeCount - result.ReachableNodeCount;
            for (const auto& node : definition.Nodes)
            {
                if (!reachable.contains(node.Id))
                    continue;
                result.EstimatedAluInstructions += EstimatedNodeCost(node.Kind);
                if (node.Kind == MaterialGraphNodeKind::TriplanarSample)
                    result.TextureSampleCount += 3U;
                else if (node.Kind == MaterialGraphNodeKind::TextureSample ||
                         node.Kind == MaterialGraphNodeKind::TextureSampleLevel)
                    ++result.TextureSampleCount;
            }
            return result;
        }

        [[nodiscard]] const MaterialGraphNode& RequireNode(const MaterialGraphDefinition& definition, const AssetId id)
        {
            const auto found = std::ranges::find(definition.Nodes, id, &MaterialGraphNode::Id);
            if (found == definition.Nodes.end())
                throw std::invalid_argument("Material Graph connection references an unknown node.");
            return *found;
        }

        [[nodiscard]] const MaterialGraphPin& RequirePin(const MaterialGraphNode& node, const AssetId id)
        {
            const auto found = std::ranges::find(node.Pins, id, &MaterialGraphPin::Id);
            if (found == node.Pins.end())
                throw std::invalid_argument("Material Graph connection references an unknown pin.");
            return *found;
        }

        template <typename Variant> void ValidateFiniteValue(const Variant& value)
        {
            std::visit(
                [](const auto& typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, float>)
                    {
                        if (!std::isfinite(typed))
                            throw std::invalid_argument("Material Graph values must be finite.");
                    }
                    else if constexpr (std::same_as<T, Vector2>)
                    {
                        if (!std::isfinite(typed.X) || !std::isfinite(typed.Y))
                            throw std::invalid_argument("Material Graph values must be finite.");
                    }
                    else if constexpr (std::same_as<T, Vector3>)
                    {
                        if (!std::isfinite(typed.X) || !std::isfinite(typed.Y) || !std::isfinite(typed.Z))
                            throw std::invalid_argument("Material Graph values must be finite.");
                    }
                    else if constexpr (std::same_as<T, Vector4>)
                    {
                        if (!Math::IsFinite(typed))
                            throw std::invalid_argument("Material Graph values must be finite.");
                    }
                    else if constexpr (std::same_as<T, Color>)
                    {
                        if (!std::isfinite(typed.Red) || !std::isfinite(typed.Green) || !std::isfinite(typed.Blue) ||
                            !std::isfinite(typed.Alpha))
                            throw std::invalid_argument("Material Graph values must be finite.");
                    }
                },
                value);
        }

        [[nodiscard]] bool ValueMatchesType(const MaterialGraphValue& value, const MaterialGraphValueType type)
        {
            return value.index() == static_cast<std::size_t>(type);
        }

        [[nodiscard]] std::string FloatLiteral(const float value)
        {
            std::ostringstream result;
            result << std::setprecision(9) << value;
            std::string text = result.str();
            if (text.find_first_of(".eE") == std::string::npos)
                text += ".0";
            return text + "F";
        }

        [[nodiscard]] Expression Literal(const MaterialGraphValue& value, const MaterialGraphValueType type)
        {
            const auto components = [&](const std::span<const float> values, const std::string_view hlsl)
            {
                std::string result(hlsl);
                result += '(';
                for (std::size_t index = 0; index < values.size(); ++index)
                {
                    if (index != 0)
                        result += ", ";
                    result += FloatLiteral(values[index]);
                }
                result += ')';
                return Expression{std::move(result), type};
            };
            if (const auto* scalar = std::get_if<float>(&value))
                return {FloatLiteral(*scalar), type};
            if (const auto* vector = std::get_if<Vector2>(&value))
            {
                const std::array values{vector->X, vector->Y};
                return components(values, "float2");
            }
            if (const auto* vector = std::get_if<Vector3>(&value))
            {
                const std::array values{vector->X, vector->Y, vector->Z};
                return components(values, "float3");
            }
            if (const auto* vector = std::get_if<Vector4>(&value))
            {
                const std::array values{vector->X, vector->Y, vector->Z, vector->W};
                return components(values, "float4");
            }
            if (const auto* color = std::get_if<Color>(&value))
            {
                const std::array values{color->Red, color->Green, color->Blue, color->Alpha};
                return components(values, "float4");
            }
            if (std::holds_alternative<MaterialGraphMaterialAttributesValue>(value))
                return {"DefaultMaterialGraphSurface()", MaterialGraphValueType::MaterialAttributes};
            if (std::holds_alternative<MaterialGraphBsdfValue>(value))
                return {"DefaultMaterialGraphBsdf()", MaterialGraphValueType::Bsdf};
            return {"_InvalidTexture", MaterialGraphValueType::Texture2D};
        }

        [[nodiscard]] std::string Swizzle(const MaterialGraphValueType type)
        {
            switch (type)
            {
            case MaterialGraphValueType::Scalar:
                return ".x";
            case MaterialGraphValueType::Vector2:
                return ".xy";
            case MaterialGraphValueType::Vector3:
                return ".xyz";
            case MaterialGraphValueType::Vector4:
            case MaterialGraphValueType::Color:
            case MaterialGraphValueType::Texture2D:
            case MaterialGraphValueType::MaterialAttributes:
            case MaterialGraphValueType::Bsdf:
                return {};
            }
            return {};
        }

        [[nodiscard]] std::string PropertySymbol(const std::string_view name)
        {
            return "_KeireMaterial_" + std::string(name);
        }

        [[nodiscard]] std::string VertexPropertySymbol(const std::string_view name)
        {
            return "_KeireVertexMaterial_" + std::string(name);
        }

        [[nodiscard]] bool SupportsStage(const MaterialGraphShaderStage stages,
                                         const MaterialGraphShaderStage stage) noexcept
        {
            return (static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(stage)) != 0U;
        }

        [[nodiscard]] Expression Coerce(Expression expression, const MaterialGraphValueType target)
        {
            if (expression.Type == target ||
                ((expression.Type == MaterialGraphValueType::Color && target == MaterialGraphValueType::Vector4) ||
                 (expression.Type == MaterialGraphValueType::Vector4 && target == MaterialGraphValueType::Color)))
            {
                expression.Type = target;
                return expression;
            }
            if ((expression.Type == MaterialGraphValueType::Color ||
                 expression.Type == MaterialGraphValueType::Vector4) &&
                target == MaterialGraphValueType::Vector3)
                return {"(" + expression.Code + ").xyz", target};
            if (expression.Type == MaterialGraphValueType::Vector3 &&
                (target == MaterialGraphValueType::Color || target == MaterialGraphValueType::Vector4))
                return {"float4(" + expression.Code + ", 1.0F)", target};
            if (expression.Type != MaterialGraphValueType::Scalar || target == MaterialGraphValueType::Texture2D ||
                target == MaterialGraphValueType::MaterialAttributes || target == MaterialGraphValueType::Bsdf)
                throw std::invalid_argument("Material Graph expression cannot be converted to the destination type.");
            switch (target)
            {
            case MaterialGraphValueType::Vector2:
                return {"float2(" + expression.Code + ", " + expression.Code + ")", target};
            case MaterialGraphValueType::Vector3:
                return {"float3(" + expression.Code + ", " + expression.Code + ", " + expression.Code + ")", target};
            case MaterialGraphValueType::Vector4:
            case MaterialGraphValueType::Color:
                return {"float4(" + expression.Code + ", " + expression.Code + ", " + expression.Code + ", " +
                            expression.Code + ")",
                        target};
            case MaterialGraphValueType::Scalar:
            case MaterialGraphValueType::Texture2D:
            case MaterialGraphValueType::MaterialAttributes:
            case MaterialGraphValueType::Bsdf:
                break;
            }
            throw std::invalid_argument("Material Graph scalar broadcast target is invalid.");
        }

        [[nodiscard]] std::string KeywordSuffix(const std::span<const std::string> keywords)
        {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const auto& keyword : keywords)
            {
                for (const char input : keyword)
                {
                    const auto character = static_cast<unsigned char>(input);
                    hash ^= character;
                    hash *= 1099511628211ULL;
                }
                hash ^= 0xffU;
                hash *= 1099511628211ULL;
            }
            std::ostringstream result;
            result << std::hex << std::setfill('0') << std::setw(16) << hash;
            return result.str();
        }

        [[nodiscard]] std::filesystem::path VariantSourcePath(const std::filesystem::path& base,
                                                              const std::string_view suffix)
        {
            auto result = base;
            const auto extension = result.extension();
            result.replace_filename(result.stem().string() + '-' + std::string(suffix) + extension.string());
            return result;
        }

        class GraphCompiler final
        {
          public:
            GraphCompiler(const MaterialGraphDefinition& definition, const MaterialGraphCompileOptions& options,
                          std::span<const std::string> keywords, std::vector<ShaderPropertyDefinition>& properties,
                          std::vector<std::filesystem::path>& dependencies)
                : m_Definition(definition), m_Options(options), m_Keywords(keywords), m_Properties(properties),
                  m_Dependencies(dependencies)
            {
                for (const auto& connection : m_Definition.Connections)
                    m_Incoming.emplace(connection.Input, connection.Output);
                for (const auto& node : m_Definition.Nodes)
                    if (node.Kind == MaterialGraphNodeKind::Parameter)
                        RegisterProperty(node);
            }

            [[nodiscard]] std::string BuildHlsl()
            {
                ValidateIncludes();
                const auto master =
                    std::ranges::find(m_Definition.Nodes, MaterialGraphNodeKind::Master, &MaterialGraphNode::Kind);
                if (master == m_Definition.Nodes.end())
                    throw std::invalid_argument("Material Graph has no Master node.");

                std::optional<std::string> worldPositionOffset;
                if (const auto* offsetPin = FindPin(*master, "WorldPositionOffset", MaterialGraphPinDirection::Input))
                    if (const auto incoming = m_Incoming.find({master->Id, offsetPin->Id});
                        incoming != m_Incoming.end())
                    {
                        m_CurrentStage = MaterialGraphShaderStage::Vertex;
                        m_Cache.clear();
                        m_Visiting.clear();
                        m_Preparing.clear();
                        worldPositionOffset =
                            Coerce(EvaluatePrepared(incoming->second), MaterialGraphValueType::Vector3).Code;
                    }
                m_CurrentStage = MaterialGraphShaderStage::Fragment;
                m_Cache.clear();
                m_Visiting.clear();
                m_Preparing.clear();

                const auto input = [&](const std::string_view name, const MaterialGraphValueType type)
                {
                    const auto* pin = FindPin(*master, name, MaterialGraphPinDirection::Input);
                    if (!pin)
                        throw std::invalid_argument("Material Graph Master node is missing the " + std::string(name) +
                                                    " input.");
                    return Coerce(Input(*master, *pin), type).Code;
                };
                const auto inputConnected = [&](const std::string_view name)
                {
                    const auto* pin = FindPin(*master, name, MaterialGraphPinDirection::Input);
                    return pin && m_Incoming.contains({master->Id, pin->Id});
                };
                const auto optionalInput =
                    [&](const std::string_view name, const MaterialGraphValueType type, const std::string_view fallback)
                {
                    const auto* pin = FindPin(*master, name, MaterialGraphPinDirection::Input);
                    return pin ? Coerce(Input(*master, *pin), type).Code : std::string(fallback);
                };

                const bool unlit = m_Definition.Output == MaterialGraphOutput::Unlit;
                const bool hasMaterialAttributes = !unlit && inputConnected("MaterialAttributes");
                const auto materialAttributes =
                    hasMaterialAttributes ? input("MaterialAttributes", MaterialGraphValueType::MaterialAttributes)
                                          : std::string{};
                const auto attribute = [](const std::string_view field)
                { return "graphMaterialAttributes." + std::string(field); };
                const auto baseColor = hasMaterialAttributes
                                           ? attribute("BaseColor")
                                           : input(unlit ? "Color" : "BaseColor", MaterialGraphValueType::Color);
                const auto emission =
                    hasMaterialAttributes ? attribute("Emission") : input("Emission", MaterialGraphValueType::Color);
                const auto opacity =
                    hasMaterialAttributes ? attribute("Opacity") : input("Opacity", MaterialGraphValueType::Scalar);
                const auto metallic = unlit                   ? "0.0F"
                                      : hasMaterialAttributes ? attribute("Metallic")
                                                              : input("Metallic", MaterialGraphValueType::Scalar);
                const auto roughness = unlit                   ? "1.0F"
                                       : hasMaterialAttributes ? attribute("Roughness")
                                                               : input("Roughness", MaterialGraphValueType::Scalar);
                const auto specular = unlit ? "0.5F"
                                      : hasMaterialAttributes
                                          ? attribute("Specular")
                                          : optionalInput("Specular", MaterialGraphValueType::Scalar, "0.5F");
                const auto clearCoat = unlit ? "0.0F"
                                       : hasMaterialAttributes
                                           ? attribute("ClearCoat")
                                           : optionalInput("ClearCoat", MaterialGraphValueType::Scalar, "0.0F");
                const auto clearCoatRoughness =
                    unlit ? "0.25F"
                    : hasMaterialAttributes
                        ? attribute("ClearCoatRoughness")
                        : optionalInput("ClearCoatRoughness", MaterialGraphValueType::Scalar, "0.25F");
                const auto sheenColor =
                    unlit ? "float4(0.0F, 0.0F, 0.0F, 1.0F)"
                    : hasMaterialAttributes
                        ? attribute("SheenColor")
                        : optionalInput("SheenColor", MaterialGraphValueType::Color, "float4(0.0F, 0.0F, 0.0F, 1.0F)");
                const auto sheenRoughness =
                    unlit                   ? "0.5F"
                    : hasMaterialAttributes ? attribute("SheenRoughness")
                                            : optionalInput("SheenRoughness", MaterialGraphValueType::Scalar, "0.5F");
                const auto normal = unlit || (!hasMaterialAttributes && !inputConnected("Normal")) ? "input.Normal"
                                    : hasMaterialAttributes ? attribute("Normal")
                                                            : input("Normal", MaterialGraphValueType::Vector3);
                const bool hasDetailNormal = !unlit && !hasMaterialAttributes && inputConnected("DetailNormal");
                const auto detailNormal = hasDetailNormal ? input("DetailNormal", MaterialGraphValueType::Vector3)
                                                          : std::string("input.Normal");
                const auto occlusion = unlit                   ? "1.0F"
                                       : hasMaterialAttributes ? attribute("Occlusion")
                                                               : input("Occlusion", MaterialGraphValueType::Scalar);
                const auto subsurfaceColor = unlit ? "float4(1.0F, 0.35F, 0.25F, 1.0F)"
                                             : hasMaterialAttributes
                                                 ? attribute("SubsurfaceColor")
                                                 : optionalInput("SubsurfaceColor", MaterialGraphValueType::Color,
                                                                 "float4(1.0F, 0.35F, 0.25F, 1.0F)");
                const auto subsurface = unlit ? "0.0F"
                                        : hasMaterialAttributes
                                            ? attribute("Subsurface")
                                            : optionalInput("Subsurface", MaterialGraphValueType::Scalar, "0.0F");
                const auto anisotropy = unlit ? "0.0F"
                                        : hasMaterialAttributes
                                            ? attribute("Anisotropy")
                                            : optionalInput("Anisotropy", MaterialGraphValueType::Scalar, "0.0F");
                const auto tangent = unlit ? "input.Tangent"
                                     : hasMaterialAttributes
                                         ? attribute("Tangent")
                                         : optionalInput("Tangent", MaterialGraphValueType::Vector3, "input.Tangent");
                const auto transmission = unlit ? "0.0F"
                                          : hasMaterialAttributes
                                              ? attribute("Transmission")
                                              : optionalInput("Transmission", MaterialGraphValueType::Scalar, "0.0F");
                const auto indexOfRefraction =
                    unlit ? "1.5F"
                    : hasMaterialAttributes
                        ? attribute("IndexOfRefraction")
                        : optionalInput("IndexOfRefraction", MaterialGraphValueType::Scalar, "1.5F");
                const auto refraction = unlit ? "0.0F"
                                        : hasMaterialAttributes
                                            ? attribute("Refraction")
                                            : optionalInput("Refraction", MaterialGraphValueType::Scalar, "0.0F");
                const auto thickness = unlit ? "1.0F"
                                       : hasMaterialAttributes
                                           ? attribute("Thickness")
                                           : optionalInput("Thickness", MaterialGraphValueType::Scalar, "1.0F");
                const bool hasPixelDepthOffset = inputConnected("PixelDepthOffset");
                const auto pixelDepthOffset = hasPixelDepthOffset
                                                  ? input("PixelDepthOffset", MaterialGraphValueType::Scalar)
                                                  : std::string("0.0F");

                std::ostringstream source;
                source << "// Generated by Keire Material Graph. Do not edit. Generator version "
                       << MaterialGraphGeneratedShaderVersion << ", source schema " << m_Definition.SchemaVersion
                       << ".\n";
                for (const auto& include : m_CustomIncludes)
                    source << "#include \"" << include.generic_string() << "\"\n";
                source << R"HLSL(
struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV0 : TEXCOORD2;
    float4 Color : TEXCOORD3;
    float4 Tangent : TEXCOORD4;
    float2 UV1 : TEXCOORD5;
};

struct VertexOutput
{
    float3 Normal : TEXCOORD0;
    float3 Tangent : TEXCOORD1;
    float3 Bitangent : TEXCOORD2;
    float3 ViewDirection : TEXCOORD3;
    float2 UV0 : TEXCOORD4;
    float4 Color : TEXCOORD5;
    float3 WorldPosition : TEXCOORD6;
    float2 UV1 : TEXCOORD7;
    float3 ObjectPosition : TEXCOORD8;
    float ViewDepth : TEXCOORD9;
    float4 Position : SV_Position;
};

cbuffer ObjectData : register(b0, space1)
{
    float4x4 Model;
    float4x4 View;
    float4x4 Projection;
    float4x4 NormalMatrix;
};

)HLSL";
                if (m_UsesVertexMaterialParameters)
                {
                    source << "cbuffer VertexMaterialData : register(b1, space1)\n{\n";
                    for (const auto& property : m_Properties)
                        if (property.Type != ShaderPropertyType::Texture2D)
                            source << "    float4 " << VertexPropertySymbol(property.Name) << ";\n";
                    source << "};\n\n";
                }
                source << R"HLSL(
struct InstanceData
{
    float4x4 Model;
    float4x4 NormalMatrix;
    float4 Tint;
};

StructuredBuffer<InstanceData> Instances : register(t0, space0);

struct MaterialGraphLocalLight
{
    float4 PositionRange;
    float4 DirectionOuter;
    float4 ColorIntensity;
    float4 Parameters;
};

cbuffer SceneData : register(b0, space3)
{
    float4 AmbientColorIntensity;
    float4 DirectionalColorIntensity;
    float4 DirectionalDirectionExposure;
    float4 SurfaceParameters;
    float4 LocalLightCounts;
    MaterialGraphLocalLight LocalLights[62];
    float4 FrameParameters;
};

cbuffer MaterialData : register(b1, space3)
{
)HLSL";
                std::string materialBindingSentinel;
                for (const auto& property : m_Properties)
                {
                    if (property.Type == ShaderPropertyType::Texture2D)
                        continue;
                    const auto symbol = PropertySymbol(property.Name);
                    if (materialBindingSentinel.empty())
                        materialBindingSentinel = symbol;
                    source << "    float4 " << symbol << ";\n";
                }
                if (materialBindingSentinel.empty())
                {
                    materialBindingSentinel = "_KeireMaterial_BindingSentinel";
                    source << "    float4 " << materialBindingSentinel << ";\n";
                }
                source << "};\n\n";
                std::size_t textureIndex = 0;
                for (const auto& property : m_Properties)
                {
                    if (property.Type != ShaderPropertyType::Texture2D)
                        continue;
                    const auto symbol = PropertySymbol(property.Name);
                    source << "Texture2D " << symbol << " : register(t" << textureIndex << ", space2);\n";
                    source << "SamplerState " << symbol << "Sampler : register(s" << textureIndex << ", space2);\n";
                    ++textureIndex;
                }
                if (!unlit)
                {
                    source << "Texture2DArray<float> DirectionalShadowTexture : register(t" << textureIndex
                           << ", space2);\n";
                    source << "SamplerState DirectionalShadowSampler : register(s" << textureIndex << ", space2);\n";
                    ++textureIndex;
                    source << "Texture2DArray<float> LocalShadowTexture : register(t" << textureIndex << ", space2);\n";
                    source << "SamplerState LocalShadowSampler : register(s" << textureIndex << ", space2);\n";
                    ++textureIndex;
                    source << "Texture2D EnvironmentTexture : register(t" << textureIndex << ", space2);\n";
                    source << "SamplerState EnvironmentSampler : register(s" << textureIndex << ", space2);\n";
                    ++textureIndex;
                    source << "Texture2D BrdfIntegrationLut : register(t" << textureIndex << ", space2);\n";
                    source << "SamplerState BrdfIntegrationSampler : register(s" << textureIndex << ", space2);\n";
                    ++textureIndex;
                    source << R"HLSL(

cbuffer ShadowData : register(b2, space3)
{
    float4 DirectionalShadowParameters;
    float4 DirectionalCascadeSplits;
    float4x4 DirectionalShadowMatrices[4];
    float4x4 LocalShadowMatrices[20];
    float4 LocalShadowParameters[62];
};

cbuffer EnvironmentData : register(b3, space3)
{
    float4 DiffuseIrradiance[9];
    float4 EnvironmentParameters;
    float4 EnvironmentEncoding;
};
)HLSL";
                    source << "StructuredBuffer<MaterialGraphLocalLight> ForwardPlusLights : register(t"
                           << textureIndex++ << ", space2);\n";
                    source << "StructuredBuffer<uint4> ForwardPlusTiles : register(t" << textureIndex++
                           << ", space2);\n";
                    source << "StructuredBuffer<uint4> ForwardPlusLightIndices : register(t" << textureIndex++
                           << ", space2);\n";
                }
                source << R"HLSL(
struct MaterialGraphSurface
{
    float4 BaseColor;
    float Metallic;
    float Roughness;
    float Specular;
    float ClearCoat;
    float ClearCoatRoughness;
    float4 SheenColor;
    float SheenRoughness;
    float3 Normal;
    float4 Emission;
    float Occlusion;
    float Opacity;
    float4 SubsurfaceColor;
    float Subsurface;
    float Anisotropy;
    float3 Tangent;
    float Transmission;
    float IndexOfRefraction;
    float Refraction;
    float Thickness;
};

struct MaterialGraphBsdf
{
    MaterialGraphSurface Surface;
};

static const float Pi = 3.14159265359F;

float3 SafeNormalize(const float3 value, const float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-12F && all(isfinite(value)) ? value * rsqrt(lengthSquared) : fallback;
}

MaterialGraphSurface DefaultMaterialGraphSurface()
{
    MaterialGraphSurface result;
    result.BaseColor = 1.0F.xxxx;
    result.Metallic = 0.0F;
    result.Roughness = 0.5F;
    result.Specular = 0.5F;
    result.ClearCoat = 0.0F;
    result.ClearCoatRoughness = 0.25F;
    result.SheenColor = float4(0.0F, 0.0F, 0.0F, 1.0F);
    result.SheenRoughness = 0.5F;
    result.Normal = float3(0.0F, 0.0F, 1.0F);
    result.Emission = float4(0.0F, 0.0F, 0.0F, 1.0F);
    result.Occlusion = 1.0F;
    result.Opacity = 1.0F;
    result.SubsurfaceColor = float4(1.0F, 0.35F, 0.25F, 1.0F);
    result.Subsurface = 0.0F;
    result.Anisotropy = 0.0F;
    result.Tangent = float3(1.0F, 0.0F, 0.0F);
    result.Transmission = 0.0F;
    result.IndexOfRefraction = 1.5F;
    result.Refraction = 0.0F;
    result.Thickness = 1.0F;
    return result;
}

MaterialGraphSurface MakeMaterialGraphSurface(
    const float4 baseColor, const float metallic, const float roughness, const float specular, const float clearCoat,
    const float clearCoatRoughness, const float4 sheenColor, const float sheenRoughness, const float3 normal,
    const float4 emission, const float occlusion, const float opacity, const float4 subsurfaceColor,
    const float subsurface, const float anisotropy, const float3 tangent, const float transmission,
    const float indexOfRefraction, const float refraction, const float thickness)
{
    MaterialGraphSurface result;
    result.BaseColor = baseColor;
    result.Metallic = metallic;
    result.Roughness = roughness;
    result.Specular = specular;
    result.ClearCoat = clearCoat;
    result.ClearCoatRoughness = clearCoatRoughness;
    result.SheenColor = sheenColor;
    result.SheenRoughness = sheenRoughness;
    result.Normal = normal;
    result.Emission = emission;
    result.Occlusion = occlusion;
    result.Opacity = opacity;
    result.SubsurfaceColor = subsurfaceColor;
    result.Subsurface = subsurface;
    result.Anisotropy = anisotropy;
    result.Tangent = tangent;
    result.Transmission = transmission;
    result.IndexOfRefraction = indexOfRefraction;
    result.Refraction = refraction;
    result.Thickness = thickness;
    return result;
}

MaterialGraphSurface BlendMaterialGraphSurfaces(const MaterialGraphSurface first, const MaterialGraphSurface second,
                                                const float alpha)
{
    const float factor = saturate(alpha);
    MaterialGraphSurface result;
    result.BaseColor = lerp(first.BaseColor, second.BaseColor, factor);
    result.Metallic = lerp(first.Metallic, second.Metallic, factor);
    result.Roughness = lerp(first.Roughness, second.Roughness, factor);
    result.Specular = lerp(first.Specular, second.Specular, factor);
    result.ClearCoat = lerp(first.ClearCoat, second.ClearCoat, factor);
    result.ClearCoatRoughness = lerp(first.ClearCoatRoughness, second.ClearCoatRoughness, factor);
    result.SheenColor = lerp(first.SheenColor, second.SheenColor, factor);
    result.SheenRoughness = lerp(first.SheenRoughness, second.SheenRoughness, factor);
    result.Normal = SafeNormalize(lerp(first.Normal, second.Normal, factor), float3(0.0F, 0.0F, 1.0F));
    result.Emission = lerp(first.Emission, second.Emission, factor);
    result.Occlusion = lerp(first.Occlusion, second.Occlusion, factor);
    result.Opacity = lerp(first.Opacity, second.Opacity, factor);
    result.SubsurfaceColor = lerp(first.SubsurfaceColor, second.SubsurfaceColor, factor);
    result.Subsurface = lerp(first.Subsurface, second.Subsurface, factor);
    result.Anisotropy = lerp(first.Anisotropy, second.Anisotropy, factor);
    result.Tangent = SafeNormalize(lerp(first.Tangent, second.Tangent, factor), float3(1.0F, 0.0F, 0.0F));
    result.Transmission = lerp(first.Transmission, second.Transmission, factor);
    result.IndexOfRefraction = lerp(first.IndexOfRefraction, second.IndexOfRefraction, factor);
    result.Refraction = lerp(first.Refraction, second.Refraction, factor);
    result.Thickness = lerp(first.Thickness, second.Thickness, factor);
    return result;
}

MaterialGraphBsdf DefaultMaterialGraphBsdf()
{
    MaterialGraphBsdf result;
    result.Surface = DefaultMaterialGraphSurface();
    return result;
}

MaterialGraphBsdf MakeStandardMaterialGraphBsdf(const float4 baseColor, const float metallic, const float roughness,
                                                const float specular, const float3 normal, const float4 emission,
                                                const float opacity)
{
    MaterialGraphBsdf result = DefaultMaterialGraphBsdf();
    result.Surface.BaseColor = baseColor;
    result.Surface.Metallic = metallic;
    result.Surface.Roughness = roughness;
    result.Surface.Specular = specular;
    result.Surface.Normal = normal;
    result.Surface.Emission = emission;
    result.Surface.Opacity = opacity;
    return result;
}

MaterialGraphBsdf ApplyMaterialGraphClearCoat(MaterialGraphBsdf result, const float weight, const float roughness)
{
    result.Surface.ClearCoat = weight;
    result.Surface.ClearCoatRoughness = roughness;
    return result;
}

MaterialGraphBsdf ApplyMaterialGraphSheen(MaterialGraphBsdf result, const float4 color, const float weight,
                                         const float roughness)
{
    result.Surface.SheenColor = float4(color.rgb * weight, color.a);
    result.Surface.SheenRoughness = roughness;
    return result;
}

MaterialGraphBsdf ApplyMaterialGraphSubsurface(MaterialGraphBsdf result, const float4 color, const float weight)
{
    result.Surface.SubsurfaceColor = color;
    result.Surface.Subsurface = weight;
    return result;
}

MaterialGraphBsdf ApplyMaterialGraphTransmission(MaterialGraphBsdf result, const float weight,
                                                 const float indexOfRefraction, const float refraction,
                                                 const float thickness)
{
    result.Surface.Transmission = weight;
    result.Surface.IndexOfRefraction = indexOfRefraction;
    result.Surface.Refraction = refraction;
    result.Surface.Thickness = thickness;
    return result;
}

MaterialGraphSurface MaterialGraphSurfaceFromBsdf(const MaterialGraphBsdf value)
{
    return value.Surface;
}

float3 DecodeNormal(const float4 packed, const float scale, const float3 tangent, const float3 bitangent,
                    const float3 normal)
{
    float3 tangentNormal = packed.xyz * 2.0F - 1.0F;
    tangentNormal.xy *= scale;
    tangentNormal = SafeNormalize(tangentNormal, float3(0.0F, 0.0F, 1.0F));
    return SafeNormalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z, normal);
}

float3 BlendDetailNormal(const float3 baseNormal, const float3 detailNormal, const float strength)
{
    const float3 combined = SafeNormalize(baseNormal + detailNormal, baseNormal);
    return SafeNormalize(lerp(baseNormal, combined, saturate(strength)), baseNormal);
}

float2 ParallaxUV(const float2 uv, const float height, const float scale, const float3 viewDirection,
                  const float3 tangent, const float3 bitangent, const float3 normal)
{
    const float3 tangentView = float3(dot(viewDirection, tangent), dot(viewDirection, bitangent),
                                     dot(viewDirection, normal));
    const float viewZ = max(abs(tangentView.z), 0.05F);
    return uv + tangentView.xy * ((height - 0.5F) * scale / viewZ);
}

float SafeDivide(const float first, const float second)
{
    const float divisor = abs(second) >= 1.0e-5F ? second : (second < 0.0F ? -1.0e-5F : 1.0e-5F);
    return first / divisor;
}

float2 SafeDivide(const float2 first, const float2 second)
{
    return float2(SafeDivide(first.x, second.x), SafeDivide(first.y, second.y));
}

float3 SafeDivide(const float3 first, const float3 second)
{
    return float3(SafeDivide(first.x, second.x), SafeDivide(first.y, second.y), SafeDivide(first.z, second.z));
}

float4 SafeDivide(const float4 first, const float4 second)
{
    return float4(SafeDivide(first.x, second.x), SafeDivide(first.y, second.y), SafeDivide(first.z, second.z),
                  SafeDivide(first.w, second.w));
}

float MaterialHash(const float2 value)
{
    const float2 wrapped = frac(value * float2(0.1031F, 0.1030F));
    const float mixed = dot(wrapped, wrapped.yx + 33.33F);
    return frac((wrapped.x + wrapped.y) * mixed);
}

float MaterialValueNoise(const float2 position)
{
    const float2 cell = floor(position);
    const float2 local = frac(position);
    const float2 blend = local * local * (3.0F - 2.0F * local);
    const float bottom = lerp(MaterialHash(cell), MaterialHash(cell + float2(1.0F, 0.0F)), blend.x);
    const float top = lerp(MaterialHash(cell + float2(0.0F, 1.0F)),
                           MaterialHash(cell + float2(1.0F, 1.0F)), blend.x);
    return lerp(bottom, top, blend.y);
}

float MaterialNoise(const float2 uv, const float scale, const float detail)
{
    float2 position = uv * max(abs(scale), 1.0e-4F);
    const float persistence = saturate(detail) * 0.5F;
    float amplitude = 0.5F;
    float value = MaterialValueNoise(position) * amplitude;
    float normalization = amplitude;
    [unroll]
    for (uint octave = 1U; octave < 4U; ++octave)
    {
        position = position * 2.0F + float2(17.0F, 29.0F);
        amplitude *= persistence;
        value += MaterialValueNoise(position) * amplitude;
        normalization += amplitude;
    }
    return value / max(normalization, 1.0e-5F);
}

float2 RotateMaterialUV(const float2 uv, const float2 center, const float rotation)
{
    const float sine = sin(rotation);
    const float cosine = cos(rotation);
    const float2 local = uv - center;
    return center + float2(local.x * cosine - local.y * sine, local.x * sine + local.y * cosine);
}

float4 DesaturateMaterialColor(const float4 color, const float amount)
{
    const float luminance = dot(color.rgb, float3(0.2126F, 0.7152F, 0.0722F));
    return float4(lerp(color.rgb, luminance.xxx, saturate(amount)), color.a);
}

float4 HueShiftMaterialColor(const float4 color, const float shift)
{
    const float angle = shift * 6.28318530718F;
    const float sine = sin(angle);
    const float cosine = cos(angle);
    const float3 axis = normalize(float3(1.0F, 1.0F, 1.0F));
    const float3 shifted = color.rgb * cosine + cross(axis, color.rgb) * sine +
                           axis * dot(axis, color.rgb) * (1.0F - cosine);
    return float4(max(shifted, 0.0F), color.a);
}

float4 MaterialCheckerboard(const float2 uv, const float4 colorA, const float4 colorB, const float2 scale)
{
    const float2 cell = floor(uv * max(abs(scale), 1.0e-4F));
    return lerp(colorA, colorB, fmod(cell.x + cell.y, 2.0F));
}

float2 MaterialVoronoi(const float2 uv, const float scale, const float jitter)
{
    const float2 coordinate = uv * max(abs(scale), 1.0e-4F);
    const float2 baseCell = floor(coordinate);
    const float2 local = frac(coordinate);
    float minimumDistance = 8.0F;
    float cellHash = 0.0F;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 offset = float2((float)x, (float)y);
            const float hash = MaterialHash(baseCell + offset);
            const float2 feature = offset + lerp(0.5F.xx, float2(hash, MaterialHash(baseCell + offset + 19.19F)),
                                                   saturate(jitter));
            const float distanceToFeature = length(feature - local);
            if (distanceToFeature < minimumDistance)
            {
                minimumDistance = distanceToFeature;
                cellHash = hash;
            }
        }
    }
    return float2(minimumDistance, cellHash);
}

float4 MaterialOverlayBlend(const float4 baseColor, const float4 blendColor, const float opacity)
{
    const float3 low = 2.0F * baseColor.rgb * blendColor.rgb;
    const float3 high = 1.0F - 2.0F * (1.0F - baseColor.rgb) * (1.0F - blendColor.rgb);
    const float3 overlay = lerp(low, high, step(0.5F.xxx, baseColor.rgb));
    return float4(lerp(baseColor.rgb, overlay, saturate(opacity)), baseColor.a);
}

float4 MaterialBlackbody(const float temperature)
{
    const float kelvin = clamp(temperature, 1000.0F, 40000.0F) * 0.01F;
    const float red = kelvin <= 66.0F ? 1.0F : saturate(1.29293619F * pow(kelvin - 60.0F, -0.133204759F));
    const float green = kelvin <= 66.0F
                            ? saturate(0.390081579F * log(max(kelvin, 1.0F)) - 0.631841444F)
                            : saturate(1.12989086F * pow(kelvin - 60.0F, -0.0755148492F));
    const float blue = kelvin >= 66.0F
                           ? 1.0F
                           : kelvin <= 19.0F
                                 ? 0.0F
                                 : saturate(0.543206811F * log(max(kelvin - 10.0F, 1.0F)) - 1.19625409F);
    return float4(red, green, blue, 1.0F);
}

float MaterialDitherThreshold(const float2 screenPosition)
{
    static const float thresholds[16] = {0.0F, 0.5F, 0.125F, 0.625F, 0.75F, 0.25F, 0.875F, 0.375F,
                                         0.1875F, 0.6875F, 0.0625F, 0.5625F, 0.9375F, 0.4375F, 0.8125F, 0.3125F};
    const uint2 pixel = uint2(abs(screenPosition)) & 3U.xx;
    const uint frameRotation = (uint)FrameParameters.z & 3U;
    return thresholds[((pixel.y + frameRotation) & 3U) * 4U + ((pixel.x + frameRotation) & 3U)];
}

float DistributionGgx(const float noH, const float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator = noH * noH * (alphaSquared - 1.0F) + 1.0F;
    return alphaSquared / max(Pi * denominator * denominator, 1.0e-5F);
}

float VisibilitySmithGgx(const float noV, const float noL, const float roughness)
{
    const float alpha = roughness * roughness;
    const float lambdaV = noL * sqrt(max((-noV * alpha + noV) * noV + alpha, 0.0F));
    const float lambdaL = noV * sqrt(max((-noL * alpha + noL) * noL + alpha, 0.0F));
    return 0.5F / max(lambdaV + lambdaL, 1.0e-5F);
}

float3 FresnelSchlick(const float voH, const float3 f0)
{
    const float factor = pow(1.0F - saturate(voH), 5.0F);
    return f0 + (1.0F - f0) * factor;
}
)HLSL";
                if (!unlit)
                    source << R"HLSL(
uint ForwardPlusLightIndex(const uint index)
{
    const uint4 indices = ForwardPlusLightIndices[index >> 2U];
    return indices[index & 3U];
}

float SampleShadowPcf(Texture2DArray<float> textureValue, SamplerState samplerValue, const float2 uv,
                      const float layer, const float depth, const float inverseResolution, const bool soft)
{
    if (any(uv < 0.0F.xx) || any(uv > 1.0F.xx) || depth <= 0.0F || depth >= 1.0F)
        return 1.0F;
    if (!soft)
        return depth <= textureValue.SampleLevel(samplerValue, float3(uv, layer), 0.0F) ? 1.0F : 0.0F;
    float visibility = 0.0F;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float storedDepth =
                textureValue.SampleLevel(samplerValue, float3(uv + float2(x, y) * inverseResolution, layer), 0.0F);
            visibility += depth <= storedDepth ? 1.0F : 0.0F;
        }
    }
    return visibility / 9.0F;
}

float EvaluateDirectionalShadow(const float3 worldPosition, const float viewDepth)
{
    const float cascadeCount = abs(DirectionalShadowParameters.x);
    if (SurfaceParameters.z < 0.5F || cascadeCount < 0.5F)
        return 1.0F;
    uint cascade = 0U;
    cascade += viewDepth > DirectionalCascadeSplits.x;
    cascade += viewDepth > DirectionalCascadeSplits.y;
    cascade += viewDepth > DirectionalCascadeSplits.z;
    cascade = min(cascade, (uint)cascadeCount - 1U);
    const float4 clip = mul(DirectionalShadowMatrices[cascade], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    const float visibility = SampleShadowPcf(DirectionalShadowTexture, DirectionalShadowSampler, uv, cascade,
                                             projected.z - DirectionalShadowParameters.z,
                                             DirectionalShadowParameters.w, DirectionalShadowParameters.x > 0.0F);
    return lerp(1.0F, visibility, saturate(DirectionalShadowParameters.y));
}

float2 PointShadowCoordinates(const float3 direction, out uint face, out float majorDistance)
{
    const float3 absoluteDirection = abs(direction);
    float2 projected;
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        majorDistance = absoluteDirection.x;
        face = direction.x >= 0.0F ? 0U : 1U;
        projected = direction.x >= 0.0F ? float2(-direction.z, direction.y) / majorDistance
                                        : float2(direction.z, direction.y) / majorDistance;
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        majorDistance = absoluteDirection.y;
        face = direction.y >= 0.0F ? 2U : 3U;
        projected = direction.y >= 0.0F ? float2(direction.x, -direction.z) / majorDistance
                                        : float2(direction.x, direction.z) / majorDistance;
    }
    else
    {
        majorDistance = absoluteDirection.z;
        face = direction.z >= 0.0F ? 4U : 5U;
        projected = direction.z >= 0.0F ? float2(direction.x, direction.y) / majorDistance
                                        : float2(-direction.x, direction.y) / majorDistance;
    }
    return float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
}

float EvaluateLocalShadow(const uint lightIndex, const float3 worldPosition)
{
    if (SurfaceParameters.z < 0.5F || LocalShadowParameters[lightIndex].x < 0.0F)
        return 1.0F;
    const bool spot = ForwardPlusLights[lightIndex].Parameters.y > 0.5F;
    uint matrixIndex = (uint)LocalShadowParameters[lightIndex].x;
    if (!spot)
    {
        const float3 fromLight = worldPosition - ForwardPlusLights[lightIndex].PositionRange.xyz;
        uint face = 0U;
        float majorDistance = 0.0F;
        PointShadowCoordinates(fromLight, face, majorDistance);
        matrixIndex += face;
    }
    const float4 clip = mul(LocalShadowMatrices[matrixIndex], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    const float visibility = SampleShadowPcf(LocalShadowTexture, LocalShadowSampler, uv, 0.0F,
                                             projected.z - LocalShadowParameters[lightIndex].w, 1.0F / 4096.0F,
                                             LocalShadowParameters[lightIndex].z > 0.5F);
    return lerp(1.0F, visibility, saturate(LocalShadowParameters[lightIndex].y));
}

float3 FresnelSchlickRoughness(const float noV, const float3 f0, const float roughness)
{
    return f0 + (max((1.0F - roughness).xxx, f0) - f0) * pow(1.0F - noV, 5.0F);
}

float3 RotateEnvironmentDirection(float3 direction)
{
    const float rotation = radians(EnvironmentParameters.x);
    const float sineRotation = sin(rotation);
    const float cosineRotation = cos(rotation);
    direction.xz = float2(direction.x * cosineRotation - direction.z * sineRotation,
                          direction.x * sineRotation + direction.z * cosineRotation);
    return direction;
}

float2 CubemapAtlasUv(float3 direction, const int layout)
{
    const float3 absoluteDirection = abs(direction);
    int face = 0;
    float2 local;
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        const float inverse = rcp(absoluteDirection.x);
        face = direction.x >= 0.0F ? 0 : 1;
        local = direction.x >= 0.0F ? float2(-direction.z, -direction.y) * inverse
                                    : float2(direction.z, -direction.y) * inverse;
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        const float inverse = rcp(absoluteDirection.y);
        face = direction.y >= 0.0F ? 2 : 3;
        local = direction.y >= 0.0F ? float2(direction.x, direction.z) * inverse
                                    : float2(direction.x, -direction.z) * inverse;
    }
    else
    {
        const float inverse = rcp(absoluteDirection.z);
        face = direction.z >= 0.0F ? 4 : 5;
        local = direction.z >= 0.0F ? float2(direction.x, -direction.y) * inverse
                                    : float2(-direction.x, -direction.y) * inverse;
    }
    local = local * 0.5F + 0.5F;
    uint textureWidth;
    uint textureHeight;
    EnvironmentTexture.GetDimensions(textureWidth, textureHeight);
    const float2 grid = layout == 4 ? float2(6.0F, 1.0F)
                        : layout == 5 ? float2(1.0F, 6.0F)
                        : layout == 2 ? float2(4.0F, 3.0F)
                                      : float2(3.0F, 4.0F);
    const float2 cellPixels = max(float2(textureWidth, textureHeight) / grid, 1.0F);
    local = clamp(local, 0.5F / cellPixels, 1.0F - 0.5F / cellPixels);
    if (layout == 4)
        return float2((face + local.x) / 6.0F, local.y);
    if (layout == 5)
        return float2(local.x, (face + local.y) / 6.0F);
    const int2 horizontalCells[6] = {int2(2, 1), int2(0, 1), int2(1, 0), int2(1, 2), int2(1, 1), int2(3, 1)};
    const int2 verticalCells[6] = {int2(2, 1), int2(0, 1), int2(1, 0), int2(1, 2), int2(1, 1), int2(1, 3)};
    return layout == 2 ? (float2(horizontalCells[face]) + local) / float2(4.0F, 3.0F)
                       : (float2(verticalCells[face]) + local) / float2(3.0F, 4.0F);
}

float3 DecodeRgbe(const float4 sampleValue)
{
    return sampleValue.rgb * (255.0F * exp2(sampleValue.a * 255.0F - 136.0F));
}

float3 SampleEnvironment(float3 direction, const float level)
{
    direction = RotateEnvironmentDirection(direction);
    const int encoding = (int)EnvironmentEncoding.x;
    const int layout = encoding & 15;
    const float2 uv = layout <= 1
                          ? float2(0.5F + atan2(direction.x, direction.z) / (2.0F * Pi),
                                   0.5F - asin(clamp(direction.y, -1.0F, 1.0F)) / Pi)
                          : CubemapAtlasUv(direction, layout);
    const float4 sampleValue = EnvironmentTexture.SampleLevel(EnvironmentSampler, uv, level);
    return encoding >= 16 ? DecodeRgbe(sampleValue) : sampleValue.rgb;
}

float3 EvaluateDiffuseEnvironment(float3 normal)
{
    normal = RotateEnvironmentDirection(normalize(normal));
    const float x = normal.x;
    const float y = normal.y;
    const float z = normal.z;
    return max(DiffuseIrradiance[0].rgb * 0.282095F + DiffuseIrradiance[1].rgb * (0.488603F * y) +
                   DiffuseIrradiance[2].rgb * (0.488603F * z) + DiffuseIrradiance[3].rgb * (0.488603F * x) +
                   DiffuseIrradiance[4].rgb * (1.092548F * x * y) +
                   DiffuseIrradiance[5].rgb * (1.092548F * y * z) +
                   DiffuseIrradiance[6].rgb * (0.315392F * (3.0F * y * y - 1.0F)) +
                   DiffuseIrradiance[7].rgb * (1.092548F * x * z) +
                   DiffuseIrradiance[8].rgb * (0.546274F * (z * z - x * x)),
               0.0F.xxx);
}

float AnisotropicDistributionGgx(const float3 normal, const float3 tangent, const float3 halfVector,
                                 const float roughness, const float anisotropy)
{
    const float3 normalizedTangent = SafeNormalize(tangent - normal * dot(normal, tangent),
                                                    float3(1.0F, 0.0F, 0.0F));
    const float3 bitangent = SafeNormalize(cross(normal, normalizedTangent), float3(0.0F, 1.0F, 0.0F));
    const float alpha = roughness * roughness;
    const float aspect = sqrt(max(1.0F - 0.9F * anisotropy, 0.1F));
    const float alphaT = max(alpha / aspect, 1.0e-3F);
    const float alphaB = max(alpha * aspect, 1.0e-3F);
    const float toH = dot(normalizedTangent, halfVector);
    const float boH = dot(bitangent, halfVector);
    const float noH = dot(normal, halfVector);
    const float denominator = toH * toH / (alphaT * alphaT) + boH * boH / (alphaB * alphaB) + noH * noH;
    return rcp(max(Pi * alphaT * alphaB * denominator * denominator, 1.0e-5F));
}

float3 EvaluateGraphDirectLighting(const float3 normal, const float3 tangent, const float3 viewDirection,
                                   const float3 lightDirection, const float3 radiance, const float3 baseColor,
                                   const float metallic, const float roughness, const float anisotropy,
                                   const float specularLevel, const float clearCoat, const float clearCoatRoughness,
                                   const float3 sheenColor, const float sheenRoughness, const float3 subsurfaceColor,
                                   const float subsurface, const float transmission)
{
    const float noL = saturate(dot(normal, lightDirection));
    const float noV = max(saturate(dot(normal, viewDirection)), 1.0e-4F);
    if (noL <= 0.0F)
        return 0.0F.xxx;
    const float3 halfVector = SafeNormalize(lightDirection + viewDirection, normal);
    const float noH = saturate(dot(normal, halfVector));
    const float voH = saturate(dot(viewDirection, halfVector));
    const float3 f0 = lerp((0.08F * specularLevel).xxx, baseColor, metallic);
    const float3 fresnel = FresnelSchlick(voH, f0);
    const float distribution = abs(anisotropy) > 1.0e-4F
                                   ? AnisotropicDistributionGgx(normal, tangent, halfVector, roughness, anisotropy)
                                   : DistributionGgx(noH, roughness);
    const float3 directSpecular = fresnel * distribution * VisibilitySmithGgx(noV, noL, roughness);
    const float coatFresnel = 0.04F + 0.96F * pow(1.0F - voH, 5.0F);
    const float3 coatSpecular =
        (clearCoat * coatFresnel * DistributionGgx(noH, clearCoatRoughness) *
         VisibilitySmithGgx(noV, noL, clearCoatRoughness)).xxx;
    const float3 sheen = sheenColor * pow(1.0F - noH, lerp(8.0F, 1.0F, sheenRoughness)) * (1.0F - metallic);
    const float3 diffuse = (1.0F - fresnel) * (1.0F - metallic) * baseColor / Pi;
    const float wrap = saturate((noL + 0.5F) / 1.5F);
    const float3 subsurfaceDiffuse = subsurfaceColor * (1.0F - metallic) * wrap / Pi;
    const float3 surface = lerp(diffuse, subsurfaceDiffuse, subsurface) * (1.0F - transmission);
    return (surface + directSpecular + coatSpecular + sheen) * radiance * noL;
}
)HLSL";
                source << R"HLSL(

VertexOutput VSMain(VertexInput input, const uint instanceId : SV_InstanceID)
{
    VertexOutput output;
    const InstanceData instance = Instances[instanceId];
    float4 world = mul(instance.Model, float4(input.Position, 1.0F));
)HLSL";
                if (worldPositionOffset)
                    source << "    world.xyz += " << *worldPositionOffset << ";\n";
                source << R"HLSL(
    const float4 viewPosition = mul(View, world);
    output.Position = mul(Projection, viewPosition);
    output.Normal = SafeNormalize(mul((float3x3)instance.NormalMatrix, input.Normal), float3(0.0F, 0.0F, 1.0F));
    float3 tangent = mul((float3x3)instance.Model, input.Tangent.xyz);
    tangent -= output.Normal * dot(output.Normal, tangent);
    output.Tangent = SafeNormalize(tangent, float3(1.0F, 0.0F, 0.0F));
    const float modelHandedness = determinant((float3x3)instance.Model) < 0.0F ? -1.0F : 1.0F;
    const float tangentHandedness = abs(input.Tangent.w) > 0.0001F ? input.Tangent.w : 1.0F;
    output.Bitangent = SafeNormalize(cross(output.Normal, output.Tangent) * tangentHandedness * modelHandedness,
                                     float3(0.0F, 1.0F, 0.0F));
    output.ViewDirection = SafeNormalize(mul(-viewPosition.xyz, (float3x3)View), output.Normal);
    output.UV0 = input.UV0;
    output.UV1 = input.UV1;
    output.Color = input.Color * instance.Tint;
    output.WorldPosition = world.xyz;
    output.ObjectPosition = mul(instance.Model, float4(0.0F, 0.0F, 0.0F, 1.0F)).xyz;
    output.ViewDepth = viewPosition.z;
    return output;
}
)HLSL";
                if (hasPixelDepthOffset)
                    source << R"HLSL(

struct MaterialGraphFragmentOutput
{
    float4 Color : SV_Target0;
    float Depth : SV_Depth;
};

MaterialGraphFragmentOutput PSMain(VertexOutput input)
{
)HLSL";
                else
                    source << R"HLSL(

float4 PSMain(VertexOutput input) : SV_Target0
{
)HLSL";
                if (hasMaterialAttributes)
                    source << "    const MaterialGraphSurface graphMaterialAttributes = " << materialAttributes
                           << ";\n";
                source << "    const float4 graphBaseColor = " << baseColor << ";\n";
                source << "    const float3 graphEmission = (" << emission << ").rgb;\n";
                source << "    const float graphOpacity = saturate(" << opacity << ");\n";
                if (unlit)
                    source << "    float3 graphColor = graphBaseColor.rgb + graphEmission;\n";
                else
                {
                    source << "    const float graphMetallic = saturate(" << metallic << ");\n";
                    source << "    const float graphRoughness = clamp(" << roughness << ", 0.04F, 1.0F);\n";
                    source << "    const float graphSpecular = saturate(" << specular << ");\n";
                    source << "    const float graphClearCoat = saturate(" << clearCoat << ");\n";
                    source << "    const float graphClearCoatRoughness = clamp(" << clearCoatRoughness
                           << ", 0.04F, 1.0F);\n";
                    source << "    const float3 graphSheenColor = saturate((" << sheenColor << ").rgb);\n";
                    source << "    const float graphSheenRoughness = saturate(" << sheenRoughness << ");\n";
                    source << "    const float3 graphSubsurfaceColor = saturate((" << subsurfaceColor << ").rgb);\n";
                    source << "    const float graphSubsurface = saturate(" << subsurface << ");\n";
                    source << "    const float graphAnisotropy = clamp(" << anisotropy << ", -0.99F, 0.99F);\n";
                    source << "    const float3 graphTangent = SafeNormalize(" << tangent << ", input.Tangent);\n";
                    source << "    const float graphTransmission = saturate(" << transmission << ");\n";
                    source << "    const float graphIor = clamp(abs(" << indexOfRefraction << "), 1.0F, 3.0F);\n";
                    source << "    const float graphRefraction = saturate(" << refraction << ");\n";
                    source << "    const float graphThickness = max(" << thickness << ", 0.0F);\n";
                    if (hasDetailNormal)
                        source << "    const float3 graphNormal = SafeNormalize(BlendDetailNormal(SafeNormalize("
                               << normal << ", input.Normal), SafeNormalize(" << detailNormal
                               << ", input.Normal), 1.0F), input.Normal);\n";
                    else
                        source << "    const float3 graphNormal = SafeNormalize(" << normal << ", input.Normal);\n";
                    source << "    const float ao = saturate(" << occlusion << ");\n";
                    source << R"HLSL(    const float3 viewDirection = SafeNormalize(input.ViewDirection, graphNormal);
    const float3 lightDirection =
        SafeNormalize(-DirectionalDirectionExposure.xyz, float3(0.0F, 1.0F, 0.0F));
    float3 directLighting = EvaluateGraphDirectLighting(
        graphNormal, graphTangent, viewDirection, lightDirection,
        DirectionalColorIntensity.rgb * DirectionalColorIntensity.a, graphBaseColor.rgb, graphMetallic,
        graphRoughness, graphAnisotropy, graphSpecular, graphClearCoat, graphClearCoatRoughness, graphSheenColor,
        graphSheenRoughness, graphSubsurfaceColor, graphSubsurface, graphTransmission);
    directLighting *= EvaluateDirectionalShadow(input.WorldPosition, input.ViewDepth);
    uint2 lightTile = 0U.xx;
    if (LocalLightCounts.x > 0.5F)
    {
        const uint tileColumns = max((uint)LocalLightCounts.y, 1U);
        const uint tileIndex = (uint(input.Position.y) >> 4U) * tileColumns + (uint(input.Position.x) >> 4U);
        lightTile = ForwardPlusTiles[tileIndex].xy;
    }
    for (uint tileLightIndex = 0U; tileLightIndex < lightTile.y; ++tileLightIndex)
    {
        const uint lightIndex = ForwardPlusLightIndex(lightTile.x + tileLightIndex);
        const MaterialGraphLocalLight light = ForwardPlusLights[lightIndex];
        const float3 toLight = light.PositionRange.xyz - input.WorldPosition;
        const float distanceSquared = dot(toLight, toLight);
        const float distanceToLight = sqrt(max(distanceSquared, 1.0e-8F));
        const float range = max(light.PositionRange.w, 1.0e-4F);
        if (distanceToLight >= range)
            continue;
        const float3 localDirection = toLight / distanceToLight;
        const float normalizedDistance = distanceToLight / range;
        const float rangeFade = saturate(1.0F - normalizedDistance * normalizedDistance * normalizedDistance *
                                                    normalizedDistance);
        float attenuation = rangeFade * rangeFade / max(distanceSquared, 0.01F);
        if (light.Parameters.y > 0.5F)
        {
            const float3 spotDirection = SafeNormalize(light.DirectionOuter.xyz, float3(0.0F, 0.0F, 1.0F));
            const float coneCosine = dot(spotDirection, -localDirection);
            const float outerCosine = light.DirectionOuter.w;
            const float innerCosine = max(light.Parameters.x, outerCosine + 1.0e-4F);
            attenuation *= smoothstep(outerCosine, innerCosine, coneCosine);
        }
        const float shadow = lightIndex < 62U ? EvaluateLocalShadow(lightIndex, input.WorldPosition) : 1.0F;
        const float3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.a * attenuation * shadow;
        directLighting += EvaluateGraphDirectLighting(
            graphNormal, graphTangent, viewDirection, localDirection, radiance, graphBaseColor.rgb, graphMetallic,
            graphRoughness, graphAnisotropy, graphSpecular, graphClearCoat, graphClearCoatRoughness, graphSheenColor,
            graphSheenRoughness, graphSubsurfaceColor, graphSubsurface, graphTransmission);
    }
    const float noV = saturate(dot(graphNormal, viewDirection));
    const float3 f0 = lerp((0.08F * graphSpecular).xxx, graphBaseColor.rgb, graphMetallic);
    const float3 diffuseEnvironment =
        EvaluateDiffuseEnvironment(graphNormal) * graphBaseColor.rgb * (1.0F - graphMetallic) *
        (1.0F - graphTransmission) / Pi;
    const float3 reflectionDirection = reflect(-viewDirection, graphNormal);
    const float3 reflectionRadiance =
        SampleEnvironment(reflectionDirection, graphRoughness * EnvironmentParameters.w);
    const float3 refractionDirection = refract(-viewDirection, graphNormal, rcp(graphIor));
    const float3 refractionRadiance =
        SampleEnvironment(refractionDirection, graphRoughness * EnvironmentParameters.w);
    const float3 absorption = exp(-max(1.0F - graphBaseColor.rgb, 0.0F.xxx) * graphThickness);
    const float3 transmittedEnvironment = refractionRadiance * absorption * graphTransmission;
    const float2 integratedBrdf =
        BrdfIntegrationLut.SampleLevel(BrdfIntegrationSampler, float2(noV, graphRoughness), 0.0F).rg;
    const float3 reflectedEnvironment =
        reflectionRadiance *
        (FresnelSchlickRoughness(noV, f0, graphRoughness) * integratedBrdf.x + integratedBrdf.y);
    const float3 specularEnvironment =
        lerp(reflectedEnvironment, refractionRadiance, graphRefraction * (1.0F - graphMetallic));
    const float3 flatAmbient =
        graphBaseColor.rgb * (1.0F - graphMetallic) * AmbientColorIntensity.rgb * AmbientColorIntensity.a;
    const float3 ambientLighting =
        (flatAmbient * (1.0F - graphTransmission) + diffuseEnvironment * EnvironmentParameters.y +
         specularEnvironment * EnvironmentParameters.z + transmittedEnvironment * EnvironmentParameters.y) * ao;
    float3 graphColor =
        (ambientLighting + directLighting + graphEmission) * DirectionalDirectionExposure.w;
)HLSL";
                }
                source << "    // Keep the fixed interpolator ABI dense for DXIL PSO validation on D3D12.\n";
                source << "    if (!all(isfinite(float4(input.Tangent.xy, input.Bitangent.xy))) ||\n";
                source << "        !all(isfinite(float4(input.UV0, input.UV1))) ||\n";
                source << "        !all(isfinite(float4(input.Color.zw, input.WorldPosition.xy))) ||\n";
                source << "        !all(isfinite(float4(input.WorldPosition.z, input.ObjectPosition))) ||\n";
                source << "        !isfinite(input.ViewDepth))\n";
                source << "        graphColor += input.Tangent + input.Bitangent + input.Color.xyz + "
                          "input.WorldPosition + input.ObjectPosition + float3(input.UV0 + input.UV1, 0.0F);\n";
                source << "    if (!all(isfinite(" << materialBindingSentinel << ")))\n";
                source << "        graphColor += " << materialBindingSentinel << ".xyz;\n";
                source << "    const float alpha = saturate(graphBaseColor.a * graphOpacity);\n";
                source << "    if (SurfaceParameters.y > 0.5F && SurfaceParameters.y < 1.5F)\n";
                source << "        clip(alpha - SurfaceParameters.x);\n";
                const bool premultiplied = m_Definition.Output == MaterialGraphOutput::Transparent ||
                                           m_Definition.Output == MaterialGraphOutput::Decal;
                if (hasPixelDepthOffset)
                {
                    source << "    MaterialGraphFragmentOutput output;\n";
                    source << "    output.Color = float4("
                           << (premultiplied ? "graphColor * alpha, alpha" : "graphColor, alpha") << ");\n";
                    source << "    output.Depth = saturate(input.Position.z + (" << pixelDepthOffset
                           << ") * max(fwidth(input.Position.z), 1.0e-7F));\n";
                    source << "    return output;\n";
                }
                else if (premultiplied)
                    source << "    return float4(graphColor * alpha, alpha);\n";
                else
                    source << "    return float4(graphColor, alpha);\n";
                source << "}\n";
                return source.str();
            }

            [[nodiscard]] bool UsesVertexMaterialParameters() const noexcept { return m_UsesVertexMaterialParameters; }

          private:
            void RegisterProperty(const MaterialGraphNode& node)
            {
                ShaderPropertyDefinition property;
                property.Name = node.Symbol;
                property.DisplayName = node.Name.empty() ? node.Symbol : node.Name;
                property.Category =
                    node.ParameterMetadata.Category.empty() ? "Material Graph" : node.ParameterMetadata.Category;
                property.Minimum = node.ParameterMetadata.Minimum;
                property.Maximum = node.ParameterMetadata.Maximum;
                property.Step = node.ParameterMetadata.Step;
                property.Type = static_cast<ShaderPropertyType>(node.ValueType);
                if (node.ValueType == MaterialGraphValueType::Texture2D)
                {
                    property.DefaultTexture = std::get<AssetId>(node.Value);
                    property.TextureSemantic = node.TextureSemantic;
                }
                else
                {
                    const auto literal = node.Value;
                    if (const auto* scalar = std::get_if<float>(&literal))
                        property.DefaultValue.X = *scalar;
                    else if (const auto* vector2 = std::get_if<Vector2>(&literal))
                        property.DefaultValue = {vector2->X, vector2->Y, 0.0F, 0.0F};
                    else if (const auto* vector3 = std::get_if<Vector3>(&literal))
                        property.DefaultValue = {vector3->X, vector3->Y, vector3->Z, 0.0F};
                    else if (const auto* vector4 = std::get_if<Vector4>(&literal))
                        property.DefaultValue = *vector4;
                    else if (const auto* color = std::get_if<Color>(&literal))
                        property.DefaultValue = {color->Red, color->Green, color->Blue, color->Alpha};
                }
                if (std::ranges::find(m_Properties, property.Name, &ShaderPropertyDefinition::Name) ==
                    m_Properties.end())
                    m_Properties.push_back(std::move(property));
            }

            [[nodiscard]] Expression Input(const MaterialGraphNode& node, const MaterialGraphPin& pin)
            {
                const auto found = m_Incoming.find({node.Id, pin.Id});
                if (found == m_Incoming.end())
                    return Literal(pin.DefaultValue, pin.Type);
                return Coerce(EvaluatePrepared(found->second), pin.Type);
            }

            [[nodiscard]] Expression EvaluatePrepared(const MaterialGraphEndpoint endpoint)
            {
                if (const auto found = m_Cache.find(endpoint); found != m_Cache.end())
                    return found->second;
                if (!m_Preparing.insert(endpoint.Node).second)
                    throw std::invalid_argument("Material Graph contains an expression cycle.");
                try
                {
                    const auto& node = RequireNode(m_Definition, endpoint.Node);
                    const auto prepare = [&](const MaterialGraphPin& pin)
                    {
                        const auto incoming = m_Incoming.find({node.Id, pin.Id});
                        if (incoming != m_Incoming.end())
                            (void)EvaluatePrepared(incoming->second);
                    };
                    if (node.Kind == MaterialGraphNodeKind::StaticSwitch)
                    {
                        const auto* condition = FindPin(node, "Condition", MaterialGraphPinDirection::Input);
                        const auto* trueValue = FindPin(node, "True", MaterialGraphPinDirection::Input);
                        const auto* falseValue = FindPin(node, "False", MaterialGraphPinDirection::Input);
                        if (!condition || !trueValue || !falseValue)
                            throw std::invalid_argument("Static Switch is missing a canonical input pin.");
                        prepare(*condition);
                        const auto expression = Coerce(Input(node, *condition), MaterialGraphValueType::Scalar);
                        if (expression.Code == "0.0F")
                            prepare(*falseValue);
                        else if (expression.Code == "1.0F")
                            prepare(*trueValue);
                        else
                        {
                            prepare(*trueValue);
                            prepare(*falseValue);
                        }
                    }
                    else
                        for (const auto& pin : node.Pins)
                            if (pin.Direction == MaterialGraphPinDirection::Input)
                                prepare(pin);
                }
                catch (...)
                {
                    m_Preparing.erase(endpoint.Node);
                    throw;
                }
                m_Preparing.erase(endpoint.Node);
                return Evaluate(endpoint);
            }

            [[nodiscard]] Expression Evaluate(const MaterialGraphEndpoint endpoint)
            {
                if (const auto found = m_Cache.find(endpoint); found != m_Cache.end())
                    return found->second;
                if (!m_Visiting.insert(endpoint.Node).second)
                    throw std::invalid_argument("Material Graph contains an expression cycle.");
                const auto& node = RequireNode(m_Definition, endpoint.Node);
                const auto* descriptor = FindMaterialGraphNodeDescriptor(
                    node.TypeId.empty() ? MaterialGraphNodeTypeId(node.Kind) : std::string_view(node.TypeId));
                if (!descriptor || !SupportsStage(descriptor->Stages, m_CurrentStage))
                    throw std::invalid_argument("Material Graph node '" + node.Name +
                                                "' is unavailable in the requested shader stage.");
                const auto& outputPin = RequirePin(node, endpoint.Pin);
                if (outputPin.Direction != MaterialGraphPinDirection::Output)
                    throw std::invalid_argument("Material Graph expression endpoint is not an output pin.");
                Expression result;
                const auto namedInput = [&](const std::string_view name)
                {
                    const auto* pin = FindPin(node, name, MaterialGraphPinDirection::Input);
                    if (!pin)
                        throw std::invalid_argument("Material Graph node is missing a canonical input pin.");
                    return Input(node, *pin);
                };
                switch (node.Kind)
                {
                case MaterialGraphNodeKind::Parameter:
                {
                    const bool vertex = m_CurrentStage == MaterialGraphShaderStage::Vertex;
                    m_UsesVertexMaterialParameters |= vertex;
                    result = {(vertex ? VertexPropertySymbol(node.Symbol) : PropertySymbol(node.Symbol)) +
                                  Swizzle(node.ValueType),
                              node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Constant:
                    result = Literal(node.Value, node.ValueType);
                    break;
                case MaterialGraphNodeKind::UV:
                    result = {"input.UV0", MaterialGraphValueType::Vector2};
                    break;
                case MaterialGraphNodeKind::UVTransform:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto tiling = Coerce(namedInput("Tiling"), MaterialGraphValueType::Vector2);
                    const auto offset = Coerce(namedInput("Offset"), MaterialGraphValueType::Vector2);
                    result = {"((" + uv.Code + ") * (" + tiling.Code + ") + (" + offset.Code + "))",
                              MaterialGraphValueType::Vector2};
                    break;
                }
                case MaterialGraphNodeKind::TextureSample:
                {
                    const auto texture = namedInput("Texture");
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    if (texture.Type != MaterialGraphValueType::Texture2D || !ValidIdentifier(texture.Code))
                        throw std::invalid_argument("Texture Sample requires a Texture2D Parameter connection.");
                    const auto sample = texture.Code + ".Sample(" + texture.Code + "Sampler, " + uv.Code + ")";
                    const auto swizzle = outputPin.Name == "RGB" ? ".rgb"
                                         : outputPin.Name == "R" ? ".r"
                                         : outputPin.Name == "G" ? ".g"
                                         : outputPin.Name == "B" ? ".b"
                                         : outputPin.Name == "A" ? ".a"
                                                                 : std::string{};
                    result = {"(" + sample + ")" + swizzle, outputPin.Type};
                    break;
                }
                case MaterialGraphNodeKind::NormalMap:
                {
                    const auto sample = Coerce(namedInput("Sample"), MaterialGraphValueType::Color);
                    const auto scale = Coerce(namedInput("Scale"), MaterialGraphValueType::Scalar);
                    result = {"DecodeNormal(" + sample.Code + ", " + scale.Code +
                                  ", input.Tangent, input.Bitangent, input.Normal)",
                              MaterialGraphValueType::Vector3};
                    break;
                }
                case MaterialGraphNodeKind::DetailNormal:
                {
                    const auto base = Coerce(namedInput("Base"), MaterialGraphValueType::Vector3);
                    const auto detail = Coerce(namedInput("Detail"), MaterialGraphValueType::Vector3);
                    const auto strength = Coerce(namedInput("Strength"), MaterialGraphValueType::Scalar);
                    result = {"BlendDetailNormal(" + base.Code + ", " + detail.Code + ", " + strength.Code + ")",
                              MaterialGraphValueType::Vector3};
                    break;
                }
                case MaterialGraphNodeKind::Parallax:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto height = Coerce(namedInput("Height"), MaterialGraphValueType::Scalar);
                    const auto scale = Coerce(namedInput("Scale"), MaterialGraphValueType::Scalar);
                    result = {"ParallaxUV(" + uv.Code + ", " + height.Code + ", " + scale.Code +
                                  ", input.ViewDirection, input.Tangent, input.Bitangent, input.Normal)",
                              MaterialGraphValueType::Vector2};
                    break;
                }
                case MaterialGraphNodeKind::Add:
                case MaterialGraphNodeKind::Subtract:
                case MaterialGraphNodeKind::Multiply:
                {
                    const auto left = Coerce(namedInput("A"), node.ValueType);
                    const auto right = Coerce(namedInput("B"), node.ValueType);
                    const auto operation = node.Kind == MaterialGraphNodeKind::Add        ? "+"
                                           : node.Kind == MaterialGraphNodeKind::Subtract ? "-"
                                                                                          : "*";
                    result = {"((" + left.Code + ") " + operation + " (" + right.Code + "))", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Divide:
                {
                    const auto left = Coerce(namedInput("A"), node.ValueType);
                    const auto right = Coerce(namedInput("B"), node.ValueType);
                    result = {"SafeDivide(" + left.Code + ", " + right.Code + ")", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Power:
                {
                    const auto base = Coerce(namedInput("Base"), node.ValueType);
                    const auto exponent = Coerce(namedInput("Exponent"), node.ValueType);
                    result = {"pow(max(abs(" + base.Code + "), 1.0e-6F), " + exponent.Code + ")", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Minimum:
                case MaterialGraphNodeKind::Maximum:
                {
                    const auto left = Coerce(namedInput("A"), node.ValueType);
                    const auto right = Coerce(namedInput("B"), node.ValueType);
                    result = {(node.Kind == MaterialGraphNodeKind::Minimum ? "min(" : "max(") + left.Code + ", " +
                                  right.Code + ")",
                              node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Lerp:
                {
                    const auto left = Coerce(namedInput("A"), node.ValueType);
                    const auto right = Coerce(namedInput("B"), node.ValueType);
                    const auto factor = Coerce(namedInput("T"), MaterialGraphValueType::Scalar);
                    result = {"lerp(" + left.Code + ", " + right.Code + ", " + factor.Code + ")", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::OneMinus:
                {
                    const auto value = Coerce(namedInput("Value"), node.ValueType);
                    result = {"(1.0F - (" + value.Code + "))", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Clamp:
                {
                    const auto value = Coerce(namedInput("Value"), node.ValueType);
                    result = {"saturate(" + value.Code + ")", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Absolute:
                case MaterialGraphNodeKind::Floor:
                case MaterialGraphNodeKind::Ceiling:
                case MaterialGraphNodeKind::Fraction:
                case MaterialGraphNodeKind::Sine:
                case MaterialGraphNodeKind::Cosine:
                case MaterialGraphNodeKind::Normalize:
                {
                    const auto value = Coerce(namedInput("Value"), node.ValueType);
                    const auto function = node.Kind == MaterialGraphNodeKind::Absolute   ? "abs"
                                          : node.Kind == MaterialGraphNodeKind::Floor    ? "floor"
                                          : node.Kind == MaterialGraphNodeKind::Ceiling  ? "ceil"
                                          : node.Kind == MaterialGraphNodeKind::Fraction ? "frac"
                                          : node.Kind == MaterialGraphNodeKind::Sine     ? "sin"
                                          : node.Kind == MaterialGraphNodeKind::Cosine   ? "cos"
                                                                                         : "normalize";
                    result = {std::string(function) + "(" + value.Code + ")", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Length:
                {
                    const auto value = namedInput("Value");
                    result = {"length(" + value.Code + ")", MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::Dot:
                {
                    const auto left = namedInput("A");
                    const auto right = Coerce(namedInput("B"), left.Type);
                    result = {"dot(" + left.Code + ", " + right.Code + ")", MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::Remap:
                {
                    const auto value = Coerce(namedInput("Value"), node.ValueType);
                    const auto inputMinimum = Coerce(namedInput("In Min"), node.ValueType);
                    const auto inputMaximum = Coerce(namedInput("In Max"), node.ValueType);
                    const auto outputMinimum = Coerce(namedInput("Out Min"), node.ValueType);
                    const auto outputMaximum = Coerce(namedInput("Out Max"), node.ValueType);
                    const auto factor = "SafeDivide((" + value.Code + ") - (" + inputMinimum.Code + "), (" +
                                        inputMaximum.Code + ") - (" + inputMinimum.Code + "))";
                    result = {"lerp(" + outputMinimum.Code + ", " + outputMaximum.Code + ", " + factor + ")",
                              node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::SmoothStep:
                {
                    const auto edgeMinimum = Coerce(namedInput("Edge Min"), node.ValueType);
                    const auto edgeMaximum = Coerce(namedInput("Edge Max"), node.ValueType);
                    const auto value = Coerce(namedInput("Value"), node.ValueType);
                    result = {"smoothstep(" + edgeMinimum.Code + ", " + edgeMaximum.Code + ", " + value.Code + ")",
                              node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Step:
                {
                    const auto edge = Coerce(namedInput("Edge"), node.ValueType);
                    const auto value = Coerce(namedInput("Value"), node.ValueType);
                    result = {"step(" + edge.Code + ", " + value.Code + ")", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Fresnel:
                {
                    const auto normal = Coerce(namedInput("Normal"), MaterialGraphValueType::Vector3);
                    const auto power = Coerce(namedInput("Power"), MaterialGraphValueType::Scalar);
                    const auto reflectance = Coerce(namedInput("F0"), MaterialGraphValueType::Scalar);
                    result = {"saturate((" + reflectance.Code + ") + (1.0F - (" + reflectance.Code +
                                  ")) * pow(1.0F - saturate(dot(normalize(" + normal.Code +
                                  "), normalize(input.ViewDirection))), max(abs(" + power.Code + "), 1.0e-4F)))",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::VertexColor:
                    result = {"input.Color", MaterialGraphValueType::Color};
                    break;
                case MaterialGraphNodeKind::WorldPosition:
                    result = {m_CurrentStage == MaterialGraphShaderStage::Vertex ? "world.xyz" : "input.WorldPosition",
                              MaterialGraphValueType::Vector3};
                    break;
                case MaterialGraphNodeKind::WorldNormal:
                    result = {m_CurrentStage == MaterialGraphShaderStage::Vertex
                                  ? "SafeNormalize(mul((float3x3)instance.NormalMatrix, input.Normal), "
                                    "float3(0.0F, 0.0F, 1.0F))"
                                  : "input.Normal",
                              MaterialGraphValueType::Vector3};
                    break;
                case MaterialGraphNodeKind::ViewDirection:
                    result = {"input.ViewDirection", MaterialGraphValueType::Vector3};
                    break;
                case MaterialGraphNodeKind::RotateUV:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto center = Coerce(namedInput("Center"), MaterialGraphValueType::Vector2);
                    const auto rotation = Coerce(namedInput("Rotation"), MaterialGraphValueType::Scalar);
                    result = {"RotateMaterialUV(" + uv.Code + ", " + center.Code + ", " + rotation.Code + ")",
                              MaterialGraphValueType::Vector2};
                    break;
                }
                case MaterialGraphNodeKind::SimpleNoise:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto scale = Coerce(namedInput("Scale"), MaterialGraphValueType::Scalar);
                    const auto detail = Coerce(namedInput("Detail"), MaterialGraphValueType::Scalar);
                    result = {"MaterialNoise(" + uv.Code + ", " + scale.Code + ", " + detail.Code + ")",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::Desaturate:
                {
                    const auto color = Coerce(namedInput("Color"), MaterialGraphValueType::Color);
                    const auto amount = Coerce(namedInput("Amount"), MaterialGraphValueType::Scalar);
                    result = {"DesaturateMaterialColor(" + color.Code + ", " + amount.Code + ")",
                              MaterialGraphValueType::Color};
                    break;
                }
                case MaterialGraphNodeKind::Posterize:
                {
                    const auto value = Coerce(namedInput("Value"), node.ValueType);
                    const auto steps = Coerce(namedInput("Steps"), MaterialGraphValueType::Scalar);
                    result = {"(floor((" + value.Code + ") * max(abs(" + steps.Code + "), 1.0F)) / max(abs(" +
                                  steps.Code + "), 1.0F))",
                              node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Round:
                case MaterialGraphNodeKind::Truncate:
                case MaterialGraphNodeKind::Sign:
                case MaterialGraphNodeKind::SquareRoot:
                case MaterialGraphNodeKind::ReciprocalSquareRoot:
                case MaterialGraphNodeKind::Exponential2:
                case MaterialGraphNodeKind::Logarithm2:
                case MaterialGraphNodeKind::Tangent:
                case MaterialGraphNodeKind::ArcSine:
                case MaterialGraphNodeKind::ArcCosine:
                case MaterialGraphNodeKind::DerivativeX:
                case MaterialGraphNodeKind::DerivativeY:
                case MaterialGraphNodeKind::FilterWidth:
                {
                    const auto value = Coerce(namedInput("Value"), node.ValueType);
                    const auto code =
                        node.Kind == MaterialGraphNodeKind::Round        ? "round(" + value.Code + ")"
                        : node.Kind == MaterialGraphNodeKind::Truncate   ? "trunc(" + value.Code + ")"
                        : node.Kind == MaterialGraphNodeKind::Sign       ? "sign(" + value.Code + ")"
                        : node.Kind == MaterialGraphNodeKind::SquareRoot ? "sqrt(max(" + value.Code + ", 0.0F))"
                        : node.Kind == MaterialGraphNodeKind::ReciprocalSquareRoot
                            ? "rsqrt(max(" + value.Code + ", 1.0e-8F))"
                        : node.Kind == MaterialGraphNodeKind::Exponential2 ? "exp2(" + value.Code + ")"
                        : node.Kind == MaterialGraphNodeKind::Logarithm2 ? "log2(max(abs(" + value.Code + "), 1.0e-8F))"
                        : node.Kind == MaterialGraphNodeKind::Tangent    ? "tan(" + value.Code + ")"
                        : node.Kind == MaterialGraphNodeKind::ArcSine   ? "asin(clamp(" + value.Code + ", -1.0F, 1.0F))"
                        : node.Kind == MaterialGraphNodeKind::ArcCosine ? "acos(clamp(" + value.Code + ", -1.0F, 1.0F))"
                        : node.Kind == MaterialGraphNodeKind::DerivativeX ? "ddx(" + value.Code + ")"
                        : node.Kind == MaterialGraphNodeKind::DerivativeY ? "ddy(" + value.Code + ")"
                                                                          : "fwidth(" + value.Code + ")";
                    result = {code, node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Modulo:
                case MaterialGraphNodeKind::ArcTangent2:
                {
                    const auto left = Coerce(namedInput("A"), node.ValueType);
                    const auto right = Coerce(namedInput("B"), node.ValueType);
                    result = {(node.Kind == MaterialGraphNodeKind::Modulo ? "fmod(" : "atan2(") + left.Code + ", " +
                                  right.Code + ")",
                              node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Cross:
                case MaterialGraphNodeKind::Distance:
                case MaterialGraphNodeKind::Reflect:
                {
                    const auto left = Coerce(namedInput("A"), MaterialGraphValueType::Vector3);
                    const auto right = Coerce(namedInput("B"), MaterialGraphValueType::Vector3);
                    const auto function = node.Kind == MaterialGraphNodeKind::Cross      ? "cross"
                                          : node.Kind == MaterialGraphNodeKind::Distance ? "distance"
                                                                                         : "reflect";
                    result = {std::string(function) + "(" + left.Code + ", " + right.Code + ")", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Refract:
                {
                    const auto incident = Coerce(namedInput("Incident"), MaterialGraphValueType::Vector3);
                    const auto normal = Coerce(namedInput("Normal"), MaterialGraphValueType::Vector3);
                    const auto ior = Coerce(namedInput("IOR"), MaterialGraphValueType::Scalar);
                    result = {"refract(normalize(" + incident.Code + "), normalize(" + normal.Code + "), rcp(max(abs(" +
                                  ior.Code + "), 1.0e-4F)))",
                              MaterialGraphValueType::Vector3};
                    break;
                }
                case MaterialGraphNodeKind::AppendVector:
                {
                    const auto xyz = Coerce(namedInput("XYZ"), MaterialGraphValueType::Vector3);
                    const auto w = Coerce(namedInput("W"), MaterialGraphValueType::Scalar);
                    result = {"float4(" + xyz.Code + ", " + w.Code + ")", MaterialGraphValueType::Vector4};
                    break;
                }
                case MaterialGraphNodeKind::ComponentMask:
                {
                    const auto value = Coerce(namedInput("Value"), MaterialGraphValueType::Vector4);
                    const auto swizzle = outputPin.Name == "R"     ? ".x"
                                         : outputPin.Name == "G"   ? ".y"
                                         : outputPin.Name == "B"   ? ".z"
                                         : outputPin.Name == "A"   ? ".w"
                                         : outputPin.Name == "RG"  ? ".xy"
                                         : outputPin.Name == "RGB" ? ".xyz"
                                                                   : std::string{};
                    result = {"(" + value.Code + ")" + swizzle, outputPin.Type};
                    break;
                }
                case MaterialGraphNodeKind::UV1:
                    result = {"input.UV1", MaterialGraphValueType::Vector2};
                    break;
                case MaterialGraphNodeKind::WorldTangent:
                    result = {m_CurrentStage == MaterialGraphShaderStage::Vertex
                                  ? "SafeNormalize(mul((float3x3)instance.Model, input.Tangent.xyz), "
                                    "float3(1.0F, 0.0F, 0.0F))"
                                  : "input.Tangent",
                              MaterialGraphValueType::Vector3};
                    break;
                case MaterialGraphNodeKind::CameraPosition:
                    result = {"mul(inverse(View), float4(0.0F, 0.0F, 0.0F, 1.0F)).xyz",
                              MaterialGraphValueType::Vector3};
                    break;
                case MaterialGraphNodeKind::ObjectPosition:
                    result = {m_CurrentStage == MaterialGraphShaderStage::Vertex
                                  ? "mul(instance.Model, float4(0.0F, 0.0F, 0.0F, 1.0F)).xyz"
                                  : "input.ObjectPosition",
                              MaterialGraphValueType::Vector3};
                    break;
                case MaterialGraphNodeKind::Time:
                    result = {"FrameParameters.x", MaterialGraphValueType::Scalar};
                    break;
                case MaterialGraphNodeKind::DeltaTime:
                    result = {"FrameParameters.y", MaterialGraphValueType::Scalar};
                    break;
                case MaterialGraphNodeKind::ScreenPosition:
                    result = {"input.Position.xy", MaterialGraphValueType::Vector2};
                    break;
                case MaterialGraphNodeKind::DepthFade:
                {
                    const auto distance = Coerce(namedInput("Distance"), MaterialGraphValueType::Scalar);
                    const auto fadeDistance = Coerce(namedInput("Fade Distance"), MaterialGraphValueType::Scalar);
                    result = {"saturate((" + distance.Code + ") / max(abs(" + fadeDistance.Code + "), 1.0e-4F))",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::Luminance:
                {
                    const auto color = Coerce(namedInput("Color"), MaterialGraphValueType::Color);
                    result = {"dot((" + color.Code + ").rgb, float3(0.2126F, 0.7152F, 0.0722F))",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::HueShift:
                {
                    const auto color = Coerce(namedInput("Color"), MaterialGraphValueType::Color);
                    const auto shift = Coerce(namedInput("Shift"), MaterialGraphValueType::Scalar);
                    result = {"HueShiftMaterialColor(" + color.Code + ", " + shift.Code + ")",
                              MaterialGraphValueType::Color};
                    break;
                }
                case MaterialGraphNodeKind::Checkerboard:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto colorA = Coerce(namedInput("Color A"), MaterialGraphValueType::Color);
                    const auto colorB = Coerce(namedInput("Color B"), MaterialGraphValueType::Color);
                    const auto scale = Coerce(namedInput("Scale"), MaterialGraphValueType::Vector2);
                    result = {"MaterialCheckerboard(" + uv.Code + ", " + colorA.Code + ", " + colorB.Code + ", " +
                                  scale.Code + ")",
                              MaterialGraphValueType::Color};
                    break;
                }
                case MaterialGraphNodeKind::VoronoiNoise:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto scale = Coerce(namedInput("Scale"), MaterialGraphValueType::Scalar);
                    const auto jitter = Coerce(namedInput("Jitter"), MaterialGraphValueType::Scalar);
                    const auto voronoi = "MaterialVoronoi(" + uv.Code + ", " + scale.Code + ", " + jitter.Code + ")";
                    result = {voronoi + (outputPin.Name == "Cell" ? ".y" : ".x"), MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::Panner:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto speed = Coerce(namedInput("Speed"), MaterialGraphValueType::Vector2);
                    const auto time = Coerce(namedInput("Time"), MaterialGraphValueType::Scalar);
                    result = {"((" + uv.Code + ") + (" + speed.Code + ") * (" + time.Code + "))",
                              MaterialGraphValueType::Vector2};
                    break;
                }
                case MaterialGraphNodeKind::PolarCoordinates:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto center = Coerce(namedInput("Center"), MaterialGraphValueType::Vector2);
                    const auto radialScale = Coerce(namedInput("Radial Scale"), MaterialGraphValueType::Scalar);
                    const auto lengthScale = Coerce(namedInput("Length Scale"), MaterialGraphValueType::Scalar);
                    const auto local = "((" + uv.Code + ") - (" + center.Code + "))";
                    result = {"float2(length(" + local + ") * (" + radialScale.Code + "), frac(atan2(" + local +
                                  ".y, " + local + ".x) / (2.0F * Pi) + 0.5F) * (" + lengthScale.Code + "))",
                              MaterialGraphValueType::Vector2};
                    break;
                }
                case MaterialGraphNodeKind::SphereMask:
                {
                    const auto first = Coerce(namedInput("A"), MaterialGraphValueType::Vector3);
                    const auto second = Coerce(namedInput("B"), MaterialGraphValueType::Vector3);
                    const auto radius = Coerce(namedInput("Radius"), MaterialGraphValueType::Scalar);
                    const auto hardness = Coerce(namedInput("Hardness"), MaterialGraphValueType::Scalar);
                    result = {"saturate((1.0F - distance(" + first.Code + ", " + second.Code + ") / max(abs(" +
                                  radius.Code + "), 1.0e-5F)) * max(abs(" + hardness.Code + "), 1.0F))",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::RadialGradient:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto center = Coerce(namedInput("Center"), MaterialGraphValueType::Vector2);
                    const auto radius = Coerce(namedInput("Radius"), MaterialGraphValueType::Scalar);
                    const auto density = Coerce(namedInput("Density"), MaterialGraphValueType::Scalar);
                    result = {"saturate(((" + radius.Code + ") - distance(" + uv.Code + ", " + center.Code +
                                  ")) * max(abs(" + density.Code + "), 1.0e-4F))",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::LinearGradient:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto direction = Coerce(namedInput("Direction"), MaterialGraphValueType::Vector2);
                    const auto offset = Coerce(namedInput("Offset"), MaterialGraphValueType::Scalar);
                    result = {"saturate(dot(" + uv.Code + ", (" + direction.Code + ") / max(length(" + direction.Code +
                                  "), 1.0e-5F)) + (" + offset.Code + "))",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::Contrast:
                {
                    const auto color = Coerce(namedInput("Color"), MaterialGraphValueType::Color);
                    const auto contrast = Coerce(namedInput("Contrast"), MaterialGraphValueType::Scalar);
                    const auto pivot = Coerce(namedInput("Pivot"), MaterialGraphValueType::Scalar);
                    result = {"float4((" + color.Code + ").rgb * (" + contrast.Code + ") + (" + pivot.Code +
                                  ") * (1.0F - (" + contrast.Code + ")), (" + color.Code + ").a)",
                              MaterialGraphValueType::Color};
                    break;
                }
                case MaterialGraphNodeKind::Saturation:
                {
                    const auto color = Coerce(namedInput("Color"), MaterialGraphValueType::Color);
                    const auto saturation = Coerce(namedInput("Saturation"), MaterialGraphValueType::Scalar);
                    result = {"DesaturateMaterialColor(" + color.Code + ", 1.0F - (" + saturation.Code + "))",
                              MaterialGraphValueType::Color};
                    break;
                }
                case MaterialGraphNodeKind::BlendOverlay:
                {
                    const auto base = Coerce(namedInput("Base"), MaterialGraphValueType::Color);
                    const auto blend = Coerce(namedInput("Blend"), MaterialGraphValueType::Color);
                    const auto opacity = Coerce(namedInput("Opacity"), MaterialGraphValueType::Scalar);
                    result = {"MaterialOverlayBlend(" + base.Code + ", " + blend.Code + ", " + opacity.Code + ")",
                              MaterialGraphValueType::Color};
                    break;
                }
                case MaterialGraphNodeKind::Blackbody:
                {
                    const auto temperature = Coerce(namedInput("Temperature"), MaterialGraphValueType::Scalar);
                    result = {"MaterialBlackbody(" + temperature.Code + ")", MaterialGraphValueType::Color};
                    break;
                }
                case MaterialGraphNodeKind::ReflectionVector:
                {
                    const auto normal = Coerce(namedInput("Normal"), MaterialGraphValueType::Vector3);
                    result = {"reflect(-SafeNormalize(input.ViewDirection, input.Normal), SafeNormalize(" +
                                  normal.Code + ", input.Normal))",
                              MaterialGraphValueType::Vector3};
                    break;
                }
                case MaterialGraphNodeKind::FacingRatio:
                {
                    const auto normal = Coerce(namedInput("Normal"), MaterialGraphValueType::Vector3);
                    const auto power = Coerce(namedInput("Power"), MaterialGraphValueType::Scalar);
                    result = {"pow(saturate(1.0F - dot(SafeNormalize(" + normal.Code +
                                  ", input.Normal), SafeNormalize(input.ViewDirection, input.Normal))), max(abs(" +
                                  power.Code + "), 1.0e-4F))",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::Dither:
                {
                    const auto alpha = Coerce(namedInput("Alpha"), MaterialGraphValueType::Scalar);
                    const auto screenPosition = Coerce(namedInput("Screen Position"), MaterialGraphValueType::Vector2);
                    result = {"step(MaterialDitherThreshold(" + screenPosition.Code + "), saturate(" + alpha.Code +
                                  "))",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::GradientNoise:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto scale = Coerce(namedInput("Scale"), MaterialGraphValueType::Scalar);
                    result = {"MaterialNoise(" + uv.Code + ", " + scale.Code + ", 0.65F)",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::Wave:
                {
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto direction = Coerce(namedInput("Direction"), MaterialGraphValueType::Vector2);
                    const auto frequency = Coerce(namedInput("Frequency"), MaterialGraphValueType::Scalar);
                    const auto phase = Coerce(namedInput("Phase"), MaterialGraphValueType::Scalar);
                    result = {"(sin(dot(" + uv.Code + ", (" + direction.Code + ") / max(length(" + direction.Code +
                                  "), 1.0e-5F)) * (" + frequency.Code + ") * (2.0F * Pi) + (" + phase.Code +
                                  ")) * 0.5F + 0.5F)",
                              MaterialGraphValueType::Scalar};
                    break;
                }
                case MaterialGraphNodeKind::TriplanarSample:
                {
                    const auto texture = namedInput("Texture");
                    const auto position = Coerce(namedInput("Position"), MaterialGraphValueType::Vector3);
                    const auto normal = Coerce(namedInput("Normal"), MaterialGraphValueType::Vector3);
                    const auto scale = Coerce(namedInput("Scale"), MaterialGraphValueType::Scalar);
                    const auto sharpness = Coerce(namedInput("Blend Sharpness"), MaterialGraphValueType::Scalar);
                    if (texture.Type != MaterialGraphValueType::Texture2D || !ValidIdentifier(texture.Code))
                        throw std::invalid_argument("Triplanar Sample requires a Texture2D Parameter connection.");
                    const auto weights = "(pow(abs(SafeNormalize(" + normal.Code + ", input.Normal)), max(abs(" +
                                         sharpness.Code + "), 1.0F)) / max(dot(pow(abs(SafeNormalize(" + normal.Code +
                                         ", input.Normal)), max(abs(" + sharpness.Code +
                                         "), 1.0F)), 1.0F.xxx), 1.0e-5F))";
                    const auto scaled = "((" + position.Code + ") * (" + scale.Code + "))";
                    const auto sample = "(" + texture.Code + ".Sample(" + texture.Code + "Sampler, " + scaled +
                                        ".zy) * " + weights + ".x + " + texture.Code + ".Sample(" + texture.Code +
                                        "Sampler, " + scaled + ".xz) * " + weights + ".y + " + texture.Code +
                                        ".Sample(" + texture.Code + "Sampler, " + scaled + ".xy) * " + weights + ".z)";
                    const auto swizzle = outputPin.Name == "RGB" ? ".rgb"
                                         : outputPin.Name == "R" ? ".r"
                                         : outputPin.Name == "G" ? ".g"
                                         : outputPin.Name == "B" ? ".b"
                                         : outputPin.Name == "A" ? ".a"
                                                                 : std::string{};
                    result = {sample + swizzle, outputPin.Type};
                    break;
                }
                case MaterialGraphNodeKind::TextureSampleLevel:
                {
                    const auto texture = namedInput("Texture");
                    const auto uv = Coerce(namedInput("UV"), MaterialGraphValueType::Vector2);
                    const auto level = Coerce(namedInput("Mip Level"), MaterialGraphValueType::Scalar);
                    if (texture.Type != MaterialGraphValueType::Texture2D || !ValidIdentifier(texture.Code))
                        throw std::invalid_argument("Texture Sample Level requires a Texture2D Parameter connection.");
                    const auto sample = texture.Code + ".SampleLevel(" + texture.Code + "Sampler, " + uv.Code +
                                        ", max(" + level.Code + ", 0.0F))";
                    const auto swizzle = outputPin.Name == "RGB" ? ".rgb"
                                         : outputPin.Name == "R" ? ".r"
                                         : outputPin.Name == "G" ? ".g"
                                         : outputPin.Name == "B" ? ".b"
                                         : outputPin.Name == "A" ? ".a"
                                                                 : std::string{};
                    result = {"(" + sample + ")" + swizzle, outputPin.Type};
                    break;
                }
                case MaterialGraphNodeKind::HeightToNormal:
                {
                    const auto height = Coerce(namedInput("Height"), MaterialGraphValueType::Scalar);
                    const auto strength = Coerce(namedInput("Strength"), MaterialGraphValueType::Scalar);
                    result = {"SafeNormalize(float3(-ddx(" + height.Code + ") * (" + strength.Code + "), -ddy(" +
                                  height.Code + ") * (" + strength.Code + "), 1.0F), input.Normal)",
                              MaterialGraphValueType::Vector3};
                    break;
                }
                case MaterialGraphNodeKind::FlattenNormal:
                {
                    const auto normal = Coerce(namedInput("Normal"), MaterialGraphValueType::Vector3);
                    const auto strength = Coerce(namedInput("Strength"), MaterialGraphValueType::Scalar);
                    result = {"SafeNormalize(lerp(float3(0.0F, 0.0F, 1.0F), " + normal.Code + ", saturate(" +
                                  strength.Code + ")), input.Normal)",
                              MaterialGraphValueType::Vector3};
                    break;
                }
                case MaterialGraphNodeKind::MakeMaterialAttributes:
                {
                    constexpr std::array inputs{
                        std::string_view("BaseColor"),       std::string_view("Metallic"),
                        std::string_view("Roughness"),       std::string_view("Specular"),
                        std::string_view("ClearCoat"),       std::string_view("ClearCoatRoughness"),
                        std::string_view("SheenColor"),      std::string_view("SheenRoughness"),
                        std::string_view("Normal"),          std::string_view("Emission"),
                        std::string_view("Occlusion"),       std::string_view("Opacity"),
                        std::string_view("SubsurfaceColor"), std::string_view("Subsurface"),
                        std::string_view("Anisotropy"),      std::string_view("Tangent"),
                        std::string_view("Transmission"),    std::string_view("IndexOfRefraction"),
                        std::string_view("Refraction"),      std::string_view("Thickness")};
                    std::string arguments;
                    for (const auto name : inputs)
                    {
                        if (!arguments.empty())
                            arguments += ", ";
                        arguments += namedInput(name).Code;
                    }
                    result = {"MakeMaterialGraphSurface(" + arguments + ")",
                              MaterialGraphValueType::MaterialAttributes};
                    break;
                }
                case MaterialGraphNodeKind::BreakMaterialAttributes:
                {
                    const auto attributes =
                        Coerce(namedInput("Attributes"), MaterialGraphValueType::MaterialAttributes);
                    result = {"(" + attributes.Code + ")." + outputPin.Name, outputPin.Type};
                    break;
                }
                case MaterialGraphNodeKind::BlendMaterialAttributes:
                {
                    const auto first = Coerce(namedInput("A"), MaterialGraphValueType::MaterialAttributes);
                    const auto second = Coerce(namedInput("B"), MaterialGraphValueType::MaterialAttributes);
                    const auto alpha = Coerce(namedInput("Alpha"), MaterialGraphValueType::Scalar);
                    result = {"BlendMaterialGraphSurfaces(" + first.Code + ", " + second.Code + ", " + alpha.Code + ")",
                              MaterialGraphValueType::MaterialAttributes};
                    break;
                }
                case MaterialGraphNodeKind::StandardSurfaceBsdf:
                {
                    const auto baseColor = Coerce(namedInput("BaseColor"), MaterialGraphValueType::Color);
                    const auto metallic = Coerce(namedInput("Metallic"), MaterialGraphValueType::Scalar);
                    const auto roughness = Coerce(namedInput("Roughness"), MaterialGraphValueType::Scalar);
                    const auto specular = Coerce(namedInput("Specular"), MaterialGraphValueType::Scalar);
                    const auto normal = Coerce(namedInput("Normal"), MaterialGraphValueType::Vector3);
                    const auto emission = Coerce(namedInput("Emission"), MaterialGraphValueType::Color);
                    const auto opacity = Coerce(namedInput("Opacity"), MaterialGraphValueType::Scalar);
                    result = {"MakeStandardMaterialGraphBsdf(" + baseColor.Code + ", " + metallic.Code + ", " +
                                  roughness.Code + ", " + specular.Code + ", " + normal.Code + ", " + emission.Code +
                                  ", " + opacity.Code + ")",
                              MaterialGraphValueType::Bsdf};
                    break;
                }
                case MaterialGraphNodeKind::ClearCoatBsdf:
                {
                    const auto base = Coerce(namedInput("Base"), MaterialGraphValueType::Bsdf);
                    const auto weight = Coerce(namedInput("Weight"), MaterialGraphValueType::Scalar);
                    const auto roughness = Coerce(namedInput("Roughness"), MaterialGraphValueType::Scalar);
                    result = {"ApplyMaterialGraphClearCoat(" + base.Code + ", " + weight.Code + ", " + roughness.Code +
                                  ")",
                              MaterialGraphValueType::Bsdf};
                    break;
                }
                case MaterialGraphNodeKind::SheenBsdf:
                {
                    const auto base = Coerce(namedInput("Base"), MaterialGraphValueType::Bsdf);
                    const auto color = Coerce(namedInput("Color"), MaterialGraphValueType::Color);
                    const auto weight = Coerce(namedInput("Weight"), MaterialGraphValueType::Scalar);
                    const auto roughness = Coerce(namedInput("Roughness"), MaterialGraphValueType::Scalar);
                    result = {"ApplyMaterialGraphSheen(" + base.Code + ", " + color.Code + ", " + weight.Code + ", " +
                                  roughness.Code + ")",
                              MaterialGraphValueType::Bsdf};
                    break;
                }
                case MaterialGraphNodeKind::SubsurfaceBsdf:
                {
                    const auto base = Coerce(namedInput("Base"), MaterialGraphValueType::Bsdf);
                    const auto color = Coerce(namedInput("Color"), MaterialGraphValueType::Color);
                    const auto weight = Coerce(namedInput("Weight"), MaterialGraphValueType::Scalar);
                    result = {"ApplyMaterialGraphSubsurface(" + base.Code + ", " + color.Code + ", " + weight.Code +
                                  ")",
                              MaterialGraphValueType::Bsdf};
                    break;
                }
                case MaterialGraphNodeKind::TransmissionBsdf:
                {
                    const auto base = Coerce(namedInput("Base"), MaterialGraphValueType::Bsdf);
                    const auto weight = Coerce(namedInput("Weight"), MaterialGraphValueType::Scalar);
                    const auto ior = Coerce(namedInput("IndexOfRefraction"), MaterialGraphValueType::Scalar);
                    const auto refraction = Coerce(namedInput("Refraction"), MaterialGraphValueType::Scalar);
                    const auto thickness = Coerce(namedInput("Thickness"), MaterialGraphValueType::Scalar);
                    result = {"ApplyMaterialGraphTransmission(" + base.Code + ", " + weight.Code + ", " + ior.Code +
                                  ", " + refraction.Code + ", " + thickness.Code + ")",
                              MaterialGraphValueType::Bsdf};
                    break;
                }
                case MaterialGraphNodeKind::BsdfToMaterialAttributes:
                {
                    const auto bsdf = Coerce(namedInput("BSDF"), MaterialGraphValueType::Bsdf);
                    result = {"MaterialGraphSurfaceFromBsdf(" + bsdf.Code + ")",
                              MaterialGraphValueType::MaterialAttributes};
                    break;
                }
                case MaterialGraphNodeKind::Keyword:
                    result = {std::ranges::find(m_Keywords, node.Symbol) == m_Keywords.end() ? "0.0F" : "1.0F",
                              MaterialGraphValueType::Scalar};
                    break;
                case MaterialGraphNodeKind::StaticSwitch:
                {
                    const auto condition = Coerce(namedInput("Condition"), MaterialGraphValueType::Scalar);
                    result = condition.Code == "0.0F" ? Coerce(namedInput("False"), node.ValueType)
                             : condition.Code == "1.0F"
                                 ? Coerce(namedInput("True"), node.ValueType)
                                 : Expression{"((" + condition.Code + ") != 0.0F ? (" +
                                                  Coerce(namedInput("True"), node.ValueType).Code + ") : (" +
                                                  Coerce(namedInput("False"), node.ValueType).Code + "))",
                                              node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Custom:
                {
                    std::string arguments;
                    for (const auto& pin : node.Pins)
                    {
                        if (pin.Direction != MaterialGraphPinDirection::Input)
                            continue;
                        if (!arguments.empty())
                            arguments += ", ";
                        arguments += Input(node, pin).Code;
                    }
                    result = {node.Function + "(" + arguments + ")", node.ValueType};
                    break;
                }
                case MaterialGraphNodeKind::Master:
                    throw std::invalid_argument("Master node outputs cannot feed another node.");
                }
                m_Visiting.erase(endpoint.Node);
                m_Cache.emplace(endpoint, result);
                return result;
            }

            void ValidateIncludes()
            {
                std::set<std::filesystem::path> visited;
                std::set<std::filesystem::path> visiting;
                for (const auto& node : m_Definition.Nodes)
                    if (node.Kind == MaterialGraphNodeKind::Custom)
                    {
                        DiscoverInclude(node.Include, visited, visiting);
                        const auto normalized = node.Include.lexically_normal();
                        const auto root = std::ranges::find_if(
                            m_Definition.IncludeRoots,
                            [&](const auto& candidate)
                            {
                                const auto relative = normalized.lexically_relative(candidate);
                                return !relative.empty() && !relative.generic_string().starts_with("..");
                            });
                        if (root == m_Definition.IncludeRoots.end())
                            throw std::logic_error("Validated custom Material Graph include root became unavailable.");
                        m_CustomIncludes.push_back(normalized.lexically_relative(*root));
                    }
                std::ranges::sort(m_CustomIncludes);
                m_CustomIncludes.erase(std::unique(m_CustomIncludes.begin(), m_CustomIncludes.end()),
                                       m_CustomIncludes.end());
            }

            void DiscoverInclude(const std::filesystem::path& path, std::set<std::filesystem::path>& visited,
                                 std::set<std::filesystem::path>& visiting)
            {
                const auto normalized = path.lexically_normal();
                if (!SafeRelativePath(normalized))
                    throw std::invalid_argument("Custom Material Graph includes must be confined relative paths.");
                const bool rooted =
                    std::ranges::any_of(m_Definition.IncludeRoots,
                                        [&](const auto& root)
                                        {
                                            const auto relative = normalized.lexically_relative(root);
                                            return !relative.empty() && !relative.generic_string().starts_with("..");
                                        });
                if (!rooted)
                    throw std::invalid_argument("Custom Material Graph include is outside its allowed roots: " +
                                                normalized.generic_string());
                if (!m_Options.ReadInclude)
                    throw std::invalid_argument("Custom Material Graph nodes require a confined include reader.");
                if (visiting.contains(normalized))
                    throw std::invalid_argument("Custom Material Graph include cycle detected at " +
                                                normalized.generic_string());
                if (!visited.insert(normalized).second)
                    return;
                if (visited.size() > m_Options.MaximumCustomIncludes)
                    throw std::invalid_argument("Custom Material Graph include graph exceeds its configured limit.");
                const auto source = m_Options.ReadInclude(normalized);
                if (!source || source->size() > std::size_t{1024} * 1024U)
                    throw std::invalid_argument("Custom Material Graph include is missing or too large: " +
                                                normalized.generic_string());
                if (source->find('\0') != std::string::npos)
                    throw std::invalid_argument("Custom Material Graph include contains binary data.");
                visiting.insert(normalized);
                std::istringstream lines(*source);
                std::string line;
                while (std::getline(lines, line))
                {
                    const auto hash = line.find('#');
                    if (hash == std::string::npos || line.find("include", hash) == std::string::npos)
                        continue;
                    const auto quote = line.find_first_of("\"<", hash);
                    const auto close =
                        quote == std::string::npos ? std::string::npos : line.find_first_of("\">", quote + 1U);
                    if (quote == std::string::npos || close == std::string::npos)
                        throw std::invalid_argument("Custom Material Graph include directive is malformed.");
                    const std::filesystem::path child = line.substr(quote + 1U, close - quote - 1U);
                    if (!SafeRelativePath(child))
                        throw std::invalid_argument("Custom Material Graph nested include is unsafe.");
                    auto resolved = (normalized.parent_path() / child).lexically_normal();
                    if (!m_Options.ReadInclude(resolved))
                    {
                        const auto found =
                            std::ranges::find_if(m_Definition.IncludeRoots, [&](const auto& root)
                                                 { return m_Options.ReadInclude(root / child).has_value(); });
                        if (found == m_Definition.IncludeRoots.end())
                            throw std::invalid_argument("Custom Material Graph nested include could not be resolved: " +
                                                        child.generic_string());
                        resolved = (*found / child).lexically_normal();
                    }
                    DiscoverInclude(resolved, visited, visiting);
                }
                visiting.erase(normalized);
                m_Dependencies.push_back(normalized);
            }

            const MaterialGraphDefinition& m_Definition;
            const MaterialGraphCompileOptions& m_Options;
            std::span<const std::string> m_Keywords;
            std::vector<ShaderPropertyDefinition>& m_Properties;
            std::vector<std::filesystem::path>& m_Dependencies;
            std::unordered_map<MaterialGraphEndpoint, MaterialGraphEndpoint, EndpointHash> m_Incoming;
            std::unordered_map<MaterialGraphEndpoint, Expression, EndpointHash> m_Cache;
            std::unordered_set<AssetId> m_Visiting;
            std::unordered_set<AssetId> m_Preparing;
            std::vector<std::filesystem::path> m_CustomIncludes;
            MaterialGraphShaderStage m_CurrentStage = MaterialGraphShaderStage::Fragment;
            bool m_UsesVertexMaterialParameters = false;
        };

        [[nodiscard]] bool MaterialValueMatches(const MaterialPropertyValue& value, const MaterialGraphValueType type)
        {
            return value.index() == static_cast<std::size_t>(type);
        }

        [[nodiscard]] MaterialPropertyValue ToMaterialPropertyValue(const MaterialGraphValue& decoded)
        {
            return std::visit(
                [](const auto& value) -> MaterialPropertyValue
                {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::same_as<T, MaterialGraphMaterialAttributesValue> ||
                                  std::same_as<T, MaterialGraphBsdfValue>)
                        throw std::invalid_argument("Material Graph structured values cannot become properties.");
                    else
                        return value;
                },
                decoded);
        }

        void ValidateMaterialGraphInstanceDefinition(const MaterialGraphInstanceDefinition& definition)
        {
            if (definition.SchemaVersion != 1 || !definition.Parent ||
                definition.Properties.size() > MaximumGraphProperties ||
                definition.KeywordOverrides.size() > MaximumGraphKeywords)
                throw std::invalid_argument(
                    "Material Graph instance schema, parent, or collection bounds are invalid.");
            for (const auto& [name, value] : definition.Properties)
            {
                if (!ValidIdentifier(name))
                    throw std::invalid_argument("Material Graph instance property names must be identifiers.");
                ValidateFiniteValue(value);
            }
            for (const auto& [name, value] : definition.KeywordOverrides)
                if (!ValidIdentifier(name) || (value != "true" && value != "false" && !ValidIdentifier(value)))
                    throw std::invalid_argument("Material Graph instance keyword overrides are invalid.");
        }

        [[nodiscard]] Json EncodeInstanceJson(const MaterialGraphInstanceDefinition& definition)
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

        [[nodiscard]] MaterialGraphInstanceDefinition DecodeInstanceJson(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Material Graph instance source must be an object.");
            MaterialGraphInstanceDefinition result;
            result.SchemaVersion = source.value("schemaVersion", 0U);
            result.Parent = AssetId::Parse(source.at("parent").get<std::string>());
            const auto& properties = source.value("properties", Json::object());
            const auto& keywords = source.value("keywords", Json::object());
            if (!properties.is_object() || properties.size() > MaximumGraphProperties || !keywords.is_object() ||
                keywords.size() > MaximumGraphKeywords)
                throw std::invalid_argument("Material Graph instance properties and keywords must be objects.");
            for (const auto& [name, encoded] : properties.items())
            {
                const auto type = static_cast<MaterialGraphValueType>(encoded.at("type").get<std::uint8_t>());
                if (type > MaterialGraphValueType::Texture2D)
                    throw std::invalid_argument("Material Graph instance property type is invalid.");
                const auto decoded = DecodeValue(encoded.at("value"), type);
                result.Properties.emplace(name, ToMaterialPropertyValue(decoded));
            }
            for (const auto& [name, encoded] : keywords.items())
                result.KeywordOverrides.emplace(name, encoded.get<std::string>());
            ValidateMaterialGraphInstanceDefinition(result);
            return result;
        }
    } // namespace

    bool MaterialGraphCompilation::Succeeded() const noexcept
    {
        return !Variants.empty() &&
               std::ranges::none_of(Diagnostics, [](const MaterialGraphDiagnostic& diagnostic)
                                    { return diagnostic.Severity == MaterialGraphDiagnosticSeverity::Error; });
    }

    MaterialGraphAsset::MaterialGraphAsset(MaterialGraphDefinition definition) : m_Definition(std::move(definition)) {}

    std::size_t MaterialGraphAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& node : m_Definition.Nodes)
            result += sizeof(node) + node.TypeId.size() + node.Name.size() + node.Symbol.size() + node.Function.size() +
                      node.Include.native().size() * sizeof(std::filesystem::path::value_type) +
                      node.ParameterMetadata.Description.size() + node.ParameterMetadata.Category.size() +
                      node.Pins.size() * sizeof(MaterialGraphPin);
        result += m_Definition.Connections.size() * sizeof(MaterialGraphConnection);
        return result;
    }

    Ref<MaterialGraphAsset> MaterialGraphAsset::Decode(const std::span<const std::byte> bytes)
    {
        try
        {
            if (bytes.size() > MaximumGraphAssetBytes)
                throw std::invalid_argument("Material Graph cooked data exceeds its byte limit.");
            return CreateRef<MaterialGraphAsset>(DecodeGraphJson(Json::from_cbor(ToUnsigned(bytes))));
        }
        catch (const std::exception& error)
        {
            throw std::invalid_argument(std::string("Material Graph asset decode failed: ") + error.what());
        }
    }

    std::vector<std::byte> MaterialGraphAsset::Encode(const MaterialGraphDefinition& definition)
    {
        ValidateMaterialGraph(definition);
        return ToBytes(Json::to_cbor(EncodeGraphJson(definition)));
    }

    MaterialGraphDefinition MaterialGraphAsset::DecodeSource(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > MaximumGraphAssetBytes)
            throw std::invalid_argument("Material Graph source exceeds its byte limit.");
        return DecodeGraphJson(Json::parse(Text(bytes)));
    }

    std::vector<std::byte> MaterialGraphAsset::EncodeSource(const MaterialGraphDefinition& definition)
    {
        ValidateMaterialGraph(definition);
        const auto text = EncodeGraphJson(definition).dump(2) + '\n';
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<MaterialGraphAsset> MaterialGraphAsset::Error()
    {
        return CreateRef<MaterialGraphAsset>(CreateDefaultMaterialGraph(MaterialGraphOutput::Unlit));
    }

    MaterialGraphInstanceAsset::MaterialGraphInstanceAsset(MaterialGraphInstanceDefinition definition)
        : m_Definition(std::move(definition))
    {
    }

    std::size_t MaterialGraphInstanceAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& [name, value] : m_Definition.Properties)
        {
            (void)value;
            result += name.size() + sizeof(MaterialPropertyValue);
        }
        for (const auto& [name, value] : m_Definition.KeywordOverrides)
            result += name.size() + value.size();
        return result;
    }

    Ref<MaterialGraphInstanceAsset> MaterialGraphInstanceAsset::Decode(const std::span<const std::byte> bytes)
    {
        try
        {
            if (bytes.size() > MaximumGraphAssetBytes)
                throw std::invalid_argument("Material Graph instance cooked data exceeds its byte limit.");
            return CreateRef<MaterialGraphInstanceAsset>(DecodeInstanceJson(Json::from_cbor(ToUnsigned(bytes))));
        }
        catch (const std::exception& error)
        {
            throw std::invalid_argument(std::string("Material Graph instance asset decode failed: ") + error.what());
        }
    }

    std::vector<std::byte> MaterialGraphInstanceAsset::Encode(const MaterialGraphInstanceDefinition& definition)
    {
        ValidateMaterialGraphInstanceDefinition(definition);
        return ToBytes(Json::to_cbor(EncodeInstanceJson(definition)));
    }

    MaterialGraphInstanceDefinition MaterialGraphInstanceAsset::DecodeSource(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > MaximumGraphAssetBytes)
            throw std::invalid_argument("Material Graph instance source exceeds its byte limit.");
        return DecodeInstanceJson(Json::parse(Text(bytes)));
    }

    std::vector<std::byte> MaterialGraphInstanceAsset::EncodeSource(const MaterialGraphInstanceDefinition& definition)
    {
        ValidateMaterialGraphInstanceDefinition(definition);
        const auto text = EncodeInstanceJson(definition).dump(2) + '\n';
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<MaterialGraphInstanceAsset> MaterialGraphInstanceAsset::Error()
    {
        return CreateRef<MaterialGraphInstanceAsset>();
    }

    MaterialGraphNode CreateMaterialGraphNode(const std::string_view typeId, const MaterialGraphValueType valueType)
    {
        const auto* descriptor = FindMaterialGraphNodeDescriptor(typeId);
        if (!descriptor)
            throw std::invalid_argument("Unknown Material Graph node type ID: " + std::string(typeId) + '.');
        return CreateMaterialGraphNode(descriptor->Kind, valueType);
    }

    MaterialGraphNode CreateMaterialGraphNode(const MaterialGraphNodeKind kind, const MaterialGraphValueType valueType)
    {
        MaterialGraphNode node;
        node.Id = AssetId::Generate();
        node.Kind = kind;
        node.TypeId = MaterialGraphNodeTypeId(kind);
        if (node.TypeId.empty())
            throw std::invalid_argument("Unknown Material Graph node kind.");
        node.ValueType = valueType;
        node.Value = DefaultValue(valueType);
        const auto input = [&](const std::string_view name, const MaterialGraphValueType type, MaterialGraphValue value)
        { AddPin(node, std::string(name), type, MaterialGraphPinDirection::Input, value); };
        const auto output = [&](const std::string_view name, const MaterialGraphValueType type)
        { AddPin(node, std::string(name), type, MaterialGraphPinDirection::Output, DefaultValue(type)); };
        const auto materialAttributeInputs = [&]
        {
            input("BaseColor", MaterialGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Metallic", MaterialGraphValueType::Scalar, 0.0F);
            input("Roughness", MaterialGraphValueType::Scalar, 0.5F);
            input("Specular", MaterialGraphValueType::Scalar, 0.5F);
            input("ClearCoat", MaterialGraphValueType::Scalar, 0.0F);
            input("ClearCoatRoughness", MaterialGraphValueType::Scalar, 0.25F);
            input("SheenColor", MaterialGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("SheenRoughness", MaterialGraphValueType::Scalar, 0.5F);
            input("Normal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Emission", MaterialGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("Occlusion", MaterialGraphValueType::Scalar, 1.0F);
            input("Opacity", MaterialGraphValueType::Scalar, 1.0F);
            input("SubsurfaceColor", MaterialGraphValueType::Color, Color{1.0F, 0.35F, 0.25F, 1.0F});
            input("Subsurface", MaterialGraphValueType::Scalar, 0.0F);
            input("Anisotropy", MaterialGraphValueType::Scalar, 0.0F);
            input("Tangent", MaterialGraphValueType::Vector3, Vector3{1.0F, 0.0F, 0.0F});
            input("Transmission", MaterialGraphValueType::Scalar, 0.0F);
            input("IndexOfRefraction", MaterialGraphValueType::Scalar, 1.5F);
            input("Refraction", MaterialGraphValueType::Scalar, 0.0F);
            input("Thickness", MaterialGraphValueType::Scalar, 1.0F);
        };
        const auto materialAttributeOutputs = [&]
        {
            output("BaseColor", MaterialGraphValueType::Color);
            output("Metallic", MaterialGraphValueType::Scalar);
            output("Roughness", MaterialGraphValueType::Scalar);
            output("Specular", MaterialGraphValueType::Scalar);
            output("ClearCoat", MaterialGraphValueType::Scalar);
            output("ClearCoatRoughness", MaterialGraphValueType::Scalar);
            output("SheenColor", MaterialGraphValueType::Color);
            output("SheenRoughness", MaterialGraphValueType::Scalar);
            output("Normal", MaterialGraphValueType::Vector3);
            output("Emission", MaterialGraphValueType::Color);
            output("Occlusion", MaterialGraphValueType::Scalar);
            output("Opacity", MaterialGraphValueType::Scalar);
            output("SubsurfaceColor", MaterialGraphValueType::Color);
            output("Subsurface", MaterialGraphValueType::Scalar);
            output("Anisotropy", MaterialGraphValueType::Scalar);
            output("Tangent", MaterialGraphValueType::Vector3);
            output("Transmission", MaterialGraphValueType::Scalar);
            output("IndexOfRefraction", MaterialGraphValueType::Scalar);
            output("Refraction", MaterialGraphValueType::Scalar);
            output("Thickness", MaterialGraphValueType::Scalar);
        };
        switch (kind)
        {
        case MaterialGraphNodeKind::Master:
            node.Name = "PBR Master";
            input("BaseColor", MaterialGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Metallic", MaterialGraphValueType::Scalar, 0.0F);
            input("Roughness", MaterialGraphValueType::Scalar, 0.5F);
            input("Specular", MaterialGraphValueType::Scalar, 0.5F);
            input("ClearCoat", MaterialGraphValueType::Scalar, 0.0F);
            input("ClearCoatRoughness", MaterialGraphValueType::Scalar, 0.25F);
            input("SheenColor", MaterialGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("SheenRoughness", MaterialGraphValueType::Scalar, 0.5F);
            input("Normal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("DetailNormal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Parallax", MaterialGraphValueType::Scalar, 0.0F);
            input("Emission", MaterialGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("Occlusion", MaterialGraphValueType::Scalar, 1.0F);
            input("Opacity", MaterialGraphValueType::Scalar, 1.0F);
            input("SubsurfaceColor", MaterialGraphValueType::Color, Color{1.0F, 0.35F, 0.25F, 1.0F});
            input("Subsurface", MaterialGraphValueType::Scalar, 0.0F);
            input("Anisotropy", MaterialGraphValueType::Scalar, 0.0F);
            input("Tangent", MaterialGraphValueType::Vector3, Vector3{1.0F, 0.0F, 0.0F});
            input("Transmission", MaterialGraphValueType::Scalar, 0.0F);
            input("IndexOfRefraction", MaterialGraphValueType::Scalar, 1.5F);
            input("Refraction", MaterialGraphValueType::Scalar, 0.0F);
            input("Thickness", MaterialGraphValueType::Scalar, 1.0F);
            input("MaterialAttributes", MaterialGraphValueType::MaterialAttributes,
                  MaterialGraphMaterialAttributesValue{});
            input("WorldPositionOffset", MaterialGraphValueType::Vector3, Vector3{});
            input("PixelDepthOffset", MaterialGraphValueType::Scalar, 0.0F);
            break;
        case MaterialGraphNodeKind::Parameter:
            node.Name = "Parameter";
            node.Symbol = "Parameter";
            output("Value", valueType);
            break;
        case MaterialGraphNodeKind::Constant:
            node.Name = "Constant";
            output("Value", valueType);
            break;
        case MaterialGraphNodeKind::TextureSample:
            node.Name = "Sample Texture 2D";
            input("Texture", MaterialGraphValueType::Texture2D, AssetId{});
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            output("RGBA", MaterialGraphValueType::Color);
            output("RGB", MaterialGraphValueType::Vector3);
            output("R", MaterialGraphValueType::Scalar);
            output("G", MaterialGraphValueType::Scalar);
            output("B", MaterialGraphValueType::Scalar);
            output("A", MaterialGraphValueType::Scalar);
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            break;
        case MaterialGraphNodeKind::UV:
            node.Name = "UV0";
            output("UV", MaterialGraphValueType::Vector2);
            node.ValueType = MaterialGraphValueType::Vector2;
            node.Value = Vector2{};
            break;
        case MaterialGraphNodeKind::UVTransform:
            node.Name = "UV Transform";
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Tiling", MaterialGraphValueType::Vector2, Vector2{1.0F, 1.0F});
            input("Offset", MaterialGraphValueType::Vector2, Vector2{});
            output("UV", MaterialGraphValueType::Vector2);
            node.ValueType = MaterialGraphValueType::Vector2;
            node.Value = Vector2{};
            break;
        case MaterialGraphNodeKind::NormalMap:
            node.Name = "Normal Map";
            input("Sample", MaterialGraphValueType::Color, Color{0.5F, 0.5F, 1.0F, 1.0F});
            input("Scale", MaterialGraphValueType::Scalar, 1.0F);
            output("Normal", MaterialGraphValueType::Vector3);
            node.ValueType = MaterialGraphValueType::Vector3;
            node.Value = Vector3{};
            break;
        case MaterialGraphNodeKind::DetailNormal:
            node.Name = "Detail Normal";
            input("Base", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Detail", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Strength", MaterialGraphValueType::Scalar, 1.0F);
            output("Normal", MaterialGraphValueType::Vector3);
            node.ValueType = MaterialGraphValueType::Vector3;
            node.Value = Vector3{};
            break;
        case MaterialGraphNodeKind::Parallax:
            node.Name = "Parallax Offset";
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Height", MaterialGraphValueType::Scalar, 0.5F);
            input("Scale", MaterialGraphValueType::Scalar, 0.02F);
            output("UV", MaterialGraphValueType::Vector2);
            node.ValueType = MaterialGraphValueType::Vector2;
            node.Value = Vector2{};
            break;
        case MaterialGraphNodeKind::Add:
        case MaterialGraphNodeKind::Subtract:
        case MaterialGraphNodeKind::Multiply:
        case MaterialGraphNodeKind::Divide:
        case MaterialGraphNodeKind::Minimum:
        case MaterialGraphNodeKind::Maximum:
            node.Name = kind == MaterialGraphNodeKind::Add        ? "Add"
                        : kind == MaterialGraphNodeKind::Subtract ? "Subtract"
                        : kind == MaterialGraphNodeKind::Multiply ? "Multiply"
                        : kind == MaterialGraphNodeKind::Divide   ? "Divide"
                        : kind == MaterialGraphNodeKind::Minimum  ? "Minimum"
                                                                  : "Maximum";
            input("A", valueType, DefaultValue(valueType));
            input("B", valueType,
                  kind == MaterialGraphNodeKind::Divide ? UnitValue(valueType) : DefaultValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Power:
            node.Name = "Power";
            input("Base", valueType, DefaultValue(valueType));
            input("Exponent", valueType, UnitValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Lerp:
            node.Name = "Lerp";
            input("A", valueType, DefaultValue(valueType));
            input("B", valueType, DefaultValue(valueType));
            input("T", MaterialGraphValueType::Scalar, 0.5F);
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::OneMinus:
        case MaterialGraphNodeKind::Clamp:
            node.Name = kind == MaterialGraphNodeKind::OneMinus ? "One Minus" : "Saturate";
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Absolute:
        case MaterialGraphNodeKind::Floor:
        case MaterialGraphNodeKind::Ceiling:
        case MaterialGraphNodeKind::Fraction:
        case MaterialGraphNodeKind::Sine:
        case MaterialGraphNodeKind::Cosine:
            node.Name = kind == MaterialGraphNodeKind::Absolute   ? "Absolute"
                        : kind == MaterialGraphNodeKind::Floor    ? "Floor"
                        : kind == MaterialGraphNodeKind::Ceiling  ? "Ceiling"
                        : kind == MaterialGraphNodeKind::Fraction ? "Fraction"
                        : kind == MaterialGraphNodeKind::Sine     ? "Sine"
                                                                  : "Cosine";
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Normalize:
        {
            const auto normalizedType =
                valueType == MaterialGraphValueType::Scalar ? MaterialGraphValueType::Vector3 : valueType;
            node.Name = "Normalize";
            node.ValueType = normalizedType;
            node.Value = DefaultValue(normalizedType);
            input("Value", normalizedType, DefaultValue(normalizedType));
            output("Result", normalizedType);
            break;
        }
        case MaterialGraphNodeKind::Length:
        {
            const auto inputType =
                valueType == MaterialGraphValueType::Scalar ? MaterialGraphValueType::Vector3 : valueType;
            node.Name = "Length";
            input("Value", inputType, DefaultValue(inputType));
            output("Length", MaterialGraphValueType::Scalar);
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            break;
        }
        case MaterialGraphNodeKind::Dot:
        {
            const auto inputType =
                valueType == MaterialGraphValueType::Scalar ? MaterialGraphValueType::Vector3 : valueType;
            node.Name = "Dot Product";
            input("A", inputType, DefaultValue(inputType));
            input("B", inputType, DefaultValue(inputType));
            output("Dot", MaterialGraphValueType::Scalar);
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            break;
        }
        case MaterialGraphNodeKind::Remap:
            node.Name = "Remap";
            input("Value", valueType, DefaultValue(valueType));
            input("In Min", valueType, DefaultValue(valueType));
            input("In Max", valueType, UnitValue(valueType));
            input("Out Min", valueType, DefaultValue(valueType));
            input("Out Max", valueType, UnitValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::SmoothStep:
            node.Name = "Smooth Step";
            input("Edge Min", valueType, DefaultValue(valueType));
            input("Edge Max", valueType, UnitValue(valueType));
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Step:
            node.Name = "Step";
            input("Edge", valueType, DefaultValue(valueType));
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Fresnel:
            node.Name = "Fresnel";
            input("Normal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Power", MaterialGraphValueType::Scalar, 5.0F);
            input("F0", MaterialGraphValueType::Scalar, 0.04F);
            output("Fresnel", MaterialGraphValueType::Scalar);
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            break;
        case MaterialGraphNodeKind::VertexColor:
            node.Name = "Vertex Color";
            output("Color", MaterialGraphValueType::Color);
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            break;
        case MaterialGraphNodeKind::WorldPosition:
        case MaterialGraphNodeKind::WorldNormal:
        case MaterialGraphNodeKind::ViewDirection:
            node.Name = kind == MaterialGraphNodeKind::WorldPosition ? "World Position"
                        : kind == MaterialGraphNodeKind::WorldNormal ? "World Normal"
                                                                     : "View Direction";
            output("Vector", MaterialGraphValueType::Vector3);
            node.ValueType = MaterialGraphValueType::Vector3;
            node.Value = Vector3{};
            break;
        case MaterialGraphNodeKind::RotateUV:
            node.Name = "Rotate UV";
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Center", MaterialGraphValueType::Vector2, Vector2{0.5F, 0.5F});
            input("Rotation", MaterialGraphValueType::Scalar, 0.0F);
            output("UV", MaterialGraphValueType::Vector2);
            node.ValueType = MaterialGraphValueType::Vector2;
            node.Value = Vector2{};
            break;
        case MaterialGraphNodeKind::SimpleNoise:
            node.Name = "Simple Noise";
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Scale", MaterialGraphValueType::Scalar, 5.0F);
            input("Detail", MaterialGraphValueType::Scalar, 0.5F);
            output("Noise", MaterialGraphValueType::Scalar);
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            break;
        case MaterialGraphNodeKind::Desaturate:
            node.Name = "Desaturate";
            input("Color", MaterialGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Amount", MaterialGraphValueType::Scalar, 1.0F);
            output("Color", MaterialGraphValueType::Color);
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            break;
        case MaterialGraphNodeKind::Posterize:
            node.Name = "Posterize";
            input("Value", valueType, DefaultValue(valueType));
            input("Steps", MaterialGraphValueType::Scalar, 4.0F);
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Round:
        case MaterialGraphNodeKind::Truncate:
        case MaterialGraphNodeKind::Sign:
        case MaterialGraphNodeKind::SquareRoot:
        case MaterialGraphNodeKind::ReciprocalSquareRoot:
        case MaterialGraphNodeKind::Exponential2:
        case MaterialGraphNodeKind::Logarithm2:
        case MaterialGraphNodeKind::Tangent:
        case MaterialGraphNodeKind::ArcSine:
        case MaterialGraphNodeKind::ArcCosine:
        case MaterialGraphNodeKind::DerivativeX:
        case MaterialGraphNodeKind::DerivativeY:
        case MaterialGraphNodeKind::FilterWidth:
            node.Name = kind == MaterialGraphNodeKind::Round                  ? "Round"
                        : kind == MaterialGraphNodeKind::Truncate             ? "Truncate"
                        : kind == MaterialGraphNodeKind::Sign                 ? "Sign"
                        : kind == MaterialGraphNodeKind::SquareRoot           ? "Square Root"
                        : kind == MaterialGraphNodeKind::ReciprocalSquareRoot ? "Reciprocal Square Root"
                        : kind == MaterialGraphNodeKind::Exponential2         ? "Exponential 2"
                        : kind == MaterialGraphNodeKind::Logarithm2           ? "Logarithm 2"
                        : kind == MaterialGraphNodeKind::Tangent              ? "Tangent"
                        : kind == MaterialGraphNodeKind::ArcSine              ? "Arc Sine"
                        : kind == MaterialGraphNodeKind::ArcCosine            ? "Arc Cosine"
                        : kind == MaterialGraphNodeKind::DerivativeX          ? "Derivative X"
                        : kind == MaterialGraphNodeKind::DerivativeY          ? "Derivative Y"
                                                                              : "Filter Width";
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Modulo:
        case MaterialGraphNodeKind::ArcTangent2:
            node.Name = kind == MaterialGraphNodeKind::Modulo ? "Modulo" : "Arc Tangent 2";
            input("A", valueType, DefaultValue(valueType));
            input("B", valueType, UnitValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Cross:
        case MaterialGraphNodeKind::Distance:
        case MaterialGraphNodeKind::Reflect:
        {
            node.Name = kind == MaterialGraphNodeKind::Cross      ? "Cross Product"
                        : kind == MaterialGraphNodeKind::Distance ? "Distance"
                                                                  : "Reflect";
            node.ValueType = kind == MaterialGraphNodeKind::Distance ? MaterialGraphValueType::Scalar
                                                                     : MaterialGraphValueType::Vector3;
            node.Value = DefaultValue(node.ValueType);
            input("A", MaterialGraphValueType::Vector3, Vector3{});
            input("B", MaterialGraphValueType::Vector3,
                  kind == MaterialGraphNodeKind::Reflect ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{});
            output("Result", node.ValueType);
            break;
        }
        case MaterialGraphNodeKind::Refract:
            node.Name = "Refract";
            node.ValueType = MaterialGraphValueType::Vector3;
            node.Value = Vector3{};
            input("Incident", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, -1.0F});
            input("Normal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("IOR", MaterialGraphValueType::Scalar, 1.5F);
            output("Result", MaterialGraphValueType::Vector3);
            break;
        case MaterialGraphNodeKind::AppendVector:
            node.Name = "Append Vector";
            node.ValueType = MaterialGraphValueType::Vector4;
            node.Value = Vector4{};
            input("XYZ", MaterialGraphValueType::Vector3, Vector3{});
            input("W", MaterialGraphValueType::Scalar, 1.0F);
            output("Result", MaterialGraphValueType::Vector4);
            break;
        case MaterialGraphNodeKind::ComponentMask:
            node.Name = "Component Mask";
            node.ValueType = MaterialGraphValueType::Vector4;
            node.Value = Vector4{};
            input("Value", MaterialGraphValueType::Vector4, Vector4{});
            output("R", MaterialGraphValueType::Scalar);
            output("G", MaterialGraphValueType::Scalar);
            output("B", MaterialGraphValueType::Scalar);
            output("A", MaterialGraphValueType::Scalar);
            output("RG", MaterialGraphValueType::Vector2);
            output("RGB", MaterialGraphValueType::Vector3);
            output("RGBA", MaterialGraphValueType::Vector4);
            break;
        case MaterialGraphNodeKind::UV1:
            node.Name = "UV1";
            node.ValueType = MaterialGraphValueType::Vector2;
            node.Value = Vector2{};
            output("UV", MaterialGraphValueType::Vector2);
            break;
        case MaterialGraphNodeKind::WorldTangent:
        case MaterialGraphNodeKind::CameraPosition:
        case MaterialGraphNodeKind::ObjectPosition:
            node.Name = kind == MaterialGraphNodeKind::WorldTangent     ? "World Tangent"
                        : kind == MaterialGraphNodeKind::CameraPosition ? "Camera Position"
                                                                        : "Object Position";
            node.ValueType = MaterialGraphValueType::Vector3;
            node.Value = Vector3{};
            output("Vector", MaterialGraphValueType::Vector3);
            break;
        case MaterialGraphNodeKind::Time:
        case MaterialGraphNodeKind::DeltaTime:
            node.Name = kind == MaterialGraphNodeKind::Time ? "Time" : "Delta Time";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            output("Seconds", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::ScreenPosition:
            node.Name = "Screen Position";
            node.ValueType = MaterialGraphValueType::Vector2;
            node.Value = Vector2{};
            output("UV", MaterialGraphValueType::Vector2);
            break;
        case MaterialGraphNodeKind::DepthFade:
            node.Name = "Depth Fade";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("Distance", MaterialGraphValueType::Scalar, 0.0F);
            input("Fade Distance", MaterialGraphValueType::Scalar, 100.0F);
            output("Fade", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::Luminance:
            node.Name = "Luminance";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("Color", MaterialGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            output("Luminance", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::HueShift:
            node.Name = "Hue Shift";
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            input("Color", MaterialGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Shift", MaterialGraphValueType::Scalar, 0.0F);
            output("Color", MaterialGraphValueType::Color);
            break;
        case MaterialGraphNodeKind::Checkerboard:
            node.Name = "Checkerboard";
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Color A", MaterialGraphValueType::Color, Color{0.05F, 0.05F, 0.05F, 1.0F});
            input("Color B", MaterialGraphValueType::Color, Color{0.8F, 0.8F, 0.8F, 1.0F});
            input("Scale", MaterialGraphValueType::Vector2, Vector2{8.0F, 8.0F});
            output("Color", MaterialGraphValueType::Color);
            break;
        case MaterialGraphNodeKind::VoronoiNoise:
            node.Name = "Voronoi Noise";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Scale", MaterialGraphValueType::Scalar, 5.0F);
            input("Jitter", MaterialGraphValueType::Scalar, 1.0F);
            output("Distance", MaterialGraphValueType::Scalar);
            output("Cell", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::Panner:
            node.Name = "Panner";
            node.ValueType = MaterialGraphValueType::Vector2;
            node.Value = Vector2{};
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Speed", MaterialGraphValueType::Vector2, Vector2{0.1F, 0.0F});
            input("Time", MaterialGraphValueType::Scalar, 0.0F);
            output("UV", MaterialGraphValueType::Vector2);
            break;
        case MaterialGraphNodeKind::PolarCoordinates:
            node.Name = "Polar Coordinates";
            node.ValueType = MaterialGraphValueType::Vector2;
            node.Value = Vector2{};
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Center", MaterialGraphValueType::Vector2, Vector2{0.5F, 0.5F});
            input("Radial Scale", MaterialGraphValueType::Scalar, 1.0F);
            input("Length Scale", MaterialGraphValueType::Scalar, 1.0F);
            output("Polar", MaterialGraphValueType::Vector2);
            break;
        case MaterialGraphNodeKind::SphereMask:
            node.Name = "Sphere Mask";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("A", MaterialGraphValueType::Vector3, Vector3{});
            input("B", MaterialGraphValueType::Vector3, Vector3{});
            input("Radius", MaterialGraphValueType::Scalar, 1.0F);
            input("Hardness", MaterialGraphValueType::Scalar, 8.0F);
            output("Mask", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::RadialGradient:
            node.Name = "Radial Gradient";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Center", MaterialGraphValueType::Vector2, Vector2{0.5F, 0.5F});
            input("Radius", MaterialGraphValueType::Scalar, 0.5F);
            input("Density", MaterialGraphValueType::Scalar, 4.0F);
            output("Gradient", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::LinearGradient:
            node.Name = "Linear Gradient";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Direction", MaterialGraphValueType::Vector2, Vector2{1.0F, 0.0F});
            input("Offset", MaterialGraphValueType::Scalar, 0.0F);
            output("Gradient", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::Contrast:
            node.Name = "Contrast";
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            input("Color", MaterialGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Contrast", MaterialGraphValueType::Scalar, 1.0F);
            input("Pivot", MaterialGraphValueType::Scalar, 0.5F);
            output("Color", MaterialGraphValueType::Color);
            break;
        case MaterialGraphNodeKind::Saturation:
            node.Name = "Saturation";
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            input("Color", MaterialGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Saturation", MaterialGraphValueType::Scalar, 1.0F);
            output("Color", MaterialGraphValueType::Color);
            break;
        case MaterialGraphNodeKind::BlendOverlay:
            node.Name = "Overlay Blend";
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            input("Base", MaterialGraphValueType::Color, Color{0.5F, 0.5F, 0.5F, 1.0F});
            input("Blend", MaterialGraphValueType::Color, Color{0.5F, 0.5F, 0.5F, 1.0F});
            input("Opacity", MaterialGraphValueType::Scalar, 1.0F);
            output("Color", MaterialGraphValueType::Color);
            break;
        case MaterialGraphNodeKind::Blackbody:
            node.Name = "Blackbody";
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            input("Temperature", MaterialGraphValueType::Scalar, 6500.0F);
            output("Color", MaterialGraphValueType::Color);
            break;
        case MaterialGraphNodeKind::ReflectionVector:
            node.Name = "Reflection Vector";
            node.ValueType = MaterialGraphValueType::Vector3;
            node.Value = Vector3{};
            input("Normal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            output("Vector", MaterialGraphValueType::Vector3);
            break;
        case MaterialGraphNodeKind::FacingRatio:
            node.Name = "Facing Ratio";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("Normal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Power", MaterialGraphValueType::Scalar, 1.0F);
            output("Ratio", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::Dither:
            node.Name = "Dither";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("Alpha", MaterialGraphValueType::Scalar, 1.0F);
            input("Screen Position", MaterialGraphValueType::Vector2, Vector2{});
            output("Value", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::GradientNoise:
            node.Name = "Gradient Noise";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Scale", MaterialGraphValueType::Scalar, 5.0F);
            output("Noise", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::Wave:
            node.Name = "Wave";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Direction", MaterialGraphValueType::Vector2, Vector2{1.0F, 0.0F});
            input("Frequency", MaterialGraphValueType::Scalar, 8.0F);
            input("Phase", MaterialGraphValueType::Scalar, 0.0F);
            output("Wave", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::TriplanarSample:
            node.Name = "Triplanar Sample";
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            input("Texture", MaterialGraphValueType::Texture2D, AssetId{});
            input("Position", MaterialGraphValueType::Vector3, Vector3{});
            input("Normal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Scale", MaterialGraphValueType::Scalar, 1.0F);
            input("Blend Sharpness", MaterialGraphValueType::Scalar, 4.0F);
            output("RGBA", MaterialGraphValueType::Color);
            output("RGB", MaterialGraphValueType::Vector3);
            output("R", MaterialGraphValueType::Scalar);
            output("G", MaterialGraphValueType::Scalar);
            output("B", MaterialGraphValueType::Scalar);
            output("A", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::TextureSampleLevel:
            node.Name = "Sample Texture 2D Level";
            node.ValueType = MaterialGraphValueType::Color;
            node.Value = Color{};
            input("Texture", MaterialGraphValueType::Texture2D, AssetId{});
            input("UV", MaterialGraphValueType::Vector2, Vector2{});
            input("Mip Level", MaterialGraphValueType::Scalar, 0.0F);
            output("RGBA", MaterialGraphValueType::Color);
            output("RGB", MaterialGraphValueType::Vector3);
            output("R", MaterialGraphValueType::Scalar);
            output("G", MaterialGraphValueType::Scalar);
            output("B", MaterialGraphValueType::Scalar);
            output("A", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::HeightToNormal:
            node.Name = "Height To Normal";
            node.ValueType = MaterialGraphValueType::Vector3;
            node.Value = Vector3{};
            input("Height", MaterialGraphValueType::Scalar, 0.5F);
            input("Strength", MaterialGraphValueType::Scalar, 1.0F);
            output("Normal", MaterialGraphValueType::Vector3);
            break;
        case MaterialGraphNodeKind::FlattenNormal:
            node.Name = "Flatten Normal";
            node.ValueType = MaterialGraphValueType::Vector3;
            node.Value = Vector3{};
            input("Normal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Strength", MaterialGraphValueType::Scalar, 1.0F);
            output("Normal", MaterialGraphValueType::Vector3);
            break;
        case MaterialGraphNodeKind::MakeMaterialAttributes:
            node.Name = "Make Material Attributes";
            node.ValueType = MaterialGraphValueType::MaterialAttributes;
            node.Value = MaterialGraphMaterialAttributesValue{};
            materialAttributeInputs();
            output("Attributes", MaterialGraphValueType::MaterialAttributes);
            break;
        case MaterialGraphNodeKind::BreakMaterialAttributes:
            node.Name = "Break Material Attributes";
            node.ValueType = MaterialGraphValueType::MaterialAttributes;
            node.Value = MaterialGraphMaterialAttributesValue{};
            input("Attributes", MaterialGraphValueType::MaterialAttributes, MaterialGraphMaterialAttributesValue{});
            materialAttributeOutputs();
            break;
        case MaterialGraphNodeKind::BlendMaterialAttributes:
            node.Name = "Blend Material Attributes";
            node.ValueType = MaterialGraphValueType::MaterialAttributes;
            node.Value = MaterialGraphMaterialAttributesValue{};
            input("A", MaterialGraphValueType::MaterialAttributes, MaterialGraphMaterialAttributesValue{});
            input("B", MaterialGraphValueType::MaterialAttributes, MaterialGraphMaterialAttributesValue{});
            input("Alpha", MaterialGraphValueType::Scalar, 0.5F);
            output("Attributes", MaterialGraphValueType::MaterialAttributes);
            break;
        case MaterialGraphNodeKind::StandardSurfaceBsdf:
            node.Name = "Standard Surface BSDF";
            node.ValueType = MaterialGraphValueType::Bsdf;
            node.Value = MaterialGraphBsdfValue{};
            input("BaseColor", MaterialGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Metallic", MaterialGraphValueType::Scalar, 0.0F);
            input("Roughness", MaterialGraphValueType::Scalar, 0.5F);
            input("Specular", MaterialGraphValueType::Scalar, 0.5F);
            input("Normal", MaterialGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Emission", MaterialGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("Opacity", MaterialGraphValueType::Scalar, 1.0F);
            output("BSDF", MaterialGraphValueType::Bsdf);
            break;
        case MaterialGraphNodeKind::ClearCoatBsdf:
            node.Name = "Clear Coat BSDF";
            node.ValueType = MaterialGraphValueType::Bsdf;
            node.Value = MaterialGraphBsdfValue{};
            input("Base", MaterialGraphValueType::Bsdf, MaterialGraphBsdfValue{});
            input("Weight", MaterialGraphValueType::Scalar, 1.0F);
            input("Roughness", MaterialGraphValueType::Scalar, 0.25F);
            output("BSDF", MaterialGraphValueType::Bsdf);
            break;
        case MaterialGraphNodeKind::SheenBsdf:
            node.Name = "Sheen BSDF";
            node.ValueType = MaterialGraphValueType::Bsdf;
            node.Value = MaterialGraphBsdfValue{};
            input("Base", MaterialGraphValueType::Bsdf, MaterialGraphBsdfValue{});
            input("Color", MaterialGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Weight", MaterialGraphValueType::Scalar, 1.0F);
            input("Roughness", MaterialGraphValueType::Scalar, 0.5F);
            output("BSDF", MaterialGraphValueType::Bsdf);
            break;
        case MaterialGraphNodeKind::SubsurfaceBsdf:
            node.Name = "Subsurface BSDF";
            node.ValueType = MaterialGraphValueType::Bsdf;
            node.Value = MaterialGraphBsdfValue{};
            input("Base", MaterialGraphValueType::Bsdf, MaterialGraphBsdfValue{});
            input("Color", MaterialGraphValueType::Color, Color{1.0F, 0.35F, 0.25F, 1.0F});
            input("Weight", MaterialGraphValueType::Scalar, 1.0F);
            output("BSDF", MaterialGraphValueType::Bsdf);
            break;
        case MaterialGraphNodeKind::TransmissionBsdf:
            node.Name = "Transmission BSDF";
            node.ValueType = MaterialGraphValueType::Bsdf;
            node.Value = MaterialGraphBsdfValue{};
            input("Base", MaterialGraphValueType::Bsdf, MaterialGraphBsdfValue{});
            input("Weight", MaterialGraphValueType::Scalar, 1.0F);
            input("IndexOfRefraction", MaterialGraphValueType::Scalar, 1.5F);
            input("Refraction", MaterialGraphValueType::Scalar, 1.0F);
            input("Thickness", MaterialGraphValueType::Scalar, 1.0F);
            output("BSDF", MaterialGraphValueType::Bsdf);
            break;
        case MaterialGraphNodeKind::BsdfToMaterialAttributes:
            node.Name = "BSDF To Material Attributes";
            node.ValueType = MaterialGraphValueType::MaterialAttributes;
            node.Value = MaterialGraphMaterialAttributesValue{};
            input("BSDF", MaterialGraphValueType::Bsdf, MaterialGraphBsdfValue{});
            output("Attributes", MaterialGraphValueType::MaterialAttributes);
            break;
        case MaterialGraphNodeKind::Keyword:
            node.Name = "Keyword";
            node.Symbol = "KEYWORD";
            node.ValueType = MaterialGraphValueType::Scalar;
            node.Value = 0.0F;
            output("Enabled", MaterialGraphValueType::Scalar);
            break;
        case MaterialGraphNodeKind::StaticSwitch:
            node.Name = "Static Switch";
            input("Condition", MaterialGraphValueType::Scalar, 0.0F);
            input("True", valueType, DefaultValue(valueType));
            input("False", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case MaterialGraphNodeKind::Custom:
            node.Name = "Custom Function";
            node.Function = "EvaluateCustomMaterialNode";
            input("Input", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        }
        return node;
    }

    MaterialGraphDefinition CreateDefaultMaterialGraph(const MaterialGraphOutput output)
    {
        MaterialGraphDefinition definition;
        definition.Output = output;
        auto master = CreateMaterialGraphNode(MaterialGraphNodeKind::Master, MaterialGraphValueType::Color);
        master.EditorPosition = {480.0F, 120.0F};
        if (output == MaterialGraphOutput::Unlit)
        {
            master.Name = "Unlit Master";
            std::erase_if(master.Pins,
                          [](const MaterialGraphPin& pin)
                          {
                              return pin.Name != "BaseColor" && pin.Name != "Emission" && pin.Name != "Opacity" &&
                                     pin.Name != "WorldPositionOffset" && pin.Name != "PixelDepthOffset";
                          });
            master.Pins.front().Name = "Color";
        }
        else if (output == MaterialGraphOutput::Transparent)
            master.Name = "Transparent PBR Master";
        else if (output == MaterialGraphOutput::Decal)
            master.Name = "Decal PBR Master";
        else if (output == MaterialGraphOutput::Hair)
        {
            master.Name = "Hair PBR Master";
            const auto anisotropy = std::ranges::find(master.Pins, "Anisotropy", &MaterialGraphPin::Name);
            const auto roughness = std::ranges::find(master.Pins, "Roughness", &MaterialGraphPin::Name);
            const auto sheen = std::ranges::find(master.Pins, "SheenColor", &MaterialGraphPin::Name);
            anisotropy->DefaultValue = 0.8F;
            roughness->DefaultValue = 0.35F;
            sheen->DefaultValue = Color{0.12F, 0.08F, 0.04F, 1.0F};
        }
        else if (output == MaterialGraphOutput::Eye)
        {
            master.Name = "Eye PBR Master";
            const auto clearCoat = std::ranges::find(master.Pins, "ClearCoat", &MaterialGraphPin::Name);
            const auto clearCoatRoughness =
                std::ranges::find(master.Pins, "ClearCoatRoughness", &MaterialGraphPin::Name);
            const auto ior = std::ranges::find(master.Pins, "IndexOfRefraction", &MaterialGraphPin::Name);
            const auto refraction = std::ranges::find(master.Pins, "Refraction", &MaterialGraphPin::Name);
            clearCoat->DefaultValue = 1.0F;
            clearCoatRoughness->DefaultValue = 0.05F;
            ior->DefaultValue = 1.336F;
            refraction->DefaultValue = 0.2F;
        }
        definition.Nodes.push_back(std::move(master));
        return definition;
    }

    void ValidateMaterialGraph(const MaterialGraphDefinition& definition)
    {
        if ((definition.SchemaVersion != 1 && definition.SchemaVersion != MaterialGraphSourceSchemaVersion) ||
            definition.Output > MaterialGraphOutput::Eye || definition.Nodes.empty() ||
            definition.Nodes.size() > MaximumGraphNodes || definition.Connections.size() > MaximumGraphConnections ||
            definition.Keywords.size() > MaximumGraphKeywords || definition.IncludeRoots.empty() ||
            definition.IncludeRoots.size() > MaximumGraphIncludeRoots)
            throw std::invalid_argument("Material Graph has an unsupported schema or exceeds a bounded collection.");

        std::set<AssetId> identities;
        std::set<std::string, std::less<>> properties;
        std::set<std::string, std::less<>> keywordNodeSymbols;
        std::size_t masters = 0;
        std::size_t propertyCount = 0;
        std::size_t texturePropertyCount = 0;
        for (const auto& root : definition.IncludeRoots)
            if (!SafeRelativePath(root))
                throw std::invalid_argument("Material Graph include roots must be confined relative paths.");
        for (const auto& node : definition.Nodes)
        {
            if (!node.Id || !identities.insert(node.Id).second ||
                node.Kind > MaterialGraphNodeKind::BsdfToMaterialAttributes ||
                node.ValueType > MaterialGraphValueType::Bsdf || node.Name.size() > MaximumGraphText ||
                node.TextureSemantic > ShaderTextureSemantic::Roughness || node.Pins.empty() ||
                node.Pins.size() > MaximumGraphPinsPerNode || !Math::IsFinite(node.EditorPosition) ||
                !ValueMatchesType(node.Value, node.ValueType))
                throw std::invalid_argument("Material Graph node identity, type, position, or pins are invalid.");
            const auto expectedTypeId = MaterialGraphNodeTypeId(node.Kind);
            if (expectedTypeId.empty() || (definition.SchemaVersion >= 2 && node.TypeId != expectedTypeId) ||
                (!node.TypeId.empty() && node.TypeId != expectedTypeId))
                throw std::invalid_argument("Material Graph node type ID does not match its node contract.");
            ValidateFiniteValue(node.Value);
            if (node.Kind == MaterialGraphNodeKind::Master)
                ++masters;
            if (node.Kind == MaterialGraphNodeKind::Parameter)
            {
                if (node.ValueType == MaterialGraphValueType::MaterialAttributes ||
                    node.ValueType == MaterialGraphValueType::Bsdf)
                    throw std::invalid_argument("Material Graph structured values cannot be exposed as parameters.");
                ++propertyCount;
                texturePropertyCount += node.ValueType == MaterialGraphValueType::Texture2D ? 1U : 0U;
                if (!ValidIdentifier(node.Symbol) || !properties.insert(node.Symbol).second)
                    throw std::invalid_argument("Material Graph parameter symbols must be unique identifiers.");
                const auto& metadata = node.ParameterMetadata;
                const auto validOptional = [](const std::optional<float>& value)
                { return !value || std::isfinite(*value); };
                if (metadata.Description.size() > MaximumGraphText * 4U ||
                    metadata.Category.size() > MaximumGraphText || !validOptional(metadata.Minimum) ||
                    !validOptional(metadata.Maximum) || !validOptional(metadata.Step) ||
                    (metadata.Minimum && metadata.Maximum && *metadata.Minimum > *metadata.Maximum) ||
                    (metadata.Step && *metadata.Step <= 0.0F))
                    throw std::invalid_argument("Material Graph parameter metadata is invalid.");
            }
            else if (node.ParameterMetadata != MaterialGraphParameterMetadata{})
                throw std::invalid_argument("Only Material Graph Parameter nodes may contain parameter metadata.");
            if (node.Kind == MaterialGraphNodeKind::Keyword)
            {
                if (!ValidIdentifier(node.Symbol))
                    throw std::invalid_argument("Material Graph Keyword node requires a valid symbol.");
                keywordNodeSymbols.insert(node.Symbol);
            }
            if (node.Kind == MaterialGraphNodeKind::Custom &&
                (!SafeRelativePath(node.Include) || !ValidIdentifier(node.Function)))
                throw std::invalid_argument("Custom Material Graph nodes require a safe include and function name.");
            std::set<std::string, std::less<>> inputPinNames;
            std::set<std::string, std::less<>> outputPinNames;
            for (const auto& pin : node.Pins)
            {
                const auto qualifiedName = node.Name + "." + pin.Name;
                if (!pin.Id)
                    throw std::invalid_argument("Material Graph pin has no identity: " + qualifiedName + '.');
                if (!identities.insert(pin.Id).second)
                    throw std::invalid_argument("Material Graph pin identity is duplicated: " + qualifiedName + '.');
                auto& pinNames = pin.Direction == MaterialGraphPinDirection::Input ? inputPinNames : outputPinNames;
                if (pin.Name.empty() || pin.Name.size() > MaximumGraphText || !pinNames.insert(pin.Name).second)
                    throw std::invalid_argument("Material Graph pin name is invalid or duplicated: " + qualifiedName +
                                                '.');
                if (pin.Type > MaterialGraphValueType::Bsdf || pin.Direction > MaterialGraphPinDirection::Output)
                    throw std::invalid_argument("Material Graph pin type or direction is invalid: " + qualifiedName +
                                                '.');
                if (!ValueMatchesType(pin.DefaultValue, pin.Type))
                    throw std::invalid_argument(
                        "Material Graph pin default has the wrong value type: " + qualifiedName + '.');
                ValidateFiniteValue(pin.DefaultValue);
            }
            if (node.Kind == MaterialGraphNodeKind::Master)
            {
                if (!outputPinNames.empty())
                    throw std::invalid_argument("Material Graph Master nodes cannot expose output pins.");
            }
            else if (node.Kind == MaterialGraphNodeKind::Custom)
            {
                if (outputPinNames.size() != 1 || node.Pins.size() == outputPinNames.size())
                    throw std::invalid_argument("Custom Material Graph nodes require inputs and exactly one output.");
            }
            else
            {
                const auto canonical = CreateMaterialGraphNode(node.Kind, node.ValueType);
                if (canonical.Pins.size() != node.Pins.size())
                    throw std::invalid_argument(
                        "Material Graph node does not match its canonical pin contract: " + node.Name + '.');
                for (std::size_t index = 0; index < node.Pins.size(); ++index)
                {
                    const auto& expected = canonical.Pins[index];
                    const auto& actual = node.Pins[index];
                    if (actual.Name != expected.Name || actual.Type != expected.Type ||
                        actual.Direction != expected.Direction)
                        throw std::invalid_argument("Material Graph node has a malformed canonical pin: " + node.Name +
                                                    "." + actual.Name + '.');
                }
            }
            if ((NumericNode(node.Kind) || node.Kind == MaterialGraphNodeKind::Constant) &&
                (node.ValueType == MaterialGraphValueType::Texture2D ||
                 node.ValueType == MaterialGraphValueType::MaterialAttributes ||
                 node.ValueType == MaterialGraphValueType::Bsdf))
                throw std::invalid_argument("Material Graph numeric nodes require scalar, vector, or color values.");
        }
        if (masters != 1 || propertyCount > MaximumGraphProperties)
            throw std::invalid_argument("Material Graph requires one Master node and at most 80 properties.");
        const auto maximumTextures = definition.Output == MaterialGraphOutput::Unlit ? 16U : 12U;
        if (texturePropertyCount > maximumTextures)
            throw std::invalid_argument("Material Graph texture parameters exceed the portable sampler budget.");
        const auto master =
            std::ranges::find(definition.Nodes, MaterialGraphNodeKind::Master, &MaterialGraphNode::Kind);
        const auto required = definition.Output == MaterialGraphOutput::Unlit
                                  ? std::array<std::string_view, 3>{"Color", "Emission", "Opacity"}
                                  : std::array<std::string_view, 3>{"BaseColor", "Emission", "Opacity"};
        for (const auto name : required)
            if (const auto* pin = FindPin(*master, name, MaterialGraphPinDirection::Input); !pin)
                throw std::invalid_argument("Material Graph Master node is missing a required input.");

        std::set<std::string, std::less<>> keywordNames;
        std::set<std::string, std::less<>> tokens;
        for (const auto& keyword : definition.Keywords)
        {
            if (!ValidIdentifier(keyword.Name) || !keywordNames.insert(keyword.Name).second ||
                keyword.Options.size() > 8)
                throw std::invalid_argument("Material Graph keyword definitions are invalid or duplicated.");
            if (keyword.Options.empty())
            {
                if (!keyword.DefaultOption.empty() && keyword.DefaultOption != "true" &&
                    keyword.DefaultOption != "false")
                    throw std::invalid_argument("Boolean Material Graph keyword defaults must be true or false.");
                if (!tokens.insert(keyword.Name).second)
                    throw std::invalid_argument("Material Graph keyword tokens must be unique.");
                continue;
            }
            bool hasDefault = keyword.DefaultOption.empty();
            for (const auto& option : keyword.Options)
            {
                if (!ValidIdentifier(option))
                    throw std::invalid_argument("Material Graph keyword options must be identifiers.");
                const auto token = keyword.Name + "_" + option;
                if (!tokens.insert(token).second)
                    throw std::invalid_argument("Material Graph keyword tokens must be unique.");
                hasDefault |= option == keyword.DefaultOption;
            }
            if (!hasDefault)
                throw std::invalid_argument("Material Graph enum keyword default is not one of its options.");
        }
        for (const auto& symbol : keywordNodeSymbols)
            if (!tokens.contains(symbol))
                throw std::invalid_argument("Material Graph Keyword nodes must reference a declared keyword token.");

        std::set<std::pair<AssetId, AssetId>> inputs;
        for (const auto& connection : definition.Connections)
        {
            if (!connection.Id || !identities.insert(connection.Id).second || !connection.Output.Node ||
                !connection.Output.Pin || !connection.Input.Node || !connection.Input.Pin ||
                connection.Output.Node == connection.Input.Node ||
                !inputs.emplace(connection.Input.Node, connection.Input.Pin).second)
                throw std::invalid_argument("Material Graph connection identity or destination is invalid.");
            const auto& outputNode = RequireNode(definition, connection.Output.Node);
            const auto& inputNode = RequireNode(definition, connection.Input.Node);
            const auto& outputPin = RequirePin(outputNode, connection.Output.Pin);
            const auto& inputPin = RequirePin(inputNode, connection.Input.Pin);
            if (outputPin.Direction != MaterialGraphPinDirection::Output ||
                inputPin.Direction != MaterialGraphPinDirection::Input || !Compatible(outputPin.Type, inputPin.Type))
                throw std::invalid_argument("Material Graph connection directions or value types are incompatible.");
        }

        std::unordered_map<AssetId, std::size_t> indegrees;
        std::unordered_map<AssetId, std::vector<AssetId>> adjacency;
        for (const auto& node : definition.Nodes)
            indegrees.emplace(node.Id, 0);
        for (const auto& connection : definition.Connections)
        {
            adjacency[connection.Output.Node].push_back(connection.Input.Node);
            ++indegrees[connection.Input.Node];
        }
        std::deque<AssetId> ready;
        for (const auto& [node, degree] : indegrees)
            if (degree == 0)
                ready.push_back(node);
        std::size_t visited = 0;
        while (!ready.empty())
        {
            const auto node = ready.front();
            ready.pop_front();
            ++visited;
            for (const auto target : adjacency[node])
                if (--indegrees[target] == 0)
                    ready.push_back(target);
        }
        if (visited != definition.Nodes.size())
            throw std::invalid_argument("Material Graph contains a cycle.");
    }

    std::vector<std::vector<std::string>>
    EnumerateMaterialGraphKeywordVariants(const std::span<const MaterialGraphKeyword> keywords,
                                          const std::size_t maximumVariants)
    {
        if (maximumVariants == 0 || maximumVariants > 1024)
            throw std::invalid_argument("Material Graph variant limit must be between 1 and 1,024.");
        std::vector<std::vector<std::string>> result(1);
        for (const auto& keyword : keywords)
        {
            std::vector<std::optional<std::string>> choices;
            if (keyword.Options.empty())
                choices = {std::nullopt, keyword.Name};
            else
                for (const auto& option : keyword.Options)
                    choices.emplace_back(keyword.Name + "_" + option);
            if (choices.empty() || result.size() > maximumVariants / choices.size())
                throw std::invalid_argument("Material Graph keyword permutations exceed the configured variant limit.");
            std::vector<std::vector<std::string>> expanded;
            expanded.reserve(result.size() * choices.size());
            for (const auto& existing : result)
                for (const auto& choice : choices)
                {
                    auto variant = existing;
                    if (choice)
                        variant.push_back(*choice);
                    expanded.push_back(std::move(variant));
                }
            result = std::move(expanded);
        }
        return result;
    }

    MaterialGraphCompilation CompileMaterialGraph(const MaterialGraphDefinition& definition,
                                                  const MaterialGraphCompileOptions& options)
    {
        MaterialGraphCompilation result;
        try
        {
            if (options.MaximumNodes == 0 || options.MaximumNodes > MaximumGraphNodes ||
                options.MaximumConnections == 0 || options.MaximumConnections > MaximumGraphConnections ||
                options.MaximumCustomIncludes == 0 || options.MaximumCustomIncludes > 256 ||
                !SafeRelativePath(options.GeneratedSource) || definition.Nodes.size() > options.MaximumNodes ||
                definition.Connections.size() > options.MaximumConnections)
                throw std::invalid_argument("Material Graph compile options or graph bounds are invalid.");
            ValidateMaterialGraph(definition);
            result.Statistics = AnalyzeGraph(definition);
            const auto variants = EnumerateMaterialGraphKeywordVariants(definition.Keywords, options.MaximumVariants);
            result.Statistics.VariantCount = variants.size();
            std::size_t unusedDiagnostics = 0;
            constexpr std::size_t MaximumUnusedDiagnostics = 16;
            if (result.Statistics.UnusedNodeCount != 0)
            {
                const auto master =
                    std::ranges::find(definition.Nodes, MaterialGraphNodeKind::Master, &MaterialGraphNode::Kind);
                std::unordered_set<AssetId> reachable{master->Id};
                std::vector<AssetId> pending{master->Id};
                while (!pending.empty())
                {
                    const auto inputNode = pending.back();
                    pending.pop_back();
                    for (const auto& connection : definition.Connections)
                        if (connection.Input.Node == inputNode && reachable.insert(connection.Output.Node).second)
                            pending.push_back(connection.Output.Node);
                }
                for (const auto& node : definition.Nodes)
                {
                    if (reachable.contains(node.Id) || unusedDiagnostics == MaximumUnusedDiagnostics)
                        continue;
                    result.Diagnostics.push_back(
                        {MaterialGraphDiagnosticSeverity::Warning,
                         "MG1001",
                         "Node '" + node.Name + "' does not contribute to the material output.",
                         node.Id,
                         {},
                         0});
                    ++unusedDiagnostics;
                }
                if (result.Statistics.UnusedNodeCount > MaximumUnusedDiagnostics)
                    result.Diagnostics.push_back(
                        {MaterialGraphDiagnosticSeverity::Warning,
                         "MG1002",
                         std::to_string(result.Statistics.UnusedNodeCount - MaximumUnusedDiagnostics) +
                             " additional unused nodes were omitted from diagnostics.",
                         {},
                         {},
                         0});
            }
            if (result.Statistics.VariantCount > 16)
                result.Diagnostics.push_back(
                    {MaterialGraphDiagnosticSeverity::Warning,
                     "MG1101",
                     "This graph produces " + std::to_string(result.Statistics.VariantCount) +
                         " shader variants. Consider reducing independent keywords to control build and memory cost.",
                     {},
                     {},
                     0});
            if (result.Statistics.EstimatedAluInstructions > 192)
                result.Diagnostics.push_back({MaterialGraphDiagnosticSeverity::Warning,
                                              "MG1201",
                                              "The reachable graph has a high estimated arithmetic cost (" +
                                                  std::to_string(result.Statistics.EstimatedAluInstructions) + " ALU).",
                                              {},
                                              {},
                                              0});
            else if (result.Statistics.EstimatedAluInstructions > 96)
                result.Diagnostics.push_back({MaterialGraphDiagnosticSeverity::Info,
                                              "MG1200",
                                              "The reachable graph has a moderate estimated arithmetic cost (" +
                                                  std::to_string(result.Statistics.EstimatedAluInstructions) + " ALU).",
                                              {},
                                              {},
                                              0});
            const auto master =
                std::ranges::find(definition.Nodes, MaterialGraphNodeKind::Master, &MaterialGraphNode::Kind);
            if (master != definition.Nodes.end())
            {
                const auto attributes = FindPin(*master, "MaterialAttributes", MaterialGraphPinDirection::Input);
                const bool attributesConnected =
                    attributes &&
                    std::ranges::any_of(definition.Connections, [&](const MaterialGraphConnection& value)
                                        { return value.Input == MaterialGraphEndpoint{master->Id, attributes->Id}; });
                if (attributesConnected)
                {
                    std::vector<std::string_view> ignoredInputs;
                    for (const auto& connection : definition.Connections)
                    {
                        if (connection.Input.Node != master->Id || connection.Input.Pin == attributes->Id)
                            continue;
                        const auto& pin = RequirePin(*master, connection.Input.Pin);
                        if (pin.Name != "WorldPositionOffset" && pin.Name != "PixelDepthOffset")
                            ignoredInputs.push_back(pin.Name);
                    }
                    if (!ignoredInputs.empty())
                    {
                        std::string names;
                        for (const auto name : ignoredInputs)
                        {
                            if (!names.empty())
                                names += ", ";
                            names += name;
                        }
                        result.Diagnostics.push_back(
                            {MaterialGraphDiagnosticSeverity::Warning, "MG1300",
                             "MaterialAttributes overrides these connected Master inputs: " + names + '.', master->Id,
                             attributes->Id, 0});
                    }
                }
            }
            for (const auto& keywords : variants)
            {
                GraphCompiler compiler(definition, options, keywords, result.Properties, result.Dependencies);
                auto hlsl = compiler.BuildHlsl();
                auto suffix = KeywordSuffix(keywords);
                auto generatedSource = VariantSourcePath(options.GeneratedSource, suffix);
                result.Variants.push_back(
                    {keywords, std::move(suffix), generatedSource, std::move(hlsl),
                     Detail::BuildMaterialGraphManifest(definition, generatedSource, result.Properties, keywords,
                                                        compiler.UsesVertexMaterialParameters())});
            }
            std::ranges::sort(result.Dependencies);
            result.Dependencies.erase(std::unique(result.Dependencies.begin(), result.Dependencies.end()),
                                      result.Dependencies.end());
        }
        catch (const std::exception& error)
        {
            result.Variants.clear();
            result.Properties.clear();
            result.Dependencies.clear();
            result.Diagnostics.push_back({MaterialGraphDiagnosticSeverity::Error, "MG0001", error.what(), {}, {}, 0});
        }
        return result;
    }

    ResolvedMaterialGraphInstance
    ResolveMaterialGraphInstance(const MaterialGraphDefinition& graph,
                                 const std::span<const MaterialGraphInstanceDefinition> ancestry)
    {
        ValidateMaterialGraph(graph);
        if (ancestry.empty() || ancestry.size() > 16)
            throw std::invalid_argument("Material Graph instance ancestry must contain between 1 and 16 entries.");
        std::map<std::string, MaterialGraphValueType, std::less<>> propertyTypes;
        for (const auto& node : graph.Nodes)
            if (node.Kind == MaterialGraphNodeKind::Parameter)
                propertyTypes.emplace(node.Symbol, node.ValueType);
        ResolvedMaterialGraphInstance result;
        for (const auto& node : graph.Nodes)
            if (node.Kind == MaterialGraphNodeKind::Parameter)
                result.Properties.emplace(node.Symbol, ToMaterialPropertyValue(node.Value));
        std::map<std::string, std::string, std::less<>> keywordValues;
        for (const auto& keyword : graph.Keywords)
            keywordValues[keyword.Name] = keyword.DefaultOption.empty()
                                              ? (keyword.Options.empty() ? "false" : keyword.Options.front())
                                              : keyword.DefaultOption;
        for (const auto& instance : ancestry)
        {
            ValidateMaterialGraphInstanceDefinition(instance);
            for (const auto& [name, value] : instance.Properties)
            {
                const auto found = propertyTypes.find(name);
                if (found == propertyTypes.end() || !MaterialValueMatches(value, found->second))
                    throw std::invalid_argument("Material Graph instance property is unknown or has the wrong type: " +
                                                name);
                result.Properties.insert_or_assign(name, value);
            }
            for (const auto& [name, value] : instance.KeywordOverrides)
            {
                const auto found = std::ranges::find(graph.Keywords, name, &MaterialGraphKeyword::Name);
                if (found == graph.Keywords.end())
                    throw std::invalid_argument("Material Graph instance keyword is unknown: " + name);
                if (found->Options.empty())
                {
                    if (value != "true" && value != "false")
                        throw std::invalid_argument("Boolean Material Graph keyword overrides must be true or false.");
                }
                else if (std::ranges::find(found->Options, value) == found->Options.end())
                    throw std::invalid_argument("Material Graph enum keyword override is invalid: " + name);
                keywordValues[name] = value;
            }
        }
        for (const auto& keyword : graph.Keywords)
        {
            const auto& value = keywordValues.at(keyword.Name);
            if (keyword.Options.empty())
            {
                if (value == "true")
                    result.Keywords.push_back(keyword.Name);
            }
            else
                result.Keywords.push_back(keyword.Name + "_" + value);
        }
        return result;
    }

    MaterialAssetDefinition
    BakeMaterialGraphInstance(const MaterialGraphDefinition& graph, const ResolvedMaterialGraphInstance& instance,
                              const std::function<AssetId(std::span<const std::string>)>& resolveShaderVariant)
    {
        if (!resolveShaderVariant)
            throw std::invalid_argument("Baking a Material Graph instance requires a shader-variant resolver.");
        ValidateMaterialGraph(graph);
        if (instance.Properties.size() > MaximumGraphProperties)
            throw std::invalid_argument("Material Graph instance properties exceed their bound.");
        for (const auto& [name, value] : instance.Properties)
        {
            const auto parameter =
                std::ranges::find_if(graph.Nodes, [&](const MaterialGraphNode& node)
                                     { return node.Kind == MaterialGraphNodeKind::Parameter && node.Symbol == name; });
            if (parameter == graph.Nodes.end() || !MaterialValueMatches(value, parameter->ValueType))
                throw std::invalid_argument("Material Graph instance property is unknown or has the wrong type: " +
                                            name);
            ValidateFiniteValue(value);
        }
        const auto variants = EnumerateMaterialGraphKeywordVariants(graph.Keywords);
        if (std::ranges::find(variants, instance.Keywords) == variants.end())
            throw std::invalid_argument("Material Graph instance selected an unavailable keyword variant.");
        MaterialAssetDefinition result;
        result.Shader = resolveShaderVariant(instance.Keywords);
        if (!result.Shader)
            throw std::runtime_error("Material Graph shader variant is not published.");
        result.Properties = instance.Properties;
        if (graph.Output == MaterialGraphOutput::Transparent || graph.Output == MaterialGraphOutput::Decal)
            result.Surface.AlphaMode = MaterialAlphaMode::Blend;
        else if (graph.Output == MaterialGraphOutput::Hair)
            result.Surface.AlphaMode = MaterialAlphaMode::Mask;
        result.Surface.DoubleSided =
            graph.Output == MaterialGraphOutput::Decal || graph.Output == MaterialGraphOutput::Hair;
        return result;
    }

    AssetImporterRegistration CreateMaterialGraphAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.MaterialGraph";
        result.Version = 14;
        result.Type = MaterialGraphAsset::StaticType();
        result.Extensions = {".keirematerialgraph"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            if (!context.Asset || context.ProjectRoot.empty() || context.SourceRoot.empty() ||
                !context.ReadProjectFile || !context.ResolveSubAssetId)
            {
                throw std::invalid_argument(
                    "Material Graph import requires a complete project context and stable subasset resolver.");
            }
            const auto definition = MaterialGraphAsset::DecodeSource(bytes);
            AssetImportOutput output;
            output.Bytes = MaterialGraphAsset::Encode(definition);
            for (const auto& node : definition.Nodes)
                if (node.Kind == MaterialGraphNodeKind::Parameter &&
                    node.ValueType == MaterialGraphValueType::Texture2D)
                {
                    const auto texture = std::get<AssetId>(node.Value);
                    if (texture)
                        output.AssetDependencies.push_back(texture);
                }
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());

            MaterialGraphCompileOptions compileOptions;
            compileOptions.GeneratedSource = std::filesystem::relative(context.SourceRoot, context.ProjectRoot) /
                                             "Generated" / "MaterialGraphs" / context.Asset.ToString() /
                                             "MaterialGraph.hlsl";
            compileOptions.ReadInclude =
                [&context](const std::filesystem::path& requested) -> std::optional<std::string>
            {
                try
                {
                    const auto include = context.ReadProjectFile(requested);
                    return std::string(reinterpret_cast<const char*>(include.data()), include.size());
                }
                catch (...)
                {
                    return std::nullopt;
                }
            };
            const auto compilation = CompileMaterialGraph(definition, compileOptions);
            if (!compilation.Succeeded() || compilation.Variants.empty())
            {
                const auto diagnostic = compilation.Diagnostics.empty()
                                            ? std::string("Material Graph generated no shader variants.")
                                            : compilation.Diagnostics.front().Message;
                throw std::runtime_error("Material Graph runtime material compilation failed: " + diagnostic);
            }

            const auto shaderImporter = CreateShaderAssetImporter();
            if (!shaderImporter.ContextualImport)
                throw std::logic_error("Material Graph import requires the contextual shader importer.");
            std::vector<std::pair<std::vector<std::string>, AssetId>> shaderVariants;
            shaderVariants.reserve(compilation.Variants.size());
            std::set<std::filesystem::path> sourceDependencies;
            for (const auto& variant : compilation.Variants)
            {
                const auto shaderKey = "shader/" + variant.StableSuffix;
                const auto shaderId = context.ResolveSubAssetId(shaderKey);
                auto shaderContext = context;
                shaderContext.Asset = shaderId;
                shaderContext.RelativePath = variant.GeneratedSource;
                shaderContext.RelativePath.replace_extension(".keireshader");
                shaderContext.SourcePath = context.ProjectRoot / shaderContext.RelativePath;
                shaderContext.MetadataPath = shaderContext.SourcePath;
                shaderContext.MetadataPath += ".keiremeta";
                shaderContext.ReadProjectFile =
                    [readProjectFile = context.ReadProjectFile, generatedSource = variant.GeneratedSource,
                     generatedBytes = TextBytes(variant.Hlsl)](const std::filesystem::path& requested)
                {
                    if (requested.lexically_normal() == generatedSource.lexically_normal())
                        return generatedBytes;
                    return readProjectFile(requested);
                };
                const auto importedShader = shaderImporter.ContextualImport(shaderContext, TextBytes(variant.Manifest));
                for (const auto& dependency : importedShader.SourceDependencies)
                {
                    if (dependency.RelativePath.lexically_normal() == variant.GeneratedSource.lexically_normal() ||
                        !sourceDependencies.insert(dependency.RelativePath.lexically_normal()).second)
                    {
                        continue;
                    }
                    output.SourceDependencies.push_back(dependency);
                }
                output.SubAssets.push_back({shaderId, ShaderAsset::StaticType(), shaderKey,
                                            variant.Keywords.empty() ? "Default Shader" : "Shader " + shaderKey,
                                            importedShader.Bytes, importedShader.AssetDependencies});
                shaderVariants.emplace_back(variant.Keywords, shaderId);
            }

            MaterialGraphInstanceDefinition defaults;
            defaults.Parent = context.Asset;
            const std::array ancestry{defaults};
            const auto resolved = ResolveMaterialGraphInstance(definition, ancestry);
            const auto material = BakeMaterialGraphInstance(
                definition, resolved,
                [&shaderVariants](const std::span<const std::string> keywords)
                {
                    const auto found = std::ranges::find_if(shaderVariants, [keywords](const auto& variant)
                                                            { return std::ranges::equal(variant.first, keywords); });
                    return found == shaderVariants.end() ? AssetId{} : found->second;
                });
            auto materialDependencies = output.AssetDependencies;
            materialDependencies.push_back(material.Shader);
            std::ranges::sort(materialDependencies);
            materialDependencies.erase(std::unique(materialDependencies.begin(), materialDependencies.end()),
                                       materialDependencies.end());
            output.SubAssets.push_back({context.ResolveSubAssetId("material/default"), MaterialAsset::StaticType(),
                                        "material/default", "Default Material", MaterialAsset::Encode(material),
                                        std::move(materialDependencies)});
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateMaterialGraphAssetDecoder()
    {
        return {MaterialGraphAsset::StaticType(), MaterialGraphAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return MaterialGraphAsset::Decode(bytes); }};
    }

    AssetImporterRegistration CreateMaterialGraphInstanceAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.MaterialGraphInstance";
        result.Version = 2;
        result.Type = MaterialGraphInstanceAsset::StaticType();
        result.Extensions = {".keirematerialinstance"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            if (!context.Asset || context.ProjectRoot.empty() || context.SourceRoot.empty() ||
                !context.ReadProjectFile || !context.ResolveSubAssetId || !context.ResolveSubAssetIdFor ||
                !context.ResolveAssetSource)
            {
                throw std::invalid_argument(
                    "Material Graph instance import requires source and stable subasset resolvers.");
            }
            const auto definition = MaterialGraphInstanceAsset::DecodeSource(bytes);
            AssetImportOutput output;
            output.Bytes = MaterialGraphInstanceAsset::Encode(definition);
            std::vector<MaterialGraphInstanceDefinition> ancestry{definition};
            MaterialGraphDefinition graph;
            AssetId graphAsset;
            AssetId parent = definition.Parent;
            std::set<AssetId> visited{context.Asset};
            const auto sourcePrefix = std::filesystem::relative(context.SourceRoot, context.ProjectRoot);
            for (std::size_t depth = 0; depth < 16 && parent; ++depth)
            {
                if (!visited.insert(parent).second)
                    throw std::invalid_argument("Material Graph instance parent chain contains a cycle.");
                output.AssetDependencies.push_back(parent);
                const auto source = context.ResolveAssetSource(parent);
                if (!source)
                    throw std::runtime_error("Material Graph instance parent is not present in the source index: " +
                                             parent.ToString());
                const auto parentBytes = context.ReadProjectFile(sourcePrefix / source->RelativePath);
                if (source->Type == MaterialGraphAsset::StaticType())
                {
                    graph = MaterialGraphAsset::DecodeSource(parentBytes);
                    graphAsset = parent;
                    break;
                }
                if (source->Type != MaterialGraphInstanceAsset::StaticType())
                    throw std::invalid_argument("Material Graph instance parent must be a graph or another instance.");
                auto parentInstance = MaterialGraphInstanceAsset::DecodeSource(parentBytes);
                parent = parentInstance.Parent;
                ancestry.push_back(std::move(parentInstance));
            }
            if (!graphAsset)
                throw std::invalid_argument("Material Graph instance parent chain exceeds 16 entries or has no graph.");

            std::ranges::reverse(ancestry);
            const auto resolved = ResolveMaterialGraphInstance(graph, ancestry);
            const auto material = BakeMaterialGraphInstance(
                graph, resolved, [&context, graphAsset](const std::span<const std::string> keywords)
                { return context.ResolveSubAssetIdFor(graphAsset, "shader/" + KeywordSuffix(keywords)); });
            for (const auto& [name, value] : resolved.Properties)
            {
                (void)name;
                if (const auto* asset = std::get_if<AssetId>(&value); asset && *asset)
                    output.AssetDependencies.push_back(*asset);
            }
            auto materialDependencies = output.AssetDependencies;
            materialDependencies.push_back(material.Shader);
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
            std::ranges::sort(materialDependencies);
            materialDependencies.erase(std::unique(materialDependencies.begin(), materialDependencies.end()),
                                       materialDependencies.end());
            output.SubAssets.push_back({context.ResolveSubAssetId("material/default"), MaterialAsset::StaticType(),
                                        "material/default", "Runtime Material", MaterialAsset::Encode(material),
                                        std::move(materialDependencies)});
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateMaterialGraphInstanceAssetDecoder()
    {
        return {MaterialGraphInstanceAsset::StaticType(), MaterialGraphInstanceAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return MaterialGraphInstanceAsset::Decode(bytes); }};
    }
} // namespace Keire
