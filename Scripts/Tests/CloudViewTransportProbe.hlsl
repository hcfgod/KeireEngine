// Run with CloudAppearanceGpuProbe in air-check mode. Only the first 128x64 pixels perform transport checks.
#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#define KEIRE_CLOUD_NOISE_SPACE space0
#include "CloudVolumeView.hlsli"

RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 ProbeDensity;
    float4 ProbeShape;
    uint4 ProbeQuality;
    float4 ProbeRegional;
    float4 ProbeWeather;
    float4 ProbeLighting;
};

// Deliberately retain the pre-split view-ray construction as a regression reference.
CloudVolumeViewTransport ReferenceView(float2 uv, float depth, CloudVolumeView view)
{
    const float2 clip = uv * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F);
    const float3 nearView = CloudVolumeViewPosition(clip, 0.0F, view);
    const float3 middleView = CloudVolumeViewPosition(clip, 0.5F, view);
    const bool orthographic = view.CameraControls.x > 0.5F;
    const float3 origin = orthographic ? mul((float3x3)view.InverseView, nearView) : 0.0F;
    const float3 direction = normalize(mul((float3x3)view.InverseView,
        orthographic ? middleView - nearView : middleView));
    float maximumDistance = view.Geometry.w;
    float surfaceDistance = 0.0F;
    if (depth < 1.0F)
    {
        const float3 surface = mul((float3x3)view.InverseView, CloudVolumeViewPosition(clip, depth, view));
        surfaceDistance = max(0.0F, dot(surface - origin, direction));
        maximumDistance = min(maximumDistance, surfaceDistance);
    }
    CloudVolumeViewTransport result;
    const CloudVolumeParameters parameters = MakeCloudVolumeParameters(view);
    const float4 cloud = IntegrateCloudVolumeWithDepth(origin, direction, 0.0F, maximumDistance,
        view.SunDirection.xyz, view.SunRadiance.xyz, view.AmbientRadiance.xyz, parameters,
        view.Atmosphere, true, result.CloudDistance);
    result.CloudOpacity = cloud.a;
    result.Radiance = cloud.rgb;
    result.Transmission = (1.0F - cloud.a).xxx;
    if (depth < 1.0F && view.Atmosphere.Controls.x > 0.5F)
    {
        const AtmosphereTransport air = IntegrateAtmosphereMedium(
            view.Atmosphere.ObserverRadius.xyz + origin * 0.001F, direction,
            surfaceDistance * 0.001F, view.Atmosphere, true);
        result.Radiance += result.Transmission * air.Radiance;
        result.Transmission *= air.Transmission;
    }
    result.Radiance = float3(VolumeExposedRadiance(result.Radiance.r, view.CameraControls.y),
        VolumeExposedRadiance(result.Radiance.g, view.CameraControls.y),
        VolumeExposedRadiance(result.Radiance.b, view.CameraControls.y));
    return result;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= ProbeQuality.x || id.y >= ProbeQuality.y)
    {
        return;
    }
    const uint index = id.y * ProbeQuality.x + id.x;
    Pixels[index] = 0.0F;
    if (id.x >= 128U || id.y >= 64U)
    {
        return;
    }
    CloudVolumeView view = (CloudVolumeView)0;
    view.InverseProjection = float4x4(1,0,0,0, 0,1,0,0, 0,0,0,1000, 0,0,-1,1);
    view.InverseView = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
    view.CameraControls = float4((id.x & 1U) != 0U, float(id.y % 4U) * 0.5F, 0, 0);
    if (view.CameraControls.x > 0.5F)
    {
        view.InverseProjection = float4x4(6000,0,0,0, 0,6000,0,0, 0,0,50000,0, 0,0,0,1);
    }
    else
    {
        view.InverseProjection[0][0] = 1000;
        view.InverseProjection[1][1] = 1000;
    }
    view.Geometry = float4(1490, 1200, 3000, 100000);
    view.Density = float4(0.6F, 0.004F, 0.35F, 0.65F);
    view.PatternOrigin = float4(0, 10.0F / 3000.0F, 9.0F / 3000.0F, 0.5F);
    view.Albedo = float4(0.95F, 0.95F, 0.95F, 0.5F);
    view.Quality = uint4(1, 1, 24, 4);
    view.SunDirection = float4(normalize(float3(0.2F, 0.7F, -0.4F)), 0);
    view.SunRadiance = float4(3, 3, 3, 0);
    view.AmbientRadiance = float4(0.1F, 0.1F, 0.1F, 0);
    view.ShapeControls = float4(4, 0.5F, 2, 1);
    view.RegionalControls = float4(1, 2, 0, 1.35F);
    view.HeightProfile = float4(0, 0.15F, 0.7F, 1);
    view.WeatherControls = float4(1, 0.8F, 0.5F, 1000);
    view.Atmosphere.ObserverRadius = float4(0, 6360.01F, 0, 6360);
    view.Atmosphere.SunHeight = float4(view.SunDirection.xyz, 100);
    view.Atmosphere.RayleighScale = float4(0.005802F, 0.013558F, 0.0331F, 8);
    view.Atmosphere.Mie = float4(0.003996F, 0.00444F, 0.8F, 1.2F);
    view.Atmosphere.AbsorptionPeak = float4(0.00065F, 0.001881F, 0.000085F, 25);
    view.Atmosphere.SolarWidth = float4(20, 20, 20, 15);
    view.Atmosphere.Controls = float4((id.x & 2U) != 0U, 8, 4, 0.004675F);
    const float2 uv = (float2(id.xy) + 0.5F) / float2(128, 64);
    const float depth = (id.y & 4U) != 0U ? 1.0F : 0.95F;
    const CloudVolumeViewTransport expected = ReferenceView(uv, depth, view);
    const CloudVolumeViewTransport actual = EvaluateCloudVolumeView(uv, depth, view, true);
    const bool failed = !all(isfinite(actual.Radiance)) || !all(isfinite(actual.Transmission)) ||
        !isfinite(actual.CloudDistance) || any(abs(actual.Radiance - expected.Radiance) > 0.00001F) ||
        any(abs(actual.Transmission - expected.Transmission) > 0.000001F) ||
        abs(actual.CloudOpacity - expected.CloudOpacity) > 0.000001F ||
        abs(actual.CloudDistance - expected.CloudDistance) > 0.01F;
    Pixels[index] = failed ? 1.0F : 0.0F;
}
