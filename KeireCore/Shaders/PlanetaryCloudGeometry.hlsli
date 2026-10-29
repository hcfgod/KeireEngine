#pragma once

#include "PlanetaryCloudIntersections.hlsli"

struct PlanetaryCloudGeometry
{
    float4 ObserverRadialAltitude;
    float4 RadiusGeometry;
    float4 ObserverToPlanetX;
    float4 ObserverToPlanetY;
    float4 ObserverToPlanetZ;
    uint4 Controls;
};

float3 PlanetaryCloudToPlanet(float3 offset, PlanetaryCloudGeometry planet)
{
    return float3(dot(offset, planet.ObserverToPlanetX.xyz), dot(offset, planet.ObserverToPlanetY.xyz),
        dot(offset, planet.ObserverToPlanetZ.xyz));
}

float3 PlanetaryCloudToObserver(float3 offset, PlanetaryCloudGeometry planet)
{
    return offset.x * planet.ObserverToPlanetX.xyz + offset.y * planet.ObserverToPlanetY.xyz +
        offset.z * planet.ObserverToPlanetZ.xyz;
}

float3 PlanetaryCloudRadialVector(float3 position, PlanetaryCloudGeometry planet)
{
    return planet.ObserverRadialAltitude.xyz * (planet.RadiusGeometry.x + planet.ObserverRadialAltitude.w) + position;
}

float PlanetaryCloudAltitude(float3 position, PlanetaryCloudGeometry planet)
{
    const float observerRadius = planet.RadiusGeometry.x + planet.ObserverRadialAltitude.w;
    const float3 radial = planet.ObserverRadialAltitude.xyz * observerRadius;
    const float sampleRadius = length(radial + position);
    // Difference of squares retains small height changes near a large reference surface.
    return planet.ObserverRadialAltitude.w +
        (2.0F * dot(radial, position) + dot(position, position)) / max(sampleRadius + observerRadius, 0.0001F);
}

PlanetaryCloudIntervals PlanetaryCloudRayIntervals(float3 origin, float3 direction,
    PlanetaryCloudGeometry planet, float baseHeight, float thickness, float maximumDistance)
{
    const float altitude = PlanetaryCloudAltitude(origin, planet);
    const float radialCosine = dot(normalize(PlanetaryCloudRadialVector(origin, planet)), direction);
    return IntersectPlanetaryCloudShell(planet.RadiusGeometry.x, altitude, radialCosine,
        baseHeight, thickness, maximumDistance);
}
