#include "CloudRadianceEncoding.hlsli"
#define KEIRE_CLOUD_NOISE_TEXTURE t1
#define KEIRE_CLOUD_NOISE_SAMPLER s1
#define KEIRE_CLOUD_LIGHT_CACHE
StructuredBuffer<float4> CloudLightTransmissionCache : register(t2, space2);
cbuffer CloudCacheConstants : register(b1, space3)
{
    float4 CloudCacheOrigin;
    float4 CloudCacheCellSize;
    uint4 CloudCacheDimensions;
};
#include "CloudVolumeView.hlsli"

cbuffer VolumeConstants : register(b0, space3)
{
    CloudVolumeView VolumeView;
};

cbuffer TraceConstants : register(b2, space3)
{
    // Accepted sample ordinal and jitter enable. Reserved lanes must be zero.
    uint4 TraceControls;
};

Texture2D<float> SceneDepth : register(t0, space2);
SamplerState SceneDepthSampler : register(s0, space2);

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

struct TraceOutput
{
    float4 RadianceOpacity : SV_Target0;
    // Metres along the ray; scene distance -1 marks background sky, cloud distance 0 marks empty rays.
    float2 CloudAndSceneDistance : SV_Target1;
};

TraceOutput PSMain(VertexOutput input)
{
    const float depth = SceneDepth.SampleLevel(SceneDepthSampler, input.UV, 0.0F);
    const CloudVolumeViewRay ray = MakeCloudVolumeViewRay(input.UV, depth, VolumeView);
    const uint2 pixel = uint2(input.Position.xy);
    uint hash = pixel.x * 1973U + pixel.y * 9277U + 0x68bc21ebU;
    hash = (hash ^ (hash >> 16U)) * 0x7feb352dU;
    hash = (hash ^ (hash >> 15U)) * 0x846ca68bU;
    hash ^= hash >> 16U;
    // Bound the ordinal before conversion so long sessions retain subpixel phase precision.
    const float phase = TraceControls.y != 0U ?
        max(1.0F / 65536.0F, frac((float(hash & 65535U) + 0.5F) / 65536.0F +
            float(TraceControls.x & 1023U) * 0.61803398875F)) : 0.0F;
    TraceOutput output;
    output.RadianceOpacity = EvaluateCloudVolumeTrace(ray, VolumeView, true, phase,
        output.CloudAndSceneDistance.x);
    output.RadianceOpacity = EncodeCloudRadiance(output.RadianceOpacity);
    output.CloudAndSceneDistance.y = depth < 1.0F ? ray.SurfaceDistance : -1.0F;
    return output;
}
