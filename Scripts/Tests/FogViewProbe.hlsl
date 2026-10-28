#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#define KEIRE_CLOUD_NOISE_SPACE space0
#include "CloudVolumeView.hlsli"

RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    CloudVolumeView view = (CloudVolumeView)0;
    view.InverseProjection = float4x4(100, 0, 0, 0, 0, 100, 0, 0, 0, 0, 100, 0, 0, 0, 0, 1);
    view.InverseView = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    view.CameraControls.y = 1.0F;
    view.AmbientRadiance = 1.0F.xxxx;
    view.Fog.Controls = uint4(1U, 4U, 0U, 0U);
    view.Fog.HeightDensity = float4(0, 0, 0.01F, 100);
    view.Fog.Scattering = float4(0.5F, 0.5F, 0.5F, 0);
    const float depth = (float(id.x) + 0.5F) / float(Quality.x);
    const CloudVolumeViewTransport surface = EvaluateCloudVolumeView(0.5F.xx, depth, view, true);
    const float expectedTransmission = exp(-depth);
    bool valid = all(abs(surface.Transmission - expectedTransmission) < 2.0e-5F);
    valid = valid && all(abs(surface.Radiance - (1.0F - expectedTransmission) * 0.5F) < 2.0e-5F);
    const CloudVolumeViewTransport sky = EvaluateCloudVolumeView(0.5F.xx, 1.0F, view, true);
    valid = valid && all(abs(sky.Transmission - exp(-1.0F)) < 2.0e-5F);
    // Off-axis perspective rays travel farther than their view-space depth.
    const float2 offAxis = float2(0.25F, 0.75F);
    CloudVolumeView perspectiveView = view;
    perspectiveView.InverseProjection = float4x4(100, 0, 0, 0, 0, 100, 0, 0,
        0, 0, 0, 100, 0, 0, -0.9F, 1);
    perspectiveView.Fog.HeightDensity.w = 1000.0F;
    const float3 endpoint = float3(-50.0F, -50.0F, 100.0F) / (1.0F - 0.9F * depth);
    const float offAxisTransmission = exp(-min(length(endpoint), 1000.0F) * 0.01F);
    const CloudVolumeViewTransport perspective = EvaluateCloudVolumeView(offAxis, depth, perspectiveView, true);
    valid = valid && all(abs(perspective.Transmission - offAxisTransmission) < 2.0e-5F);
    // Orthographic origins vary across the screen but their parallel rays have the same path length.
    view.CameraControls.x = 1.0F;
    const CloudVolumeViewTransport orthographic = EvaluateCloudVolumeView(offAxis, depth, view, true);
    valid = valid && all(abs(orthographic.Transmission - expectedTransmission) < 2.0e-5F);
    view.CameraControls.y = 4.0F;
    const CloudVolumeViewTransport exposed = EvaluateCloudVolumeView(offAxis, depth, view, true);
    valid = valid && all(abs(exposed.Transmission - orthographic.Transmission) < 2.0e-5F);
    valid = valid && all(abs(exposed.Radiance - orthographic.Radiance * 4.0F) < 2.0e-5F);
    view.Fog.Controls.x = 0U;
    const CloudVolumeViewTransport disabled = EvaluateCloudVolumeView(0.5F.xx, depth, view, true);
    valid = valid && all(disabled.Transmission == 1.0F) && all(disabled.Radiance == 0.0F);
    Pixels[id.y * Quality.x + id.x] = float4(surface.Radiance, valid ? 0.0F : 1.0F);
}
