#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorInternal.h"

#include "KeireClient/Editor/ShaderGraphPreviewTextureSampling.h"

#include <algorithm>
#include <string>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    using Detail::PreviewMaterial;

    namespace
    {
        [[nodiscard]] float Clamp01(const float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }
    } // namespace

    Detail::PreviewMaterial ShaderGraphPreviewEvaluator::Resolve(const Keire::Vector2 uv, const Keire::Vector3 normal,
                                                                 const Keire::Vector3 position)
    {
        PreviewMaterial result;
        result.Unlit = m_Request.Output == Keire::ShaderGraphOutput::Unlit ||
                       m_Request.Output == Keire::ShaderGraphOutput::Fullscreen;
        bool foundColor = false;
        for (const auto& property : m_Request.Properties)
        {
            const auto name = Detail::LowerShaderGraphPreviewText(property.Name);
            if (property.Type == Keire::ShaderPropertyType::Texture2D)
            {
                result.HasBaseTexture |= property.TextureSemantic == Keire::ShaderTextureSemantic::BaseColor;
                continue;
            }
            if ((name == "basecolor" || name == "color" || name == "tint" ||
                 (!foundColor && property.Type == Keire::ShaderPropertyType::Color)))
            {
                result.BaseColor = property.DefaultValue;
                foundColor = true;
            }
            else if (name == "metallic")
                result.Metallic = property.DefaultValue.X;
            else if (name == "roughness")
                result.Roughness = property.DefaultValue.X;
            else if (name == "specular")
                result.Specular = property.DefaultValue.X;
            else if (name == "clearcoat")
                result.ClearCoat = property.DefaultValue.X;
            else if (name == "clearcoatroughness")
                result.ClearCoatRoughness = property.DefaultValue.X;
            else if (name == "sheencolor")
                result.SheenColor = {property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z};
            else if (name == "sheenroughness")
                result.SheenRoughness = property.DefaultValue.X;
            else if (name == "opacity")
                result.Opacity = property.DefaultValue.X;
            else if (name == "emission" || name == "emissive" || name.find("emission") != std::string::npos ||
                     name.find("emissive") != std::string::npos)
                result.Emission = {property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z};
        }
        if (m_Impl)
        {
            result.HasBaseTexture = false;
            SetContext(uv, normal, position);
            if (const auto attributes = MasterAttributes())
            {
                result.BaseColor = attributes->BaseColor;
                result.Emission = {attributes->Emission.X, attributes->Emission.Y, attributes->Emission.Z};
                result.Metallic = attributes->Metallic;
                result.Roughness = attributes->Roughness;
                result.Specular = attributes->Specular;
                result.ClearCoat = attributes->ClearCoat;
                result.ClearCoatRoughness = attributes->ClearCoatRoughness;
                result.SheenColor = {attributes->SheenColor.X, attributes->SheenColor.Y, attributes->SheenColor.Z};
                result.SheenRoughness = attributes->SheenRoughness;
                result.Opacity = attributes->Opacity;
                result.Occlusion = attributes->Occlusion;
                result.Normal = attributes->Normal;
                result.HasNormal = true;
            }
            else
            {
                const auto colorName = result.Unlit ? "Color" : "BaseColor";
                if (const auto value = MasterInput(colorName, Keire::ShaderGraphValueType::Color))
                    result.BaseColor = *value;
                if (const auto value = MasterInput("Emission", Keire::ShaderGraphValueType::Color))
                    result.Emission = {value->X, value->Y, value->Z};
                if (const auto value = MasterInput("Metallic", Keire::ShaderGraphValueType::Scalar))
                    result.Metallic = value->X;
                if (const auto value = MasterInput("Roughness", Keire::ShaderGraphValueType::Scalar))
                    result.Roughness = value->X;
                if (const auto value = MasterInput("Specular", Keire::ShaderGraphValueType::Scalar))
                    result.Specular = value->X;
                if (const auto value = MasterInput("ClearCoat", Keire::ShaderGraphValueType::Scalar))
                    result.ClearCoat = value->X;
                if (const auto value = MasterInput("ClearCoatRoughness", Keire::ShaderGraphValueType::Scalar))
                    result.ClearCoatRoughness = value->X;
                if (const auto value = MasterInput("SheenColor", Keire::ShaderGraphValueType::Color))
                    result.SheenColor = {value->X, value->Y, value->Z};
                if (const auto value = MasterInput("SheenRoughness", Keire::ShaderGraphValueType::Scalar))
                    result.SheenRoughness = value->X;
                if (const auto value = MasterInput("Opacity", Keire::ShaderGraphValueType::Scalar))
                    result.Opacity = value->X;
                if (const auto value = MasterInput("Occlusion", Keire::ShaderGraphValueType::Scalar))
                    result.Occlusion = value->X;
                if (const auto value = MasterInput("Normal", Keire::ShaderGraphValueType::Vector3))
                {
                    result.Normal = {value->X, value->Y, value->Z};
                    result.HasNormal = true;
                }
            }
        }
        if (result.HasNormal)
        {
            // Material normals are tangent-space values, including the neutral (0, 0, 1) default.
            const auto n = Normalize(normal);
            const auto tangent = Normalize(Cross({0.0F, 1.0F, 0.0F}, n), {1.0F, 0.0F, 0.0F});
            const auto bitangent = Cross(n, tangent);
            const auto mapped = result.Normal;
            result.Normal = Normalize({tangent.X * mapped.X + bitangent.X * mapped.Y + n.X * mapped.Z,
                                       tangent.Y * mapped.X + bitangent.Y * mapped.Y + n.Y * mapped.Z,
                                       tangent.Z * mapped.X + bitangent.Z * mapped.Y + n.Z * mapped.Z},
                                      n);
        }
        result.BaseColor.X = Clamp01(result.BaseColor.X);
        result.BaseColor.Y = Clamp01(result.BaseColor.Y);
        result.BaseColor.Z = Clamp01(result.BaseColor.Z);
        result.BaseColor.W = Clamp01(result.BaseColor.W);
        result.Metallic = Clamp01(result.Metallic);
        result.Roughness = std::clamp(result.Roughness, 0.04F, 1.0F);
        result.Specular = Clamp01(result.Specular);
        result.ClearCoat = Clamp01(result.ClearCoat);
        result.ClearCoatRoughness = std::clamp(result.ClearCoatRoughness, 0.04F, 1.0F);
        result.SheenColor.X = Clamp01(result.SheenColor.X);
        result.SheenColor.Y = Clamp01(result.SheenColor.Y);
        result.SheenColor.Z = Clamp01(result.SheenColor.Z);
        result.SheenRoughness = std::clamp(result.SheenRoughness, 0.04F, 1.0F);
        result.Opacity = Clamp01(result.Opacity);
        result.Occlusion = Clamp01(result.Occlusion);
        return result;
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
