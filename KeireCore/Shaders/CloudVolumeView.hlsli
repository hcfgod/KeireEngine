#pragma once

#include "CloudVolume.hlsli"
#include "VolumeSurfaceComposition.hlsli"

// Shared background/surface constants. Keep the host's 1520-byte layout in this order.
struct CloudVolumeView
{
    float4x4 InverseProjection;
    float4x4 InverseView;
    float4 Geometry;
    float4 Density;
    float4 PatternOrigin;
    float4 Albedo;
    uint4 Quality;
    float4 SunDirection;
    float4 SunRadiance;
    float4 AmbientRadiance;
    // X: orthographic projection; Y: scene exposure; ZW: inverse render-target dimensions.
    float4 CameraControls;
    float4 ShapeControls;
    float4 RegionalControls;
    AtmosphereMedium Atmosphere;
    float4 HeightProfile;
    float4 WeatherControls;
    FogViewDensity Fog;
};

struct CloudVolumeViewTransport
{
    float3 Radiance;
    float3 Transmission;
    float CloudOpacity;
    float CloudDistance;
};

float3 CloudVolumeViewPosition(float2 clip, float depth, CloudVolumeView view)
{
    const float4 homogeneous = mul(view.InverseProjection, float4(clip, depth, 1.0F));
    return homogeneous.xyz / homogeneous.w;
}

CloudVolumeParameters MakeCloudVolumeParameters(CloudVolumeView view)
{
    CloudVolumeParameters parameters;
    parameters.RaySamplePhase = 0.0F;
    parameters.BaseHeight = view.Geometry.x;
    parameters.Thickness = view.Geometry.y;
    parameters.PatternSize = view.Geometry.z;
    parameters.Coverage = view.Density.x;
    parameters.Extinction = view.Quality.x != 0U ? view.Density.y : 0.0F;
    parameters.DetailAmount = view.Density.z;
    // Zero in the formerly reserved lane preserves coupled detail for older uniform producers.
    parameters.ErosionAmount = VolumeCloudErosionAmount(view.RegionalControls.w, view.Density.z);
    parameters.Anisotropy = view.Density.w;
    parameters.Seed = view.Quality.y;
    parameters.WindOffset = view.PatternOrigin.xyz * view.Geometry.z;
    parameters.ViewSteps = view.Quality.z;
    parameters.ScatteringAlbedo = view.Albedo.xyz;
    parameters.ShadowSteps = view.Quality.w;
    parameters.MaximumDistance = view.Geometry.w;
    parameters.MultipleScatteringContribution = view.Albedo.w;
    parameters.MultipleScatteringOcclusion = view.PatternOrigin.w;
    parameters.AmbientOcclusionStrength = view.AmbientRadiance.w;
    parameters.ShapeControls = view.ShapeControls;
    parameters.RegionalControls = view.RegionalControls;
    parameters.HeightProfile = view.HeightProfile;
    parameters.WeatherControls = view.WeatherControls;
    return parameters;
}

struct CloudVolumeViewRay
{
    float3 Origin;
    float3 Direction;
    float MaximumDistance;
    float SurfaceDistance;
};

CloudVolumeViewRay MakeCloudVolumeViewRay(float2 uv, float depth, CloudVolumeView view)
{
    CloudVolumeViewRay ray;
    const float2 clip = uv * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F);
    const float3 nearView = CloudVolumeViewPosition(clip, 0.0F, view);
    const float3 middleView = CloudVolumeViewPosition(clip, 0.5F, view);
    const bool orthographic = view.CameraControls.x > 0.5F;
    ray.Origin = orthographic ? mul((float3x3)view.InverseView, nearView) : 0.0F;
    ray.Direction = normalize(mul((float3x3)view.InverseView,
        orthographic ? middleView - nearView : middleView));
    ray.MaximumDistance = max(view.Quality.x != 0U ? view.Geometry.w : 0.0F,
        view.Fog.Controls.x != 0U ? view.Fog.HeightDensity.w : 0.0F);
    ray.SurfaceDistance = 0.0F;
    if (depth < 1.0F)
    {
        const float3 surface = mul((float3x3)view.InverseView, CloudVolumeViewPosition(clip, depth, view));
        ray.SurfaceDistance = max(0.0F, dot(surface - ray.Origin, ray.Direction));
        ray.MaximumDistance = min(ray.MaximumDistance, ray.SurfaceDistance);
    }
    return ray;
}

// Unexposed cloud transport includes air in front of cloud opacity events, but not the remaining
// clear-air background integral. Keeping that integral separate prevents geometry from entering history.
float4 EvaluateCloudVolumeTrace(CloudVolumeViewRay ray, CloudVolumeView view, bool includeRadiance,
    float samplePhase, out float cloudDistance)
{
    CloudVolumeParameters parameters = MakeCloudVolumeParameters(view);
    parameters.RaySamplePhase = samplePhase;
    cloudDistance = 0.0F;
    float4 volume = 0.0F;
    if (view.Quality.x != 0U || view.Fog.Controls.x != 0U)
    {
        volume = IntegrateCloudVolumeWithDepth(ray.Origin, ray.Direction, 0.0F, ray.MaximumDistance,
            view.SunDirection.xyz, view.SunRadiance.xyz, view.AmbientRadiance.xyz, parameters,
            view.Atmosphere, includeRadiance, view.Fog, cloudDistance);
    }
    return volume;
}

CloudVolumeViewTransport ComposeCloudVolumeTrace(float4 volume, float cloudDistance, CloudVolumeViewRay ray,
    float depth, CloudVolumeView view, bool includeRadiance)
{
    CloudVolumeViewTransport result;
    result.CloudDistance = cloudDistance;
    result.CloudOpacity = volume.a;
    result.Radiance = volume.rgb;
    result.Transmission = (1.0F - volume.a).xxx;
    // Sky already includes clear-air transport. Opaque and transparent surfaces need it to their own depth,
    // even when that surface is farther away than the cloud march's maximum distance.
    if (depth < 1.0F && view.Atmosphere.Controls.x > 0.5F)
    {
        const float3 airOrigin = view.Atmosphere.ObserverRadius.xyz + ray.Origin * 0.001F;
        const AtmosphereTransport air = IntegrateAtmosphereToSurface(
            airOrigin, ray.Direction, ray.SurfaceDistance * 0.001F, view.Atmosphere, includeRadiance);
        result.Radiance += result.Transmission * air.Radiance;
        result.Transmission *= air.Transmission;
    }
    result.Radiance = float3(VolumeExposedRadiance(result.Radiance.r, view.CameraControls.y),
        VolumeExposedRadiance(result.Radiance.g, view.CameraControls.y),
        VolumeExposedRadiance(result.Radiance.b, view.CameraControls.y));
    return result;
}

// The caller supplies its own depth: opaque scene depth or a transparent fragment's depth.
// Output matches the engine's pre-exposed HDR target; opacity must not be exposed.
CloudVolumeViewTransport EvaluateCloudVolumeView(float2 uv, float depth, CloudVolumeView view, bool includeRadiance)
{
    if (view.Quality.x == 0U && view.Atmosphere.Controls.x <= 0.5F && view.Fog.Controls.x == 0U)
    {
        CloudVolumeViewTransport empty = (CloudVolumeViewTransport)0;
        empty.Transmission = 1.0F;
        return empty;
    }
    const CloudVolumeViewRay ray = MakeCloudVolumeViewRay(uv, depth, view);
    float cloudDistance;
    const float4 volume = EvaluateCloudVolumeTrace(ray, view, includeRadiance, 0.0F, cloudDistance);
    return ComposeCloudVolumeTrace(volume, cloudDistance, ray, depth, view, includeRadiance);
}
