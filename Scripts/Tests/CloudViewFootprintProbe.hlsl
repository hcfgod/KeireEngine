#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#include "CloudVolumeView.hlsli"

RWStructuredBuffer<float4> Results : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id != 0U))
    {
        return;
    }

    CloudVolumeView view = (CloudVolumeView)0;
    view.InverseProjection = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    view.InverseView = view.InverseProjection;
    view.CameraControls = float4(0, 1, 0.001F, 0.001F);
    const CloudVolumeViewRay full = MakeCloudVolumeViewRay(float2(0.5F, 0.5F), 1.0F, view);
    const CloudVolumeViewRay half = MakeCloudVolumeViewRay(float2(0.5F, 0.5F), 1.0F, view, 2.0F);
    Results[0] = float4(
        full.PixelFootprint.x > 0.0F && half.PixelFootprint.x > full.PixelFootprint.x ? 0.0F : 1.0F,
        full.PixelFootprint.y, half.PixelFootprint.y, 0.0F);

    view.InverseView = float4x4(0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1);
    const CloudVolumeViewRay rotated = MakeCloudVolumeViewRay(float2(0.5F, 0.5F), 1.0F, view);
    Results[1] = float4(abs(rotated.PixelFootprint - full.PixelFootprint), 0, 0);

    view.CameraControls.x = 1.0F;
    const CloudVolumeViewRay ortho = MakeCloudVolumeViewRay(float2(0.5F, 0.5F), 1.0F, view);
    const CloudVolumeViewRay orthoEdge = MakeCloudVolumeViewRay(float2(0.9F, 0.1F), 1.0F, view);
    Results[2] = float4(ortho.PixelFootprint.x, abs(ortho.PixelFootprint.y - 0.002F),
        abs(orthoEdge.PixelFootprint.y - ortho.PixelFootprint.y), 0.0F);
}
