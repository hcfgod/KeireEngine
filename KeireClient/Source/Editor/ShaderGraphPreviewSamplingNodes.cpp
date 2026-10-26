#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorState.h"

#include "KeireClient/Editor/ShaderGraphPreviewTextureSampling.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    [[nodiscard]] PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::TextureSample(const PreviewGraphValue texture,
                                                                                     Keire::Vector2 uv) const noexcept
    {
        uv = {uv.X * texture.TextureTransform.X + texture.TextureTransform.Z,
              uv.Y * texture.TextureTransform.Y + texture.TextureTransform.W};
        if (texture.Texture)
        {
            if (const auto sample = Detail::SampleShaderGraphPreviewTexture(m_Textures, texture.Texture, uv))
            {
                return {.Data = *sample, .Type = Keire::ShaderGraphValueType::Color};
            }
        }
        const auto alternateAxis = [](const float coordinate)
        {
            return std::isfinite(coordinate) &&
                   std::fmod(std::floor(static_cast<double>(coordinate) * 10.0), 2.0) != 0.0;
        };
        const bool alternate = alternateAxis(uv.X) != alternateAxis(uv.Y);
        const float checker = alternate ? 0.58F : 0.88F;
        Keire::Vector4 sample{checker, checker, checker, 1.0F};
        switch (texture.TextureSemantic)
        {
        case Keire::ShaderTextureSemantic::Normal:
            sample = {0.5F, 0.5F, 1.0F, 1.0F};
            break;
        case Keire::ShaderTextureSemantic::Metallic:
            sample = {0.1F, 0.1F, 0.1F, 1.0F};
            break;
        case Keire::ShaderTextureSemantic::Roughness:
            sample = {0.55F, 0.55F, 0.55F, 1.0F};
            break;
        case Keire::ShaderTextureSemantic::MetallicRoughness:
            sample = {0.1F, 0.55F, 0.0F, 1.0F};
            break;
        case Keire::ShaderTextureSemantic::Occlusion:
            sample = {1.0F, 1.0F, 1.0F, 1.0F};
            break;
        case Keire::ShaderTextureSemantic::Emissive:
            sample = {checker * 0.25F, checker * 0.25F, checker * 0.25F, 1.0F};
            break;
        case Keire::ShaderTextureSemantic::Generic:
        case Keire::ShaderTextureSemantic::BaseColor:
            break;
        }
        return {.Data = sample, .Type = Keire::ShaderGraphValueType::Color};
    }

    PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::EvaluateSamplingNode(const Keire::ShaderGraphNode& node,
                                                                              const Keire::ShaderGraphPin& outputPin)
    {
        PreviewGraphValue result;
        switch (node.Kind)
        {
        case Keire::ShaderGraphNodeKind::Parameter:
        {
            const auto property = std::ranges::find(m_Properties, node.Symbol, &Keire::ShaderPropertyDefinition::Name);
            if (node.ValueType == Keire::ShaderGraphValueType::Texture2D)
            {
                result = GraphValue(node.Value, node.ValueType, node.TextureSemantic);
                if (property != m_Properties.end())
                {
                    result.Texture = property->DefaultTexture;
                }
                const auto transform = m_Nodes.find(node.ParameterMetadata.TextureTransformProperty);
                if (transform != m_Nodes.end())
                {
                    const auto& parameter = m_Definition.Nodes[transform->second];
                    const auto value =
                        std::ranges::find(m_Properties, parameter.Symbol, &Keire::ShaderPropertyDefinition::Name);
                    result.TextureTransform =
                        value == m_Properties.end() ? ValueVector(parameter.Value) : value->DefaultValue;
                }
            }
            else if (property != m_Properties.end())
            {
                result = {.Data = property->DefaultValue, .Type = node.ValueType};
            }
            else
            {
                result = GraphValue(node.Value, node.ValueType);
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::Constant:
            result = GraphValue(node.Value, node.ValueType);
            break;
        case Keire::ShaderGraphNodeKind::TextureSample:
        {
            const auto texture = NamedInput(node, "Texture");
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            result = TextureSample(texture, {uv.X, uv.Y});
            if (outputPin.Name == "RGB")
            {
                result.Type = Keire::ShaderGraphValueType::Vector3;
                result.Data.W = 0.0F;
            }
            else if (outputPin.Type == Keire::ShaderGraphValueType::Scalar)
            {
                const float component = outputPin.Name == "R"   ? result.Data.X
                                        : outputPin.Name == "G" ? result.Data.Y
                                        : outputPin.Name == "B" ? result.Data.Z
                                                                : result.Data.W;
                result = {.Data = {component, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::TriplanarSample:
        {
            const auto texture = NamedInput(node, "Texture");
            const auto position = Coerce(NamedInput(node, "Position"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
            const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float sharpness = std::max(
                std::abs(Coerce(NamedInput(node, "Blend Sharpness"), Keire::ShaderGraphValueType::Scalar).Data.X),
                1.0F);
            auto weights = Keire::Vector3{std::pow(std::abs(normalValue.X), sharpness),
                                          std::pow(std::abs(normalValue.Y), sharpness),
                                          std::pow(std::abs(normalValue.Z), sharpness)};
            const float total = std::max(weights.X + weights.Y + weights.Z, 1.0e-5F);
            weights = {weights.X / total, weights.Y / total, weights.Z / total};
            const auto xSample = TextureSample(texture, {position.Z * scale, position.Y * scale}).Data;
            const auto ySample = TextureSample(texture, {position.X * scale, position.Z * scale}).Data;
            const auto zSample = TextureSample(texture, {position.X * scale, position.Y * scale}).Data;
            const Keire::Vector4 sample{xSample.X * weights.X + ySample.X * weights.Y + zSample.X * weights.Z,
                                        xSample.Y * weights.X + ySample.Y * weights.Y + zSample.Y * weights.Z,
                                        xSample.Z * weights.X + ySample.Z * weights.Y + zSample.Z * weights.Z,
                                        xSample.W * weights.X + ySample.W * weights.Y + zSample.W * weights.Z};
            if (outputPin.Name == "RGB")
            {
                result = {.Data = sample, .Type = Keire::ShaderGraphValueType::Vector3};
            }
            else if (outputPin.Type == Keire::ShaderGraphValueType::Scalar)
            {
                const float component = outputPin.Name == "R"   ? sample.X
                                        : outputPin.Name == "G" ? sample.Y
                                        : outputPin.Name == "B" ? sample.Z
                                                                : sample.W;
                result = {.Data = {component, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            }
            else
            {
                result = {.Data = sample, .Type = Keire::ShaderGraphValueType::Color};
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::TextureSampleLevel:
        {
            const auto texture = NamedInput(node, "Texture");
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            result = TextureSample(texture, {uv.X, uv.Y});
            if (outputPin.Name == "RGB")
            {
                result.Type = Keire::ShaderGraphValueType::Vector3;
                result.Data.W = 0.0F;
            }
            else if (outputPin.Type == Keire::ShaderGraphValueType::Scalar)
            {
                const float component = outputPin.Name == "R"   ? result.Data.X
                                        : outputPin.Name == "G" ? result.Data.Y
                                        : outputPin.Name == "B" ? result.Data.Z
                                                                : result.Data.W;
                result = {.Data = {component, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            }
            break;
        }
        default:
            throw std::logic_error("Preview node dispatched to an incompatible sampling family.");
        }
        return result;
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
