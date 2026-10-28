#include "CloudRadianceEncoding.hlsli"
// Ray reconstruction uses the shared view layout; unused cloud-noise declarations are eliminated.
#define KEIRE_CLOUD_NOISE_TEXTURE t4
#define KEIRE_CLOUD_NOISE_SAMPLER s4
#include "CloudVolumeView.hlsli"
#include "CloudTemporalRejection.hlsli"

Texture2D<float4> CurrentRadiance : register(t0, space2);
SamplerState CurrentRadianceSampler : register(s0, space2);
Texture2D<float2> CurrentDepth : register(t1, space2);
SamplerState CurrentDepthSampler : register(s1, space2);
Texture2D<float4> PreviousRadiance : register(t2, space2);
SamplerState PreviousRadianceSampler : register(s2, space2);
Texture2D<float2> PreviousDepth : register(t3, space2);
SamplerState PreviousDepthSampler : register(s3, space2);

cbuffer CurrentViewConstants : register(b0, space3)
{
    CloudVolumeView CurrentView;
};

cbuffer PreviousViewConstants : register(b1, space3)
{
    CloudVolumeView PreviousView;
};

cbuffer ReprojectionConstants : register(b2, space3)
{
    float4x4 PreviousRotationViewProjection;
    // Current camera-relative point -> previous camera-relative point, including horizontal wind motion.
    float4 CloudPositionOffset;
    // Camera translation only: opaque geometry must not inherit cloud wind motion.
    float4 ScenePositionOffset;
    // History weight, absolute cloud depth tolerance, relative depth tolerance, reserved.
    float4 HistoryControls;
};

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

struct ResolveOutput
{
    float4 RadianceOpacity : SV_Target0;
    float2 CloudAndSceneDistance : SV_Target1;
};

bool PreviousUv(float3 position, out float2 uv)
{
    const float4 clip = mul(PreviousRotationViewProjection, float4(position, 1.0F));
    uv = 0.0F;
    // Sky clouds can lie beyond the opaque geometry far plane; their projected direction is still valid.
    if (!all(isfinite(clip)) || clip.w <= 0.0F || clip.z < 0.0F)
    {
        return false;
    }
    uv = clip.xy / clip.w * float2(0.5F, -0.5F) + 0.5F;
    return all(uv >= 0.0F) && all(uv < 1.0F);
}

ResolveOutput PSMain(VertexOutput input)
{
    ResolveOutput output;
    output.RadianceOpacity = CurrentRadiance.SampleLevel(CurrentRadianceSampler, input.UV, 0.0F);
    output.CloudAndSceneDistance = CurrentDepth.SampleLevel(CurrentDepthSampler, input.UV, 0.0F);
    const float cloudDistance = output.CloudAndSceneDistance.x;
    if (HistoryControls.x <= 0.0F || cloudDistance <= 0.0F || output.RadianceOpacity.a <= 0.0F)
    {
        return output;
    }
    const CloudVolumeViewRay currentRay = MakeCloudVolumeViewRay(input.UV, 1.0F, CurrentView);
    const float3 previousCloudPosition = currentRay.Origin + currentRay.Direction * cloudDistance +
        CloudPositionOffset.xyz;
    float2 previousUv;
    if (!PreviousUv(previousCloudPosition, previousUv))
    {
        return output;
    }
    const CloudVolumeViewRay previousRay = MakeCloudVolumeViewRay(previousUv, 1.0F, PreviousView);
    const float expectedCloudDistance = dot(previousCloudPosition - previousRay.Origin, previousRay.Direction);
    const float2 previousDistances = PreviousDepth.SampleLevel(PreviousDepthSampler, previousUv, 0.0F);
    if (!CloudTemporalDepthMatches(expectedCloudDistance, previousDistances.x,
        HistoryControls.y, HistoryControls.z))
    {
        return output;
    }

    // Test the current opaque endpoint in its own previous pixel. Wind reprojection can select a different
    // pixel from the background, so checking geometry at the cloud coordinate alone misses disocclusion.
    const float sceneDistance = output.CloudAndSceneDistance.y;
    float previousSceneDistance = previousDistances.y;
    float expectedSceneDistance = -1.0F;
    if (sceneDistance >= 0.0F)
    {
        const float3 previousScenePosition = currentRay.Origin + currentRay.Direction * sceneDistance +
            ScenePositionOffset.xyz;
        float2 sceneUv;
        if (!PreviousUv(previousScenePosition, sceneUv))
        {
            return output;
        }
        const CloudVolumeViewRay previousSceneRay = MakeCloudVolumeViewRay(sceneUv, 1.0F, PreviousView);
        expectedSceneDistance = dot(previousScenePosition - previousSceneRay.Origin, previousSceneRay.Direction);
        previousSceneDistance = PreviousDepth.SampleLevel(PreviousDepthSampler, sceneUv, 0.0F).y;
    }
    if (!CloudTemporalSceneMatches(sceneDistance, previousSceneDistance, expectedSceneDistance,
        1.0F, HistoryControls.z))
    {
        return output;
    }

    const float4 history = DecodeCloudRadiance(
        PreviousRadiance.SampleLevel(PreviousRadianceSampler, previousUv, 0.0F));
    if (!all(isfinite(history)) || history.a <= 0.0F)
    {
        return output;
    }
    uint width;
    uint height;
    CurrentRadiance.GetDimensions(width, height);
    const int2 size = int2(width, height);
    const int2 center = int2(input.Position.xy);
    const float4 current = DecodeCloudRadiance(output.RadianceOpacity);
    float4 minimumValue = current;
    float4 maximumValue = current;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 pixel = clamp(center + int2(x, y), int2(0, 0), size - 1);
            const float4 neighbor = DecodeCloudRadiance(CurrentRadiance.Load(int3(pixel, 0)));
            minimumValue = min(minimumValue, neighbor);
            maximumValue = max(maximumValue, neighbor);
        }
    }
    // Clamp to the current neighborhood so stale illumination cannot persist behind changed cloud boundaries.
    output.RadianceOpacity = EncodeCloudRadiance(lerp(current, clamp(history, minimumValue, maximumValue),
        clamp(HistoryControls.x, 0.0F, 0.95F)));
    // Never blend geometry metadata: it always describes the current ray and current visible scene.
    return output;
}
