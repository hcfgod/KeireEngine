#include "KeireInternal/Rendering/ShaderGraphCompilerInternal.h"

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire::Detail
{
    ShaderGraphCompiler::ShaderGraphCompiler(const ShaderGraphDefinition& definition,
                                             const ShaderGraphCompileOptions& options,
                                             std::span<const std::string> keywords,
                                             std::vector<ShaderPropertyDefinition>& properties,
                                             std::vector<std::filesystem::path>& dependencies)
        : m_Definition(definition), m_Options(options), m_Keywords(keywords), m_Properties(properties),
          m_Dependencies(dependencies)
    {
        for (const auto& connection : m_Definition.Connections)
            m_Incoming.emplace(connection.Input, connection.Output);
        for (const auto& node : m_Definition.Nodes)
            if (node.Kind == ShaderGraphNodeKind::Parameter)
                RegisterProperty(node);
    }

    void ShaderGraphCompiler::RegisterProperty(const ShaderGraphNode& node)
    {
        ShaderPropertyDefinition property;
        property.Id = node.Id;
        property.Name = node.Symbol;
        property.DisplayName = node.Name.empty() ? node.Symbol : node.Name;
        property.Category = node.ParameterMetadata.Category.empty() ? "Shader Graph" : node.ParameterMetadata.Category;
        property.Minimum = node.ParameterMetadata.Minimum;
        property.Maximum = node.ParameterMetadata.Maximum;
        property.Step = node.ParameterMetadata.Step;
        property.Type = static_cast<ShaderPropertyType>(node.ValueType);
        if (node.ValueType == ShaderGraphValueType::Texture2D)
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
        if (std::ranges::find(m_Properties, property.Name, &ShaderPropertyDefinition::Name) == m_Properties.end())
            m_Properties.push_back(std::move(property));
    }

    [[nodiscard]] ShaderGraphExpression ShaderGraphCompiler::Input(const ShaderGraphNode& node,
                                                                   const ShaderGraphPin& pin)
    {
        const auto found = m_Incoming.find({node.Id, pin.Id});
        if (found == m_Incoming.end())
            return MakeShaderGraphLiteral(pin.DefaultValue, pin.Type);
        return CoerceShaderGraphExpression(EvaluatePrepared(found->second), pin.Type);
    }

    [[nodiscard]] ShaderGraphExpression ShaderGraphCompiler::EvaluatePrepared(const ShaderGraphEndpoint endpoint)
    {
        if (const auto found = m_Cache.find(endpoint); found != m_Cache.end())
            return found->second;
        if (!m_Preparing.insert(endpoint.Node).second)
            throw std::invalid_argument("Shader Graph contains an expression cycle.");
        try
        {
            const auto& node = RequireShaderGraphNode(m_Definition, endpoint.Node);
            const auto prepare = [&](const ShaderGraphPin& pin)
            {
                const auto incoming = m_Incoming.find({node.Id, pin.Id});
                if (incoming != m_Incoming.end())
                    (void)EvaluatePrepared(incoming->second);
            };
            if (node.Kind == ShaderGraphNodeKind::StaticSwitch)
            {
                const auto* condition = FindShaderGraphPin(node, "Condition", ShaderGraphPinDirection::Input);
                const auto* trueValue = FindShaderGraphPin(node, "True", ShaderGraphPinDirection::Input);
                const auto* falseValue = FindShaderGraphPin(node, "False", ShaderGraphPinDirection::Input);
                if (!condition || !trueValue || !falseValue)
                    throw std::invalid_argument("Static Switch is missing a canonical input pin.");
                prepare(*condition);
                const auto expression =
                    CoerceShaderGraphExpression(Input(node, *condition), ShaderGraphValueType::Scalar);
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
                    if (pin.Direction == ShaderGraphPinDirection::Input)
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

    [[nodiscard]] ShaderGraphExpression ShaderGraphCompiler::Evaluate(const ShaderGraphEndpoint endpoint)
    {
        if (const auto found = m_Cache.find(endpoint); found != m_Cache.end())
            return found->second;
        if (!m_Visiting.insert(endpoint.Node).second)
            throw std::invalid_argument("Shader Graph contains an expression cycle.");
        const auto& node = RequireShaderGraphNode(m_Definition, endpoint.Node);
        const auto* descriptor = FindShaderGraphNodeDescriptor(node.TypeId.empty() ? ShaderGraphNodeTypeId(node.Kind)
                                                                                   : std::string_view(node.TypeId));
        if (!descriptor || !SupportsShaderGraphStage(descriptor->Stages, m_CurrentStage))
            throw std::invalid_argument("Shader Graph node '" + node.Name +
                                        "' is unavailable in the requested shader stage.");
        const auto& outputPin = RequireShaderGraphPin(node, endpoint.Pin);
        if (outputPin.Direction != ShaderGraphPinDirection::Output)
            throw std::invalid_argument("Shader Graph expression endpoint is not an output pin.");
        ShaderGraphExpression result;
        const auto namedInput = [&](const std::string_view name)
        {
            const auto* pin = FindShaderGraphPin(node, name, ShaderGraphPinDirection::Input);
            if (!pin)
                throw std::invalid_argument("Shader Graph node is missing a canonical input pin.");
            return Input(node, *pin);
        };
        switch (node.Kind)
        {
        case ShaderGraphNodeKind::Parameter:
        {
            const bool vertex = m_CurrentStage == ShaderGraphShaderStage::Vertex;
            m_UsesVertexMaterialParameters |= vertex;
            result = {(vertex ? ShaderGraphVertexPropertySymbol(node.Symbol) : ShaderGraphPropertySymbol(node.Symbol)) +
                          ShaderGraphSwizzle(node.ValueType),
                      node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Constant:
            result = MakeShaderGraphLiteral(node.Value, node.ValueType);
            break;
        case ShaderGraphNodeKind::UV:
            result = {"input.UV0", ShaderGraphValueType::Vector2};
            break;
        case ShaderGraphNodeKind::UVTransform:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto tiling = CoerceShaderGraphExpression(namedInput("Tiling"), ShaderGraphValueType::Vector2);
            const auto offset = CoerceShaderGraphExpression(namedInput("Offset"), ShaderGraphValueType::Vector2);
            result = {"((" + uv.Code + ") * (" + tiling.Code + ") + (" + offset.Code + "))",
                      ShaderGraphValueType::Vector2};
            break;
        }
        case ShaderGraphNodeKind::TextureSample:
        {
            const auto texture = namedInput("Texture");
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            if (texture.Type != ShaderGraphValueType::Texture2D || !IsValidShaderGraphIdentifier(texture.Code))
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
        case ShaderGraphNodeKind::NormalMap:
        {
            const auto sample = CoerceShaderGraphExpression(namedInput("Sample"), ShaderGraphValueType::Color);
            const auto scale = CoerceShaderGraphExpression(namedInput("Scale"), ShaderGraphValueType::Scalar);
            result = {"DecodeNormal(" + sample.Code + ", " + scale.Code +
                          ", input.Tangent, input.Bitangent, input.Normal)",
                      ShaderGraphValueType::Vector3};
            break;
        }
        case ShaderGraphNodeKind::DetailNormal:
        {
            const auto base = CoerceShaderGraphExpression(namedInput("Base"), ShaderGraphValueType::Vector3);
            const auto detail = CoerceShaderGraphExpression(namedInput("Detail"), ShaderGraphValueType::Vector3);
            const auto strength = CoerceShaderGraphExpression(namedInput("Strength"), ShaderGraphValueType::Scalar);
            result = {"BlendDetailNormal(" + base.Code + ", " + detail.Code + ", " + strength.Code + ")",
                      ShaderGraphValueType::Vector3};
            break;
        }
        case ShaderGraphNodeKind::Parallax:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto height = CoerceShaderGraphExpression(namedInput("Height"), ShaderGraphValueType::Scalar);
            const auto scale = CoerceShaderGraphExpression(namedInput("Scale"), ShaderGraphValueType::Scalar);
            result = {"ParallaxUV(" + uv.Code + ", " + height.Code + ", " + scale.Code +
                          ", input.ViewDirection, input.Tangent, input.Bitangent, input.Normal)",
                      ShaderGraphValueType::Vector2};
            break;
        }
        case ShaderGraphNodeKind::Add:
        case ShaderGraphNodeKind::Subtract:
        case ShaderGraphNodeKind::Multiply:
        {
            const auto left = CoerceShaderGraphExpression(namedInput("A"), node.ValueType);
            const auto right = CoerceShaderGraphExpression(namedInput("B"), node.ValueType);
            const auto operation = node.Kind == ShaderGraphNodeKind::Add        ? "+"
                                   : node.Kind == ShaderGraphNodeKind::Subtract ? "-"
                                                                                : "*";
            result = {"((" + left.Code + ") " + operation + " (" + right.Code + "))", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Divide:
        {
            const auto left = CoerceShaderGraphExpression(namedInput("A"), node.ValueType);
            const auto right = CoerceShaderGraphExpression(namedInput("B"), node.ValueType);
            result = {"SafeDivide(" + left.Code + ", " + right.Code + ")", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Power:
        {
            const auto base = CoerceShaderGraphExpression(namedInput("Base"), node.ValueType);
            const auto exponent = CoerceShaderGraphExpression(namedInput("Exponent"), node.ValueType);
            result = {"pow(max(abs(" + base.Code + "), 1.0e-6F), " + exponent.Code + ")", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Minimum:
        case ShaderGraphNodeKind::Maximum:
        {
            const auto left = CoerceShaderGraphExpression(namedInput("A"), node.ValueType);
            const auto right = CoerceShaderGraphExpression(namedInput("B"), node.ValueType);
            result = {(node.Kind == ShaderGraphNodeKind::Minimum ? "min(" : "max(") + left.Code + ", " + right.Code +
                          ")",
                      node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Lerp:
        {
            const auto left = CoerceShaderGraphExpression(namedInput("A"), node.ValueType);
            const auto right = CoerceShaderGraphExpression(namedInput("B"), node.ValueType);
            const auto factor = CoerceShaderGraphExpression(namedInput("T"), ShaderGraphValueType::Scalar);
            result = {"lerp(" + left.Code + ", " + right.Code + ", " + factor.Code + ")", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::OneMinus:
        {
            const auto value = CoerceShaderGraphExpression(namedInput("Value"), node.ValueType);
            result = {"(1.0F - (" + value.Code + "))", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Clamp:
        {
            const auto value = CoerceShaderGraphExpression(namedInput("Value"), node.ValueType);
            result = {"saturate(" + value.Code + ")", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Absolute:
        case ShaderGraphNodeKind::Floor:
        case ShaderGraphNodeKind::Ceiling:
        case ShaderGraphNodeKind::Fraction:
        case ShaderGraphNodeKind::Sine:
        case ShaderGraphNodeKind::Cosine:
        case ShaderGraphNodeKind::Normalize:
        {
            const auto value = CoerceShaderGraphExpression(namedInput("Value"), node.ValueType);
            const auto function = node.Kind == ShaderGraphNodeKind::Absolute   ? "abs"
                                  : node.Kind == ShaderGraphNodeKind::Floor    ? "floor"
                                  : node.Kind == ShaderGraphNodeKind::Ceiling  ? "ceil"
                                  : node.Kind == ShaderGraphNodeKind::Fraction ? "frac"
                                  : node.Kind == ShaderGraphNodeKind::Sine     ? "sin"
                                  : node.Kind == ShaderGraphNodeKind::Cosine   ? "cos"
                                                                               : "normalize";
            result = {std::string(function) + "(" + value.Code + ")", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Length:
        {
            const auto value = namedInput("Value");
            result = {"length(" + value.Code + ")", ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::Dot:
        {
            const auto left = namedInput("A");
            const auto right = CoerceShaderGraphExpression(namedInput("B"), left.Type);
            result = {"dot(" + left.Code + ", " + right.Code + ")", ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::Remap:
        {
            const auto value = CoerceShaderGraphExpression(namedInput("Value"), node.ValueType);
            const auto inputMinimum = CoerceShaderGraphExpression(namedInput("In Min"), node.ValueType);
            const auto inputMaximum = CoerceShaderGraphExpression(namedInput("In Max"), node.ValueType);
            const auto outputMinimum = CoerceShaderGraphExpression(namedInput("Out Min"), node.ValueType);
            const auto outputMaximum = CoerceShaderGraphExpression(namedInput("Out Max"), node.ValueType);
            const auto factor = "SafeDivide((" + value.Code + ") - (" + inputMinimum.Code + "), (" + inputMaximum.Code +
                                ") - (" + inputMinimum.Code + "))";
            result = {"lerp(" + outputMinimum.Code + ", " + outputMaximum.Code + ", " + factor + ")", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::SmoothStep:
        {
            const auto edgeMinimum = CoerceShaderGraphExpression(namedInput("Edge Min"), node.ValueType);
            const auto edgeMaximum = CoerceShaderGraphExpression(namedInput("Edge Max"), node.ValueType);
            const auto value = CoerceShaderGraphExpression(namedInput("Value"), node.ValueType);
            result = {"smoothstep(" + edgeMinimum.Code + ", " + edgeMaximum.Code + ", " + value.Code + ")",
                      node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Step:
        {
            const auto edge = CoerceShaderGraphExpression(namedInput("Edge"), node.ValueType);
            const auto value = CoerceShaderGraphExpression(namedInput("Value"), node.ValueType);
            result = {"step(" + edge.Code + ", " + value.Code + ")", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Fresnel:
        {
            const auto normal = CoerceShaderGraphExpression(namedInput("Normal"), ShaderGraphValueType::Vector3);
            const auto power = CoerceShaderGraphExpression(namedInput("Power"), ShaderGraphValueType::Scalar);
            const auto reflectance = CoerceShaderGraphExpression(namedInput("F0"), ShaderGraphValueType::Scalar);
            result = {"saturate((" + reflectance.Code + ") + (1.0F - (" + reflectance.Code +
                          ")) * pow(1.0F - saturate(dot(normalize(" + normal.Code +
                          "), normalize(input.ViewDirection))), max(abs(" + power.Code + "), 1.0e-4F)))",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::VertexColor:
            result = {"input.Color", ShaderGraphValueType::Color};
            break;
        case ShaderGraphNodeKind::WorldPosition:
            result = {m_CurrentStage == ShaderGraphShaderStage::Vertex ? "world.xyz" : "input.WorldPosition",
                      ShaderGraphValueType::Vector3};
            break;
        case ShaderGraphNodeKind::WorldNormal:
            result = {m_CurrentStage == ShaderGraphShaderStage::Vertex
                          ? "SafeNormalize(mul((float3x3)instance.NormalMatrix, input.Normal), "
                            "float3(0.0F, 0.0F, 1.0F))"
                          : "input.Normal",
                      ShaderGraphValueType::Vector3};
            break;
        case ShaderGraphNodeKind::ViewDirection:
            result = {"input.ViewDirection", ShaderGraphValueType::Vector3};
            break;
        case ShaderGraphNodeKind::RotateUV:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto center = CoerceShaderGraphExpression(namedInput("Center"), ShaderGraphValueType::Vector2);
            const auto rotation = CoerceShaderGraphExpression(namedInput("Rotation"), ShaderGraphValueType::Scalar);
            result = {"RotateMaterialUV(" + uv.Code + ", " + center.Code + ", " + rotation.Code + ")",
                      ShaderGraphValueType::Vector2};
            break;
        }
        case ShaderGraphNodeKind::SimpleNoise:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto scale = CoerceShaderGraphExpression(namedInput("Scale"), ShaderGraphValueType::Scalar);
            const auto detail = CoerceShaderGraphExpression(namedInput("Detail"), ShaderGraphValueType::Scalar);
            result = {"MaterialNoise(" + uv.Code + ", " + scale.Code + ", " + detail.Code + ")",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::Desaturate:
        {
            const auto color = CoerceShaderGraphExpression(namedInput("Color"), ShaderGraphValueType::Color);
            const auto amount = CoerceShaderGraphExpression(namedInput("Amount"), ShaderGraphValueType::Scalar);
            result = {"DesaturateMaterialColor(" + color.Code + ", " + amount.Code + ")", ShaderGraphValueType::Color};
            break;
        }
        case ShaderGraphNodeKind::Posterize:
        {
            const auto value = CoerceShaderGraphExpression(namedInput("Value"), node.ValueType);
            const auto steps = CoerceShaderGraphExpression(namedInput("Steps"), ShaderGraphValueType::Scalar);
            result = {"(floor((" + value.Code + ") * max(abs(" + steps.Code + "), 1.0F)) / max(abs(" + steps.Code +
                          "), 1.0F))",
                      node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Round:
        case ShaderGraphNodeKind::Truncate:
        case ShaderGraphNodeKind::Sign:
        case ShaderGraphNodeKind::SquareRoot:
        case ShaderGraphNodeKind::ReciprocalSquareRoot:
        case ShaderGraphNodeKind::Exponential2:
        case ShaderGraphNodeKind::Logarithm2:
        case ShaderGraphNodeKind::Tangent:
        case ShaderGraphNodeKind::ArcSine:
        case ShaderGraphNodeKind::ArcCosine:
        case ShaderGraphNodeKind::DerivativeX:
        case ShaderGraphNodeKind::DerivativeY:
        case ShaderGraphNodeKind::FilterWidth:
        {
            const auto value = CoerceShaderGraphExpression(namedInput("Value"), node.ValueType);
            const auto code =
                node.Kind == ShaderGraphNodeKind::Round                  ? "round(" + value.Code + ")"
                : node.Kind == ShaderGraphNodeKind::Truncate             ? "trunc(" + value.Code + ")"
                : node.Kind == ShaderGraphNodeKind::Sign                 ? "sign(" + value.Code + ")"
                : node.Kind == ShaderGraphNodeKind::SquareRoot           ? "sqrt(max(" + value.Code + ", 0.0F))"
                : node.Kind == ShaderGraphNodeKind::ReciprocalSquareRoot ? "rsqrt(max(" + value.Code + ", 1.0e-8F))"
                : node.Kind == ShaderGraphNodeKind::Exponential2         ? "exp2(" + value.Code + ")"
                : node.Kind == ShaderGraphNodeKind::Logarithm2           ? "log2(max(abs(" + value.Code + "), 1.0e-8F))"
                : node.Kind == ShaderGraphNodeKind::Tangent              ? "tan(" + value.Code + ")"
                : node.Kind == ShaderGraphNodeKind::ArcSine     ? "asin(clamp(" + value.Code + ", -1.0F, 1.0F))"
                : node.Kind == ShaderGraphNodeKind::ArcCosine   ? "acos(clamp(" + value.Code + ", -1.0F, 1.0F))"
                : node.Kind == ShaderGraphNodeKind::DerivativeX ? "ddx(" + value.Code + ")"
                : node.Kind == ShaderGraphNodeKind::DerivativeY ? "ddy(" + value.Code + ")"
                                                                : "fwidth(" + value.Code + ")";
            result = {code, node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Modulo:
        case ShaderGraphNodeKind::ArcTangent2:
        {
            const auto left = CoerceShaderGraphExpression(namedInput("A"), node.ValueType);
            const auto right = CoerceShaderGraphExpression(namedInput("B"), node.ValueType);
            result = {(node.Kind == ShaderGraphNodeKind::Modulo ? "fmod(" : "atan2(") + left.Code + ", " + right.Code +
                          ")",
                      node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Cross:
        case ShaderGraphNodeKind::Distance:
        case ShaderGraphNodeKind::Reflect:
        {
            const auto left = CoerceShaderGraphExpression(namedInput("A"), ShaderGraphValueType::Vector3);
            const auto right = CoerceShaderGraphExpression(namedInput("B"), ShaderGraphValueType::Vector3);
            const auto function = node.Kind == ShaderGraphNodeKind::Cross      ? "cross"
                                  : node.Kind == ShaderGraphNodeKind::Distance ? "distance"
                                                                               : "reflect";
            result = {std::string(function) + "(" + left.Code + ", " + right.Code + ")", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Refract:
        {
            const auto incident = CoerceShaderGraphExpression(namedInput("Incident"), ShaderGraphValueType::Vector3);
            const auto normal = CoerceShaderGraphExpression(namedInput("Normal"), ShaderGraphValueType::Vector3);
            const auto ior = CoerceShaderGraphExpression(namedInput("IOR"), ShaderGraphValueType::Scalar);
            result = {"refract(normalize(" + incident.Code + "), normalize(" + normal.Code + "), rcp(max(abs(" +
                          ior.Code + "), 1.0e-4F)))",
                      ShaderGraphValueType::Vector3};
            break;
        }
        case ShaderGraphNodeKind::AppendVector:
        {
            const auto xyz = CoerceShaderGraphExpression(namedInput("XYZ"), ShaderGraphValueType::Vector3);
            const auto w = CoerceShaderGraphExpression(namedInput("W"), ShaderGraphValueType::Scalar);
            result = {"float4(" + xyz.Code + ", " + w.Code + ")", ShaderGraphValueType::Vector4};
            break;
        }
        case ShaderGraphNodeKind::ComponentMask:
        {
            const auto value = CoerceShaderGraphExpression(namedInput("Value"), ShaderGraphValueType::Vector4);
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
        case ShaderGraphNodeKind::UV1:
            result = {"input.UV1", ShaderGraphValueType::Vector2};
            break;
        case ShaderGraphNodeKind::WorldTangent:
            result = {m_CurrentStage == ShaderGraphShaderStage::Vertex
                          ? "SafeNormalize(mul((float3x3)instance.Model, input.Tangent.xyz), "
                            "float3(1.0F, 0.0F, 0.0F))"
                          : "input.Tangent",
                      ShaderGraphValueType::Vector3};
            break;
        case ShaderGraphNodeKind::CameraPosition:
            result = {"mul(inverse(View), float4(0.0F, 0.0F, 0.0F, 1.0F)).xyz", ShaderGraphValueType::Vector3};
            break;
        case ShaderGraphNodeKind::ObjectPosition:
            result = {m_CurrentStage == ShaderGraphShaderStage::Vertex
                          ? "mul(instance.Model, float4(0.0F, 0.0F, 0.0F, 1.0F)).xyz"
                          : "input.ObjectPosition",
                      ShaderGraphValueType::Vector3};
            break;
        case ShaderGraphNodeKind::Time:
            result = {"FrameParameters.x", ShaderGraphValueType::Scalar};
            break;
        case ShaderGraphNodeKind::DeltaTime:
            result = {"FrameParameters.y", ShaderGraphValueType::Scalar};
            break;
        case ShaderGraphNodeKind::ScreenPosition:
            result = {"input.Position.xy", ShaderGraphValueType::Vector2};
            break;
        case ShaderGraphNodeKind::DepthFade:
        {
            const auto distance = CoerceShaderGraphExpression(namedInput("Distance"), ShaderGraphValueType::Scalar);
            const auto fadeDistance =
                CoerceShaderGraphExpression(namedInput("Fade Distance"), ShaderGraphValueType::Scalar);
            result = {"saturate((" + distance.Code + ") / max(abs(" + fadeDistance.Code + "), 1.0e-4F))",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::Luminance:
        {
            const auto color = CoerceShaderGraphExpression(namedInput("Color"), ShaderGraphValueType::Color);
            result = {"dot((" + color.Code + ").rgb, float3(0.2126F, 0.7152F, 0.0722F))", ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::HueShift:
        {
            const auto color = CoerceShaderGraphExpression(namedInput("Color"), ShaderGraphValueType::Color);
            const auto shift = CoerceShaderGraphExpression(namedInput("Shift"), ShaderGraphValueType::Scalar);
            result = {"HueShiftMaterialColor(" + color.Code + ", " + shift.Code + ")", ShaderGraphValueType::Color};
            break;
        }
        case ShaderGraphNodeKind::Checkerboard:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto colorA = CoerceShaderGraphExpression(namedInput("Color A"), ShaderGraphValueType::Color);
            const auto colorB = CoerceShaderGraphExpression(namedInput("Color B"), ShaderGraphValueType::Color);
            const auto scale = CoerceShaderGraphExpression(namedInput("Scale"), ShaderGraphValueType::Vector2);
            result = {"MaterialCheckerboard(" + uv.Code + ", " + colorA.Code + ", " + colorB.Code + ", " + scale.Code +
                          ")",
                      ShaderGraphValueType::Color};
            break;
        }
        case ShaderGraphNodeKind::VoronoiNoise:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto scale = CoerceShaderGraphExpression(namedInput("Scale"), ShaderGraphValueType::Scalar);
            const auto jitter = CoerceShaderGraphExpression(namedInput("Jitter"), ShaderGraphValueType::Scalar);
            const auto voronoi = "MaterialVoronoi(" + uv.Code + ", " + scale.Code + ", " + jitter.Code + ")";
            result = {voronoi + (outputPin.Name == "Cell" ? ".y" : ".x"), ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::Panner:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto speed = CoerceShaderGraphExpression(namedInput("Speed"), ShaderGraphValueType::Vector2);
            const auto time = CoerceShaderGraphExpression(namedInput("Time"), ShaderGraphValueType::Scalar);
            result = {"((" + uv.Code + ") + (" + speed.Code + ") * (" + time.Code + "))",
                      ShaderGraphValueType::Vector2};
            break;
        }
        case ShaderGraphNodeKind::PolarCoordinates:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto center = CoerceShaderGraphExpression(namedInput("Center"), ShaderGraphValueType::Vector2);
            const auto radialScale =
                CoerceShaderGraphExpression(namedInput("Radial Scale"), ShaderGraphValueType::Scalar);
            const auto lengthScale =
                CoerceShaderGraphExpression(namedInput("Length Scale"), ShaderGraphValueType::Scalar);
            const auto local = "((" + uv.Code + ") - (" + center.Code + "))";
            result = {"float2(length(" + local + ") * (" + radialScale.Code + "), frac(atan2(" + local + ".y, " +
                          local + ".x) / (2.0F * Pi) + 0.5F) * (" + lengthScale.Code + "))",
                      ShaderGraphValueType::Vector2};
            break;
        }
        case ShaderGraphNodeKind::SphereMask:
        {
            const auto first = CoerceShaderGraphExpression(namedInput("A"), ShaderGraphValueType::Vector3);
            const auto second = CoerceShaderGraphExpression(namedInput("B"), ShaderGraphValueType::Vector3);
            const auto radius = CoerceShaderGraphExpression(namedInput("Radius"), ShaderGraphValueType::Scalar);
            const auto hardness = CoerceShaderGraphExpression(namedInput("Hardness"), ShaderGraphValueType::Scalar);
            result = {"saturate((1.0F - distance(" + first.Code + ", " + second.Code + ") / max(abs(" + radius.Code +
                          "), 1.0e-5F)) * max(abs(" + hardness.Code + "), 1.0F))",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::RadialGradient:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto center = CoerceShaderGraphExpression(namedInput("Center"), ShaderGraphValueType::Vector2);
            const auto radius = CoerceShaderGraphExpression(namedInput("Radius"), ShaderGraphValueType::Scalar);
            const auto density = CoerceShaderGraphExpression(namedInput("Density"), ShaderGraphValueType::Scalar);
            result = {"saturate(((" + radius.Code + ") - distance(" + uv.Code + ", " + center.Code + ")) * max(abs(" +
                          density.Code + "), 1.0e-4F))",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::LinearGradient:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto direction = CoerceShaderGraphExpression(namedInput("Direction"), ShaderGraphValueType::Vector2);
            const auto offset = CoerceShaderGraphExpression(namedInput("Offset"), ShaderGraphValueType::Scalar);
            result = {"saturate(dot(" + uv.Code + ", (" + direction.Code + ") / max(length(" + direction.Code +
                          "), 1.0e-5F)) + (" + offset.Code + "))",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::Contrast:
        {
            const auto color = CoerceShaderGraphExpression(namedInput("Color"), ShaderGraphValueType::Color);
            const auto contrast = CoerceShaderGraphExpression(namedInput("Contrast"), ShaderGraphValueType::Scalar);
            const auto pivot = CoerceShaderGraphExpression(namedInput("Pivot"), ShaderGraphValueType::Scalar);
            result = {"float4((" + color.Code + ").rgb * (" + contrast.Code + ") + (" + pivot.Code + ") * (1.0F - (" +
                          contrast.Code + ")), (" + color.Code + ").a)",
                      ShaderGraphValueType::Color};
            break;
        }
        case ShaderGraphNodeKind::Saturation:
        {
            const auto color = CoerceShaderGraphExpression(namedInput("Color"), ShaderGraphValueType::Color);
            const auto saturation = CoerceShaderGraphExpression(namedInput("Saturation"), ShaderGraphValueType::Scalar);
            result = {"DesaturateMaterialColor(" + color.Code + ", 1.0F - (" + saturation.Code + "))",
                      ShaderGraphValueType::Color};
            break;
        }
        case ShaderGraphNodeKind::BlendOverlay:
        {
            const auto base = CoerceShaderGraphExpression(namedInput("Base"), ShaderGraphValueType::Color);
            const auto blend = CoerceShaderGraphExpression(namedInput("Blend"), ShaderGraphValueType::Color);
            const auto opacity = CoerceShaderGraphExpression(namedInput("Opacity"), ShaderGraphValueType::Scalar);
            result = {"MaterialOverlayBlend(" + base.Code + ", " + blend.Code + ", " + opacity.Code + ")",
                      ShaderGraphValueType::Color};
            break;
        }
        case ShaderGraphNodeKind::Blackbody:
        {
            const auto temperature =
                CoerceShaderGraphExpression(namedInput("Temperature"), ShaderGraphValueType::Scalar);
            result = {"MaterialBlackbody(" + temperature.Code + ")", ShaderGraphValueType::Color};
            break;
        }
        case ShaderGraphNodeKind::ReflectionVector:
        {
            const auto normal = CoerceShaderGraphExpression(namedInput("Normal"), ShaderGraphValueType::Vector3);
            result = {"reflect(-SafeNormalize(input.ViewDirection, input.Normal), SafeNormalize(" + normal.Code +
                          ", input.Normal))",
                      ShaderGraphValueType::Vector3};
            break;
        }
        case ShaderGraphNodeKind::FacingRatio:
        {
            const auto normal = CoerceShaderGraphExpression(namedInput("Normal"), ShaderGraphValueType::Vector3);
            const auto power = CoerceShaderGraphExpression(namedInput("Power"), ShaderGraphValueType::Scalar);
            result = {"pow(saturate(1.0F - dot(SafeNormalize(" + normal.Code +
                          ", input.Normal), SafeNormalize(input.ViewDirection, input.Normal))), max(abs(" + power.Code +
                          "), 1.0e-4F))",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::Dither:
        {
            const auto alpha = CoerceShaderGraphExpression(namedInput("Alpha"), ShaderGraphValueType::Scalar);
            const auto screenPosition =
                CoerceShaderGraphExpression(namedInput("Screen Position"), ShaderGraphValueType::Vector2);
            result = {"step(MaterialDitherThreshold(" + screenPosition.Code + "), saturate(" + alpha.Code + "))",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::GradientNoise:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto scale = CoerceShaderGraphExpression(namedInput("Scale"), ShaderGraphValueType::Scalar);
            result = {"MaterialNoise(" + uv.Code + ", " + scale.Code + ", 0.65F)", ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::Wave:
        {
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto direction = CoerceShaderGraphExpression(namedInput("Direction"), ShaderGraphValueType::Vector2);
            const auto frequency = CoerceShaderGraphExpression(namedInput("Frequency"), ShaderGraphValueType::Scalar);
            const auto phase = CoerceShaderGraphExpression(namedInput("Phase"), ShaderGraphValueType::Scalar);
            result = {"(sin(dot(" + uv.Code + ", (" + direction.Code + ") / max(length(" + direction.Code +
                          "), 1.0e-5F)) * (" + frequency.Code + ") * (2.0F * Pi) + (" + phase.Code +
                          ")) * 0.5F + 0.5F)",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::TriplanarSample:
        {
            const auto texture = namedInput("Texture");
            const auto position = CoerceShaderGraphExpression(namedInput("Position"), ShaderGraphValueType::Vector3);
            const auto normal = CoerceShaderGraphExpression(namedInput("Normal"), ShaderGraphValueType::Vector3);
            const auto scale = CoerceShaderGraphExpression(namedInput("Scale"), ShaderGraphValueType::Scalar);
            const auto sharpness =
                CoerceShaderGraphExpression(namedInput("Blend Sharpness"), ShaderGraphValueType::Scalar);
            if (texture.Type != ShaderGraphValueType::Texture2D || !IsValidShaderGraphIdentifier(texture.Code))
                throw std::invalid_argument("Triplanar Sample requires a Texture2D Parameter connection.");
            const auto weights = "(pow(abs(SafeNormalize(" + normal.Code + ", input.Normal)), max(abs(" +
                                 sharpness.Code + "), 1.0F)) / max(dot(pow(abs(SafeNormalize(" + normal.Code +
                                 ", input.Normal)), max(abs(" + sharpness.Code + "), 1.0F)), 1.0F.xxx), 1.0e-5F))";
            const auto scaled = "((" + position.Code + ") * (" + scale.Code + "))";
            const auto sample = "(" + texture.Code + ".Sample(" + texture.Code + "Sampler, " + scaled + ".zy) * " +
                                weights + ".x + " + texture.Code + ".Sample(" + texture.Code + "Sampler, " + scaled +
                                ".xz) * " + weights + ".y + " + texture.Code + ".Sample(" + texture.Code + "Sampler, " +
                                scaled + ".xy) * " + weights + ".z)";
            const auto swizzle = outputPin.Name == "RGB" ? ".rgb"
                                 : outputPin.Name == "R" ? ".r"
                                 : outputPin.Name == "G" ? ".g"
                                 : outputPin.Name == "B" ? ".b"
                                 : outputPin.Name == "A" ? ".a"
                                                         : std::string{};
            result = {sample + swizzle, outputPin.Type};
            break;
        }
        case ShaderGraphNodeKind::TextureSampleLevel:
        {
            const auto texture = namedInput("Texture");
            const auto uv = CoerceShaderGraphExpression(namedInput("UV"), ShaderGraphValueType::Vector2);
            const auto level = CoerceShaderGraphExpression(namedInput("Mip Level"), ShaderGraphValueType::Scalar);
            if (texture.Type != ShaderGraphValueType::Texture2D || !IsValidShaderGraphIdentifier(texture.Code))
                throw std::invalid_argument("Texture Sample Level requires a Texture2D Parameter connection.");
            const auto sample = texture.Code + ".SampleLevel(" + texture.Code + "Sampler, " + uv.Code + ", max(" +
                                level.Code + ", 0.0F))";
            const auto swizzle = outputPin.Name == "RGB" ? ".rgb"
                                 : outputPin.Name == "R" ? ".r"
                                 : outputPin.Name == "G" ? ".g"
                                 : outputPin.Name == "B" ? ".b"
                                 : outputPin.Name == "A" ? ".a"
                                                         : std::string{};
            result = {"(" + sample + ")" + swizzle, outputPin.Type};
            break;
        }
        case ShaderGraphNodeKind::HeightToNormal:
        {
            const auto height = CoerceShaderGraphExpression(namedInput("Height"), ShaderGraphValueType::Scalar);
            const auto strength = CoerceShaderGraphExpression(namedInput("Strength"), ShaderGraphValueType::Scalar);
            result = {"SafeNormalize(float3(-ddx(" + height.Code + ") * (" + strength.Code + "), -ddy(" + height.Code +
                          ") * (" + strength.Code + "), 1.0F), input.Normal)",
                      ShaderGraphValueType::Vector3};
            break;
        }
        case ShaderGraphNodeKind::FlattenNormal:
        {
            const auto normal = CoerceShaderGraphExpression(namedInput("Normal"), ShaderGraphValueType::Vector3);
            const auto strength = CoerceShaderGraphExpression(namedInput("Strength"), ShaderGraphValueType::Scalar);
            result = {"SafeNormalize(lerp(float3(0.0F, 0.0F, 1.0F), " + normal.Code + ", saturate(" + strength.Code +
                          ")), input.Normal)",
                      ShaderGraphValueType::Vector3};
            break;
        }
        case ShaderGraphNodeKind::MakeMaterialAttributes:
        {
            constexpr std::array inputs{std::string_view("BaseColor"),       std::string_view("Metallic"),
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
            result = {"MakeShaderGraphSurface(" + arguments + ")", ShaderGraphValueType::MaterialAttributes};
            break;
        }
        case ShaderGraphNodeKind::BreakMaterialAttributes:
        {
            const auto attributes =
                CoerceShaderGraphExpression(namedInput("Attributes"), ShaderGraphValueType::MaterialAttributes);
            result = {"(" + attributes.Code + ")." + outputPin.Name, outputPin.Type};
            break;
        }
        case ShaderGraphNodeKind::BlendMaterialAttributes:
        {
            const auto first = CoerceShaderGraphExpression(namedInput("A"), ShaderGraphValueType::MaterialAttributes);
            const auto second = CoerceShaderGraphExpression(namedInput("B"), ShaderGraphValueType::MaterialAttributes);
            const auto alpha = CoerceShaderGraphExpression(namedInput("Alpha"), ShaderGraphValueType::Scalar);
            result = {"BlendShaderGraphSurfaces(" + first.Code + ", " + second.Code + ", " + alpha.Code + ")",
                      ShaderGraphValueType::MaterialAttributes};
            break;
        }
        case ShaderGraphNodeKind::StandardSurfaceBsdf:
        {
            const auto baseColor = CoerceShaderGraphExpression(namedInput("BaseColor"), ShaderGraphValueType::Color);
            const auto metallic = CoerceShaderGraphExpression(namedInput("Metallic"), ShaderGraphValueType::Scalar);
            const auto roughness = CoerceShaderGraphExpression(namedInput("Roughness"), ShaderGraphValueType::Scalar);
            const auto specular = CoerceShaderGraphExpression(namedInput("Specular"), ShaderGraphValueType::Scalar);
            const auto normal = CoerceShaderGraphExpression(namedInput("Normal"), ShaderGraphValueType::Vector3);
            const auto emission = CoerceShaderGraphExpression(namedInput("Emission"), ShaderGraphValueType::Color);
            const auto opacity = CoerceShaderGraphExpression(namedInput("Opacity"), ShaderGraphValueType::Scalar);
            result = {"MakeStandardShaderGraphBsdf(" + baseColor.Code + ", " + metallic.Code + ", " + roughness.Code +
                          ", " + specular.Code + ", " + normal.Code + ", " + emission.Code + ", " + opacity.Code + ")",
                      ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::OpenPbrSurfaceBsdf:
        {
            constexpr std::array inputs{"BaseColor",         "Metallic",
                                        "Roughness",         "SpecularWeight",
                                        "CoatWeight",        "CoatRoughness",
                                        "FuzzColor",         "FuzzWeight",
                                        "FuzzRoughness",     "Normal",
                                        "Emission",          "Occlusion",
                                        "Opacity",           "SubsurfaceColor",
                                        "SubsurfaceWeight",  "Anisotropy",
                                        "Tangent",           "TransmissionWeight",
                                        "IndexOfRefraction", "Refraction",
                                        "Thickness"};
            std::string arguments;
            for (const auto name : inputs)
            {
                if (!arguments.empty())
                    arguments += ", ";
                arguments += namedInput(name).Code;
            }
            result = {"MakeOpenPbrShaderGraphBsdf(" + arguments + ")", ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::MixSlabs:
        {
            const auto first = CoerceShaderGraphExpression(namedInput("A"), ShaderGraphValueType::Bsdf);
            const auto second = CoerceShaderGraphExpression(namedInput("B"), ShaderGraphValueType::Bsdf);
            const auto factor = CoerceShaderGraphExpression(namedInput("Factor"), ShaderGraphValueType::Scalar);
            result = {"MixShaderGraphSlabs(" + first.Code + ", " + second.Code + ", " + factor.Code + ")",
                      ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::AddSlabs:
        {
            const auto first = CoerceShaderGraphExpression(namedInput("A"), ShaderGraphValueType::Bsdf);
            const auto second = CoerceShaderGraphExpression(namedInput("B"), ShaderGraphValueType::Bsdf);
            const auto firstWeight = CoerceShaderGraphExpression(namedInput("WeightA"), ShaderGraphValueType::Scalar);
            const auto secondWeight = CoerceShaderGraphExpression(namedInput("WeightB"), ShaderGraphValueType::Scalar);
            result = {"AddShaderGraphSlabs(" + first.Code + ", " + second.Code + ", " + firstWeight.Code + ", " +
                          secondWeight.Code + ")",
                      ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::CoatSlab:
        {
            const auto base = CoerceShaderGraphExpression(namedInput("Base"), ShaderGraphValueType::Bsdf);
            const auto weight = CoerceShaderGraphExpression(namedInput("Weight"), ShaderGraphValueType::Scalar);
            const auto roughness = CoerceShaderGraphExpression(namedInput("Roughness"), ShaderGraphValueType::Scalar);
            result = {"ApplyShaderGraphClearCoat(" + base.Code + ", " + weight.Code + ", " + roughness.Code + ")",
                      ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::FuzzSlab:
        {
            const auto base = CoerceShaderGraphExpression(namedInput("Base"), ShaderGraphValueType::Bsdf);
            const auto color = CoerceShaderGraphExpression(namedInput("Color"), ShaderGraphValueType::Color);
            const auto weight = CoerceShaderGraphExpression(namedInput("Weight"), ShaderGraphValueType::Scalar);
            const auto roughness = CoerceShaderGraphExpression(namedInput("Roughness"), ShaderGraphValueType::Scalar);
            result = {"ApplyShaderGraphSheen(" + base.Code + ", " + color.Code + ", " + weight.Code + ", " +
                          roughness.Code + ")",
                      ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::ClearCoatBsdf:
        {
            const auto base = CoerceShaderGraphExpression(namedInput("Base"), ShaderGraphValueType::Bsdf);
            const auto weight = CoerceShaderGraphExpression(namedInput("Weight"), ShaderGraphValueType::Scalar);
            const auto roughness = CoerceShaderGraphExpression(namedInput("Roughness"), ShaderGraphValueType::Scalar);
            result = {"ApplyShaderGraphClearCoat(" + base.Code + ", " + weight.Code + ", " + roughness.Code + ")",
                      ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::SheenBsdf:
        {
            const auto base = CoerceShaderGraphExpression(namedInput("Base"), ShaderGraphValueType::Bsdf);
            const auto color = CoerceShaderGraphExpression(namedInput("Color"), ShaderGraphValueType::Color);
            const auto weight = CoerceShaderGraphExpression(namedInput("Weight"), ShaderGraphValueType::Scalar);
            const auto roughness = CoerceShaderGraphExpression(namedInput("Roughness"), ShaderGraphValueType::Scalar);
            result = {"ApplyShaderGraphSheen(" + base.Code + ", " + color.Code + ", " + weight.Code + ", " +
                          roughness.Code + ")",
                      ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::SubsurfaceBsdf:
        {
            const auto base = CoerceShaderGraphExpression(namedInput("Base"), ShaderGraphValueType::Bsdf);
            const auto color = CoerceShaderGraphExpression(namedInput("Color"), ShaderGraphValueType::Color);
            const auto weight = CoerceShaderGraphExpression(namedInput("Weight"), ShaderGraphValueType::Scalar);
            result = {"ApplyShaderGraphSubsurface(" + base.Code + ", " + color.Code + ", " + weight.Code + ")",
                      ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::TransmissionBsdf:
        {
            const auto base = CoerceShaderGraphExpression(namedInput("Base"), ShaderGraphValueType::Bsdf);
            const auto weight = CoerceShaderGraphExpression(namedInput("Weight"), ShaderGraphValueType::Scalar);
            const auto ior = CoerceShaderGraphExpression(namedInput("IndexOfRefraction"), ShaderGraphValueType::Scalar);
            const auto refraction = CoerceShaderGraphExpression(namedInput("Refraction"), ShaderGraphValueType::Scalar);
            const auto thickness = CoerceShaderGraphExpression(namedInput("Thickness"), ShaderGraphValueType::Scalar);
            result = {"ApplyShaderGraphTransmission(" + base.Code + ", " + weight.Code + ", " + ior.Code + ", " +
                          refraction.Code + ", " + thickness.Code + ")",
                      ShaderGraphValueType::Bsdf};
            break;
        }
        case ShaderGraphNodeKind::BsdfToMaterialAttributes:
        {
            const auto bsdf = CoerceShaderGraphExpression(namedInput("BSDF"), ShaderGraphValueType::Bsdf);
            result = {"ShaderGraphSurfaceFromBsdf(" + bsdf.Code + ")", ShaderGraphValueType::MaterialAttributes};
            break;
        }
        case ShaderGraphNodeKind::Keyword:
            result = {std::ranges::find(m_Keywords, node.Symbol) == m_Keywords.end() ? "0.0F" : "1.0F",
                      ShaderGraphValueType::Scalar};
            break;
        case ShaderGraphNodeKind::StaticSwitch:
        {
            const auto condition = CoerceShaderGraphExpression(namedInput("Condition"), ShaderGraphValueType::Scalar);
            result = condition.Code == "0.0F" ? CoerceShaderGraphExpression(namedInput("False"), node.ValueType)
                     : condition.Code == "1.0F"
                         ? CoerceShaderGraphExpression(namedInput("True"), node.ValueType)
                         : ShaderGraphExpression{
                               "((" + condition.Code + ") != 0.0F ? (" +
                                   CoerceShaderGraphExpression(namedInput("True"), node.ValueType).Code + ") : (" +
                                   CoerceShaderGraphExpression(namedInput("False"), node.ValueType).Code + "))",
                               node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Custom:
        {
            std::string arguments;
            for (const auto& pin : node.Pins)
            {
                if (pin.Direction != ShaderGraphPinDirection::Input)
                    continue;
                if (!arguments.empty())
                    arguments += ", ";
                arguments += Input(node, pin).Code;
            }
            result = {node.Function + "(" + arguments + ")", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Reroute:
            result = CoerceShaderGraphExpression(namedInput("Input"), node.ValueType);
            break;
        case ShaderGraphNodeKind::If:
        {
            const auto left = CoerceShaderGraphExpression(namedInput("A"), ShaderGraphValueType::Scalar);
            const auto right = CoerceShaderGraphExpression(namedInput("B"), ShaderGraphValueType::Scalar);
            const auto threshold = CoerceShaderGraphExpression(namedInput("Threshold"), ShaderGraphValueType::Scalar);
            const auto greater = CoerceShaderGraphExpression(namedInput("Greater"), node.ValueType);
            const auto equal = CoerceShaderGraphExpression(namedInput("Equal"), node.ValueType);
            const auto less = CoerceShaderGraphExpression(namedInput("Less"), node.ValueType);
            result = {"(abs((" + left.Code + ") - (" + right.Code + ")) <= (" + threshold.Code + ") ? (" + equal.Code +
                          ") : ((" + left.Code + ") > (" + right.Code + ") ? (" + greater.Code + ") : (" + less.Code +
                          ")))",
                      node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::Compare:
        {
            const auto left = CoerceShaderGraphExpression(namedInput("A"), ShaderGraphValueType::Scalar);
            const auto right = CoerceShaderGraphExpression(namedInput("B"), ShaderGraphValueType::Scalar);
            const auto threshold = CoerceShaderGraphExpression(namedInput("Threshold"), ShaderGraphValueType::Scalar);
            const auto comparison = outputPin.Name == "Greater" ? ">" : outputPin.Name == "Less" ? "<" : "==";
            const auto expression = comparison == std::string_view("==")
                                        ? "abs((" + left.Code + ") - (" + right.Code + ")) <= (" + threshold.Code + ")"
                                        : "(" + left.Code + ") " + comparison + " (" + right.Code + ")";
            result = {"(" + expression + " ? 1.0F : 0.0F)", ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::BooleanAnd:
        case ShaderGraphNodeKind::BooleanOr:
        {
            const auto left = CoerceShaderGraphExpression(namedInput("A"), ShaderGraphValueType::Scalar);
            const auto right = CoerceShaderGraphExpression(namedInput("B"), ShaderGraphValueType::Scalar);
            const auto operation = node.Kind == ShaderGraphNodeKind::BooleanAnd ? "&&" : "||";
            result = {"(((" + left.Code + ") != 0.0F " + operation + " (" + right.Code + ") != 0.0F) ? 1.0F : 0.0F)",
                      ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::BooleanNot:
        {
            const auto input = CoerceShaderGraphExpression(namedInput("Input"), ShaderGraphValueType::Scalar);
            result = {"((" + input.Code + ") == 0.0F ? 1.0F : 0.0F)", ShaderGraphValueType::Scalar};
            break;
        }
        case ShaderGraphNodeKind::ArcTangent:
        case ShaderGraphNodeKind::HyperbolicSine:
        case ShaderGraphNodeKind::HyperbolicCosine:
        case ShaderGraphNodeKind::HyperbolicTangent:
        case ShaderGraphNodeKind::DegreesToRadians:
        case ShaderGraphNodeKind::RadiansToDegrees:
        case ShaderGraphNodeKind::Negate:
        case ShaderGraphNodeKind::Exponential:
        case ShaderGraphNodeKind::Logarithm:
        {
            const auto input = CoerceShaderGraphExpression(namedInput("Input"), node.ValueType);
            const auto expression =
                node.Kind == ShaderGraphNodeKind::ArcTangent          ? "atan(" + input.Code + ")"
                : node.Kind == ShaderGraphNodeKind::HyperbolicSine    ? "sinh(" + input.Code + ")"
                : node.Kind == ShaderGraphNodeKind::HyperbolicCosine  ? "cosh(" + input.Code + ")"
                : node.Kind == ShaderGraphNodeKind::HyperbolicTangent ? "tanh(" + input.Code + ")"
                : node.Kind == ShaderGraphNodeKind::DegreesToRadians  ? "((" + input.Code + ") * 0.017453292519943295F)"
                : node.Kind == ShaderGraphNodeKind::RadiansToDegrees  ? "((" + input.Code + ") * 57.29577951308232F)"
                : node.Kind == ShaderGraphNodeKind::Negate            ? "(-(" + input.Code + "))"
                : node.Kind == ShaderGraphNodeKind::Exponential       ? "exp(" + input.Code + ")"
                                                                      : "log(max(" + input.Code + ", 1.0e-8F))";
            result = {expression, node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::ScaleAndBias:
        {
            const auto input = CoerceShaderGraphExpression(namedInput("Input"), node.ValueType);
            const auto scale = CoerceShaderGraphExpression(namedInput("Scale"), ShaderGraphValueType::Scalar);
            const auto bias = CoerceShaderGraphExpression(namedInput("Bias"), ShaderGraphValueType::Scalar);
            result = {"((" + input.Code + ") * (" + scale.Code + ") + (" + bias.Code + "))", node.ValueType};
            break;
        }
        case ShaderGraphNodeKind::FunctionCall:
            throw std::logic_error("Function Call nodes must be expanded before shader compilation.");
        case ShaderGraphNodeKind::Master:
            throw std::invalid_argument("Shader Output nodes cannot feed another node.");
        }
        m_Visiting.erase(endpoint.Node);
        m_Cache.emplace(endpoint, result);
        return result;
    }

    void ShaderGraphCompiler::ValidateIncludes()
    {
        std::set<std::filesystem::path> visited;
        std::set<std::filesystem::path> visiting;
        for (const auto& node : m_Definition.Nodes)
            if (node.Kind == ShaderGraphNodeKind::Custom)
            {
                DiscoverInclude(node.Include, visited, visiting);
                const auto normalized = node.Include.lexically_normal();
                const auto root =
                    std::ranges::find_if(m_Definition.IncludeRoots,
                                         [&](const auto& candidate)
                                         {
                                             const auto relative = normalized.lexically_relative(candidate);
                                             return !relative.empty() && !relative.generic_string().starts_with("..");
                                         });
                if (root == m_Definition.IncludeRoots.end())
                    throw std::logic_error("Validated custom Shader Graph include root became unavailable.");
                m_CustomIncludes.push_back(normalized.lexically_relative(*root));
            }
        std::ranges::sort(m_CustomIncludes);
        m_CustomIncludes.erase(std::unique(m_CustomIncludes.begin(), m_CustomIncludes.end()), m_CustomIncludes.end());
    }

    void ShaderGraphCompiler::DiscoverInclude(const std::filesystem::path& path,
                                              std::set<std::filesystem::path>& visited,
                                              std::set<std::filesystem::path>& visiting)
    {
        const auto normalized = path.lexically_normal();
        if (!IsSafeShaderGraphRelativePath(normalized))
            throw std::invalid_argument("Custom Shader Graph includes must be confined relative paths.");
        const bool rooted =
            std::ranges::any_of(m_Definition.IncludeRoots,
                                [&](const auto& root)
                                {
                                    const auto relative = normalized.lexically_relative(root);
                                    return !relative.empty() && !relative.generic_string().starts_with("..");
                                });
        if (!rooted)
            throw std::invalid_argument("Custom Shader Graph include is outside its allowed roots: " +
                                        normalized.generic_string());
        if (!m_Options.ReadInclude)
            throw std::invalid_argument("Custom Shader Graph nodes require a confined include reader.");
        if (visiting.contains(normalized))
            throw std::invalid_argument("Custom Shader Graph include cycle detected at " + normalized.generic_string());
        if (!visited.insert(normalized).second)
            return;
        if (visited.size() > m_Options.MaximumCustomIncludes)
            throw std::invalid_argument("Custom Shader Graph include graph exceeds its configured limit.");
        const auto source = m_Options.ReadInclude(normalized);
        if (!source || source->size() > std::size_t{1024} * 1024U)
            throw std::invalid_argument("Custom Shader Graph include is missing or too large: " +
                                        normalized.generic_string());
        if (source->find('\0') != std::string::npos)
            throw std::invalid_argument("Custom Shader Graph include contains binary data.");
        visiting.insert(normalized);
        std::istringstream lines(*source);
        std::string line;
        while (std::getline(lines, line))
        {
            const auto hash = line.find('#');
            if (hash == std::string::npos || line.find("include", hash) == std::string::npos)
                continue;
            const auto quote = line.find_first_of("\"<", hash);
            const auto close = quote == std::string::npos ? std::string::npos : line.find_first_of("\">", quote + 1U);
            if (quote == std::string::npos || close == std::string::npos)
                throw std::invalid_argument("Custom Shader Graph include directive is malformed.");
            const std::filesystem::path child = line.substr(quote + 1U, close - quote - 1U);
            if (!IsSafeShaderGraphRelativePath(child))
                throw std::invalid_argument("Custom Shader Graph nested include is unsafe.");
            auto resolved = (normalized.parent_path() / child).lexically_normal();
            if (!m_Options.ReadInclude(resolved))
            {
                const auto found = std::ranges::find_if(m_Definition.IncludeRoots, [&](const auto& root)
                                                        { return m_Options.ReadInclude(root / child).has_value(); });
                if (found == m_Definition.IncludeRoots.end())
                    throw std::invalid_argument("Custom Shader Graph nested include could not be resolved: " +
                                                child.generic_string());
                resolved = (*found / child).lexically_normal();
            }
            DiscoverInclude(resolved, visited, visiting);
        }
        visiting.erase(normalized);
        m_Dependencies.push_back(normalized);
    }
} // namespace Keire::Detail
