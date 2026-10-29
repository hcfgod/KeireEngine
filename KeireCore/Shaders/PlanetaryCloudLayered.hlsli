#pragma once

#include "PlanetaryCloudGeometry.hlsli"
#include "PlanetaryCloudWeather.hlsli"
#include "AtmosphereMedium.hlsli"

struct PlanetaryCloudLayer
{
    float4 Geometry;
    float4 Pattern;
    float4 Color;
    uint4 Noise;
    float4 Phase;
};

float4 EvaluatePlanetaryCloudLayers(float3 direction, PlanetaryCloudLayer first, PlanetaryCloudLayer second,
    float3 ambient, float3 sunlight, float3 sunDirection, PlanetaryCloudGeometry planet, AtmosphereMedium atmosphere)
{
    PlanetaryCloudLayer layers[2];
    layers[0] = first;
    layers[1] = second;
    float distances[4];
    uint indices[4];
    const float cosine = dot(planet.ObserverRadialAltitude.xyz, direction);
    const PlanetaryCloudRoots ground = PlanetaryCloudSphereRoots(planet.RadiusGeometry.x,
        planet.ObserverRadialAltitude.w, cosine, 0.0F);
    const float groundDistance = ground.Hit && ground.Near >= 0.0F && ground.Far > 0.0F ? ground.Near : 1.0e30F;
    [unroll]
    for (uint layerIndex = 0U; layerIndex < 2U; ++layerIndex)
    {
        const PlanetaryCloudRoots roots = PlanetaryCloudSphereRoots(planet.RadiusGeometry.x,
            planet.ObserverRadialAltitude.w, cosine, layers[layerIndex].Geometry.x);
        distances[layerIndex * 2U] = roots.Hit ? roots.Near : -1.0F;
        distances[layerIndex * 2U + 1U] = roots.Hit ? roots.Far : -1.0F;
        indices[layerIndex * 2U] = layerIndex;
        indices[layerIndex * 2U + 1U] = layerIndex;
    }
    // Four fixed candidates, front-to-back. Layers can interleave around the limb.
    [unroll]
    for (uint pass = 0U; pass < 3U; ++pass)
    {
        [unroll]
        for (uint index = 0U; index < 3U; ++index)
        {
            if (distances[index] > distances[index + 1U])
            {
                const float distance = distances[index];
                distances[index] = distances[index + 1U];
                distances[index + 1U] = distance;
                const uint layer = indices[index];
                indices[index] = indices[index + 1U];
                indices[index + 1U] = layer;
            }
        }
    }
    float4 result = 0.0F;
    [unroll]
    for (uint index = 0U; index < 4U; ++index)
    {
        const float distance = distances[index];
        const PlanetaryCloudLayer layer = layers[indices[index]];
        const float3 position = direction * max(distance, 0.0F);
        const float3 coordinate = layer.Phase.xyz + PlanetaryCloudToPlanet(position, planet) * layer.Geometry.y;
        // Derivatives precede per-pixel rejection, including off-planet rays.
        const float footprint = max(length(ddx(coordinate)), length(ddy(coordinate)));
        if (distance <= 0.0F || distance >= groundDistance || planet.ObserverRadialAltitude.w < 0.0F ||
            layer.Geometry.w <= 0.0F || layer.Geometry.z <= 0.0F)
        {
            continue;
        }
        float density = 0.0F;
        float weight = 0.533333333F;
        [unroll]
        for (uint octave = 0U; octave < 4U; ++octave)
        {
            const float frequency = float(1U << octave);
            density += lerp(0.5F, PlanetaryCloudWeather(coordinate * frequency, 1.0F, layer.Noise.y + octave),
                saturate(1.0F - footprint * frequency)) * weight;
            weight *= 0.5F;
        }
        const float threshold = 1.0F - layer.Geometry.z;
        const float mask = layer.Geometry.z >= 1.0F ? 1.0F :
            smoothstep(threshold - layer.Pattern.z, threshold + layer.Pattern.z, density);
        const float3 normal = normalize(PlanetaryCloudRadialVector(position, planet));
        const float incidence = abs(dot(normal, direction));
        const float alpha = mask * layer.Geometry.w * smoothstep(0.0F, max(layer.Pattern.w, 0.0001F), incidence);
        float3 incidentSun = sunlight;
        const PlanetaryCloudRoots sunGround = PlanetaryCloudSphereRoots(planet.RadiusGeometry.x,
            layer.Geometry.x, dot(normal, sunDirection), 0.0F);
        if (sunGround.Hit && sunGround.Near >= 0.0F && sunGround.Far > 0.0F)
        {
            incidentSun = 0.0F;
        }
        const float3 airPosition = atmosphere.ObserverRadius.xyz + position * 0.001F;
        if (atmosphere.Controls.x > 0.5F)
        {
            incidentSun *= AtmosphereMediumSunTransmission(airPosition, atmosphere);
        }
        float3 radiance = layer.Color.rgb;
        if (layer.Noise.x == 1U)
        {
            const float core = lerp(1.0F, 0.45F, density);
            const float rim = 0.15F * pow(saturate(dot(direction, sunDirection)), 8.0F) * (1.0F - density);
            radiance *= ambient * (0.65F + 0.35F * (1.0F - density)) +
                incidentSun * ((0.2F + 0.8F * saturate(dot(normal, sunDirection))) * core + rim) / 3.14159265359F;
        }
        if (atmosphere.Controls.x > 0.5F)
        {
            const AtmosphereTransport foreground = IntegrateAtmosphereMedium(
                atmosphere.ObserverRadius.xyz, direction, distance * 0.001F, atmosphere);
            radiance = foreground.Radiance + foreground.Transmission * radiance;
        }
        result.rgb += (1.0F - result.a) * min(radiance, 65504.0F) * alpha;
        result.a += (1.0F - result.a) * alpha;
    }
    return result;
}
