#include "CloudLightCacheLookup.hlsli"
#include "CloudRadianceEncoding.hlsli"
#define KEIRE_CLOUD_NOISE_TEXTURE t4
#define KEIRE_CLOUD_NOISE_SAMPLER s4
#define KEIRE_CLOUD_LIGHT_CACHE
#define KEIRE_FOG_SHADOW_TEXTURE t5
#define KEIRE_FOG_SHADOW_SAMPLER s5
#define KEIRE_FOG_SHADOW_CONSTANTS b2
#include "FogTerrainShadow.hlsli"
StructuredBuffer<float4> CloudLightTransmissionCache : register(t6, space2);
cbuffer CloudCacheConstants : register(b1, space3)
{
    CloudLightCacheLookupLevel CloudCacheLevels[3];
};
#define KEIRE_PLANET_VOLUME_SET 1
#include "CloudVolumeView.hlsli"
#include "CloudTemporalRejection.hlsli"

Texture2D<float> SceneDepth : register(t0, space2);
SamplerState SceneDepthSampler : register(s0, space2);
Texture2D<float4> SceneColor : register(t1, space2);
SamplerState SceneColorSampler : register(s1, space2);
Texture2D<float4> CloudRadiance : register(t2, space2);
SamplerState CloudRadianceSampler : register(s2, space2);
Texture2D<float2> CloudDepth : register(t3, space2);
SamplerState CloudDepthSampler : register(s3, space2);

cbuffer VolumeConstants : register(b0, space3)
{
    float4 VolumePassControls;
};
StructuredBuffer<CloudVolumeView> CloudViewData : register(t7, space2);
#define VolumeView CloudViewData[0]

struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const bool compositePass = VolumePassControls.x > 1.5F;
    const bool attenuationPass = !compositePass && VolumePassControls.x > 0.5F;
    const float depth = SceneDepth.SampleLevel(SceneDepthSampler, input.UV, 0.0F);

    const CloudVolumeViewRay ray = MakeCloudVolumeViewRay(input.UV, depth, VolumeView);
    const float sceneDistance = depth < 1.0F ? ray.SurfaceDistance : -1.0F;
    uint width;
    uint height;
    CloudRadiance.GetDimensions(width, height);
    const int2 size = int2(width, height);
    const float2 coordinate = input.UV * float2(size) - 0.5F;
    const int2 base = int2(floor(coordinate));
    const float2 fraction = frac(coordinate);
    float4 cloud = 0.0F;
    float totalWeight = 0.0F;
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            const int2 pixel = clamp(base + int2(x, y), int2(0, 0), size - 1);
            const float2 distances = CloudDepth.Load(int3(pixel, 0));
            if (!CloudTemporalSceneMatches(sceneDistance, distances.y, sceneDistance, 1.0F, 0.01F))
            {
                continue;
            }
            const float4 sampleValue = DecodeCloudRadiance(CloudRadiance.Load(int3(pixel, 0)));
            if (!all(isfinite(sampleValue)))
            {
                continue;
            }
            const float2 weights = float2(x == 0 ? 1.0F - fraction.x : fraction.x,
                y == 0 ? 1.0F - fraction.y : fraction.y);
            const float weight = weights.x * weights.y;
            cloud += sampleValue * weight;
            totalWeight += weight;
        }
    }
    if (totalWeight > 0.00001F)
    {
        cloud /= totalWeight;
    }
    else
    {
        // A thin foreground surface may have no representative half-resolution sample. Trace its exact
        // ray instead of borrowing sky clouds or clearing the volume along that silhouette.
        float unusedDistance;
        cloud = EvaluateCloudVolumeTrace(ray, VolumeView, !attenuationPass, 0.0F, unusedDistance);
    }
    // History stores cloud/fog radiance and scalar opacity; clear-air extinction is RGB and remains
    // evaluated at this pixel's scene depth. Never replace Transmission with (1 - cloud.a).xxx here.
    const CloudVolumeViewTransport volume = ComposeCloudVolumeTrace(
        cloud, 0.0F, ray, depth, VolumeView, !attenuationPass);
    if (compositePass)
    {
        const float4 background = SceneColor.SampleLevel(SceneColorSampler, input.UV, 0.0F);
        return float4(volume.Radiance + volume.Transmission * background.rgb,
            VolumeSceneOpacity(background.a, volume.CloudOpacity));
    }
    // Two fixed-function MSAA blends preserve each sample's background and coverage. This shader
    // never samples a resolved background on that path. A thin-edge fallback is exact but not cheap.
    return attenuationPass ? float4(volume.Transmission, 0.0F) : float4(volume.Radiance, volume.CloudOpacity);
}
