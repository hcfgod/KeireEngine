#pragma once

#include "PlanetWeatherUniforms.hlsli"
#include "PlanetWeatherTransport.hlsli"
#include "PlanetaryCloudLayered.hlsli"

// Fixed two-layer/two-crossing capacity per planet; the host rejects planet-count overflow.
uint BuildPlanetWeatherCloudEvents(float3 direction, PlanetWeatherUniforms weather,
    PlanetAtmosphereSet atmospheres, float3 ambient, float3 sunlight, float3 sunDirection,
    out PlanetWeatherEvent events[KEIRE_MAX_PLANET_WEATHER_EVENTS])
{
    uint count = 0U;
    [loop]
    for (uint planetIndex = 0U; planetIndex < weather.Controls.x; ++planetIndex)
    {
        const PlanetWeatherBodyUniforms body = weather.Bodies[planetIndex];
        const PlanetaryCloudGeometry planet = body.Geometry;
        const float cosine = dot(planet.ObserverRadialAltitude.xyz, direction);
        [unroll]
        for (uint layerIndex = 0U; layerIndex < 2U; ++layerIndex)
        {
            PlanetaryCloudLayer layer;
            layer.Geometry = body.Clouds[layerIndex].Geometry;
            layer.Pattern = body.Clouds[layerIndex].Pattern;
            layer.Color = body.Clouds[layerIndex].Color;
            layer.Noise = body.Clouds[layerIndex].Noise;
            layer.Phase = body.Phase[layerIndex];
            const PlanetaryCloudRoots roots = PlanetaryCloudSphereRoots(planet.RadiusGeometry.x,
                planet.ObserverRadialAltitude.w, cosine, layer.Geometry.x);
            [unroll]
            for (uint crossing = 0U; crossing < 2U; ++crossing)
            {
                const float distance = roots.Hit ? (crossing == 0U ? roots.Near : roots.Far) : -1.0F;
                const float3 position = direction * max(distance, 0.0F);
                const float3 coordinate = layer.Phase.xyz + PlanetaryCloudToPlanet(position, planet) * layer.Geometry.y;
                const float footprint = max(length(ddx(coordinate)), length(ddy(coordinate)));
                if (distance <= 0.0F || layer.Geometry.w <= 0.0F || layer.Geometry.z <= 0.0F ||
                    planet.ObserverRadialAltitude.w < 0.0F || (crossing == 1U && roots.Near == roots.Far))
                {
                    continue;
                }
                float3 incidentSun = sunlight * PlanetAtmosphereSunTransmission(position * 0.001F, atmospheres);
                // Weather planets can be airless, so their opaque bodies also shadow incident sunlight.
                [loop]
                for (uint shadowIndex = 0U; shadowIndex < weather.Controls.x; ++shadowIndex)
                {
                    const PlanetaryCloudGeometry shadowPlanet = weather.Bodies[shadowIndex].Geometry;
                    const float3 radial = PlanetaryCloudRadialVector(position, shadowPlanet);
                    const float radius = length(radial);
                    const PlanetaryCloudRoots shadow = PlanetaryCloudSphereRoots(shadowPlanet.RadiusGeometry.x,
                        radius - shadowPlanet.RadiusGeometry.x, dot(radial / max(radius, 0.0001F), sunDirection), 0.0F);
                    if (shadow.Hit && shadow.Near >= 0.0F && shadow.Far > 0.0F)
                    {
                        incidentSun = 0.0F;
                    }
                }
                const float4 local = EvaluatePlanetaryCloudLocal(direction, layer, planet, position, coordinate,
                    footprint, ambient, incidentSun, sunDirection);
                PlanetWeatherEvent event;
                event.DistanceKilometres = distance * 0.001F;
                event.PlanetIndex = planetIndex;
                event.Layer = layerIndex;
                event.Crossing = crossing;
                event.Radiance = local.rgb;
                event.Transmission = 1.0F - local.a;
                events[count++] = event;
            }
        }
    }
    return count;
}
