#define KEIRE_CLOUD_NOISE_SPACE space0
#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#include "PlanetVolumeTransport.hlsli"

RWStructuredBuffer<float4> Results : register(u0, space1);

// Five absolute-error records. Constant fog profiles remove texture sampling from this probe;
// cloud-noise sampling and real image acceptance require the renderer harness separately.
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id != 0U))
    {
        return;
    }
    PlanetVolumeWeatherUniforms weather = (PlanetVolumeWeatherUniforms)0;
    weather.Controls.x = 2U;
    for (uint index = 0U; index < 2U; ++index)
    {
        weather.Bodies[index].PlanetaryCloud.ObserverRadialAltitude = float4(0.0F, 1.0F, 0.0F, 1000000.0F);
        weather.Bodies[index].PlanetaryCloud.RadiusGeometry.x = 1000.0F;
        weather.Bodies[index].PlanetaryCloud.Controls.x = 1U;
        weather.Bodies[index].Fog.Controls = uint4(1U, 7U + index * 4U, 0U, 0U);
    }
    weather.Bodies[0].Fog.HeightDensity = float4(0.0F, 0.0F, 0.2F, 3.0F);
    weather.Bodies[0].Fog.Scattering = float4(1.0F, 0.0F, 0.0F, 0.0F);
    weather.Bodies[1].Fog.HeightDensity = float4(0.0F, 0.0F, 0.3F, 4.0F);
    weather.Bodies[1].Fog.Scattering = float4(0.0F, 0.0F, 1.0F, 0.0F);
    const FogViewDensity noFog = (FogViewDensity)0;
    const PlanetAtmosphereSet vacuum = (PlanetAtmosphereSet)0;
    const float3 direction = float3(0.0F, 0.0F, 1.0F);
    const float3 sunDirection = float3(0.0F, 1.0F, 0.0F);
    float distance;
    const float4 full = IntegratePlanetVolumeWeather(0.0F, direction, 4.0F, sunDirection,
        0.0F, 1.0F, weather, noFog, vacuum, true, 0.0F, distance);
    const float firstTransmission = exp(-1.5F);
    const float3 firstSource = float3(0.4F, 0.0F, 0.6F);
    const float3 expected = firstSource * (1.0F - firstTransmission) +
        float3(0.0F, 0.0F, 1.0F) * firstTransmission * (1.0F - exp(-0.3F));
    Results[0] = abs(full - float4(expected, 1.0F - exp(-1.8F)));
    const float4 clipped = IntegratePlanetVolumeWeather(0.0F, direction, 2.0F, sunDirection,
        0.0F, 1.0F, weather, noFog, vacuum, true, 0.5F, distance);
    Results[1] = abs(clipped - float4(firstSource * (1.0F - exp(-1.0F)), 1.0F - exp(-1.0F)));
    const PlanetVolumeWeatherBodyUniforms first = weather.Bodies[0];
    weather.Bodies[0] = weather.Bodies[1];
    weather.Bodies[1] = first;
    const float4 reversed = IntegratePlanetVolumeWeather(0.0F, direction, 4.0F, sunDirection,
        0.0F, 1.0F, weather, noFog, vacuum, true, 0.125F, distance);
    Results[2] = abs(reversed - full);
    const float4 attenuation = IntegratePlanetVolumeWeather(0.0F, direction, 4.0F, sunDirection,
        0.0F, 1.0F, weather, noFog, vacuum, false, 0.0F, distance);
    Results[3] = float4(abs(attenuation.rgb), abs(attenuation.a - full.a));
    FogViewDensity common = noFog;
    common.Controls = uint4(1U, 13U, 0U, 0U);
    common.HeightDensity = float4(0.0F, 0.0F, 0.1F, 2.0F);
    common.Scattering = float4(1.0F, 1.0F, 0.0F, 0.0F);
    const float4 combined = IntegratePlanetVolumeWeather(0.0F, direction, 2.0F, sunDirection,
        0.0F, 1.0F, weather, common, vacuum, true, 0.0F, distance);
    const float3 combinedSource = float3(0.3F, 0.1F, 0.3F) / 0.6F;
    Results[4] = abs(combined - float4(combinedSource * (1.0F - exp(-1.2F)), 1.0F - exp(-1.2F)));
}
