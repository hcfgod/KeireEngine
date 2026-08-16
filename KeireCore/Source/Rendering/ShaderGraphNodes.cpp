#include "Keire/Rendering/ShaderGraph.h"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] ShaderGraphValue DefaultValue(const ShaderGraphValueType type)
        {
            switch (type)
            {
            case ShaderGraphValueType::Scalar:
                return 0.0F;
            case ShaderGraphValueType::Vector2:
                return Vector2{};
            case ShaderGraphValueType::Vector3:
                return Vector3{};
            case ShaderGraphValueType::Vector4:
                return Vector4{};
            case ShaderGraphValueType::Color:
                return Color{};
            case ShaderGraphValueType::Texture2D:
                return AssetId{};
            case ShaderGraphValueType::MaterialAttributes:
                return ShaderGraphMaterialAttributesValue{};
            case ShaderGraphValueType::Bsdf:
                return ShaderGraphBsdfValue{};
            }
            return 0.0F;
        }

        [[nodiscard]] ShaderGraphValue UnitValue(const ShaderGraphValueType type)
        {
            switch (type)
            {
            case ShaderGraphValueType::Scalar:
                return 1.0F;
            case ShaderGraphValueType::Vector2:
                return Vector2{1.0F, 1.0F};
            case ShaderGraphValueType::Vector3:
                return Vector3{1.0F, 1.0F, 1.0F};
            case ShaderGraphValueType::Vector4:
                return Vector4{1.0F, 1.0F, 1.0F, 1.0F};
            case ShaderGraphValueType::Color:
                return Color{1.0F, 1.0F, 1.0F, 1.0F};
            case ShaderGraphValueType::Texture2D:
                return AssetId{};
            case ShaderGraphValueType::MaterialAttributes:
                return ShaderGraphMaterialAttributesValue{};
            case ShaderGraphValueType::Bsdf:
                return ShaderGraphBsdfValue{};
            }
            return 1.0F;
        }

        void AddPin(ShaderGraphNode& node, std::string name, const ShaderGraphValueType type,
                    const ShaderGraphPinDirection direction, ShaderGraphValue value)
        {
            node.Pins.push_back({AssetId::Generate(), std::move(name), type, direction, value});
        }
    } // namespace

    ShaderGraphNode CreateShaderGraphNode(const std::string_view typeId, const ShaderGraphValueType valueType)
    {
        const auto* descriptor = FindShaderGraphNodeDescriptor(typeId);
        if (!descriptor)
            throw std::invalid_argument("Unknown Shader Graph node type ID: " + std::string(typeId) + '.');
        return CreateShaderGraphNode(descriptor->Kind, valueType);
    }

    ShaderGraphNode CreateShaderGraphNode(const ShaderGraphNodeKind kind, const ShaderGraphValueType valueType)
    {
        ShaderGraphNode node;
        node.Id = AssetId::Generate();
        node.Kind = kind;
        node.TypeId = ShaderGraphNodeTypeId(kind);
        if (node.TypeId.empty())
            throw std::invalid_argument("Unknown Shader Graph node kind.");
        node.ValueType = valueType;
        node.Value = DefaultValue(valueType);
        const auto input = [&](const std::string_view name, const ShaderGraphValueType type, ShaderGraphValue value)
        { AddPin(node, std::string(name), type, ShaderGraphPinDirection::Input, value); };
        const auto output = [&](const std::string_view name, const ShaderGraphValueType type)
        { AddPin(node, std::string(name), type, ShaderGraphPinDirection::Output, DefaultValue(type)); };
        const auto materialAttributeInputs = [&]
        {
            input("BaseColor", ShaderGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Metallic", ShaderGraphValueType::Scalar, 0.0F);
            input("Roughness", ShaderGraphValueType::Scalar, 0.5F);
            input("Specular", ShaderGraphValueType::Scalar, 0.5F);
            input("ClearCoat", ShaderGraphValueType::Scalar, 0.0F);
            input("ClearCoatRoughness", ShaderGraphValueType::Scalar, 0.25F);
            input("SheenColor", ShaderGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("SheenRoughness", ShaderGraphValueType::Scalar, 0.5F);
            input("Normal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Emission", ShaderGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("Occlusion", ShaderGraphValueType::Scalar, 1.0F);
            input("Opacity", ShaderGraphValueType::Scalar, 1.0F);
            input("SubsurfaceColor", ShaderGraphValueType::Color, Color{1.0F, 0.35F, 0.25F, 1.0F});
            input("Subsurface", ShaderGraphValueType::Scalar, 0.0F);
            input("Anisotropy", ShaderGraphValueType::Scalar, 0.0F);
            input("Tangent", ShaderGraphValueType::Vector3, Vector3{1.0F, 0.0F, 0.0F});
            input("Transmission", ShaderGraphValueType::Scalar, 0.0F);
            input("IndexOfRefraction", ShaderGraphValueType::Scalar, 1.5F);
            input("Refraction", ShaderGraphValueType::Scalar, 0.0F);
            input("Thickness", ShaderGraphValueType::Scalar, 1.0F);
        };
        const auto materialAttributeOutputs = [&]
        {
            output("BaseColor", ShaderGraphValueType::Color);
            output("Metallic", ShaderGraphValueType::Scalar);
            output("Roughness", ShaderGraphValueType::Scalar);
            output("Specular", ShaderGraphValueType::Scalar);
            output("ClearCoat", ShaderGraphValueType::Scalar);
            output("ClearCoatRoughness", ShaderGraphValueType::Scalar);
            output("SheenColor", ShaderGraphValueType::Color);
            output("SheenRoughness", ShaderGraphValueType::Scalar);
            output("Normal", ShaderGraphValueType::Vector3);
            output("Emission", ShaderGraphValueType::Color);
            output("Occlusion", ShaderGraphValueType::Scalar);
            output("Opacity", ShaderGraphValueType::Scalar);
            output("SubsurfaceColor", ShaderGraphValueType::Color);
            output("Subsurface", ShaderGraphValueType::Scalar);
            output("Anisotropy", ShaderGraphValueType::Scalar);
            output("Tangent", ShaderGraphValueType::Vector3);
            output("Transmission", ShaderGraphValueType::Scalar);
            output("IndexOfRefraction", ShaderGraphValueType::Scalar);
            output("Refraction", ShaderGraphValueType::Scalar);
            output("Thickness", ShaderGraphValueType::Scalar);
        };
        switch (kind)
        {
        case ShaderGraphNodeKind::Master:
            node.Name = "Lit Shader Output";
            input("BaseColor", ShaderGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Metallic", ShaderGraphValueType::Scalar, 0.0F);
            input("Roughness", ShaderGraphValueType::Scalar, 0.5F);
            input("Specular", ShaderGraphValueType::Scalar, 0.5F);
            input("ClearCoat", ShaderGraphValueType::Scalar, 0.0F);
            input("ClearCoatRoughness", ShaderGraphValueType::Scalar, 0.25F);
            input("SheenColor", ShaderGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("SheenRoughness", ShaderGraphValueType::Scalar, 0.5F);
            input("Normal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("DetailNormal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Parallax", ShaderGraphValueType::Scalar, 0.0F);
            input("Emission", ShaderGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("Occlusion", ShaderGraphValueType::Scalar, 1.0F);
            input("Opacity", ShaderGraphValueType::Scalar, 1.0F);
            input("SubsurfaceColor", ShaderGraphValueType::Color, Color{1.0F, 0.35F, 0.25F, 1.0F});
            input("Subsurface", ShaderGraphValueType::Scalar, 0.0F);
            input("Anisotropy", ShaderGraphValueType::Scalar, 0.0F);
            input("Tangent", ShaderGraphValueType::Vector3, Vector3{1.0F, 0.0F, 0.0F});
            input("Transmission", ShaderGraphValueType::Scalar, 0.0F);
            input("IndexOfRefraction", ShaderGraphValueType::Scalar, 1.5F);
            input("Refraction", ShaderGraphValueType::Scalar, 0.0F);
            input("Thickness", ShaderGraphValueType::Scalar, 1.0F);
            input("MaterialAttributes", ShaderGraphValueType::MaterialAttributes, ShaderGraphMaterialAttributesValue{});
            input("WorldPositionOffset", ShaderGraphValueType::Vector3, Vector3{});
            input("PixelDepthOffset", ShaderGraphValueType::Scalar, 0.0F);
            break;
        case ShaderGraphNodeKind::Parameter:
            node.Name = "Parameter";
            node.Symbol = "Parameter";
            output("Value", valueType);
            break;
        case ShaderGraphNodeKind::Constant:
            node.Name = "Constant";
            output("Value", valueType);
            break;
        case ShaderGraphNodeKind::TextureSample:
            node.Name = "Sample Texture 2D";
            input("Texture", ShaderGraphValueType::Texture2D, AssetId{});
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            output("RGBA", ShaderGraphValueType::Color);
            output("RGB", ShaderGraphValueType::Vector3);
            output("R", ShaderGraphValueType::Scalar);
            output("G", ShaderGraphValueType::Scalar);
            output("B", ShaderGraphValueType::Scalar);
            output("A", ShaderGraphValueType::Scalar);
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            break;
        case ShaderGraphNodeKind::UV:
            node.Name = "UV0";
            output("UV", ShaderGraphValueType::Vector2);
            node.ValueType = ShaderGraphValueType::Vector2;
            node.Value = Vector2{};
            break;
        case ShaderGraphNodeKind::UVTransform:
            node.Name = "UV Transform";
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Tiling", ShaderGraphValueType::Vector2, Vector2{1.0F, 1.0F});
            input("Offset", ShaderGraphValueType::Vector2, Vector2{});
            output("UV", ShaderGraphValueType::Vector2);
            node.ValueType = ShaderGraphValueType::Vector2;
            node.Value = Vector2{};
            break;
        case ShaderGraphNodeKind::NormalMap:
            node.Name = "Normal Map";
            input("Sample", ShaderGraphValueType::Color, Color{0.5F, 0.5F, 1.0F, 1.0F});
            input("Scale", ShaderGraphValueType::Scalar, 1.0F);
            output("Normal", ShaderGraphValueType::Vector3);
            node.ValueType = ShaderGraphValueType::Vector3;
            node.Value = Vector3{};
            break;
        case ShaderGraphNodeKind::DetailNormal:
            node.Name = "Detail Normal";
            input("Base", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Detail", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Strength", ShaderGraphValueType::Scalar, 1.0F);
            output("Normal", ShaderGraphValueType::Vector3);
            node.ValueType = ShaderGraphValueType::Vector3;
            node.Value = Vector3{};
            break;
        case ShaderGraphNodeKind::Parallax:
            node.Name = "Parallax Offset";
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Height", ShaderGraphValueType::Scalar, 0.5F);
            input("Scale", ShaderGraphValueType::Scalar, 0.02F);
            output("UV", ShaderGraphValueType::Vector2);
            node.ValueType = ShaderGraphValueType::Vector2;
            node.Value = Vector2{};
            break;
        case ShaderGraphNodeKind::Add:
        case ShaderGraphNodeKind::Subtract:
        case ShaderGraphNodeKind::Multiply:
        case ShaderGraphNodeKind::Divide:
        case ShaderGraphNodeKind::Minimum:
        case ShaderGraphNodeKind::Maximum:
            node.Name = kind == ShaderGraphNodeKind::Add        ? "Add"
                        : kind == ShaderGraphNodeKind::Subtract ? "Subtract"
                        : kind == ShaderGraphNodeKind::Multiply ? "Multiply"
                        : kind == ShaderGraphNodeKind::Divide   ? "Divide"
                        : kind == ShaderGraphNodeKind::Minimum  ? "Minimum"
                                                                : "Maximum";
            input("A", valueType, DefaultValue(valueType));
            input("B", valueType, kind == ShaderGraphNodeKind::Divide ? UnitValue(valueType) : DefaultValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Power:
            node.Name = "Power";
            input("Base", valueType, DefaultValue(valueType));
            input("Exponent", valueType, UnitValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Lerp:
            node.Name = "Lerp";
            input("A", valueType, DefaultValue(valueType));
            input("B", valueType, DefaultValue(valueType));
            input("T", ShaderGraphValueType::Scalar, 0.5F);
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::OneMinus:
        case ShaderGraphNodeKind::Clamp:
            node.Name = kind == ShaderGraphNodeKind::OneMinus ? "One Minus" : "Saturate";
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Absolute:
        case ShaderGraphNodeKind::Floor:
        case ShaderGraphNodeKind::Ceiling:
        case ShaderGraphNodeKind::Fraction:
        case ShaderGraphNodeKind::Sine:
        case ShaderGraphNodeKind::Cosine:
            node.Name = kind == ShaderGraphNodeKind::Absolute   ? "Absolute"
                        : kind == ShaderGraphNodeKind::Floor    ? "Floor"
                        : kind == ShaderGraphNodeKind::Ceiling  ? "Ceiling"
                        : kind == ShaderGraphNodeKind::Fraction ? "Fraction"
                        : kind == ShaderGraphNodeKind::Sine     ? "Sine"
                                                                : "Cosine";
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Normalize:
        {
            const auto normalizedType =
                valueType == ShaderGraphValueType::Scalar ? ShaderGraphValueType::Vector3 : valueType;
            node.Name = "Normalize";
            node.ValueType = normalizedType;
            node.Value = DefaultValue(normalizedType);
            input("Value", normalizedType, DefaultValue(normalizedType));
            output("Result", normalizedType);
            break;
        }
        case ShaderGraphNodeKind::Length:
        {
            const auto inputType =
                valueType == ShaderGraphValueType::Scalar ? ShaderGraphValueType::Vector3 : valueType;
            node.Name = "Length";
            input("Value", inputType, DefaultValue(inputType));
            output("Length", ShaderGraphValueType::Scalar);
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            break;
        }
        case ShaderGraphNodeKind::Dot:
        {
            const auto inputType =
                valueType == ShaderGraphValueType::Scalar ? ShaderGraphValueType::Vector3 : valueType;
            node.Name = "Dot Product";
            input("A", inputType, DefaultValue(inputType));
            input("B", inputType, DefaultValue(inputType));
            output("Dot", ShaderGraphValueType::Scalar);
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            break;
        }
        case ShaderGraphNodeKind::Remap:
            node.Name = "Remap";
            input("Value", valueType, DefaultValue(valueType));
            input("In Min", valueType, DefaultValue(valueType));
            input("In Max", valueType, UnitValue(valueType));
            input("Out Min", valueType, DefaultValue(valueType));
            input("Out Max", valueType, UnitValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::SmoothStep:
            node.Name = "Smooth Step";
            input("Edge Min", valueType, DefaultValue(valueType));
            input("Edge Max", valueType, UnitValue(valueType));
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Step:
            node.Name = "Step";
            input("Edge", valueType, DefaultValue(valueType));
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Fresnel:
            node.Name = "Fresnel";
            input("Normal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Power", ShaderGraphValueType::Scalar, 5.0F);
            input("F0", ShaderGraphValueType::Scalar, 0.04F);
            output("Fresnel", ShaderGraphValueType::Scalar);
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            break;
        case ShaderGraphNodeKind::VertexColor:
            node.Name = "Vertex Color";
            output("Color", ShaderGraphValueType::Color);
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            break;
        case ShaderGraphNodeKind::WorldPosition:
        case ShaderGraphNodeKind::WorldNormal:
        case ShaderGraphNodeKind::ViewDirection:
            node.Name = kind == ShaderGraphNodeKind::WorldPosition ? "World Position"
                        : kind == ShaderGraphNodeKind::WorldNormal ? "World Normal"
                                                                   : "View Direction";
            output("Vector", ShaderGraphValueType::Vector3);
            node.ValueType = ShaderGraphValueType::Vector3;
            node.Value = Vector3{};
            break;
        case ShaderGraphNodeKind::RotateUV:
            node.Name = "Rotate UV";
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Center", ShaderGraphValueType::Vector2, Vector2{0.5F, 0.5F});
            input("Rotation", ShaderGraphValueType::Scalar, 0.0F);
            output("UV", ShaderGraphValueType::Vector2);
            node.ValueType = ShaderGraphValueType::Vector2;
            node.Value = Vector2{};
            break;
        case ShaderGraphNodeKind::SimpleNoise:
            node.Name = "Simple Noise";
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Scale", ShaderGraphValueType::Scalar, 5.0F);
            input("Detail", ShaderGraphValueType::Scalar, 0.5F);
            output("Noise", ShaderGraphValueType::Scalar);
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            break;
        case ShaderGraphNodeKind::Desaturate:
            node.Name = "Desaturate";
            input("Color", ShaderGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Amount", ShaderGraphValueType::Scalar, 1.0F);
            output("Color", ShaderGraphValueType::Color);
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            break;
        case ShaderGraphNodeKind::Posterize:
            node.Name = "Posterize";
            input("Value", valueType, DefaultValue(valueType));
            input("Steps", ShaderGraphValueType::Scalar, 4.0F);
            output("Result", valueType);
            break;
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
            node.Name = kind == ShaderGraphNodeKind::Round                  ? "Round"
                        : kind == ShaderGraphNodeKind::Truncate             ? "Truncate"
                        : kind == ShaderGraphNodeKind::Sign                 ? "Sign"
                        : kind == ShaderGraphNodeKind::SquareRoot           ? "Square Root"
                        : kind == ShaderGraphNodeKind::ReciprocalSquareRoot ? "Reciprocal Square Root"
                        : kind == ShaderGraphNodeKind::Exponential2         ? "Exponential 2"
                        : kind == ShaderGraphNodeKind::Logarithm2           ? "Logarithm 2"
                        : kind == ShaderGraphNodeKind::Tangent              ? "Tangent"
                        : kind == ShaderGraphNodeKind::ArcSine              ? "Arc Sine"
                        : kind == ShaderGraphNodeKind::ArcCosine            ? "Arc Cosine"
                        : kind == ShaderGraphNodeKind::DerivativeX          ? "Derivative X"
                        : kind == ShaderGraphNodeKind::DerivativeY          ? "Derivative Y"
                                                                            : "Filter Width";
            input("Value", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Modulo:
        case ShaderGraphNodeKind::ArcTangent2:
            node.Name = kind == ShaderGraphNodeKind::Modulo ? "Modulo" : "Arc Tangent 2";
            input("A", valueType, DefaultValue(valueType));
            input("B", valueType, UnitValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Cross:
        case ShaderGraphNodeKind::Distance:
        case ShaderGraphNodeKind::Reflect:
        {
            node.Name = kind == ShaderGraphNodeKind::Cross      ? "Cross Product"
                        : kind == ShaderGraphNodeKind::Distance ? "Distance"
                                                                : "Reflect";
            node.ValueType =
                kind == ShaderGraphNodeKind::Distance ? ShaderGraphValueType::Scalar : ShaderGraphValueType::Vector3;
            node.Value = DefaultValue(node.ValueType);
            input("A", ShaderGraphValueType::Vector3, Vector3{});
            input("B", ShaderGraphValueType::Vector3,
                  kind == ShaderGraphNodeKind::Reflect ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{});
            output("Result", node.ValueType);
            break;
        }
        case ShaderGraphNodeKind::Refract:
            node.Name = "Refract";
            node.ValueType = ShaderGraphValueType::Vector3;
            node.Value = Vector3{};
            input("Incident", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, -1.0F});
            input("Normal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("IOR", ShaderGraphValueType::Scalar, 1.5F);
            output("Result", ShaderGraphValueType::Vector3);
            break;
        case ShaderGraphNodeKind::AppendVector:
            node.Name = "Append Vector";
            node.ValueType = ShaderGraphValueType::Vector4;
            node.Value = Vector4{};
            input("XYZ", ShaderGraphValueType::Vector3, Vector3{});
            input("W", ShaderGraphValueType::Scalar, 1.0F);
            output("Result", ShaderGraphValueType::Vector4);
            break;
        case ShaderGraphNodeKind::ComponentMask:
            node.Name = "Component Mask";
            node.ValueType = ShaderGraphValueType::Vector4;
            node.Value = Vector4{};
            input("Value", ShaderGraphValueType::Vector4, Vector4{});
            output("R", ShaderGraphValueType::Scalar);
            output("G", ShaderGraphValueType::Scalar);
            output("B", ShaderGraphValueType::Scalar);
            output("A", ShaderGraphValueType::Scalar);
            output("RG", ShaderGraphValueType::Vector2);
            output("RGB", ShaderGraphValueType::Vector3);
            output("RGBA", ShaderGraphValueType::Vector4);
            break;
        case ShaderGraphNodeKind::UV1:
            node.Name = "UV1";
            node.ValueType = ShaderGraphValueType::Vector2;
            node.Value = Vector2{};
            output("UV", ShaderGraphValueType::Vector2);
            break;
        case ShaderGraphNodeKind::WorldTangent:
        case ShaderGraphNodeKind::CameraPosition:
        case ShaderGraphNodeKind::ObjectPosition:
            node.Name = kind == ShaderGraphNodeKind::WorldTangent     ? "World Tangent"
                        : kind == ShaderGraphNodeKind::CameraPosition ? "Camera Position"
                                                                      : "Object Position";
            node.ValueType = ShaderGraphValueType::Vector3;
            node.Value = Vector3{};
            output("Vector", ShaderGraphValueType::Vector3);
            break;
        case ShaderGraphNodeKind::Time:
        case ShaderGraphNodeKind::DeltaTime:
            node.Name = kind == ShaderGraphNodeKind::Time ? "Time" : "Delta Time";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            output("Seconds", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::ScreenPosition:
            node.Name = "Screen Position";
            node.ValueType = ShaderGraphValueType::Vector2;
            node.Value = Vector2{};
            output("UV", ShaderGraphValueType::Vector2);
            break;
        case ShaderGraphNodeKind::DepthFade:
            node.Name = "Depth Fade";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("Distance", ShaderGraphValueType::Scalar, 0.0F);
            input("Fade Distance", ShaderGraphValueType::Scalar, 100.0F);
            output("Fade", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::Luminance:
            node.Name = "Luminance";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("Color", ShaderGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            output("Luminance", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::HueShift:
            node.Name = "Hue Shift";
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            input("Color", ShaderGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Shift", ShaderGraphValueType::Scalar, 0.0F);
            output("Color", ShaderGraphValueType::Color);
            break;
        case ShaderGraphNodeKind::Checkerboard:
            node.Name = "Checkerboard";
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Color A", ShaderGraphValueType::Color, Color{0.05F, 0.05F, 0.05F, 1.0F});
            input("Color B", ShaderGraphValueType::Color, Color{0.8F, 0.8F, 0.8F, 1.0F});
            input("Scale", ShaderGraphValueType::Vector2, Vector2{8.0F, 8.0F});
            output("Color", ShaderGraphValueType::Color);
            break;
        case ShaderGraphNodeKind::VoronoiNoise:
            node.Name = "Voronoi Noise";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Scale", ShaderGraphValueType::Scalar, 5.0F);
            input("Jitter", ShaderGraphValueType::Scalar, 1.0F);
            output("Distance", ShaderGraphValueType::Scalar);
            output("Cell", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::Panner:
            node.Name = "Panner";
            node.ValueType = ShaderGraphValueType::Vector2;
            node.Value = Vector2{};
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Speed", ShaderGraphValueType::Vector2, Vector2{0.1F, 0.0F});
            input("Time", ShaderGraphValueType::Scalar, 0.0F);
            output("UV", ShaderGraphValueType::Vector2);
            break;
        case ShaderGraphNodeKind::PolarCoordinates:
            node.Name = "Polar Coordinates";
            node.ValueType = ShaderGraphValueType::Vector2;
            node.Value = Vector2{};
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Center", ShaderGraphValueType::Vector2, Vector2{0.5F, 0.5F});
            input("Radial Scale", ShaderGraphValueType::Scalar, 1.0F);
            input("Length Scale", ShaderGraphValueType::Scalar, 1.0F);
            output("Polar", ShaderGraphValueType::Vector2);
            break;
        case ShaderGraphNodeKind::SphereMask:
            node.Name = "Sphere Mask";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("A", ShaderGraphValueType::Vector3, Vector3{});
            input("B", ShaderGraphValueType::Vector3, Vector3{});
            input("Radius", ShaderGraphValueType::Scalar, 1.0F);
            input("Hardness", ShaderGraphValueType::Scalar, 8.0F);
            output("Mask", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::RadialGradient:
            node.Name = "Radial Gradient";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Center", ShaderGraphValueType::Vector2, Vector2{0.5F, 0.5F});
            input("Radius", ShaderGraphValueType::Scalar, 0.5F);
            input("Density", ShaderGraphValueType::Scalar, 4.0F);
            output("Gradient", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::LinearGradient:
            node.Name = "Linear Gradient";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Direction", ShaderGraphValueType::Vector2, Vector2{1.0F, 0.0F});
            input("Offset", ShaderGraphValueType::Scalar, 0.0F);
            output("Gradient", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::Contrast:
            node.Name = "Contrast";
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            input("Color", ShaderGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Contrast", ShaderGraphValueType::Scalar, 1.0F);
            input("Pivot", ShaderGraphValueType::Scalar, 0.5F);
            output("Color", ShaderGraphValueType::Color);
            break;
        case ShaderGraphNodeKind::Saturation:
            node.Name = "Saturation";
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            input("Color", ShaderGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Saturation", ShaderGraphValueType::Scalar, 1.0F);
            output("Color", ShaderGraphValueType::Color);
            break;
        case ShaderGraphNodeKind::BlendOverlay:
            node.Name = "Overlay Blend";
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            input("Base", ShaderGraphValueType::Color, Color{0.5F, 0.5F, 0.5F, 1.0F});
            input("Blend", ShaderGraphValueType::Color, Color{0.5F, 0.5F, 0.5F, 1.0F});
            input("Opacity", ShaderGraphValueType::Scalar, 1.0F);
            output("Color", ShaderGraphValueType::Color);
            break;
        case ShaderGraphNodeKind::Blackbody:
            node.Name = "Blackbody";
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            input("Temperature", ShaderGraphValueType::Scalar, 6500.0F);
            output("Color", ShaderGraphValueType::Color);
            break;
        case ShaderGraphNodeKind::ReflectionVector:
            node.Name = "Reflection Vector";
            node.ValueType = ShaderGraphValueType::Vector3;
            node.Value = Vector3{};
            input("Normal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            output("Vector", ShaderGraphValueType::Vector3);
            break;
        case ShaderGraphNodeKind::FacingRatio:
            node.Name = "Facing Ratio";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("Normal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Power", ShaderGraphValueType::Scalar, 1.0F);
            output("Ratio", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::Dither:
            node.Name = "Dither";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("Alpha", ShaderGraphValueType::Scalar, 1.0F);
            input("Screen Position", ShaderGraphValueType::Vector2, Vector2{});
            output("Value", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::GradientNoise:
            node.Name = "Gradient Noise";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Scale", ShaderGraphValueType::Scalar, 5.0F);
            output("Noise", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::Wave:
            node.Name = "Wave";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Direction", ShaderGraphValueType::Vector2, Vector2{1.0F, 0.0F});
            input("Frequency", ShaderGraphValueType::Scalar, 8.0F);
            input("Phase", ShaderGraphValueType::Scalar, 0.0F);
            output("Wave", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::TriplanarSample:
            node.Name = "Triplanar Sample";
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            input("Texture", ShaderGraphValueType::Texture2D, AssetId{});
            input("Position", ShaderGraphValueType::Vector3, Vector3{});
            input("Normal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Scale", ShaderGraphValueType::Scalar, 1.0F);
            input("Blend Sharpness", ShaderGraphValueType::Scalar, 4.0F);
            output("RGBA", ShaderGraphValueType::Color);
            output("RGB", ShaderGraphValueType::Vector3);
            output("R", ShaderGraphValueType::Scalar);
            output("G", ShaderGraphValueType::Scalar);
            output("B", ShaderGraphValueType::Scalar);
            output("A", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::TextureSampleLevel:
            node.Name = "Sample Texture 2D Level";
            node.ValueType = ShaderGraphValueType::Color;
            node.Value = Color{};
            input("Texture", ShaderGraphValueType::Texture2D, AssetId{});
            input("UV", ShaderGraphValueType::Vector2, Vector2{});
            input("Mip Level", ShaderGraphValueType::Scalar, 0.0F);
            output("RGBA", ShaderGraphValueType::Color);
            output("RGB", ShaderGraphValueType::Vector3);
            output("R", ShaderGraphValueType::Scalar);
            output("G", ShaderGraphValueType::Scalar);
            output("B", ShaderGraphValueType::Scalar);
            output("A", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::HeightToNormal:
            node.Name = "Height To Normal";
            node.ValueType = ShaderGraphValueType::Vector3;
            node.Value = Vector3{};
            input("Height", ShaderGraphValueType::Scalar, 0.5F);
            input("Strength", ShaderGraphValueType::Scalar, 1.0F);
            output("Normal", ShaderGraphValueType::Vector3);
            break;
        case ShaderGraphNodeKind::FlattenNormal:
            node.Name = "Flatten Normal";
            node.ValueType = ShaderGraphValueType::Vector3;
            node.Value = Vector3{};
            input("Normal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Strength", ShaderGraphValueType::Scalar, 1.0F);
            output("Normal", ShaderGraphValueType::Vector3);
            break;
        case ShaderGraphNodeKind::MakeMaterialAttributes:
            node.Name = "Make Material Attributes";
            node.ValueType = ShaderGraphValueType::MaterialAttributes;
            node.Value = ShaderGraphMaterialAttributesValue{};
            materialAttributeInputs();
            output("Attributes", ShaderGraphValueType::MaterialAttributes);
            break;
        case ShaderGraphNodeKind::BreakMaterialAttributes:
            node.Name = "Break Material Attributes";
            node.ValueType = ShaderGraphValueType::MaterialAttributes;
            node.Value = ShaderGraphMaterialAttributesValue{};
            input("Attributes", ShaderGraphValueType::MaterialAttributes, ShaderGraphMaterialAttributesValue{});
            materialAttributeOutputs();
            break;
        case ShaderGraphNodeKind::BlendMaterialAttributes:
            node.Name = "Blend Material Attributes";
            node.ValueType = ShaderGraphValueType::MaterialAttributes;
            node.Value = ShaderGraphMaterialAttributesValue{};
            input("A", ShaderGraphValueType::MaterialAttributes, ShaderGraphMaterialAttributesValue{});
            input("B", ShaderGraphValueType::MaterialAttributes, ShaderGraphMaterialAttributesValue{});
            input("Alpha", ShaderGraphValueType::Scalar, 0.5F);
            output("Attributes", ShaderGraphValueType::MaterialAttributes);
            break;
        case ShaderGraphNodeKind::StandardSurfaceBsdf:
            node.Name = "Standard Surface BSDF";
            node.ValueType = ShaderGraphValueType::Bsdf;
            node.Value = ShaderGraphBsdfValue{};
            input("BaseColor", ShaderGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Metallic", ShaderGraphValueType::Scalar, 0.0F);
            input("Roughness", ShaderGraphValueType::Scalar, 0.5F);
            input("Specular", ShaderGraphValueType::Scalar, 0.5F);
            input("Normal", ShaderGraphValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F});
            input("Emission", ShaderGraphValueType::Color, Color{0.0F, 0.0F, 0.0F, 1.0F});
            input("Opacity", ShaderGraphValueType::Scalar, 1.0F);
            output("BSDF", ShaderGraphValueType::Bsdf);
            break;
        case ShaderGraphNodeKind::ClearCoatBsdf:
            node.Name = "Clear Coat BSDF";
            node.ValueType = ShaderGraphValueType::Bsdf;
            node.Value = ShaderGraphBsdfValue{};
            input("Base", ShaderGraphValueType::Bsdf, ShaderGraphBsdfValue{});
            input("Weight", ShaderGraphValueType::Scalar, 1.0F);
            input("Roughness", ShaderGraphValueType::Scalar, 0.25F);
            output("BSDF", ShaderGraphValueType::Bsdf);
            break;
        case ShaderGraphNodeKind::SheenBsdf:
            node.Name = "Sheen BSDF";
            node.ValueType = ShaderGraphValueType::Bsdf;
            node.Value = ShaderGraphBsdfValue{};
            input("Base", ShaderGraphValueType::Bsdf, ShaderGraphBsdfValue{});
            input("Color", ShaderGraphValueType::Color, Color{1.0F, 1.0F, 1.0F, 1.0F});
            input("Weight", ShaderGraphValueType::Scalar, 1.0F);
            input("Roughness", ShaderGraphValueType::Scalar, 0.5F);
            output("BSDF", ShaderGraphValueType::Bsdf);
            break;
        case ShaderGraphNodeKind::SubsurfaceBsdf:
            node.Name = "Subsurface BSDF";
            node.ValueType = ShaderGraphValueType::Bsdf;
            node.Value = ShaderGraphBsdfValue{};
            input("Base", ShaderGraphValueType::Bsdf, ShaderGraphBsdfValue{});
            input("Color", ShaderGraphValueType::Color, Color{1.0F, 0.35F, 0.25F, 1.0F});
            input("Weight", ShaderGraphValueType::Scalar, 1.0F);
            output("BSDF", ShaderGraphValueType::Bsdf);
            break;
        case ShaderGraphNodeKind::TransmissionBsdf:
            node.Name = "Transmission BSDF";
            node.ValueType = ShaderGraphValueType::Bsdf;
            node.Value = ShaderGraphBsdfValue{};
            input("Base", ShaderGraphValueType::Bsdf, ShaderGraphBsdfValue{});
            input("Weight", ShaderGraphValueType::Scalar, 1.0F);
            input("IndexOfRefraction", ShaderGraphValueType::Scalar, 1.5F);
            input("Refraction", ShaderGraphValueType::Scalar, 1.0F);
            input("Thickness", ShaderGraphValueType::Scalar, 1.0F);
            output("BSDF", ShaderGraphValueType::Bsdf);
            break;
        case ShaderGraphNodeKind::BsdfToMaterialAttributes:
            node.Name = "BSDF To Material Attributes";
            node.ValueType = ShaderGraphValueType::MaterialAttributes;
            node.Value = ShaderGraphMaterialAttributesValue{};
            input("BSDF", ShaderGraphValueType::Bsdf, ShaderGraphBsdfValue{});
            output("Attributes", ShaderGraphValueType::MaterialAttributes);
            break;
        case ShaderGraphNodeKind::Keyword:
            node.Name = "Keyword";
            node.Symbol = "KEYWORD";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            output("Enabled", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::StaticSwitch:
            node.Name = "Static Switch";
            input("Condition", ShaderGraphValueType::Scalar, 0.0F);
            input("True", valueType, DefaultValue(valueType));
            input("False", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Custom:
            node.Name = "Custom Function";
            node.Function = "EvaluateCustomMaterialNode";
            input("Input", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::FunctionCall:
            node.Name = "Function Call";
            break;
        case ShaderGraphNodeKind::Reroute:
            node.Name = "Reroute";
            input("Input", valueType, DefaultValue(valueType));
            output("Output", valueType);
            break;
        case ShaderGraphNodeKind::If:
            node.Name = "If";
            input("A", ShaderGraphValueType::Scalar, 0.0F);
            input("B", ShaderGraphValueType::Scalar, 0.0F);
            input("Greater", valueType, DefaultValue(valueType));
            input("Equal", valueType, DefaultValue(valueType));
            input("Less", valueType, DefaultValue(valueType));
            input("Threshold", ShaderGraphValueType::Scalar, 1.0e-5F);
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::Compare:
            node.Name = "Compare";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("A", ShaderGraphValueType::Scalar, 0.0F);
            input("B", ShaderGraphValueType::Scalar, 0.0F);
            input("Threshold", ShaderGraphValueType::Scalar, 1.0e-5F);
            output("Equal", ShaderGraphValueType::Scalar);
            output("Greater", ShaderGraphValueType::Scalar);
            output("Less", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::BooleanAnd:
        case ShaderGraphNodeKind::BooleanOr:
            node.Name = kind == ShaderGraphNodeKind::BooleanAnd ? "And" : "Or";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("A", ShaderGraphValueType::Scalar, 0.0F);
            input("B", ShaderGraphValueType::Scalar, 0.0F);
            output("Result", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::BooleanNot:
            node.Name = "Not";
            node.ValueType = ShaderGraphValueType::Scalar;
            node.Value = 0.0F;
            input("Input", ShaderGraphValueType::Scalar, 0.0F);
            output("Result", ShaderGraphValueType::Scalar);
            break;
        case ShaderGraphNodeKind::ArcTangent:
        case ShaderGraphNodeKind::HyperbolicSine:
        case ShaderGraphNodeKind::HyperbolicCosine:
        case ShaderGraphNodeKind::HyperbolicTangent:
        case ShaderGraphNodeKind::DegreesToRadians:
        case ShaderGraphNodeKind::RadiansToDegrees:
        case ShaderGraphNodeKind::Negate:
        case ShaderGraphNodeKind::Exponential:
        case ShaderGraphNodeKind::Logarithm:
            node.Name = kind == ShaderGraphNodeKind::ArcTangent          ? "Arc Tangent"
                        : kind == ShaderGraphNodeKind::HyperbolicSine    ? "Hyperbolic Sine"
                        : kind == ShaderGraphNodeKind::HyperbolicCosine  ? "Hyperbolic Cosine"
                        : kind == ShaderGraphNodeKind::HyperbolicTangent ? "Hyperbolic Tangent"
                        : kind == ShaderGraphNodeKind::DegreesToRadians  ? "Degrees To Radians"
                        : kind == ShaderGraphNodeKind::RadiansToDegrees  ? "Radians To Degrees"
                        : kind == ShaderGraphNodeKind::Negate            ? "Negate"
                        : kind == ShaderGraphNodeKind::Exponential       ? "Exponential"
                                                                         : "Logarithm";
            input("Input", valueType, DefaultValue(valueType));
            output("Result", valueType);
            break;
        case ShaderGraphNodeKind::ScaleAndBias:
            node.Name = "Scale And Bias";
            input("Input", valueType, DefaultValue(valueType));
            input("Scale", ShaderGraphValueType::Scalar, 1.0F);
            input("Bias", ShaderGraphValueType::Scalar, 0.0F);
            output("Result", valueType);
            break;
        }
        return node;
    }

    ShaderGraphNode CreateShaderGraphFunctionCallNode(const AssetId function,
                                                      const ShaderGraphDefinition& functionDefinition)
    {
        if (!function || functionDefinition.Purpose == ShaderGraphPurpose::Shader)
            throw std::invalid_argument("Function Call creation requires a reusable graph asset.");
        ValidateShaderGraph(functionDefinition);
        const auto master =
            std::ranges::find(functionDefinition.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
        if (master == functionDefinition.Nodes.end())
            throw std::invalid_argument("Reusable graph asset has no output node.");

        ShaderGraphNode result;
        result.Id = AssetId::Generate();
        result.Kind = ShaderGraphNodeKind::FunctionCall;
        result.TypeId = std::string(ShaderGraphNodeTypeId(result.Kind));
        result.Name = "Function Call";
        result.ReferencedAsset = function;
        for (const auto& parameter : functionDefinition.Nodes)
        {
            if (parameter.Kind != ShaderGraphNodeKind::Parameter)
                continue;
            const auto output =
                std::ranges::find(parameter.Pins, ShaderGraphPinDirection::Output, &ShaderGraphPin::Direction);
            if (output == parameter.Pins.end())
                throw std::invalid_argument("Reusable graph parameter has no output pin.");
            AddPin(result, parameter.Symbol, output->Type, ShaderGraphPinDirection::Input, parameter.Value);
        }
        for (const auto& pin : master->Pins)
        {
            if (pin.Direction != ShaderGraphPinDirection::Input)
                throw std::invalid_argument("Reusable graph output node contains an invalid output pin.");
            AddPin(result, pin.Name, pin.Type, ShaderGraphPinDirection::Output, pin.DefaultValue);
        }
        if (result.Pins.empty() || std::ranges::none_of(result.Pins, [](const ShaderGraphPin& pin)
                                                        { return pin.Direction == ShaderGraphPinDirection::Output; }))
            throw std::invalid_argument("Reusable graph functions require at least one output.");
        const auto firstOutput =
            std::ranges::find(result.Pins, ShaderGraphPinDirection::Output, &ShaderGraphPin::Direction);
        result.ValueType = firstOutput->Type;
        result.Value = DefaultValue(result.ValueType);
        return result;
    }

} // namespace Keire
