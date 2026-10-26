#include "CloudVolume.hlsli"

struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

cbuffer VolumeConstants : register(b0, space3)
{
    float4x4 InverseProjection;
    float4x4 InverseView;
    float4 VolumeGeometry;
    float4 VolumeDensity;
    float4 VolumePatternOrigin;
    float4 VolumeAlbedo;
    uint4 VolumeQuality;
    float4 SunDirection;
    float4 SunRadiance;
    float4 AmbientRadiance;
    // X: orthographic projection. Remaining components reserved.
    float4 CameraControls;
};

Texture2D<float> SceneDepth : register(t0, space2);
SamplerState SceneDepthSampler : register(s0, space2);

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

float3 VolumeViewPosition(float2 clip, float depth)
{
    const float4 homogeneous = mul(InverseProjection, float4(clip, depth, 1.0F));
    return homogeneous.xyz / homogeneous.w;
}

// Premultiplied volume contribution. Host blends ONE / ONE_MINUS_SRC_ALPHA before bloom/exposure.
// Opaque depth must be current; transparent materials require their own volume-aware composition.
float4 PSMain(VertexOutput input) : SV_Target0
{
    if (VolumeQuality.x == 0U)
    {
        return 0.0F;
    }
    const float2 clip = input.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F);
    const float3 nearView = VolumeViewPosition(clip, 0.0F);
    // Interior depth avoids an infinite far plane while retaining the correct ray direction.
    const float3 middleView = VolumeViewPosition(clip, 0.5F);
    const bool orthographic = CameraControls.x > 0.5F;
    const float3 origin = orthographic ? mul((float3x3)InverseView, nearView) : 0.0F;
    const float3 direction = normalize(mul((float3x3)InverseView,
                                          orthographic ? middleView - nearView : middleView));
    float maximumDistance = VolumeGeometry.w;
    const float depth = SceneDepth.SampleLevel(SceneDepthSampler, input.UV, 0.0F);
    if (depth < 1.0F)
    {
        const float3 surface = mul((float3x3)InverseView, VolumeViewPosition(clip, depth));
        maximumDistance = min(maximumDistance, max(0.0F, dot(surface - origin, direction)));
    }

    CloudVolumeParameters parameters;
    parameters.BaseHeight = VolumeGeometry.x;
    parameters.Thickness = VolumeGeometry.y;
    parameters.PatternSize = VolumeGeometry.z;
    parameters.Coverage = VolumeDensity.x;
    parameters.Extinction = VolumeDensity.y;
    parameters.DetailAmount = VolumeDensity.z;
    parameters.Anisotropy = VolumeDensity.w;
    parameters.Seed = VolumeQuality.y;
    parameters.WindOffset = VolumePatternOrigin.xyz * VolumeGeometry.z;
    parameters.ViewSteps = VolumeQuality.z;
    parameters.ScatteringAlbedo = VolumeAlbedo.xyz;
    parameters.ShadowSteps = VolumeQuality.w;
    const float4 volume = IntegrateCloudVolume(origin, direction, 0.0F, maximumDistance,
                                               SunDirection.xyz, SunRadiance.xyz, AmbientRadiance.xyz, parameters);
    return float4(min(volume.rgb, 65504.0F), volume.a);
}
