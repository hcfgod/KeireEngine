#include "KeireInternal/Rendering/ShaderGraphCompilerInternal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Keire::Detail
{
    std::size_t ShaderGraphEndpointHash::operator()(const ShaderGraphEndpoint& value) const noexcept
    {
        auto result = std::hash<AssetId>{}(value.Node);
        result ^= std::hash<AssetId>{}(value.Pin) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
        return result;
    }

    [[nodiscard]] bool IsUnlitShaderGraphOutput(const ShaderGraphOutput output) noexcept
    {
        return output == ShaderGraphOutput::Unlit || output == ShaderGraphOutput::Fullscreen;
    }

    [[nodiscard]] bool IsValidShaderGraphIdentifier(const std::string_view value)
    {
        if (value.empty() || value.size() > MaximumShaderGraphText ||
            !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
            return false;
        return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                   { return std::isalnum(character) || character == '_'; });
    }

    [[nodiscard]] bool IsSafeShaderGraphRelativePath(const std::filesystem::path& value)
    {
        if (value.empty() || value.is_absolute())
            return false;
        const auto normalized = value.lexically_normal().generic_string();
        return !normalized.empty() && normalized.size() <= MaximumShaderGraphPath && normalized != "." &&
               !normalized.starts_with("..") && normalized.find(':') == std::string::npos;
    }

    [[nodiscard]] const ShaderGraphPin* FindShaderGraphPin(const ShaderGraphNode& node, const std::string_view name,
                                                           const ShaderGraphPinDirection direction)
    {
        const auto found = std::ranges::find_if(node.Pins, [name, direction](const ShaderGraphPin& pin)
                                                { return pin.Name == name && pin.Direction == direction; });
        return found == node.Pins.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::size_t EstimatedNodeCost(const ShaderGraphNodeKind kind) noexcept
    {
        switch (kind)
        {
        case ShaderGraphNodeKind::Master:
            return 48;
        case ShaderGraphNodeKind::TextureSample:
        case ShaderGraphNodeKind::TextureSampleLevel:
            return 4;
        case ShaderGraphNodeKind::TriplanarSample:
            return 18;
        case ShaderGraphNodeKind::NormalMap:
            return 14;
        case ShaderGraphNodeKind::DetailNormal:
            return 10;
        case ShaderGraphNodeKind::Parallax:
            return 16;
        case ShaderGraphNodeKind::Divide:
        case ShaderGraphNodeKind::Power:
        case ShaderGraphNodeKind::Normalize:
        case ShaderGraphNodeKind::Length:
        case ShaderGraphNodeKind::Fresnel:
        case ShaderGraphNodeKind::Refract:
            return 8;
        case ShaderGraphNodeKind::SimpleNoise:
            return 28;
        case ShaderGraphNodeKind::VoronoiNoise:
            return 36;
        case ShaderGraphNodeKind::GradientNoise:
            return 30;
        case ShaderGraphNodeKind::RotateUV:
        case ShaderGraphNodeKind::Desaturate:
        case ShaderGraphNodeKind::Remap:
        case ShaderGraphNodeKind::SmoothStep:
            return 6;
        case ShaderGraphNodeKind::Sine:
        case ShaderGraphNodeKind::Cosine:
        case ShaderGraphNodeKind::Posterize:
        case ShaderGraphNodeKind::SquareRoot:
        case ShaderGraphNodeKind::ReciprocalSquareRoot:
            return 4;
        case ShaderGraphNodeKind::HueShift:
            return 12;
        case ShaderGraphNodeKind::Checkerboard:
            return 8;
        case ShaderGraphNodeKind::Parameter:
        case ShaderGraphNodeKind::Constant:
        case ShaderGraphNodeKind::UV:
        case ShaderGraphNodeKind::Keyword:
        case ShaderGraphNodeKind::VertexColor:
        case ShaderGraphNodeKind::WorldPosition:
        case ShaderGraphNodeKind::WorldNormal:
        case ShaderGraphNodeKind::ViewDirection:
            return 0;
        default:
            return 2;
        }
    }

    [[nodiscard]] ShaderGraphStatistics AnalyzeShaderGraph(const ShaderGraphDefinition& definition)
    {
        ShaderGraphStatistics result;
        result.NodeCount = definition.Nodes.size();
        result.ConnectionCount = definition.Connections.size();
        const auto master = std::ranges::find(definition.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
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
            if (node.Kind == ShaderGraphNodeKind::TriplanarSample)
                result.TextureSampleCount += 3U;
            else if (node.Kind == ShaderGraphNodeKind::TextureSample ||
                     node.Kind == ShaderGraphNodeKind::TextureSampleLevel)
                ++result.TextureSampleCount;
        }
        return result;
    }

    [[nodiscard]] const ShaderGraphNode& RequireShaderGraphNode(const ShaderGraphDefinition& definition,
                                                                const AssetId id)
    {
        const auto found = std::ranges::find(definition.Nodes, id, &ShaderGraphNode::Id);
        if (found == definition.Nodes.end())
            throw std::invalid_argument("Shader Graph connection references an unknown node.");
        return *found;
    }

    [[nodiscard]] const ShaderGraphPin& RequireShaderGraphPin(const ShaderGraphNode& node, const AssetId id)
    {
        const auto found = std::ranges::find(node.Pins, id, &ShaderGraphPin::Id);
        if (found == node.Pins.end())
            throw std::invalid_argument("Shader Graph connection references an unknown pin.");
        return *found;
    }

    [[nodiscard]] MaterialPropertyValue ToMaterialPropertyValue(const ShaderGraphValue& value)
    {
        return std::visit(
            [](const auto& decoded) -> MaterialPropertyValue
            {
                using T = std::decay_t<decltype(decoded)>;
                if constexpr (std::same_as<T, ShaderGraphMaterialAttributesValue> ||
                              std::same_as<T, ShaderGraphBsdfValue>)
                    throw std::invalid_argument("Shader Graph structured values cannot become properties.");
                else
                    return decoded;
            },
            value);
    }

    template <typename Variant> void ValidateFiniteShaderGraphValueImpl(const Variant& value)
    {
        std::visit(
            [](const auto& typed)
            {
                using T = std::decay_t<decltype(typed)>;
                if constexpr (std::same_as<T, float>)
                {
                    if (!std::isfinite(typed))
                        throw std::invalid_argument("Shader Graph values must be finite.");
                }
                else if constexpr (std::same_as<T, Vector2>)
                {
                    if (!std::isfinite(typed.X) || !std::isfinite(typed.Y))
                        throw std::invalid_argument("Shader Graph values must be finite.");
                }
                else if constexpr (std::same_as<T, Vector3>)
                {
                    if (!std::isfinite(typed.X) || !std::isfinite(typed.Y) || !std::isfinite(typed.Z))
                        throw std::invalid_argument("Shader Graph values must be finite.");
                }
                else if constexpr (std::same_as<T, Vector4>)
                {
                    if (!Math::IsFinite(typed))
                        throw std::invalid_argument("Shader Graph values must be finite.");
                }
                else if constexpr (std::same_as<T, Color>)
                {
                    if (!std::isfinite(typed.Red) || !std::isfinite(typed.Green) || !std::isfinite(typed.Blue) ||
                        !std::isfinite(typed.Alpha))
                        throw std::invalid_argument("Shader Graph values must be finite.");
                }
            },
            value);
    }

    void ValidateFiniteShaderGraphValue(const ShaderGraphValue& value) { ValidateFiniteShaderGraphValueImpl(value); }

    void ValidateFiniteShaderGraphValue(const MaterialPropertyValue& value)
    {
        ValidateFiniteShaderGraphValueImpl(value);
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

    [[nodiscard]] ShaderGraphExpression MakeShaderGraphLiteral(const ShaderGraphValue& value,
                                                               const ShaderGraphValueType type)
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
            return ShaderGraphExpression{std::move(result), type};
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
        if (std::holds_alternative<ShaderGraphMaterialAttributesValue>(value))
            return {"DefaultShaderGraphSurface()", ShaderGraphValueType::MaterialAttributes};
        if (std::holds_alternative<ShaderGraphBsdfValue>(value))
            return {"DefaultShaderGraphBsdf()", ShaderGraphValueType::Bsdf};
        return {"_InvalidTexture", ShaderGraphValueType::Texture2D};
    }

    [[nodiscard]] std::string ShaderGraphSwizzle(const ShaderGraphValueType type)
    {
        switch (type)
        {
        case ShaderGraphValueType::Scalar:
            return ".x";
        case ShaderGraphValueType::Vector2:
            return ".xy";
        case ShaderGraphValueType::Vector3:
            return ".xyz";
        case ShaderGraphValueType::Vector4:
        case ShaderGraphValueType::Color:
        case ShaderGraphValueType::Texture2D:
        case ShaderGraphValueType::MaterialAttributes:
        case ShaderGraphValueType::Bsdf:
            return {};
        }
        return {};
    }

    [[nodiscard]] std::string ShaderGraphPropertySymbol(const std::string_view name)
    {
        return "_KeireMaterial_" + std::string(name);
    }

    [[nodiscard]] std::string ShaderGraphVertexPropertySymbol(const std::string_view name)
    {
        return "_KeireVertexMaterial_" + std::string(name);
    }

    [[nodiscard]] bool SupportsShaderGraphStage(const ShaderGraphShaderStage stages,
                                                const ShaderGraphShaderStage stage) noexcept
    {
        return (static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(stage)) != 0U;
    }

    [[nodiscard]] ShaderGraphExpression CoerceShaderGraphExpression(ShaderGraphExpression expression,
                                                                    const ShaderGraphValueType target)
    {
        if (expression.Type == target ||
            ((expression.Type == ShaderGraphValueType::Color && target == ShaderGraphValueType::Vector4) ||
             (expression.Type == ShaderGraphValueType::Vector4 && target == ShaderGraphValueType::Color)))
        {
            expression.Type = target;
            return expression;
        }
        if ((expression.Type == ShaderGraphValueType::Color || expression.Type == ShaderGraphValueType::Vector4) &&
            target == ShaderGraphValueType::Vector3)
            return {"(" + expression.Code + ").xyz", target};
        if (expression.Type == ShaderGraphValueType::Vector3 &&
            (target == ShaderGraphValueType::Color || target == ShaderGraphValueType::Vector4))
            return {"float4(" + expression.Code + ", 1.0F)", target};
        if (expression.Type != ShaderGraphValueType::Scalar || target == ShaderGraphValueType::Texture2D ||
            target == ShaderGraphValueType::MaterialAttributes || target == ShaderGraphValueType::Bsdf)
            throw std::invalid_argument("Shader Graph expression cannot be converted to the destination type.");
        switch (target)
        {
        case ShaderGraphValueType::Vector2:
            return {"float2(" + expression.Code + ", " + expression.Code + ")", target};
        case ShaderGraphValueType::Vector3:
            return {"float3(" + expression.Code + ", " + expression.Code + ", " + expression.Code + ")", target};
        case ShaderGraphValueType::Vector4:
        case ShaderGraphValueType::Color:
            return {"float4(" + expression.Code + ", " + expression.Code + ", " + expression.Code + ", " +
                        expression.Code + ")",
                    target};
        case ShaderGraphValueType::Scalar:
        case ShaderGraphValueType::Texture2D:
        case ShaderGraphValueType::MaterialAttributes:
        case ShaderGraphValueType::Bsdf:
            break;
        }
        throw std::invalid_argument("Shader Graph scalar broadcast target is invalid.");
    }

    [[nodiscard]] std::string ShaderGraphKeywordSuffix(const std::span<const std::string> keywords)
    {
        std::vector<std::string> canonical(keywords.begin(), keywords.end());
        std::ranges::sort(canonical);
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto& keyword : canonical)
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

    [[nodiscard]] std::filesystem::path ShaderGraphVariantSourcePath(const std::filesystem::path& base,
                                                                     const std::string_view suffix)
    {
        auto result = base;
        const auto extension = result.extension();
        result.replace_filename(result.stem().string() + '-' + std::string(suffix) + extension.string());
        return result;
    }
} // namespace Keire::Detail
