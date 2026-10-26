#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::EvaluateProceduralNode(const Keire::ShaderGraphNode& node,
                                                                                const Keire::ShaderGraphPin& outputPin)
    {
        PreviewGraphValue result;
        switch (node.Kind)
        {
        case Keire::ShaderGraphNodeKind::SimpleNoise:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float detail = Coerce(NamedInput(node, "Detail"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Data = {MaterialNoise({uv.X, uv.Y}, scale, detail), 0.0F, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::Desaturate:
        {
            const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
            const float amount =
                std::clamp(Coerce(NamedInput(node, "Amount"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
            const float luminance = color.X * 0.2126F + color.Y * 0.7152F + color.Z * 0.0722F;
            result = {.Data = {std::lerp(color.X, luminance, amount), std::lerp(color.Y, luminance, amount),
                               std::lerp(color.Z, luminance, amount), color.W},
                      .Type = Keire::ShaderGraphValueType::Color};
            break;
        }
        case Keire::ShaderGraphNodeKind::Posterize:
        {
            const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
            const float steps =
                std::max(std::abs(Coerce(NamedInput(node, "Steps"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0F);
            result.Type = node.ValueType;
            for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
            {
                SetComponent(result.Data, component, std::floor(Component(value.Data, component) * steps) / steps);
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::Luminance:
        {
            const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
            result = {.Data = {color.X * 0.2126F + color.Y * 0.7152F + color.Z * 0.0722F, 0.0F, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::HueShift:
        {
            const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
            const float angle =
                Coerce(NamedInput(node, "Shift"), Keire::ShaderGraphValueType::Scalar).Data.X * 6.28318530718F;
            constexpr auto inverseRootThree = 0.57735026919F;
            const Keire::Vector3 axis{inverseRootThree, inverseRootThree, inverseRootThree};
            const Keire::Vector3 source{color.X, color.Y, color.Z};
            const auto axisCross = Cross(axis, source);
            const float axisDot = Dot(axis, source);
            const float sine = std::sin(angle);
            const float cosine = std::cos(angle);
            result = {
                .Data = {std::max(source.X * cosine + axisCross.X * sine + axis.X * axisDot * (1.0F - cosine), 0.0F),
                         std::max(source.Y * cosine + axisCross.Y * sine + axis.Y * axisDot * (1.0F - cosine), 0.0F),
                         std::max(source.Z * cosine + axisCross.Z * sine + axis.Z * axisDot * (1.0F - cosine), 0.0F),
                         color.W},
                .Type = Keire::ShaderGraphValueType::Color};
            break;
        }
        case Keire::ShaderGraphNodeKind::Checkerboard:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto colorA = Coerce(NamedInput(node, "Color A"), Keire::ShaderGraphValueType::Color).Data;
            const auto colorB = Coerce(NamedInput(node, "Color B"), Keire::ShaderGraphValueType::Color).Data;
            const auto scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto parity = static_cast<std::int64_t>(std::floor(uv.X * scale.X) + std::floor(uv.Y * scale.Y)) & 1;
            result = {.Data = parity != 0 ? colorB : colorA, .Type = Keire::ShaderGraphValueType::Color};
            break;
        }
        case Keire::ShaderGraphNodeKind::VoronoiNoise:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float value = MaterialValueNoise({uv.X * scale, uv.Y * scale});
            result = {.Data = {outputPin.Name == "Cell" ? Fraction(value * 17.0F) : value, 0.0F, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::SphereMask:
        {
            const auto first = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto second = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Vector3).Data;
            const float x = first.X - second.X;
            const float y = first.Y - second.Y;
            const float z = first.Z - second.Z;
            const float radius = std::max(
                std::abs(Coerce(NamedInput(node, "Radius"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0e-5F);
            const float hardness = std::max(
                std::abs(Coerce(NamedInput(node, "Hardness"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0F);
            const float mask = std::clamp((1.0F - std::sqrt(x * x + y * y + z * z) / radius) * hardness, 0.0F, 1.0F);
            result = {.Data = {mask, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::RadialGradient:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto center = Coerce(NamedInput(node, "Center"), Keire::ShaderGraphValueType::Vector2).Data;
            const float radius = Coerce(NamedInput(node, "Radius"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float density = std::max(
                std::abs(Coerce(NamedInput(node, "Density"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0e-4F);
            const float x = uv.X - center.X;
            const float y = uv.Y - center.Y;
            const float gradient = std::clamp((radius - std::sqrt(x * x + y * y)) * density, 0.0F, 1.0F);
            result = {.Data = {gradient, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::LinearGradient:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto direction = Coerce(NamedInput(node, "Direction"), Keire::ShaderGraphValueType::Vector2).Data;
            const float offset = Coerce(NamedInput(node, "Offset"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float inverseLength =
                1.0F / std::max(std::sqrt(direction.X * direction.X + direction.Y * direction.Y), 1.0e-5F);
            const float gradient =
                std::clamp((uv.X * direction.X + uv.Y * direction.Y) * inverseLength + offset, 0.0F, 1.0F);
            result = {.Data = {gradient, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::Contrast:
        {
            const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
            const float contrast = Coerce(NamedInput(node, "Contrast"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float pivot = Coerce(NamedInput(node, "Pivot"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Data = {color.X * contrast + pivot * (1.0F - contrast),
                               color.Y * contrast + pivot * (1.0F - contrast),
                               color.Z * contrast + pivot * (1.0F - contrast), color.W},
                      .Type = Keire::ShaderGraphValueType::Color};
            break;
        }
        case Keire::ShaderGraphNodeKind::Saturation:
        {
            const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
            const float saturation = Coerce(NamedInput(node, "Saturation"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float luminance = color.X * 0.2126F + color.Y * 0.7152F + color.Z * 0.0722F;
            result = {.Data = {std::lerp(luminance, color.X, saturation), std::lerp(luminance, color.Y, saturation),
                               std::lerp(luminance, color.Z, saturation), color.W},
                      .Type = Keire::ShaderGraphValueType::Color};
            break;
        }
        case Keire::ShaderGraphNodeKind::BlendOverlay:
        {
            const auto base = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Color).Data;
            const auto blend = Coerce(NamedInput(node, "Blend"), Keire::ShaderGraphValueType::Color).Data;
            const float opacity =
                std::clamp(Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
            const auto overlay = [](const float first, const float second)
            { return first < 0.5F ? 2.0F * first * second : 1.0F - 2.0F * (1.0F - first) * (1.0F - second); };
            result = {.Data = {std::lerp(base.X, overlay(base.X, blend.X), opacity),
                               std::lerp(base.Y, overlay(base.Y, blend.Y), opacity),
                               std::lerp(base.Z, overlay(base.Z, blend.Z), opacity), base.W},
                      .Type = Keire::ShaderGraphValueType::Color};
            break;
        }
        case Keire::ShaderGraphNodeKind::Blackbody:
        {
            const float kelvin =
                std::clamp(Coerce(NamedInput(node, "Temperature"), Keire::ShaderGraphValueType::Scalar).Data.X, 1000.0F,
                           40000.0F) *
                0.01F;
            const float red =
                kelvin <= 66.0F ? 1.0F : std::clamp(1.29293619F * std::pow(kelvin - 60.0F, -0.133204759F), 0.0F, 1.0F);
            const float green =
                kelvin <= 66.0F ? std::clamp(0.390081579F * std::log(std::max(kelvin, 1.0F)) - 0.631841444F, 0.0F, 1.0F)
                                : std::clamp(1.12989086F * std::pow(kelvin - 60.0F, -0.0755148492F), 0.0F, 1.0F);
            const float blue =
                kelvin >= 66.0F ? 1.0F
                : kelvin <= 19.0F
                    ? 0.0F
                    : std::clamp(0.543206811F * std::log(std::max(kelvin - 10.0F, 1.0F)) - 1.19625409F, 0.0F, 1.0F);
            result = {.Data = {red, green, blue, 1.0F}, .Type = Keire::ShaderGraphValueType::Color};
            break;
        }
        case Keire::ShaderGraphNodeKind::Dither:
        {
            const float alpha =
                std::clamp(Coerce(NamedInput(node, "Alpha"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
            const auto screen = Coerce(NamedInput(node, "Screen Position"), Keire::ShaderGraphValueType::Vector2).Data;
            constexpr std::array thresholds{0.0F,    0.5F,    0.125F,  0.625F,  0.75F,   0.25F,   0.875F,  0.375F,
                                            0.1875F, 0.6875F, 0.0625F, 0.5625F, 0.9375F, 0.4375F, 0.8125F, 0.3125F};
            const auto x = static_cast<std::size_t>(std::abs(static_cast<int>(screen.X * 256.0F))) & 3U;
            const auto y = static_cast<std::size_t>(std::abs(static_cast<int>(screen.Y * 256.0F))) & 3U;
            result = {.Data = {alpha >= thresholds[y * 4U + x] ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::GradientNoise:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Data = {MaterialNoise({uv.X, uv.Y}, scale, 0.65F), 0.0F, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::Wave:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto direction = Coerce(NamedInput(node, "Direction"), Keire::ShaderGraphValueType::Vector2).Data;
            const float frequency = Coerce(NamedInput(node, "Frequency"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float phase = Coerce(NamedInput(node, "Phase"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float inverseLength =
                1.0F / std::max(std::sqrt(direction.X * direction.X + direction.Y * direction.Y), 1.0e-5F);
            const float wave =
                std::sin((uv.X * direction.X + uv.Y * direction.Y) * inverseLength * frequency * 6.28318530718F +
                         phase) *
                    0.5F +
                0.5F;
            result = {.Data = {wave, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        default:
            throw std::logic_error("Preview node dispatched to an incompatible procedural family.");
        }
        return result;
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
