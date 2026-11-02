#define KEIRE_CLOUD_NOISE_SPACE space0
#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#define KEIRE_PLANET_VOLUME_SET 1
#include "CloudVolumeView.hlsli"

RWStructuredBuffer<float4> Results : register(u0, space1);

// Three exact structural error records. Does not claim sampled-cloud GPU performance.
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
    view.PlanetWeather.Controls.x = 8U;
    for (uint index = 0U; index < 8U; ++index)
    {
        PlanetVolumeWeatherBodyUniforms body = (PlanetVolumeWeatherBodyUniforms)0;
        body.Quality.x = 1U;
        body.Geometry = float4(100.0F, 100.0F, 1000.0F, 10000.0F);
        body.Density = float4(0.5F, index == 0U ? 0.01F : 0.0F, 0.0F, 0.0F);
        body.PlanetaryCloud.Controls.x = 1U;
        body.PlanetaryCloud.RadiusGeometry.x = 1000.0F;
        body.PlanetaryCloud.ObserverRadialAltitude = float4(0.0F, 1.0F, 0.0F,
            index == 0U ? 1.0F : 1.0e12F);
        view.PlanetWeather.Bodies[index] = body;
    }
    const uint mask = PlanetVolumeActiveCloudMask(view.PlanetWeather);
    const CloudVolumeViewRay ray = MakeCloudVolumeViewRay(float2(0.5F, 0.5F), 1.0F, view);
    Results[0] = float4(abs(float(mask) - 1.0F), abs(ray.MaximumDistance - 2401.0F), 0.0F, 0.0F);
    const CloudVolumeViewRay clipped = MakeCloudVolumeViewRay(float2(0.5F, 0.5F), 0.5F, view);
    Results[1] = float4(abs(clipped.MaximumDistance - 0.5F), abs(clipped.SurfaceDistance - 0.5F), 0.0F, 0.0F);
    // An optically empty planet still blocks sunlight. Never cull its solid geometry with the cloud mask.
    view.PlanetWeather.Bodies[7].PlanetaryCloud.ObserverRadialAltitude = float4(0.0F, -1.0F, 0.0F, 1.0F);
    bool blocked;
    const float opticalDepth = PlanetVolumeAllCloudOpticalDepth(0.0F, float3(0.0F, 1.0F, 0.0F),
        view.PlanetWeather, 4U, mask, blocked);
    Results[2] = float4(blocked ? 0.0F : 1.0F, abs(opticalDepth - 80.0F), 0.0F, 0.0F);
}
