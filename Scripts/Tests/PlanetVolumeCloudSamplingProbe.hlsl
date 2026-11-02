#define KEIRE_CLOUD_NOISE_SPACE space0
#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#define KEIRE_PLANET_VOLUME_SET 1
#include "CloudVolumeView.hlsli"

RWStructuredBuffer<float4> Results : register(u0, space1);

// The host binds a constant initialized noise texture. Coincident media with the same density
// profile must match a single volume with summed extinction and extinction-weighted albedo. This validates
// actual texture sampling and composition, not heterogeneous noise appearance or speed.
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id != 0U))
    {
        return;
    }
    PlanetVolumeWeatherBodyUniforms body = (PlanetVolumeWeatherBodyUniforms)0;
    body.Geometry = float4(100.0F, 100.0F, 1000.0F, 10000.0F);
    body.Density = float4(1.0F, 0.003F, 0.0F, 0.0F);
    body.Albedo = float4(0.8F, 0.7F, 0.6F, 0.0F);
    body.Quality = uint4(1U, 1U, 16U, 4U);
    body.ShapeControls = float4(8.0F, 0.0F, 1.0F, 1.0F);
    body.RegionalControls = float4(0.0F, 1.0F, 0.0F, 1.0F);
    body.HeightProfile = float4(0.1F, 0.25F, 0.75F, 1.0F);
    body.PlanetaryCloud.Controls.x = 1U;
    body.PlanetaryCloud.RadiusGeometry.x = 1000.0F;
    body.PlanetaryCloud.ObserverRadialAltitude = float4(0.0F, 1.0F, 0.0F, 150.0F);
    PlanetVolumeWeatherUniforms weather = (PlanetVolumeWeatherUniforms)0;
    const FogViewDensity noFog = (FogViewDensity)0;
    const PlanetAtmosphereSet vacuum = (PlanetAtmosphereSet)0;
    const AtmosphereMedium noAir = (AtmosphereMedium)0;
    const float3 direction = float3(0.0F, 1.0F, 0.0F);
    float minimumOpacity = 1.0F;
    for (uint test = 0U; test < 3U; ++test)
    {
        const uint count = test == 0U ? 2U : 8U;
        weather.Controls.x = count;
        float totalExtinction = 0.0F;
        float3 weightedAlbedo = 0.0F;
        for (uint index = 0U; index < count; ++index)
        {
            PlanetVolumeWeatherBodyUniforms candidate = body;
            if (test == 2U)
            {
                // Distinct media exercise indexed uniform reads, including a disabled middle slot.
                candidate.Density.y *= float(index + 1U);
                candidate.Albedo.xyz *= float(index + 1U) / float(count);
                candidate.Quality.x = index == 3U ? 0U : 1U;
            }
            weather.Bodies[index] = candidate;
            const float extinction = candidate.Quality.x != 0U ? candidate.Density.y : 0.0F;
            totalExtinction += extinction;
            weightedAlbedo += extinction * candidate.Albedo.xyz;
        }
        float distance;
        const float4 combined = IntegratePlanetVolumeWeather(0.0F, direction, 25.0F, direction,
            0.0F, 1.0F, weather, noFog, vacuum, true, 0.0F, distance);
        CloudVolumeParameters equivalent = MakePlanetVolumeParameters(body, 0.0F);
        equivalent.Extinction = totalExtinction;
        equivalent.ScatteringAlbedo = weightedAlbedo / totalExtinction;
        const float4 reference = IntegrateCloudVolumeWithDepth(0.0F, direction, 0.0F, 25.0F,
            direction, 0.0F, 1.0F, equivalent, noAir, true, noFog, distance);
        Results[test] = abs(combined - reference);
        minimumOpacity = min(minimumOpacity, min(combined.a, reference.a));
    }
    // Selecting a single planetary medium must preserve both transport and its depth centroid.
    CloudVolumeView view = (CloudVolumeView)0;
    view.PlanetWeather.Controls.x = 1U;
    view.PlanetWeather.Bodies[0] = body;
    view.SunDirection = float4(direction, 0.0F);
    view.AmbientRadiance = 1.0F;
    CloudVolumeViewRay ray = (CloudVolumeViewRay)0;
    ray.Direction = direction;
    ray.MaximumDistance = 25.0F;
    float selectedDistance;
    const float4 selected = EvaluateCloudVolumeTrace(ray, view, true, 0.0F, selectedDistance);
    float referenceDistance;
    const float4 singleReference = IntegrateCloudVolumeWithDepth(0.0F, direction, 0.0F, 25.0F,
        direction, 0.0F, 1.0F, MakePlanetVolumeParameters(body, 0.0F), noAir, true, noFog, referenceDistance);
    Results[3] = abs(selected - singleReference);
    Results[4] = float4(abs(selectedDistance - referenceDistance), 0.0F, 0.0F, 0.0F);
    minimumOpacity = min(minimumOpacity, min(selected.a, singleReference.a));

    // A broken/empty density path must not pass merely because both implementations output zero.
    Results[5] = float4(minimumOpacity > 0.001F ? 0.0F : 1.0F, 0.0F, 0.0F, 0.0F);
}
