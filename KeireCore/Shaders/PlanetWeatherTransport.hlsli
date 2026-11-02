#pragma once

#include "PlanetAtmosphereSet.hlsli"

// Two layers, with near/far intersections, for each supported visible planet.
#define KEIRE_MAX_PLANET_WEATHER_EVENTS (4 * KEIRE_MAX_PLANET_ATMOSPHERES)

struct PlanetWeatherEvent
{
    float DistanceKilometres;
    uint PlanetIndex;
    uint Layer;
    uint Crossing;
    float3 Radiance;
    float3 Transmission;
};

bool PlanetWeatherEventBefore(PlanetWeatherEvent left, PlanetWeatherEvent right)
{
    if (left.DistanceKilometres != right.DistanceKilometres)
    {
        return left.DistanceKilometres < right.DistanceKilometres;
    }
    // Host planet indices follow stable planet identity, independent of selection or authoring order.
    if (left.PlanetIndex != right.PlanetIndex)
    {
        return left.PlanetIndex < right.PlanetIndex;
    }
    if (left.Layer != right.Layer)
    {
        return left.Layer < right.Layer;
    }
    return left.Crossing < right.Crossing;
}

void AppendPlanetWeatherTransport(inout AtmosphereTransport front, AtmosphereTransport behind)
{
    front.Radiance += front.Transmission * behind.Radiance;
    front.Transmission *= behind.Transmission;
}

AtmosphereTransport PlanetWeatherAirSegment(float3 direction, float beginKilometres, float endKilometres,
    PlanetAtmosphereSet planets)
{
    uint unusedGround;
    float unusedEnd;
    // Offset the observer to the interval start. Never integrate camera-to-cloud air repeatedly.
    return IntegratePlanetAtmosphereSet(direction * beginKilometres, direction,
        endKilometres - beginKilometres, planets, true, false, unusedGround, unusedEnd);
}

// Event radiance is already premultiplied by opacity and contains local cloud lighting only.
// The caller supplies the nearest opaque endpoint and valid unique events; no background is added here.
AtmosphereTransport ComposePlanetWeather(float3 direction, float maximumDistanceKilometres,
    PlanetAtmosphereSet planets, inout PlanetWeatherEvent events[KEIRE_MAX_PLANET_WEATHER_EVENTS], uint eventCount,
    out float3 cloudRadiance, out float3 cloudTransmission)
{
    cloudRadiance = 0.0F;
    cloudTransmission = 1.0F;
    AtmosphereTransport result;
    result.Radiance = 0.0F;
    result.Transmission = 1.0F;
    [loop]
    for (uint index = 1U; index < eventCount; ++index)
    {
        const PlanetWeatherEvent current = events[index];
        uint destination = index;
        [loop]
        while (destination > 0U)
        {
            if (!PlanetWeatherEventBefore(current, events[destination - 1U]))
            {
                break;
            }
            events[destination] = events[destination - 1U];
            --destination;
        }
        events[destination] = current;
    }
    float currentDistance = 0.0F;
    [loop]
    for (uint index = 0U; index < eventCount; ++index)
    {
        const PlanetWeatherEvent event = events[index];
        if (event.DistanceKilometres >= maximumDistanceKilometres)
        {
            break;
        }
        if (event.DistanceKilometres > currentDistance)
        {
            AppendPlanetWeatherTransport(result,
                PlanetWeatherAirSegment(direction, currentDistance, event.DistanceKilometres, planets));
        }
        AtmosphereTransport shell;
        shell.Radiance = event.Radiance;
        shell.Transmission = event.Transmission;
        cloudRadiance += result.Transmission * shell.Radiance;
        cloudTransmission *= shell.Transmission;
        AppendPlanetWeatherTransport(result, shell);
        currentDistance = event.DistanceKilometres;
    }
    if (currentDistance < maximumDistanceKilometres)
    {
        AppendPlanetWeatherTransport(result,
            PlanetWeatherAirSegment(direction, currentDistance, maximumDistanceKilometres, planets));
    }
    return result;
}

AtmosphereTransport ComposePlanetWeather(float3 direction, float maximumDistanceKilometres,
    PlanetAtmosphereSet planets, inout PlanetWeatherEvent events[KEIRE_MAX_PLANET_WEATHER_EVENTS], uint eventCount)
{
    float3 cloudRadiance;
    float3 cloudTransmission;
    return ComposePlanetWeather(direction, maximumDistanceKilometres, planets, events, eventCount,
        cloudRadiance, cloudTransmission);
}
