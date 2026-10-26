#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorState.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::EvaluateSurfaceNode(const Keire::ShaderGraphNode& node,
                                                                             const Keire::ShaderGraphPin& outputPin)
    {
        PreviewGraphValue result;
        switch (node.Kind)
        {
        case Keire::ShaderGraphNodeKind::MakeMaterialAttributes:
        {
            PreviewMaterialSurface surface;
            surface.BaseColor = Coerce(NamedInput(node, "BaseColor"), Keire::ShaderGraphValueType::Color).Data;
            surface.Metallic = Coerce(NamedInput(node, "Metallic"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Roughness = Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Specular = Coerce(NamedInput(node, "Specular"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.ClearCoat = Coerce(NamedInput(node, "ClearCoat"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.ClearCoatRoughness =
                Coerce(NamedInput(node, "ClearCoatRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.SheenColor = Coerce(NamedInput(node, "SheenColor"), Keire::ShaderGraphValueType::Color).Data;
            surface.SheenRoughness =
                Coerce(NamedInput(node, "SheenRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
            surface.Normal = {normal.X, normal.Y, normal.Z};
            surface.Emission = Coerce(NamedInput(node, "Emission"), Keire::ShaderGraphValueType::Color).Data;
            surface.Occlusion = Coerce(NamedInput(node, "Occlusion"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Opacity = Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.SubsurfaceColor =
                Coerce(NamedInput(node, "SubsurfaceColor"), Keire::ShaderGraphValueType::Color).Data;
            surface.Subsurface = Coerce(NamedInput(node, "Subsurface"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Anisotropy = Coerce(NamedInput(node, "Anisotropy"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const auto tangent = Coerce(NamedInput(node, "Tangent"), Keire::ShaderGraphValueType::Vector3).Data;
            surface.Tangent = {tangent.X, tangent.Y, tangent.Z};
            surface.Transmission = Coerce(NamedInput(node, "Transmission"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.IndexOfRefraction =
                Coerce(NamedInput(node, "IndexOfRefraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Refraction = Coerce(NamedInput(node, "Refraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Thickness = Coerce(NamedInput(node, "Thickness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Type = Keire::ShaderGraphValueType::MaterialAttributes, .Surface = surface};
            break;
        }
        case Keire::ShaderGraphNodeKind::BreakMaterialAttributes:
        {
            const auto attributes =
                Coerce(NamedInput(node, "Attributes"), Keire::ShaderGraphValueType::MaterialAttributes);
            const auto surface = attributes.Surface.value_or(PreviewMaterialSurface{});
            result.Type = outputPin.Type;
            if (outputPin.Name == "BaseColor")
            {
                result.Data = surface.BaseColor;
            }
            else if (outputPin.Name == "Metallic")
            {
                result.Data.X = surface.Metallic;
            }
            else if (outputPin.Name == "Roughness")
            {
                result.Data.X = surface.Roughness;
            }
            else if (outputPin.Name == "Specular")
            {
                result.Data.X = surface.Specular;
            }
            else if (outputPin.Name == "ClearCoat")
            {
                result.Data.X = surface.ClearCoat;
            }
            else if (outputPin.Name == "ClearCoatRoughness")
            {
                result.Data.X = surface.ClearCoatRoughness;
            }
            else if (outputPin.Name == "SheenColor")
            {
                result.Data = surface.SheenColor;
            }
            else if (outputPin.Name == "SheenRoughness")
            {
                result.Data.X = surface.SheenRoughness;
            }
            else if (outputPin.Name == "Normal")
            {
                result.Data = {surface.Normal.X, surface.Normal.Y, surface.Normal.Z, 0.0F};
            }
            else if (outputPin.Name == "Emission")
            {
                result.Data = surface.Emission;
            }
            else if (outputPin.Name == "Occlusion")
            {
                result.Data.X = surface.Occlusion;
            }
            else if (outputPin.Name == "Opacity")
            {
                result.Data.X = surface.Opacity;
            }
            else if (outputPin.Name == "SubsurfaceColor")
            {
                result.Data = surface.SubsurfaceColor;
            }
            else if (outputPin.Name == "Subsurface")
            {
                result.Data.X = surface.Subsurface;
            }
            else if (outputPin.Name == "Anisotropy")
            {
                result.Data.X = surface.Anisotropy;
            }
            else if (outputPin.Name == "Tangent")
            {
                result.Data = {surface.Tangent.X, surface.Tangent.Y, surface.Tangent.Z, 0.0F};
            }
            else if (outputPin.Name == "Transmission")
            {
                result.Data.X = surface.Transmission;
            }
            else if (outputPin.Name == "IndexOfRefraction")
            {
                result.Data.X = surface.IndexOfRefraction;
            }
            else if (outputPin.Name == "Refraction")
            {
                result.Data.X = surface.Refraction;
            }
            else if (outputPin.Name == "Thickness")
            {
                result.Data.X = surface.Thickness;
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::BlendMaterialAttributes:
        {
            const auto first = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::MaterialAttributes)
                                   .Surface.value_or(PreviewMaterialSurface{});
            const auto second = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::MaterialAttributes)
                                    .Surface.value_or(PreviewMaterialSurface{});
            const float alpha =
                std::clamp(Coerce(NamedInput(node, "Alpha"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
            const auto blendVector4 = [alpha](const Keire::Vector4 a, const Keire::Vector4 b)
            {
                return Keire::Vector4{std::lerp(a.X, b.X, alpha), std::lerp(a.Y, b.Y, alpha),
                                      std::lerp(a.Z, b.Z, alpha), std::lerp(a.W, b.W, alpha)};
            };
            PreviewMaterialSurface surface;
            surface.BaseColor = blendVector4(first.BaseColor, second.BaseColor);
            surface.Metallic = std::lerp(first.Metallic, second.Metallic, alpha);
            surface.Roughness = std::lerp(first.Roughness, second.Roughness, alpha);
            surface.Specular = std::lerp(first.Specular, second.Specular, alpha);
            surface.ClearCoat = std::lerp(first.ClearCoat, second.ClearCoat, alpha);
            surface.ClearCoatRoughness = std::lerp(first.ClearCoatRoughness, second.ClearCoatRoughness, alpha);
            surface.SheenColor = blendVector4(first.SheenColor, second.SheenColor);
            surface.SheenRoughness = std::lerp(first.SheenRoughness, second.SheenRoughness, alpha);
            surface.Normal = Normalize({std::lerp(first.Normal.X, second.Normal.X, alpha),
                                        std::lerp(first.Normal.Y, second.Normal.Y, alpha),
                                        std::lerp(first.Normal.Z, second.Normal.Z, alpha)});
            surface.Emission = blendVector4(first.Emission, second.Emission);
            surface.Occlusion = std::lerp(first.Occlusion, second.Occlusion, alpha);
            surface.Opacity = std::lerp(first.Opacity, second.Opacity, alpha);
            surface.SubsurfaceColor = blendVector4(first.SubsurfaceColor, second.SubsurfaceColor);
            surface.Subsurface = std::lerp(first.Subsurface, second.Subsurface, alpha);
            surface.Anisotropy = std::lerp(first.Anisotropy, second.Anisotropy, alpha);
            surface.Tangent = Normalize({std::lerp(first.Tangent.X, second.Tangent.X, alpha),
                                         std::lerp(first.Tangent.Y, second.Tangent.Y, alpha),
                                         std::lerp(first.Tangent.Z, second.Tangent.Z, alpha)},
                                        {1.0F, 0.0F, 0.0F});
            surface.Transmission = std::lerp(first.Transmission, second.Transmission, alpha);
            surface.IndexOfRefraction = std::lerp(first.IndexOfRefraction, second.IndexOfRefraction, alpha);
            surface.Refraction = std::lerp(first.Refraction, second.Refraction, alpha);
            surface.Thickness = std::lerp(first.Thickness, second.Thickness, alpha);
            result = {.Type = Keire::ShaderGraphValueType::MaterialAttributes, .Surface = surface};
            break;
        }
        case Keire::ShaderGraphNodeKind::StandardSurfaceBsdf:
        {
            PreviewMaterialSurface surface;
            surface.BaseColor = Coerce(NamedInput(node, "BaseColor"), Keire::ShaderGraphValueType::Color).Data;
            surface.Metallic = Coerce(NamedInput(node, "Metallic"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Roughness = Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Specular = Coerce(NamedInput(node, "Specular"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
            surface.Normal = {normal.X, normal.Y, normal.Z};
            surface.Emission = Coerce(NamedInput(node, "Emission"), Keire::ShaderGraphValueType::Color).Data;
            surface.Opacity = Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
            break;
        }
        case Keire::ShaderGraphNodeKind::OpenPbrSurfaceBsdf:
        {
            PreviewMaterialSurface surface;
            surface.BaseColor = Coerce(NamedInput(node, "BaseColor"), Keire::ShaderGraphValueType::Color).Data;
            surface.Metallic = Coerce(NamedInput(node, "Metallic"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Roughness = Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Specular = Coerce(NamedInput(node, "SpecularWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.ClearCoat = Coerce(NamedInput(node, "CoatWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.ClearCoatRoughness =
                Coerce(NamedInput(node, "CoatRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const auto fuzz = Coerce(NamedInput(node, "FuzzColor"), Keire::ShaderGraphValueType::Color).Data;
            const float fuzzWeight = Coerce(NamedInput(node, "FuzzWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.SheenColor = {fuzz.X * fuzzWeight, fuzz.Y * fuzzWeight, fuzz.Z * fuzzWeight, fuzz.W};
            surface.SheenRoughness =
                Coerce(NamedInput(node, "FuzzRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
            surface.Normal = Normalize({normal.X, normal.Y, normal.Z});
            surface.Emission = Coerce(NamedInput(node, "Emission"), Keire::ShaderGraphValueType::Color).Data;
            surface.Occlusion = Coerce(NamedInput(node, "Occlusion"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Opacity = Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.SubsurfaceColor =
                Coerce(NamedInput(node, "SubsurfaceColor"), Keire::ShaderGraphValueType::Color).Data;
            surface.Subsurface =
                Coerce(NamedInput(node, "SubsurfaceWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Anisotropy = Coerce(NamedInput(node, "Anisotropy"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const auto tangent = Coerce(NamedInput(node, "Tangent"), Keire::ShaderGraphValueType::Vector3).Data;
            surface.Tangent = Normalize({tangent.X, tangent.Y, tangent.Z}, {1.0F, 0.0F, 0.0F});
            surface.Transmission =
                Coerce(NamedInput(node, "TransmissionWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.IndexOfRefraction =
                Coerce(NamedInput(node, "IndexOfRefraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Refraction = Coerce(NamedInput(node, "Refraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Thickness = Coerce(NamedInput(node, "Thickness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
            break;
        }
        case Keire::ShaderGraphNodeKind::MixSlabs:
        case Keire::ShaderGraphNodeKind::AddSlabs:
        {
            const auto first = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Bsdf)
                                   .Surface.value_or(PreviewMaterialSurface{});
            const auto second = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Bsdf)
                                    .Surface.value_or(PreviewMaterialSurface{});
            float factor = 0.0F;
            if (node.Kind == Keire::ShaderGraphNodeKind::MixSlabs)
            {
                factor = Coerce(NamedInput(node, "Factor"), Keire::ShaderGraphValueType::Scalar).Data.X;
            }
            else
            {
                const float firstWeight =
                    std::max(Coerce(NamedInput(node, "WeightA"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F);
                const float secondWeight =
                    std::max(Coerce(NamedInput(node, "WeightB"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F);
                const float total = firstWeight + secondWeight;
                factor = total > 1.0e-8F ? secondWeight / total : 0.0F;
            }
            result = {.Type = Keire::ShaderGraphValueType::Bsdf,
                      .Surface = BlendPreviewMaterialSurfaces(first, second, factor)};
            break;
        }
        case Keire::ShaderGraphNodeKind::CoatSlab:
        case Keire::ShaderGraphNodeKind::ClearCoatBsdf:
        {
            auto surface = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Bsdf)
                               .Surface.value_or(PreviewMaterialSurface{});
            surface.ClearCoat = Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.ClearCoatRoughness =
                Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
            break;
        }
        case Keire::ShaderGraphNodeKind::FuzzSlab:
        case Keire::ShaderGraphNodeKind::SheenBsdf:
        {
            auto surface = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Bsdf)
                               .Surface.value_or(PreviewMaterialSurface{});
            const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
            const float weight = Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.SheenColor = {color.X * weight, color.Y * weight, color.Z * weight, color.W};
            surface.SheenRoughness = Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
            break;
        }
        case Keire::ShaderGraphNodeKind::SubsurfaceBsdf:
        {
            auto surface = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Bsdf)
                               .Surface.value_or(PreviewMaterialSurface{});
            surface.SubsurfaceColor = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
            surface.Subsurface = Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
            break;
        }
        case Keire::ShaderGraphNodeKind::TransmissionBsdf:
        {
            auto surface = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Bsdf)
                               .Surface.value_or(PreviewMaterialSurface{});
            surface.Transmission = Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.IndexOfRefraction =
                Coerce(NamedInput(node, "IndexOfRefraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Refraction = Coerce(NamedInput(node, "Refraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
            surface.Thickness = Coerce(NamedInput(node, "Thickness"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
            break;
        }
        case Keire::ShaderGraphNodeKind::BsdfToMaterialAttributes:
        {
            result = Coerce(NamedInput(node, "BSDF"), Keire::ShaderGraphValueType::Bsdf);
            result.Type = Keire::ShaderGraphValueType::MaterialAttributes;
            break;
        }
        default:
            throw std::logic_error("Preview node dispatched to an incompatible surface family.");
        }
        return result;
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
