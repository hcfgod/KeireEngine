#pragma once

Texture2DArray<float> FogTerrainDepth : register(KEIRE_FOG_SHADOW_TEXTURE, space2);
SamplerState FogTerrainSampler : register(KEIRE_FOG_SHADOW_SAMPLER, space2);
cbuffer FogTerrainConstants : register(KEIRE_FOG_SHADOW_CONSTANTS, space3)
{
    float4 FogShadowParameters;
    float4 FogCascadeSplits;
    float4x4 FogShadowMatrices[4];
    float4 FogViewDepthDirection;
};

float EvaluateFogTerrainShadow(float3 cameraRelativePosition)
{
    const uint count = min((uint)abs(FogShadowParameters.x), 4U);
    if (count == 0U || FogShadowParameters.y <= 0.0F)
    {
        return 1.0F;
    }
    const float viewDepth = dot(cameraRelativePosition, FogViewDepthDirection.xyz);
    uint cascade = uint(viewDepth > FogCascadeSplits.x) + uint(viewDepth > FogCascadeSplits.y) +
        uint(viewDepth > FogCascadeSplits.z);
    cascade = min(cascade, count - 1U);
    const float4 clip = mul(FogShadowMatrices[cascade], float4(cameraRelativePosition, 1.0F));
    if (abs(clip.w) < 1.0e-8F)
    {
        return 1.0F;
    }
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = projected.xy * float2(0.5F, -0.5F) + 0.5F;
    const float depth = projected.z - FogShadowParameters.z;
    if (any(uv < 0.0F) || any(uv > 1.0F) || depth <= 0.0F || depth >= 1.0F)
    {
        return 1.0F;
    }
    float visibility = 0.0F;
    if (FogShadowParameters.x < 0.0F)
    {
        visibility = depth <= FogTerrainDepth.SampleLevel(FogTerrainSampler, float3(uv, cascade), 0.0F);
    }
    else
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                const float weight = (2.0F - abs(float(x))) * (2.0F - abs(float(y)));
                const float2 sampleUv = uv + float2(x, y) * FogShadowParameters.w;
                const float sampleVisibility = any(sampleUv < 0.0F) || any(sampleUv > 1.0F) ? 1.0F :
                    float(depth <= FogTerrainDepth.SampleLevel(FogTerrainSampler, float3(sampleUv, cascade), 0.0F));
                visibility += weight * sampleVisibility / 16.0F;
            }
        }
    }
    return lerp(1.0F, visibility, saturate(FogShadowParameters.y));
}

#define KEIRE_FOG_TERRAIN_VISIBILITY(position) EvaluateFogTerrainShadow(position)
