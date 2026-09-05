#include "KeireInternal/Rendering/ShaderGraphCompilerInternal.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    [[nodiscard]] std::string_view ShaderGraphSpatialLightingHlsl() noexcept;

    [[nodiscard]] std::string ShaderGraphCompiler::BuildHlsl()
    {
        ValidateIncludes();
        const auto master = std::ranges::find(m_Definition.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
        if (master == m_Definition.Nodes.end())
            throw std::invalid_argument("Shader Graph has no Shader Output node.");

        std::optional<std::string> worldPositionOffset;
        if (const auto* offsetPin = FindShaderGraphPin(*master, "WorldPositionOffset", ShaderGraphPinDirection::Input))
            if (const auto incoming = m_Incoming.find({master->Id, offsetPin->Id}); incoming != m_Incoming.end())
            {
                m_CurrentStage = ShaderGraphShaderStage::Vertex;
                m_Cache.clear();
                m_Visiting.clear();
                m_Preparing.clear();
                worldPositionOffset =
                    CoerceShaderGraphExpression(EvaluatePrepared(incoming->second), ShaderGraphValueType::Vector3).Code;
            }
        std::optional<std::string> previousWorldPositionOffset;
        if (worldPositionOffset)
        {
            previousWorldPositionOffset = *worldPositionOffset;
            const auto replaceAll = [&](const std::string_view from, const std::string_view to)
            {
                std::size_t offset = 0;
                while ((offset = previousWorldPositionOffset->find(from, offset)) != std::string::npos)
                {
                    previousWorldPositionOffset->replace(offset, from.size(), to);
                    offset += to.size();
                }
            };
            replaceAll("FrameParameters.x", "(FrameParameters.x - FrameParameters.y)");
            replaceAll("instance.NormalMatrix", "Model");
            replaceAll("instance.Model", "Model");
            replaceAll("input.Position", "input.PreviousPosition");
            replaceAll("world.xyz", "previousWorld.xyz");
        }
        m_CurrentStage = ShaderGraphShaderStage::Fragment;
        m_Cache.clear();
        m_Visiting.clear();
        m_Preparing.clear();

        const auto input = [&](const std::string_view name, const ShaderGraphValueType type)
        {
            const auto* pin = FindShaderGraphPin(*master, name, ShaderGraphPinDirection::Input);
            if (!pin)
                throw std::invalid_argument("Shader Output node is missing the " + std::string(name) + " input.");
            return CoerceShaderGraphExpression(Input(*master, *pin), type).Code;
        };
        const auto inputConnected = [&](const std::string_view name)
        {
            const auto* pin = FindShaderGraphPin(*master, name, ShaderGraphPinDirection::Input);
            return pin && m_Incoming.contains({master->Id, pin->Id});
        };
        const auto optionalInput =
            [&](const std::string_view name, const ShaderGraphValueType type, const std::string_view fallback)
        {
            const auto* pin = FindShaderGraphPin(*master, name, ShaderGraphPinDirection::Input);
            return pin ? CoerceShaderGraphExpression(Input(*master, *pin), type).Code : std::string(fallback);
        };

        const bool unlit = IsUnlitShaderGraphOutput(m_Definition.Output);
        const bool hasMaterialAttributes = !unlit && inputConnected("MaterialAttributes");
        const auto materialAttributes = hasMaterialAttributes
                                            ? input("MaterialAttributes", ShaderGraphValueType::MaterialAttributes)
                                            : std::string{};
        const auto attribute = [](const std::string_view field)
        { return "graphMaterialAttributes." + std::string(field); };
        const auto baseColor = hasMaterialAttributes
                                   ? attribute("BaseColor")
                                   : input(unlit ? "Color" : "BaseColor", ShaderGraphValueType::Color);
        const auto emission =
            hasMaterialAttributes ? attribute("Emission") : input("Emission", ShaderGraphValueType::Color);
        const auto opacity =
            hasMaterialAttributes ? attribute("Opacity") : input("Opacity", ShaderGraphValueType::Scalar);
        const auto metallic = unlit                   ? "0.0F"
                              : hasMaterialAttributes ? attribute("Metallic")
                                                      : input("Metallic", ShaderGraphValueType::Scalar);
        const auto roughness = unlit                   ? "1.0F"
                               : hasMaterialAttributes ? attribute("Roughness")
                                                       : input("Roughness", ShaderGraphValueType::Scalar);
        const auto specular = unlit                   ? "0.5F"
                              : hasMaterialAttributes ? attribute("Specular")
                                                      : optionalInput("Specular", ShaderGraphValueType::Scalar, "0.5F");
        const auto clearCoat = unlit ? "0.0F"
                               : hasMaterialAttributes
                                   ? attribute("ClearCoat")
                                   : optionalInput("ClearCoat", ShaderGraphValueType::Scalar, "0.0F");
        const auto clearCoatRoughness =
            unlit                   ? "0.25F"
            : hasMaterialAttributes ? attribute("ClearCoatRoughness")
                                    : optionalInput("ClearCoatRoughness", ShaderGraphValueType::Scalar, "0.25F");
        const auto sheenColor = unlit                   ? "float4(0.0F, 0.0F, 0.0F, 1.0F)"
                                : hasMaterialAttributes ? attribute("SheenColor")
                                                        : optionalInput("SheenColor", ShaderGraphValueType::Color,
                                                                        "float4(0.0F, 0.0F, 0.0F, 1.0F)");
        const auto sheenRoughness = unlit ? "0.5F"
                                    : hasMaterialAttributes
                                        ? attribute("SheenRoughness")
                                        : optionalInput("SheenRoughness", ShaderGraphValueType::Scalar, "0.5F");
        const auto normal = unlit || (!hasMaterialAttributes && !inputConnected("Normal")) ? "input.Normal"
                            : hasMaterialAttributes ? attribute("Normal")
                                                    : input("Normal", ShaderGraphValueType::Vector3);
        const bool hasDetailNormal = !unlit && !hasMaterialAttributes && inputConnected("DetailNormal");
        const auto detailNormal =
            hasDetailNormal ? input("DetailNormal", ShaderGraphValueType::Vector3) : std::string("input.Normal");
        const auto occlusion = unlit                   ? "1.0F"
                               : hasMaterialAttributes ? attribute("Occlusion")
                                                       : input("Occlusion", ShaderGraphValueType::Scalar);
        const auto subsurfaceColor =
            unlit ? "float4(1.0F, 0.35F, 0.25F, 1.0F)"
            : hasMaterialAttributes
                ? attribute("SubsurfaceColor")
                : optionalInput("SubsurfaceColor", ShaderGraphValueType::Color, "float4(1.0F, 0.35F, 0.25F, 1.0F)");
        const auto subsurface = unlit ? "0.0F"
                                : hasMaterialAttributes
                                    ? attribute("Subsurface")
                                    : optionalInput("Subsurface", ShaderGraphValueType::Scalar, "0.0F");
        const auto anisotropy = unlit ? "0.0F"
                                : hasMaterialAttributes
                                    ? attribute("Anisotropy")
                                    : optionalInput("Anisotropy", ShaderGraphValueType::Scalar, "0.0F");
        const auto tangent = unlit ? "input.Tangent"
                             : hasMaterialAttributes
                                 ? attribute("Tangent")
                                 : optionalInput("Tangent", ShaderGraphValueType::Vector3, "input.Tangent");
        const auto transmission = unlit ? "0.0F"
                                  : hasMaterialAttributes
                                      ? attribute("Transmission")
                                      : optionalInput("Transmission", ShaderGraphValueType::Scalar, "0.0F");
        const auto indexOfRefraction = unlit ? "1.5F"
                                       : hasMaterialAttributes
                                           ? attribute("IndexOfRefraction")
                                           : optionalInput("IndexOfRefraction", ShaderGraphValueType::Scalar, "1.5F");
        const auto refraction = unlit ? "0.0F"
                                : hasMaterialAttributes
                                    ? attribute("Refraction")
                                    : optionalInput("Refraction", ShaderGraphValueType::Scalar, "0.0F");
        const auto thickness = unlit ? "1.0F"
                               : hasMaterialAttributes
                                   ? attribute("Thickness")
                                   : optionalInput("Thickness", ShaderGraphValueType::Scalar, "1.0F");
        const bool hasPixelDepthOffset = inputConnected("PixelDepthOffset");
        const auto pixelDepthOffset =
            hasPixelDepthOffset ? input("PixelDepthOffset", ShaderGraphValueType::Scalar) : std::string("0.0F");
        m_MaximumWorldPositionDisplacementRadius =
            worldPositionOffset && m_Definition.MaximumWorldPositionDisplacementRadius <= 0.0F
                ? std::optional<float>{}
                : std::optional<float>{worldPositionOffset ? m_Definition.MaximumWorldPositionDisplacementRadius
                                                           : 0.0F};
        if (worldPositionOffset)
        {
            m_OcclusionSupport =
                m_MaximumWorldPositionDisplacementRadius && *m_MaximumWorldPositionDisplacementRadius > 0.0F
                    ? ShaderOcclusionSupport::ConservativeBounds
                    : ShaderOcclusionSupport::None;
        }
        else if (hasPixelDepthOffset)
            m_OcclusionSupport = ShaderOcclusionSupport::ConservativeBounds;
        else
        {
            m_OcclusionSupport =
                ShaderOcclusionSupport::ConservativeBounds | ShaderOcclusionSupport::DepthOnlyGeometryMatch;
        }

        std::ostringstream source;
        source << "// Generated by Keire Shader Graph. Do not edit. Generator version "
               << ShaderGraphGeneratedShaderVersion << ", source schema " << m_Definition.SchemaVersion << ".\n";
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
#if defined(KEIRE_PASS_DEPTH_VELOCITY)
    float3 PreviousPosition : TEXCOORD6;
#endif
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
#if defined(KEIRE_PASS_DEPTH_VELOCITY)
    float4 CurrentClipPosition : TEXCOORD10;
    float4 PreviousClipPosition : TEXCOORD11;
#endif
    float4 Position : SV_Position;
};

cbuffer ObjectData : register(b0, space1)
{
    float4x4 Model;
    float4x4 View;
    float4x4 Projection;
    float4x4 NormalMatrix;
};

cbuffer InstanceAddressingData : register(b2, space1)
{
    uint4 InstanceParameters;
};

)HLSL";
        if (m_UsesVertexMaterialParameters)
        {
            source << "cbuffer VertexMaterialData : register(b1, space1)\n{\n";
            for (const auto& property : m_Properties)
                if (property.Type != ShaderPropertyType::Texture2D)
                    source << "    float4 " << ShaderGraphVertexPropertySymbol(property.Name) << ";\n";
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

struct ShaderGraphLocalLight
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
    ShaderGraphLocalLight LocalLights[62];
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
            const auto symbol = ShaderGraphPropertySymbol(property.Name);
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
        std::uint32_t textureIndex = 0;
        for (const auto& property : m_Properties)
        {
            if (property.Type != ShaderPropertyType::Texture2D)
                continue;
            const auto symbol = ShaderGraphPropertySymbol(property.Name);
            source << "Texture2D " << symbol << " : register(t" << textureIndex << ", space2);\n";
            source << "SamplerState " << symbol << "Sampler : register(s" << textureIndex << ", space2);\n";
            ++textureIndex;
        }
        const auto resourceDeclarations =
            GenerateShaderGraphResourceDeclarations(m_Definition.Resources, textureIndex, textureIndex);
        source << resourceDeclarations.Hlsl;
        textureIndex = std::max(resourceDeclarations.NextTextureRegister, resourceDeclarations.NextSamplerRegister);
        if (!unlit)
        {
            source << "Texture2DArray<float> DirectionalShadowTexture : register(t" << textureIndex << ", space2);\n";
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
    float4 LocalShadowSampleBounds[20];
};

struct ShaderGraphReflectionProbe
{
    float4x4 WorldToLocal;
    float4x4 LocalToWorld;
    float4 ExtentsWeight;
    float4 Parameters;
};

cbuffer EnvironmentData : register(b3, space3)
{
    float4 DiffuseIrradiance[9];
    float4 EnvironmentParameters;
    float4 EnvironmentEncoding;
    float4 LightmapScaleOffset;
    float4 LightmapParameters;
    float4 ShadowMaskParameters;
    float4 ProbeIrradiance[9];
    ShaderGraphReflectionProbe ReflectionProbes[2];
    float4 CookieTransforms[8];
    float4 CookieRotations[2];
    float4 DirectionalCookieAndContact;
    float4x4 SpatialViewProjection;
    uint4 SpatialSelection;
};
)HLSL";
            source << "Texture2DArray<float4> LightmapTexture : register(t" << textureIndex << ", space2);\n";
            source << "SamplerState LightmapSampler : register(s" << textureIndex++ << ", space2);\n";
            source << "Texture2DArray<float4> LightmapDirectionalityTexture : register(t" << textureIndex
                   << ", space2);\n";
            source << "SamplerState LightmapDirectionalitySampler : register(s" << textureIndex++ << ", space2);\n";
            source << "Texture2DArray<float4> ShadowMaskTexture : register(t" << textureIndex << ", space2);\n";
            source << "SamplerState ShadowMaskSampler : register(s" << textureIndex++ << ", space2);\n";
            source << "TextureCubeArray<float4> ReflectionProbeTexture : register(t" << textureIndex << ", space2);\n";
            source << "SamplerState ReflectionProbeSampler : register(s" << textureIndex++ << ", space2);\n";
            source << "Texture2D<float4> CookieAtlasTexture : register(t" << textureIndex << ", space2);\n";
            source << "SamplerState CookieAtlasSampler : register(s" << textureIndex++ << ", space2);\n";
            source << "StructuredBuffer<ShaderGraphLocalLight> ForwardPlusLights : register(t" << textureIndex++
                   << ", space2);\n";
            source << "StructuredBuffer<uint4> ForwardPlusTiles : register(t" << textureIndex++ << ", space2);\n";
            source << "StructuredBuffer<uint4> ForwardPlusLightIndices : register(t" << textureIndex++
                   << ", space2);\n";
            source << R"HLSL(
struct ShaderGraphSpatialSelectionRecord
{
    float4 ProbeIrradiance[9];
    ShaderGraphReflectionProbe ReflectionProbes[2];
    uint4 Metadata;
};
)HLSL";
            source << "StructuredBuffer<ShaderGraphSpatialSelectionRecord> SpatialSelectionRecords : register(t"
                   << textureIndex++ << ", space2);\n";
        }
        source << R"HLSL(
struct ShaderGraphSurface
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

struct ShaderGraphBsdf
{
    ShaderGraphSurface Surface;
};

static const float Pi = 3.14159265359F;

float3 SafeNormalize(const float3 value, const float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-12F && all(isfinite(value)) ? value * rsqrt(lengthSquared) : fallback;
}

ShaderGraphSurface DefaultShaderGraphSurface()
{
    ShaderGraphSurface result;
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

ShaderGraphSurface MakeShaderGraphSurface(
    const float4 baseColor, const float metallic, const float roughness, const float specular, const float clearCoat,
    const float clearCoatRoughness, const float4 sheenColor, const float sheenRoughness, const float3 normal,
    const float4 emission, const float occlusion, const float opacity, const float4 subsurfaceColor,
    const float subsurface, const float anisotropy, const float3 tangent, const float transmission,
    const float indexOfRefraction, const float refraction, const float thickness)
{
    ShaderGraphSurface result;
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

ShaderGraphSurface BlendShaderGraphSurfaces(const ShaderGraphSurface first, const ShaderGraphSurface second,
                                        const float alpha)
{
    const float factor = saturate(alpha);
    ShaderGraphSurface result;
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

ShaderGraphBsdf DefaultShaderGraphBsdf()
{
    ShaderGraphBsdf result;
    result.Surface = DefaultShaderGraphSurface();
    return result;
}

ShaderGraphBsdf MakeStandardShaderGraphBsdf(const float4 baseColor, const float metallic, const float roughness,
                                        const float specular, const float3 normal, const float4 emission,
                                        const float opacity)
{
    ShaderGraphBsdf result = DefaultShaderGraphBsdf();
    result.Surface.BaseColor = baseColor;
    result.Surface.Metallic = metallic;
    result.Surface.Roughness = roughness;
    result.Surface.Specular = specular;
    result.Surface.Normal = normal;
    result.Surface.Emission = emission;
    result.Surface.Opacity = opacity;
    return result;
}
)HLSL";
        if (!unlit)
            source << R"HLSL(

ShaderGraphBsdf MakeOpenPbrShaderGraphBsdf(
    const float4 baseColor, const float metallic, const float roughness, const float specularWeight,
    const float coatWeight, const float coatRoughness, const float4 fuzzColor, const float fuzzWeight,
    const float fuzzRoughness,
    const float3 normal, const float4 emission, const float occlusion, const float opacity,
    const float4 subsurfaceColor, const float subsurfaceWeight, const float anisotropy, const float3 tangent,
    const float transmissionWeight, const float indexOfRefraction, const float refraction, const float thickness)
{
    ShaderGraphBsdf result;
    result.Surface = MakeShaderGraphSurface(
        baseColor, saturate(metallic), saturate(roughness), saturate(specularWeight), saturate(coatWeight),
        saturate(coatRoughness), float4(fuzzColor.rgb * saturate(fuzzWeight), fuzzColor.a), saturate(fuzzRoughness),
        SafeNormalize(normal, float3(0.0F, 0.0F, 1.0F)),
        emission, saturate(occlusion), saturate(opacity), subsurfaceColor, saturate(subsurfaceWeight),
        clamp(anisotropy, -1.0F, 1.0F), SafeNormalize(tangent, float3(1.0F, 0.0F, 0.0F)),
        saturate(transmissionWeight), max(indexOfRefraction, 1.0F), refraction, max(thickness, 0.0F));
    return result;
}

ShaderGraphBsdf MixShaderGraphSlabs(const ShaderGraphBsdf first, const ShaderGraphBsdf second, const float factor)
{
    ShaderGraphBsdf result;
    result.Surface = BlendShaderGraphSurfaces(first.Surface, second.Surface, saturate(factor));
    return result;
}

ShaderGraphBsdf AddShaderGraphSlabs(const ShaderGraphBsdf first, const ShaderGraphBsdf second,
                                    const float firstWeight, const float secondWeight)
{
    const float boundedFirst = max(firstWeight, 0.0F);
    const float boundedSecond = max(secondWeight, 0.0F);
    const float total = boundedFirst + boundedSecond;
    if (total > 1.0e-8F)
        return MixShaderGraphSlabs(first, second, boundedSecond / total);
    return DefaultShaderGraphBsdf();
}

ShaderGraphBsdf ApplyShaderGraphClearCoat(ShaderGraphBsdf result, const float weight, const float roughness)
{
    result.Surface.ClearCoat = weight;
    result.Surface.ClearCoatRoughness = roughness;
    return result;
}

ShaderGraphBsdf ApplyShaderGraphSheen(ShaderGraphBsdf result, const float4 color, const float weight,
                                 const float roughness)
{
    result.Surface.SheenColor = float4(color.rgb * weight, color.a);
    result.Surface.SheenRoughness = roughness;
    return result;
}

ShaderGraphBsdf ApplyShaderGraphSubsurface(ShaderGraphBsdf result, const float4 color, const float weight)
{
    result.Surface.SubsurfaceColor = color;
    result.Surface.Subsurface = weight;
    return result;
}

ShaderGraphBsdf ApplyShaderGraphTransmission(ShaderGraphBsdf result, const float weight,
                                         const float indexOfRefraction, const float refraction,
                                         const float thickness)
{
    result.Surface.Transmission = weight;
    result.Surface.IndexOfRefraction = indexOfRefraction;
    result.Surface.Refraction = refraction;
    result.Surface.Thickness = thickness;
    return result;
}

ShaderGraphSurface ShaderGraphSurfaceFromBsdf(const ShaderGraphBsdf value)
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
              const float layer, const float depth, const float inverseResolution, const bool soft,
              const float4 sampleBounds, const bool clampSamples)
{
    if (any(uv < sampleBounds.xy) || any(uv > sampleBounds.zw) || depth <= 0.0F || depth >= 1.0F)
return 1.0F;
    if (!soft)
return depth <= textureValue.SampleLevel(samplerValue, float3(uv, layer), 0.0F) ? 1.0F : 0.0F;
    float visibility = 0.0F;
    float totalWeight = 0.0F;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
[unroll]
for (int x = -1; x <= 1; ++x)
{
    const float weight = (2.0F - abs((float)x)) * (2.0F - abs((float)y));
    const float2 unclampedUv = uv + float2(x, y) * inverseResolution;
    if (!clampSamples &&
        (any(unclampedUv < sampleBounds.xy) || any(unclampedUv > sampleBounds.zw)))
    {
        visibility += weight;
    }
    else
    {
        const float2 sampleUv = clamp(unclampedUv, sampleBounds.xy, sampleBounds.zw);
        const float storedDepth =
            textureValue.SampleLevel(samplerValue, float3(sampleUv, layer), 0.0F);
        visibility += depth <= storedDepth ? weight : 0.0F;
    }
    totalWeight += weight;
}
    }
    return visibility / max(totalWeight, 1.0F);
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
                                     DirectionalShadowParameters.w, DirectionalShadowParameters.x > 0.0F,
                                     float4(0.0F, 0.0F, 1.0F, 1.0F), false);
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
                                     LocalShadowParameters[lightIndex].z > 0.5F,
                                     LocalShadowSampleBounds[matrixIndex], true);
    return lerp(1.0F, visibility, saturate(LocalShadowParameters[lightIndex].y));
}
)HLSL";
        if (!unlit)
            source << R"HLSL(

float3 SampleEnvironment(float3 direction, const float level);
float3 EvaluateDiffuseEnvironment(float3 normal);
float3 DecodeRgbe(const float4 sampleValue);

)HLSL";
        if (!unlit)
            source << ShaderGraphSpatialLightingHlsl();
        if (!unlit)
            source << R"HLSL(
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
    const InstanceData instance = Instances[InstanceParameters.x + instanceId];
    float4 world = mul(instance.Model, float4(input.Position, 1.0F));
)HLSL";
        if (worldPositionOffset)
            source << "    world.xyz += " << *worldPositionOffset << ";\n";
        source << R"HLSL(
#if defined(KEIRE_PASS_DEPTH_VELOCITY)
    float4 previousWorld = mul(Model, float4(input.PreviousPosition, 1.0F));
)HLSL";
        if (previousWorldPositionOffset)
            source << "    previousWorld.xyz += " << *previousWorldPositionOffset << ";\n";
        source << R"HLSL(
#endif
    const float4 viewPosition = mul(View, world);
    output.Position = mul(Projection, viewPosition);
#if defined(KEIRE_PASS_DEPTH_VELOCITY)
    output.CurrentClipPosition = output.Position;
    output.PreviousClipPosition = mul(NormalMatrix, previousWorld);
#endif
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
        source << R"HLSL(

#if defined(KEIRE_PASS_DEFERRED_GBUFFER_STANDARD)
struct ShaderGraphFragmentOutput
{
    float4 BaseColorMetallic : SV_Target0;
    float4 NormalRoughness : SV_Target1;
    float4 Material : SV_Target2;
    float4 Lighting : SV_Target3;
)HLSL";
        if (hasPixelDepthOffset)
            source << "    float Depth : SV_Depth;\n";
        source << R"HLSL(};
#elif defined(KEIRE_PASS_DEPTH_VELOCITY)
struct ShaderGraphFragmentOutput
{
    float2 Velocity : SV_Target0;
)HLSL";
        if (hasPixelDepthOffset)
            source << "    float Depth : SV_Depth;\n";
        source << R"HLSL(};
#elif defined(KEIRE_PASS_DECAL_DBUFFER)
struct ShaderGraphFragmentOutput
{
    float4 BaseColor : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Material : SV_Target2;
)HLSL";
        if (hasPixelDepthOffset)
            source << "    float Depth : SV_Depth;\n";
        source << R"HLSL(};
#else
struct ShaderGraphFragmentOutput
{
    float4 Color : SV_Target0;
)HLSL";
        if (hasPixelDepthOffset)
            source << "    float Depth : SV_Depth;\n";
        source << R"HLSL(};
#endif

ShaderGraphFragmentOutput PSMain(VertexOutput input)
{
)HLSL";
        if (hasMaterialAttributes)
            source << "    const ShaderGraphSurface graphMaterialAttributes = " << materialAttributes << ";\n";
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
            source << "    const float graphClearCoatRoughness = clamp(" << clearCoatRoughness << ", 0.04F, 1.0F);\n";
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
                source << "    const float3 graphNormal = SafeNormalize(BlendDetailNormal(SafeNormalize(" << normal
                       << ", input.Normal), SafeNormalize(" << detailNormal
                       << ", input.Normal), 1.0F), input.Normal);\n";
            else
                source << "    const float3 graphNormal = SafeNormalize(" << normal << ", input.Normal);\n";
            source << "    const float ao = saturate(" << occlusion << ");\n";
            source << R"HLSL(    const float3 viewDirection = SafeNormalize(input.ViewDirection, graphNormal);
    const float3 lightDirection =
SafeNormalize(-DirectionalDirectionExposure.xyz, float3(0.0F, 1.0F, 0.0F));
    const float2 lightmapUv = input.UV1 * LightmapScaleOffset.xy + LightmapScaleOffset.zw;
    float3 directLighting = EvaluateGraphDirectLighting(
graphNormal, graphTangent, viewDirection, lightDirection,
DirectionalColorIntensity.rgb * DirectionalColorIntensity.a, graphBaseColor.rgb, graphMetallic,
graphRoughness, graphAnisotropy, graphSpecular, graphClearCoat, graphClearCoatRoughness, graphSheenColor,
graphSheenRoughness, graphSubsurfaceColor, graphSubsurface, graphTransmission);
    directLighting *= min(EvaluateDirectionalShadow(input.WorldPosition, input.ViewDepth),
                          SampleSpatialMixedVisibility(lightmapUv, LightmapParameters.w)) *
                      EvaluateDirectionalSpatialCookie(input.WorldPosition);
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
const ShaderGraphLocalLight light = ForwardPlusLights[lightIndex];
const uint lightContribution = (uint)max(light.Parameters.w, 0.0F) >> 5U;
if (lightContribution != SpatialSelection.y + 1U)
    continue;
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
float visibility = lightIndex < 62U ? EvaluateLocalShadow(lightIndex, input.WorldPosition) : 1.0F;
visibility = min(visibility, SampleSpatialMixedVisibility(lightmapUv, light.Parameters.z)) *
              EvaluateLocalSpatialCookie(light, input.WorldPosition);
const float3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.a * attenuation * visibility;
directLighting += EvaluateGraphDirectLighting(
    graphNormal, graphTangent, viewDirection, localDirection, radiance, graphBaseColor.rgb, graphMetallic,
    graphRoughness, graphAnisotropy, graphSpecular, graphClearCoat, graphClearCoatRoughness, graphSheenColor,
    graphSheenRoughness, graphSubsurfaceColor, graphSubsurface, graphTransmission);
    }
    const float noV = saturate(dot(graphNormal, viewDirection));
    const float3 f0 = lerp((0.08F * graphSpecular).xxx, graphBaseColor.rgb, graphMetallic);
    const float3 diffuseEnvironment =
EvaluateSpatialProbeDiffuse(graphNormal, lightmapUv) * graphBaseColor.rgb * (1.0F - graphMetallic) *
(1.0F - graphTransmission) / Pi;
    const float3 reflectionDirection = reflect(-viewDirection, graphNormal);
    const float3 reflectionRadiance =
SampleSpatialReflection(input.WorldPosition, reflectionDirection, graphRoughness);
    const float3 refractionDirection = refract(-viewDirection, graphNormal, rcp(graphIor));
    const float3 refractionRadiance =
SampleEnvironment(refractionDirection, graphRoughness * EnvironmentParameters.w);
    const float3 absorption = exp(-max(1.0F - graphBaseColor.rgb, 0.0F.xxx) * graphThickness);
    const float3 transmittedEnvironment = refractionRadiance * absorption * graphTransmission;
    const float2 integratedBrdf =
ApproximateSpatialIntegratedBrdf(noV, graphRoughness);
    const float3 reflectedEnvironment =
reflectionRadiance *
(FresnelSchlickRoughness(noV, f0, graphRoughness) * integratedBrdf.x + integratedBrdf.y);
    const float3 specularEnvironment =
lerp(reflectedEnvironment, refractionRadiance, graphRefraction * (1.0F - graphMetallic));
    const float3 flatAmbient =
graphBaseColor.rgb * (1.0F - graphMetallic) * AmbientColorIntensity.rgb * AmbientColorIntensity.a / Pi;
    const float3 ambientLighting =
(flatAmbient * (1.0F - graphTransmission) + diffuseEnvironment + specularEnvironment +
 transmittedEnvironment * EnvironmentParameters.y) * ao;
    float3 graphColor =
(ambientLighting + directLighting + graphEmission) * DirectionalDirectionExposure.w;
)HLSL";
        }
        source << "    // Keep the fixed interpolator ABI dense for DXIL PSO validation on D3D12.\n";
        source << "    if (!all(isfinite(float4(input.Normal, input.ViewDirection.x))) ||\n";
        source << "        !all(isfinite(float4(input.ViewDirection.yz, input.Tangent.xy))) ||\n";
        source << "        !all(isfinite(float4(input.UV0, input.UV1))) ||\n";
        source << "        !all(isfinite(float4(input.Color.zw, input.WorldPosition.xy))) ||\n";
        source << "        !all(isfinite(float4(input.WorldPosition.z, input.ObjectPosition))) ||\n";
        source << "        !isfinite(input.ViewDepth))\n";
        source << "        graphColor += input.Normal + input.Tangent + input.Bitangent + input.ViewDirection + "
                  "input.Color.xyz + input.WorldPosition + input.ObjectPosition + "
                  "float3(input.UV0 + input.UV1, 0.0F);\n";
        source << "    if (!all(isfinite(" << materialBindingSentinel << ")))\n";
        source << "        graphColor += " << materialBindingSentinel << ".xyz;\n";
        if (!unlit)
        {
            source << R"HLSL(
    // Every lit permutation uses the same fixed binding ABI, including permutations whose constants eliminate a lobe.
    if (!all(isfinite(ShadowMaskParameters)) || !all(isfinite(EnvironmentParameters)))
    {
        const float2 retentionUv = frac(input.UV0);
        const float retentionShadow =
            DirectionalShadowTexture.SampleLevel(DirectionalShadowSampler, float3(retentionUv, 0.0F), 0.0F) +
            LocalShadowTexture.SampleLevel(LocalShadowSampler, float3(retentionUv, 0.0F), 0.0F);
        const float4 retainedLightmap =
            LightmapTexture.SampleLevel(LightmapSampler, float3(retentionUv, 0.0F), 0.0F);
        const float4 retainedDirectionality =
            LightmapDirectionalityTexture.SampleLevel(LightmapDirectionalitySampler, float3(retentionUv, 0.0F), 0.0F);
        const float4 retainedMask =
            ShadowMaskTexture.SampleLevel(ShadowMaskSampler, float3(retentionUv, 0.0F), 0.0F);
        const float4 retainedReflection = ReflectionProbeTexture.SampleLevel(
            ReflectionProbeSampler, float4(float3(0.0F, 0.0F, 1.0F), 0.0F), 0.0F);
        graphColor += retentionShadow.xxx +
                      EnvironmentTexture.SampleLevel(EnvironmentSampler, retentionUv, 0.0F).rgb +
                      BrdfIntegrationLut.SampleLevel(BrdfIntegrationSampler, retentionUv, 0.0F).rrr +
                      retainedLightmap.rgb + retainedDirectionality.rgb + retainedMask.rgb +
                      retainedReflection.rgb +
                      CookieAtlasTexture.SampleLevel(CookieAtlasSampler, retentionUv, 0.0F).rgb;
    }
)HLSL";
        }
        source << "    const float alpha = saturate(graphBaseColor.a * graphOpacity);\n";
        source << "    if (SurfaceParameters.y > 0.5F && SurfaceParameters.y < 1.5F)\n";
        source << "        clip(alpha - SurfaceParameters.x);\n";
        source << "    const float graphAbiRetention =\n";
        source << "        all(isfinite(float4(graphColor, alpha))) ? 0.0F : graphColor.x;\n";
        const bool premultiplied =
            m_Definition.Output == ShaderGraphOutput::Transparent || m_Definition.Output == ShaderGraphOutput::Decal;
        source << "#if defined(KEIRE_PASS_DEFERRED_GBUFFER_STANDARD)\n";
        source << "    ShaderGraphFragmentOutput output;\n";
        source << "    output.BaseColorMetallic = float4(graphBaseColor.rgb, " << (unlit ? "0.0F" : "graphMetallic")
               << ");\n";
        source << "    output.BaseColorMetallic.rgb += graphAbiRetention.xxx;\n";
        source << "    output.NormalRoughness = float4(SafeNormalize(" << (unlit ? "input.Normal" : "graphNormal")
               << ", input.Normal) * 0.5F + 0.5F, " << (unlit ? "1.0F" : "graphRoughness") << ");\n";
        source << "    output.Material = float4(" << (unlit ? "1.0F" : "ao") << ", "
               << (unlit ? "0.5F" : "graphSpecular") << ", " << (unlit ? "1.0F" : "0.0F")
               << ", SurfaceParameters.z > 0.5F ? 1.0F : 0.75F);\n";
        if (unlit)
        {
            source << "    output.Lighting = float4(0.0F, 0.0F, 0.0F, FrameParameters.w * 65536.0F);\n";
        }
        else
        {
            source << "    const uint lightmapLayer = ((uint)LightmapParameters.z & 1U) != 0U && "
                      "LightmapParameters.x < 4095.0F ? (uint)LightmapParameters.x + 1U : 0U;\n";
            source << "    const uint shadowMaskLayer = ((uint)LightmapParameters.z & 1U) != 0U && "
                      "LightmapParameters.y < 4095.0F ? (uint)LightmapParameters.y + 1U : 0U;\n";
            source << "    const uint spatialRecord = SpatialSelection.x != 0xffffffffU && "
                      "SpatialSelection.x < 65535U ? SpatialSelection.x + 1U : 0U;\n";
            source << "    const uint contribution = SpatialSelection.y < 255U ? SpatialSelection.y + 1U : 0U;\n";
            source << "    const float2 payloadLightmapUv = "
                      "input.UV1 * LightmapScaleOffset.xy + LightmapScaleOffset.zw;\n";
            source << "    output.Lighting = float4(payloadLightmapUv, "
                      "(float)(lightmapLayer + shadowMaskLayer * 4096U), "
                      "(float)(spatialRecord + contribution * 65536U));\n";
        }
        source << "#elif defined(KEIRE_PASS_DEPTH_VELOCITY)\n";
        source << "    ShaderGraphFragmentOutput output;\n";
        source << "    const float2 currentNdc = input.CurrentClipPosition.xy / "
                  "max(abs(input.CurrentClipPosition.w), 1.0e-6F);\n";
        source << "    const float2 previousNdc = input.PreviousClipPosition.xy / "
                  "max(abs(input.PreviousClipPosition.w), 1.0e-6F);\n";
        source << "    output.Velocity = (currentNdc - previousNdc) * float2(0.5F, -0.5F);\n";
        source << "    output.Velocity.x += graphAbiRetention;\n";
        source << "#elif defined(KEIRE_PASS_DECAL_DBUFFER)\n";
        source << "    ShaderGraphFragmentOutput output;\n";
        source << "    output.BaseColor = float4(graphBaseColor.rgb, alpha);\n";
        source << "    output.BaseColor.rgb += graphAbiRetention.xxx;\n";
        source << "    output.Normal = float4(SafeNormalize(" << (unlit ? "input.Normal" : "graphNormal")
               << ", input.Normal) * 0.5F + 0.5F, alpha);\n";
        source << "    output.Material = float4(" << (unlit ? "0.0F" : "graphMetallic") << ", "
               << (unlit ? "1.0F" : "graphRoughness") << ", " << (unlit ? "0.5F" : "graphSpecular") << ", alpha);\n";
        source << "#else\n";
        source << "    ShaderGraphFragmentOutput output;\n";
        source << "    output.Color = float4(" << (premultiplied ? "graphColor * alpha, alpha" : "graphColor, alpha")
               << ");\n";
        source << "#endif\n";
        if (hasPixelDepthOffset)
            source << "    output.Depth = saturate(input.Position.z + (" << pixelDepthOffset
                   << ") * max(fwidth(input.Position.z), 1.0e-7F));\n";
        source << "    return output;\n";
        source << "}\n";
        return source.str();
    }
} // namespace Keire::Detail
