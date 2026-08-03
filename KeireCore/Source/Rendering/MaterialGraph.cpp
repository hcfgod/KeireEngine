#include "Keire/Rendering/MaterialGraph.h"

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
        constexpr std::size_t MaximumGraphAssetBytes = 32U * 1024U * 1024U;

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

        [[nodiscard]] Json EncodeValue(const MaterialGraphValue& value)
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
                nodes.push_back({{"id", node.Id.ToString()},
                                 {"kind", static_cast<std::uint8_t>(node.Kind)},
                                 {"name", node.Name},
                                 {"position", {node.EditorPosition.X, node.EditorPosition.Y}},
                                 {"valueType", static_cast<std::uint8_t>(node.ValueType)},
                                 {"value", EncodeValue(node.Value)},
                                 {"textureSemantic", static_cast<std::uint8_t>(node.TextureSemantic)},
                                 {"symbol", node.Symbol},
                                 {"include", node.Include.generic_string()},
                                 {"function", node.Function},
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
            return {{"schemaVersion", definition.SchemaVersion},
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
            result.SchemaVersion = source.value("schemaVersion", 0U);
            result.Output = static_cast<MaterialGraphOutput>(source.value("output", static_cast<std::uint8_t>(0)));
            result.IncludeRoots.clear();
            for (const auto& root : includeRoots)
                result.IncludeRoots.emplace_back(root.get<std::string>());
            for (const auto& encoded : nodes)
            {
                MaterialGraphNode node;
                node.Id = AssetId::Parse(encoded.at("id").get<std::string>());
                node.Kind = static_cast<MaterialGraphNodeKind>(encoded.at("kind").get<std::uint8_t>());
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
            ValidateMaterialGraph(result);
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
            }
            return 1.0F;
        }

        void AddPin(MaterialGraphNode& node, std::string name, const MaterialGraphValueType type,
                    const MaterialGraphPinDirection direction, MaterialGraphValue value)
        {
            node.Pins.push_back({AssetId::Generate(), std::move(name), type, direction, std::move(value)});
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
                    (output == MaterialGraphValueType::Vector4 && input == MaterialGraphValueType::Color)) ||
                   (output == MaterialGraphValueType::Scalar && input != MaterialGraphValueType::Texture2D);
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
                return 4;
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
                return 8;
            case MaterialGraphNodeKind::SimpleNoise:
                return 28;
            case MaterialGraphNodeKind::RotateUV:
            case MaterialGraphNodeKind::Desaturate:
            case MaterialGraphNodeKind::Remap:
            case MaterialGraphNodeKind::SmoothStep:
                return 6;
            case MaterialGraphNodeKind::Sine:
            case MaterialGraphNodeKind::Cosine:
            case MaterialGraphNodeKind::Posterize:
                return 4;
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
                result.TextureSampleCount += node.Kind == MaterialGraphNodeKind::TextureSample ? 1U : 0U;
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

        void ValidateFiniteValue(const MaterialGraphValue& value)
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
                return {};
            case MaterialGraphValueType::Texture2D:
                return {};
            }
            return {};
        }

        [[nodiscard]] std::string HlslType(const MaterialGraphValueType type)
        {
            switch (type)
            {
            case MaterialGraphValueType::Scalar:
                return "float";
            case MaterialGraphValueType::Vector2:
                return "float2";
            case MaterialGraphValueType::Vector3:
                return "float3";
            case MaterialGraphValueType::Vector4:
            case MaterialGraphValueType::Color:
                return "float4";
            case MaterialGraphValueType::Texture2D:
                return "Texture2D";
            }
            return "float";
        }

        [[nodiscard]] std::string PropertySymbol(const std::string_view name)
        {
            return "_KeireMaterial_" + std::string(name);
        }

        [[nodiscard]] std::string KeywordDefine(const std::string_view name) { return "KEIRE_MG_" + std::string(name); }

        [[nodiscard]] Expression Coerce(Expression expression, const MaterialGraphValueType target)
        {
            if (expression.Type == target ||
                ((expression.Type == MaterialGraphValueType::Color && target == MaterialGraphValueType::Vector4) ||
                 (expression.Type == MaterialGraphValueType::Vector4 && target == MaterialGraphValueType::Color)))
            {
                expression.Type = target;
                return expression;
            }
            if (expression.Type != MaterialGraphValueType::Scalar || target == MaterialGraphValueType::Texture2D)
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
                break;
            }
            throw std::invalid_argument("Material Graph scalar broadcast target is invalid.");
        }

        [[nodiscard]] std::string KeywordSuffix(const std::span<const std::string> keywords)
        {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const auto& keyword : keywords)
            {
                for (const unsigned char character : keyword)
                {
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

        [[nodiscard]] std::string PropertyTypeName(const MaterialGraphValueType type)
        {
            switch (type)
            {
            case MaterialGraphValueType::Scalar:
                return "Float";
            case MaterialGraphValueType::Vector2:
                return "Vector2";
            case MaterialGraphValueType::Vector3:
                return "Vector3";
            case MaterialGraphValueType::Vector4:
                return "Vector4";
            case MaterialGraphValueType::Color:
                return "Color";
            case MaterialGraphValueType::Texture2D:
                return "Texture2D";
            }
            return "Float";
        }

        [[nodiscard]] std::string SemanticName(const ShaderTextureSemantic semantic)
        {
            constexpr std::array names{"Generic",   "BaseColor", "Normal",   "MetallicRoughness",
                                       "Occlusion", "Emissive",  "Metallic", "Roughness"};
            const auto index = static_cast<std::size_t>(semantic);
            return index < names.size() ? names[index] : names.front();
        }

        [[nodiscard]] Json ManifestProperty(const ShaderPropertyDefinition& property)
        {
            Json result{
                {"name", property.Name}, {"displayName", property.DisplayName}, {"category", property.Category}};
            const auto graphType = static_cast<MaterialGraphValueType>(property.Type);
            result["type"] = PropertyTypeName(graphType);
            if (property.Type == ShaderPropertyType::Texture2D)
            {
                result["semantic"] = SemanticName(property.TextureSemantic);
                result["default"] = property.DefaultTexture ? Json(property.DefaultTexture.ToString()) : Json(nullptr);
            }
            else
                result["default"] = Json::array({property.DefaultValue.X, property.DefaultValue.Y,
                                                 property.DefaultValue.Z, property.DefaultValue.W});
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
                const auto baseColor = input(unlit ? "Color" : "BaseColor", MaterialGraphValueType::Color);
                const auto emission = input("Emission", MaterialGraphValueType::Color);
                const auto opacity = input("Opacity", MaterialGraphValueType::Scalar);
                const auto metallic = unlit ? "0.0F" : input("Metallic", MaterialGraphValueType::Scalar);
                const auto roughness = unlit ? "1.0F" : input("Roughness", MaterialGraphValueType::Scalar);
                const auto specular =
                    unlit ? "0.5F" : optionalInput("Specular", MaterialGraphValueType::Scalar, "0.5F");
                const auto clearCoat =
                    unlit ? "0.0F" : optionalInput("ClearCoat", MaterialGraphValueType::Scalar, "0.0F");
                const auto clearCoatRoughness =
                    unlit ? "0.25F" : optionalInput("ClearCoatRoughness", MaterialGraphValueType::Scalar, "0.25F");
                const auto sheenColor = unlit ? "float4(0.0F, 0.0F, 0.0F, 1.0F)"
                                              : optionalInput("SheenColor", MaterialGraphValueType::Color,
                                                              "float4(0.0F, 0.0F, 0.0F, 1.0F)");
                const auto sheenRoughness =
                    unlit ? "0.5F" : optionalInput("SheenRoughness", MaterialGraphValueType::Scalar, "0.5F");
                const auto normal = unlit || !inputConnected("Normal")
                                        ? "input.Normal"
                                        : input("Normal", MaterialGraphValueType::Vector3);
                const bool hasDetailNormal = !unlit && inputConnected("DetailNormal");
                const auto detailNormal = hasDetailNormal ? input("DetailNormal", MaterialGraphValueType::Vector3)
                                                          : std::string("input.Normal");
                const auto occlusion = unlit ? "1.0F" : input("Occlusion", MaterialGraphValueType::Scalar);

                std::ostringstream source;
                source << "// Generated by Keire Material Graph. Do not edit.\n";
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
    float4 Position : SV_Position;
};

cbuffer ObjectData : register(b0, space1)
{
    float4x4 Model;
    float4x4 View;
    float4x4 Projection;
    float4x4 NormalMatrix;
};

struct InstanceData
{
    float4x4 Model;
    float4x4 NormalMatrix;
    float4 Tint;
};

StructuredBuffer<InstanceData> Instances : register(t0, space0);

cbuffer SceneData : register(b0, space3)
{
    float4 AmbientColorIntensity;
    float4 DirectionalColorIntensity;
    float4 DirectionalDirectionExposure;
    float4 SurfaceParameters;
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
                source << R"HLSL(
static const float Pi = 3.14159265359F;

float3 SafeNormalize(const float3 value, const float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-12F && all(isfinite(value)) ? value * rsqrt(lengthSquared) : fallback;
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

float MaterialNoise(const float2 uv, const float scale, const float detail)
{
    const float2 position = uv * max(abs(scale), 1.0e-4F);
    const float2 cell = floor(position);
    const float2 local = frac(position);
    const float2 blend = local * local * (3.0F - 2.0F * local);
    const float base = lerp(lerp(MaterialHash(cell), MaterialHash(cell + float2(1.0F, 0.0F)), blend.x),
                            lerp(MaterialHash(cell + float2(0.0F, 1.0F)),
                                 MaterialHash(cell + float2(1.0F, 1.0F)), blend.x),
                            blend.y);
    const float octave = MaterialHash(floor(position * 2.0F)) * 0.5F;
    return lerp(base, saturate(base * 0.75F + octave), saturate(detail));
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

VertexOutput VSMain(VertexInput input, const uint instanceId : SV_InstanceID)
{
    VertexOutput output;
    const InstanceData instance = Instances[instanceId];
    const float4 world = mul(instance.Model, float4(input.Position, 1.0F));
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
    output.Color = input.Color * instance.Tint;
    output.WorldPosition = world.xyz;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
)HLSL";
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
                    if (hasDetailNormal)
                        source << "    const float3 graphNormal = SafeNormalize(BlendDetailNormal(SafeNormalize("
                               << normal << ", input.Normal), SafeNormalize(" << detailNormal
                               << ", input.Normal), 1.0F), input.Normal);\n";
                    else
                        source << "    const float3 graphNormal = SafeNormalize(" << normal << ", input.Normal);\n";
                    source << "    const float3 lightDirection = SafeNormalize(-DirectionalDirectionExposure.xyz, "
                              "float3(0.0F, 1.0F, 0.0F));\n";
                    source << "    const float noL = saturate(dot(graphNormal, lightDirection));\n";
                    source << "    const float3 dielectric = (0.08F * graphSpecular).xxx;\n";
                    source << "    const float3 f0 = lerp(dielectric, graphBaseColor.rgb, graphMetallic);\n";
                    source << "    const float3 viewDirection = SafeNormalize(input.ViewDirection, graphNormal);\n";
                    source << "    const float3 halfVector = SafeNormalize(lightDirection + viewDirection, "
                              "graphNormal);\n";
                    source << "    const float noV = max(saturate(dot(graphNormal, viewDirection)), 1.0e-4F);\n";
                    source << "    const float noH = saturate(dot(graphNormal, halfVector));\n";
                    source << "    const float voH = saturate(dot(viewDirection, halfVector));\n";
                    source << "    const float3 fresnel = FresnelSchlick(voH, f0);\n";
                    source << "    const float distribution = DistributionGgx(noH, graphRoughness);\n";
                    source << "    const float visibility = VisibilitySmithGgx(noV, noL, graphRoughness);\n";
                    source << "    const float3 specular = fresnel * distribution * visibility;\n";
                    source << "    const float clearCoatDistribution = DistributionGgx(noH, "
                              "graphClearCoatRoughness);\n";
                    source << "    const float clearCoatVisibility = VisibilitySmithGgx(noV, noL, "
                              "graphClearCoatRoughness);\n";
                    source << "    const float clearCoatFresnel = 0.04F + 0.96F * pow(1.0F - voH, 5.0F);\n";
                    source << "    const float3 clearCoatSpecular = (graphClearCoat * clearCoatDistribution * "
                              "clearCoatVisibility * clearCoatFresnel).xxx;\n";
                    source << "    const float sheenFactor = pow(1.0F - noH, lerp(8.0F, 1.0F, "
                              "graphSheenRoughness));\n";
                    source << "    const float3 sheen = graphSheenColor * sheenFactor * (1.0F - graphMetallic);\n";
                    source << "    const float3 diffuseWeight = (1.0F - fresnel) * (1.0F - graphMetallic);\n";
                    source << "    const float3 diffuse = diffuseWeight * graphBaseColor.rgb / Pi;\n";
                    source << "    const float ao = saturate(" << occlusion << ");\n";
                    source << "    float3 graphColor = graphBaseColor.rgb * (1.0F - graphMetallic) * "
                              "AmbientColorIntensity.rgb * "
                              "AmbientColorIntensity.a * ao;\n";
                    source << "    graphColor += (diffuse + specular + clearCoatSpecular + sheen) * "
                              "DirectionalColorIntensity.rgb * "
                              "DirectionalColorIntensity.a * noL;\n";
                    source << "    graphColor += graphEmission;\n";
                }
                source << "    if (!all(isfinite(" << materialBindingSentinel << ")))\n";
                source << "        graphColor += " << materialBindingSentinel << ".xyz;\n";
                source << "    const float alpha = saturate(graphBaseColor.a * graphOpacity);\n";
                source << "    if (SurfaceParameters.y > 0.5F && SurfaceParameters.y < 1.5F)\n";
                source << "        clip(alpha - SurfaceParameters.x);\n";
                if (m_Definition.Output == MaterialGraphOutput::Transparent ||
                    m_Definition.Output == MaterialGraphOutput::Decal)
                    source << "    return float4(graphColor * alpha, alpha);\n";
                else
                    source << "    return float4(graphColor, alpha);\n";
                source << "}\n";
                return source.str();
            }

          private:
            void RegisterProperty(const MaterialGraphNode& node)
            {
                ShaderPropertyDefinition property;
                property.Name = node.Symbol;
                property.DisplayName = node.Name.empty() ? node.Symbol : node.Name;
                property.Category = "Material Graph";
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
                return Coerce(Evaluate(found->second), pin.Type);
            }

            [[nodiscard]] Expression Evaluate(const MaterialGraphEndpoint endpoint)
            {
                if (const auto found = m_Cache.find(endpoint); found != m_Cache.end())
                    return found->second;
                if (!m_Visiting.insert(endpoint.Node).second)
                    throw std::invalid_argument("Material Graph contains an expression cycle.");
                const auto& node = RequireNode(m_Definition, endpoint.Node);
                if (RequirePin(node, endpoint.Pin).Direction != MaterialGraphPinDirection::Output)
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
                    result = {PropertySymbol(node.Symbol) + Swizzle(node.ValueType), node.ValueType};
                    break;
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
                    result = {texture.Code + ".Sample(" + texture.Code + "Sampler, " + uv.Code + ")",
                              MaterialGraphValueType::Color};
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
                    result = {"input.WorldPosition", MaterialGraphValueType::Vector3};
                    break;
                case MaterialGraphNodeKind::WorldNormal:
                    result = {"input.Normal", MaterialGraphValueType::Vector3};
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
                        m_CustomIncludes.push_back(node.Include.lexically_normal());
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
                if (!source || source->size() > 1024U * 1024U)
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
            std::vector<std::filesystem::path> m_CustomIncludes;
        };

        [[nodiscard]] std::string BuildManifest(const MaterialGraphDefinition& definition,
                                                const std::filesystem::path& generatedSource,
                                                const std::span<const ShaderPropertyDefinition> properties,
                                                const std::span<const std::string> keywords)
        {
            Json encodedProperties = Json::array();
            for (const auto& property : properties)
                encodedProperties.push_back(ManifestProperty(property));
            Json defines = Json::object();
            for (const auto& keyword : keywords)
                defines[KeywordDefine(keyword)] = "1";
            Json roots = Json::array();
            for (const auto& root : definition.IncludeRoots)
                roots.push_back(root.generic_string());
            const bool transparent = definition.Output == MaterialGraphOutput::Transparent ||
                                     definition.Output == MaterialGraphOutput::Decal;
            const Json manifest{{"schemaVersion", 1},
                                {"source", generatedSource.generic_string()},
                                {"vertexLayoutVersion", 2},
                                {"receivesShadows", false},
                                {"usesForwardPlus", false},
                                {"usesInstancing", true},
                                {"usesImageBasedLighting", false},
                                {"stages", {{"vertex", "VSMain"}, {"fragment", "PSMain"}}},
                                {"defines", std::move(defines)},
                                {"includeRoots", std::move(roots)},
                                {"renderState",
                                 {{"topology", "TriangleList"},
                                  {"culling", definition.Output == MaterialGraphOutput::Decal ? "Front" : "Back"},
                                  {"depthTest", true},
                                  {"depthWrite", !transparent},
                                  {"blend", transparent}}},
                                {"properties", std::move(encodedProperties)}};
            return manifest.dump(2) + '\n';
        }

        [[nodiscard]] bool MaterialValueMatches(const MaterialPropertyValue& value, const MaterialGraphValueType type)
        {
            return value.index() == static_cast<std::size_t>(type);
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
                result.Properties.emplace(name, DecodeValue(encoded.at("value"), type));
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
            result += sizeof(node) + node.Name.size() + node.Symbol.size() + node.Function.size() +
                      node.Include.generic_string().size() + node.Pins.size() * sizeof(MaterialGraphPin);
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

    MaterialGraphNode CreateMaterialGraphNode(const MaterialGraphNodeKind kind, const MaterialGraphValueType valueType)
    {
        MaterialGraphNode node;
        node.Id = AssetId::Generate();
        node.Kind = kind;
        node.ValueType = valueType;
        node.Value = DefaultValue(valueType);
        const auto input = [&](const std::string_view name, const MaterialGraphValueType type, MaterialGraphValue value)
        { AddPin(node, std::string(name), type, MaterialGraphPinDirection::Input, std::move(value)); };
        const auto output = [&](const std::string_view name, const MaterialGraphValueType type)
        { AddPin(node, std::string(name), type, MaterialGraphPinDirection::Output, DefaultValue(type)); };
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
            std::erase_if(master.Pins, [](const MaterialGraphPin& pin)
                          { return pin.Name != "BaseColor" && pin.Name != "Emission" && pin.Name != "Opacity"; });
            master.Pins.front().Name = "Color";
        }
        else if (output == MaterialGraphOutput::Transparent)
            master.Name = "Transparent PBR Master";
        else if (output == MaterialGraphOutput::Decal)
            master.Name = "Decal PBR Master";
        definition.Nodes.push_back(std::move(master));
        return definition;
    }

    void ValidateMaterialGraph(const MaterialGraphDefinition& definition)
    {
        if (definition.SchemaVersion != 1 || definition.Output > MaterialGraphOutput::Unlit ||
            definition.Nodes.empty() || definition.Nodes.size() > MaximumGraphNodes ||
            definition.Connections.size() > MaximumGraphConnections ||
            definition.Keywords.size() > MaximumGraphKeywords || definition.IncludeRoots.empty() ||
            definition.IncludeRoots.size() > MaximumGraphIncludeRoots)
            throw std::invalid_argument("Material Graph has an unsupported schema or exceeds a bounded collection.");

        std::set<AssetId> identities;
        std::set<std::string, std::less<>> properties;
        std::set<std::string, std::less<>> keywordNodeSymbols;
        std::size_t masters = 0;
        std::size_t propertyCount = 0;
        for (const auto& root : definition.IncludeRoots)
            if (!SafeRelativePath(root))
                throw std::invalid_argument("Material Graph include roots must be confined relative paths.");
        for (const auto& node : definition.Nodes)
        {
            if (!node.Id || !identities.insert(node.Id).second || node.Kind > MaterialGraphNodeKind::Posterize ||
                node.ValueType > MaterialGraphValueType::Texture2D || node.Name.size() > MaximumGraphText ||
                node.TextureSemantic > ShaderTextureSemantic::Roughness || node.Pins.empty() ||
                node.Pins.size() > MaximumGraphPinsPerNode || !Math::IsFinite(node.EditorPosition) ||
                !ValueMatchesType(node.Value, node.ValueType))
                throw std::invalid_argument("Material Graph node identity, type, position, or pins are invalid.");
            ValidateFiniteValue(node.Value);
            if (node.Kind == MaterialGraphNodeKind::Master)
                ++masters;
            if (node.Kind == MaterialGraphNodeKind::Parameter)
            {
                ++propertyCount;
                if (!ValidIdentifier(node.Symbol) || !properties.insert(node.Symbol).second)
                    throw std::invalid_argument("Material Graph parameter symbols must be unique identifiers.");
            }
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
                if (pin.Type > MaterialGraphValueType::Texture2D || pin.Direction > MaterialGraphPinDirection::Output)
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
                node.ValueType == MaterialGraphValueType::Texture2D)
                throw std::invalid_argument("Material Graph numeric nodes cannot use Texture2D values.");
        }
        if (masters != 1 || propertyCount > MaximumGraphProperties)
            throw std::invalid_argument("Material Graph requires one Master node and at most 80 properties.");
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

        std::set<MaterialGraphEndpoint, bool (*)(const MaterialGraphEndpoint&, const MaterialGraphEndpoint&)> inputs(
            [](const MaterialGraphEndpoint& left, const MaterialGraphEndpoint& right)
            { return left.Node < right.Node || (left.Node == right.Node && left.Pin < right.Pin); });
        for (const auto& connection : definition.Connections)
        {
            if (!connection.Id || !identities.insert(connection.Id).second || !connection.Output.Node ||
                !connection.Output.Pin || !connection.Input.Node || !connection.Input.Pin ||
                connection.Output.Node == connection.Input.Node || !inputs.insert(connection.Input).second)
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
            for (const auto& keywords : variants)
            {
                GraphCompiler compiler(definition, options, keywords, result.Properties, result.Dependencies);
                auto hlsl = compiler.BuildHlsl();
                auto suffix = KeywordSuffix(keywords);
                auto generatedSource = VariantSourcePath(options.GeneratedSource, suffix);
                result.Variants.push_back({keywords, std::move(suffix), generatedSource, std::move(hlsl),
                                           BuildManifest(definition, generatedSource, result.Properties, keywords)});
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
        result.Surface.DoubleSided = graph.Output == MaterialGraphOutput::Decal;
        return result;
    }

    AssetImporterRegistration CreateMaterialGraphAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.MaterialGraph";
        result.Version = 2;
        result.Type = MaterialGraphAsset::StaticType();
        result.Extensions = {".keirematerialgraph"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
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
        result.Version = 1;
        result.Type = MaterialGraphInstanceAsset::StaticType();
        result.Extensions = {".keirematerialinstance"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto definition = MaterialGraphInstanceAsset::DecodeSource(bytes);
            AssetImportOutput output;
            output.Bytes = MaterialGraphInstanceAsset::Encode(definition);
            output.AssetDependencies.push_back(definition.Parent);
            for (const auto& [name, value] : definition.Properties)
            {
                (void)name;
                if (const auto* asset = std::get_if<AssetId>(&value); asset && *asset)
                    output.AssetDependencies.push_back(*asset);
            }
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
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
