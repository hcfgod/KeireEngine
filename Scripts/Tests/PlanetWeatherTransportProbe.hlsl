#include "PlanetWeatherTransport.hlsli"

RWStructuredBuffer<float4> Results : register(u0, space1);

// Dispatch one group with six float4 records. Records 0-2 are exact vacuum checks.
// Records 3-5 contain absolute errors, with a 1e-5 tolerance for 64-sample midpoint quadrature.
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id != 0U))
    {
        return;
    }
    PlanetAtmosphereSet vacuum = (PlanetAtmosphereSet)0;
    PlanetWeatherEvent events[KEIRE_MAX_PLANET_WEATHER_EVENTS];
    [unroll]
    for (uint index = 0U; index < KEIRE_MAX_PLANET_WEATHER_EVENTS; ++index)
    {
        events[index] = (PlanetWeatherEvent)0;
    }
    events[0].DistanceKilometres = 1.0F;
    events[0].PlanetIndex = 1U;
    events[0].Radiance = float3(0.0F, 0.0F, 4.0F);
    events[0].Transmission = 0.5F;
    events[1].DistanceKilometres = 1.0F;
    events[1].PlanetIndex = 0U;
    events[1].Radiance = float3(2.0F, 0.0F, 0.0F);
    events[1].Transmission = 0.25F;
    const float3 direction = float3(0.0F, 0.0F, 1.0F);
    float3 cloudRadiance;
    float3 cloudTransmission;
    const AtmosphereTransport tied = ComposePlanetWeather(direction, 2.0F, vacuum, events, 2U,
        cloudRadiance, cloudTransmission);
    Results[0] = float4(abs(tied.Radiance - float3(2.0F, 0.0F, 1.0F)) +
        abs(cloudRadiance - tied.Radiance) + abs(cloudTransmission - tied.Transmission),
        max(abs(tied.Transmission.x - 0.125F),
            max(abs(tied.Transmission.y - 0.125F), abs(tied.Transmission.z - 0.125F))));
    const AtmosphereTransport clipped = ComposePlanetWeather(direction, 1.0F, vacuum, events, 2U);
    Results[1] = float4(abs(clipped.Radiance), max(abs(clipped.Transmission.x - 1.0F),
        max(abs(clipped.Transmission.y - 1.0F), abs(clipped.Transmission.z - 1.0F))));
    events[0].DistanceKilometres = 3.0F;
    events[1].DistanceKilometres = 1.0F;
    const AtmosphereTransport ordered = ComposePlanetWeather(direction, 4.0F, vacuum, events, 2U);
    Results[2] = float4(abs(ordered.Radiance - float3(1.0F, 0.0F, 4.0F)),
        max(abs(ordered.Transmission.x - 0.125F),
            max(abs(ordered.Transmission.y - 0.125F), abs(ordered.Transmission.z - 0.125F))));

    PlanetAtmosphereSet air = (PlanetAtmosphereSet)0;
    air.Controls.x = 1U;
    air.Media[0].ObserverRadius = float4(0.0F, 11.0F, 0.0F, 10.0F);
    air.Media[0].SunHeight = float4(0.0F, 1.0F, 0.0F, 10.0F);
    air.Media[0].RayleighScale.w = 2.0F;
    air.Media[0].Mie = float4(0.0F, 0.2F, 0.0F, 2.0F);
    air.Media[0].SolarWidth.w = 1.0F;
    air.Media[0].Controls = float4(1.0F, 64.0F, 4.0F, 0.0F);
    const float3 upward = float3(0.0F, 1.0F, 0.0F);
    // Integral of extinction 0.2 * exp(-(1+s)/2), from s=0 to d kilometres.
    const float opticalDepthScale = 0.4F * exp(-0.5F);
    const float transmission1 = exp(-opticalDepthScale * (1.0F - exp(-0.5F)));
    const float transmission3 = exp(-opticalDepthScale * (1.0F - exp(-1.5F)));
    const float transmission4 = exp(-opticalDepthScale * (1.0F - exp(-2.0F)));
    events[0].DistanceKilometres = 1.0F;
    events[0].Radiance = float3(2.0F, 0.0F, 0.0F);
    events[0].Transmission = 0.25F;
    events[1].DistanceKilometres = 3.0F;
    events[1].Radiance = float3(0.0F, 0.0F, 4.0F);
    events[1].Transmission = 0.5F;
    const AtmosphereTransport absorbing = ComposePlanetWeather(upward, 4.0F, air, events, 2U);
    Results[3] = float4(abs(absorbing.Radiance - float3(2.0F * transmission1, 0.0F, transmission3)),
        abs(absorbing.Transmission.x - 0.125F * transmission4));
    uint ground;
    float endDistance;
    const AtmosphereTransport reference = IntegratePlanetAtmosphereSet(0.0F, upward, 4.0F,
        air, true, false, ground, endDistance);
    Results[4] = float4(abs(absorbing.Transmission - reference.Transmission * 0.125F),
        abs(reference.Transmission.x - transmission4));
    events[0].Radiance = 0.0F;
    events[0].Transmission = 1.0F;
    events[1].Radiance = 0.0F;
    events[1].Transmission = 1.0F;
    const AtmosphereTransport partitioned = ComposePlanetWeather(upward, 4.0F, air, events, 2U);
    Results[5] = float4(abs(partitioned.Transmission - reference.Transmission),
        max(abs(partitioned.Radiance.x), max(abs(partitioned.Radiance.y), abs(partitioned.Radiance.z))));
}
