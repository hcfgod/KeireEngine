#define KEIRE_CLOUD_NOISE_TEXTURE t1
#define KEIRE_CLOUD_NOISE_SAMPLER s1
#define KEIRE_CLOUD_LIGHT_CACHE
#define KEIRE_FOG_SHADOW_TEXTURE t3
#define KEIRE_FOG_SHADOW_SAMPLER s3
#define KEIRE_FOG_SHADOW_CONSTANTS b2
#include "FogTerrainShadow.hlsli"
StructuredBuffer<float4> CloudLightTransmissionCache : register(t4, space2);
cbuffer CloudCacheConstants : register(b1, space3)
{
    float4 CloudCacheOrigin;
    float4 CloudCacheCellSize;
    uint4 CloudCacheDimensions;
};
#include "CloudVolumeView.hlsli"

struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

cbuffer VolumeConstants : register(b0, space3)
{
    CloudVolumeView VolumeView;
};

Texture2D<float> SceneDepth : register(t0, space2);
SamplerState SceneDepthSampler : register(s0, space2);
Texture2D<float4> SceneColor : register(t2, space2);
SamplerState SceneColorSampler : register(s2, space2);

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

// RegionalControls.z: radiance 0, attenuation 1, single-sample scene composition 2.
float4 PSMain(VertexOutput input) : SV_Target0
{
    const float depth = SceneDepth.SampleLevel(SceneDepthSampler, input.UV, 0.0F);
    const bool compositePass = VolumeView.RegionalControls.z > 1.5F;
    const bool attenuationPass = !compositePass && VolumeView.RegionalControls.z > 0.5F;
    const CloudVolumeViewTransport volume = EvaluateCloudVolumeView(input.UV, depth, VolumeView, !attenuationPass);
    if (compositePass)
    {
        const float4 background = SceneColor.SampleLevel(SceneColorSampler, input.UV, 0.0F);
        return float4(volume.Radiance + volume.Transmission * background.rgb,
            VolumeSceneOpacity(background.a, volume.CloudOpacity));
    }
    return attenuationPass ? float4(volume.Transmission, 0.0F) : float4(volume.Radiance, volume.CloudOpacity);
}
