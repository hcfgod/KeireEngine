#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorInternal.h"

#include <algorithm>
#include <cmath>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    PreviewMaterialSurface BlendPreviewMaterialSurfaces(const PreviewMaterialSurface& first,
                                                        const PreviewMaterialSurface& second,
                                                        const float factor) noexcept
    {
        const float alpha = std::clamp(factor, 0.0F, 1.0F);
        const auto blendVector4 = [alpha](const Keire::Vector4 a, const Keire::Vector4 b)
        {
            return Keire::Vector4{std::lerp(a.X, b.X, alpha), std::lerp(a.Y, b.Y, alpha), std::lerp(a.Z, b.Z, alpha),
                                  std::lerp(a.W, b.W, alpha)};
        };
        PreviewMaterialSurface result;
        result.BaseColor = blendVector4(first.BaseColor, second.BaseColor);
        result.Metallic = std::lerp(first.Metallic, second.Metallic, alpha);
        result.Roughness = std::lerp(first.Roughness, second.Roughness, alpha);
        result.Specular = std::lerp(first.Specular, second.Specular, alpha);
        result.ClearCoat = std::lerp(first.ClearCoat, second.ClearCoat, alpha);
        result.ClearCoatRoughness = std::lerp(first.ClearCoatRoughness, second.ClearCoatRoughness, alpha);
        result.SheenColor = blendVector4(first.SheenColor, second.SheenColor);
        result.SheenRoughness = std::lerp(first.SheenRoughness, second.SheenRoughness, alpha);
        result.Normal = Normalize({std::lerp(first.Normal.X, second.Normal.X, alpha),
                                   std::lerp(first.Normal.Y, second.Normal.Y, alpha),
                                   std::lerp(first.Normal.Z, second.Normal.Z, alpha)});
        result.Emission = blendVector4(first.Emission, second.Emission);
        result.Occlusion = std::lerp(first.Occlusion, second.Occlusion, alpha);
        result.Opacity = std::lerp(first.Opacity, second.Opacity, alpha);
        result.SubsurfaceColor = blendVector4(first.SubsurfaceColor, second.SubsurfaceColor);
        result.Subsurface = std::lerp(first.Subsurface, second.Subsurface, alpha);
        result.Anisotropy = std::lerp(first.Anisotropy, second.Anisotropy, alpha);
        result.Tangent = Normalize({std::lerp(first.Tangent.X, second.Tangent.X, alpha),
                                    std::lerp(first.Tangent.Y, second.Tangent.Y, alpha),
                                    std::lerp(first.Tangent.Z, second.Tangent.Z, alpha)},
                                   {1.0F, 0.0F, 0.0F});
        result.Transmission = std::lerp(first.Transmission, second.Transmission, alpha);
        result.IndexOfRefraction = std::lerp(first.IndexOfRefraction, second.IndexOfRefraction, alpha);
        result.Refraction = std::lerp(first.Refraction, second.Refraction, alpha);
        result.Thickness = std::lerp(first.Thickness, second.Thickness, alpha);
        return result;
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
