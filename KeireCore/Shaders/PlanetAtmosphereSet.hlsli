#pragma once

#include "AtmosphereMedium.hlsli"

// Host capture sorts by stable planet identity and rejects overflow instead of dropping visible planets.
#define KEIRE_MAX_PLANET_ATMOSPHERES 8
struct PlanetAtmosphereSet
{
    uint4 Controls;
    AtmosphereMedium Media[KEIRE_MAX_PLANET_ATMOSPHERES];
};

float3 PlanetAtmosphereSunTransmission(float3 observerOffset, PlanetAtmosphereSet planets)
{
    // All candidates share the current scene's solar direction/irradiance. Per-planet lighting profiles would
    // require a direction argument here rather than reusing each candidate's packed global sun direction.
    float3 transmission = 1.0F;
    [loop]
    for (uint index = 0U; index < planets.Controls.x; ++index)
    {
        AtmosphereMedium medium = planets.Media[index];
        const float3 position = medium.ObserverRadius.xyz + observerOffset;
        if (length(position) < medium.ObserverRadius.w)
        {
            return 0.0F;
        }
        // Optical depths add even where atmospheres overlap; independent light-ray attenuation multiplies.
        transmission *= AtmosphereMediumSunTransmission(position, medium);
    }
    return transmission;
}

// Positions and ray distances are camera-relative kilometres. Boundary partitioning skips vacuum and
// combines extinction/source within overlap regions before transport. Surface callers can disable ground
// clipping because their authored geometry owns visibility (matching the single-atmosphere contract).
AtmosphereTransport IntegratePlanetAtmosphereSet(float3 observerOffset, float3 direction, float maximumDistance,
    PlanetAtmosphereSet planets, bool includeRadiance, bool occludeGround,
    out uint groundPlanet, out float endDistance)
{
    AtmosphereTransport result;
    result.Radiance = 0.0F;
    result.Transmission = 1.0F;
    groundPlanet = KEIRE_MAX_PLANET_ATMOSPHERES;
    endDistance = maximumDistance;
    float2 intervals[KEIRE_MAX_PLANET_ATMOSPHERES];
    [unroll]
    for (uint clearIndex = 0U; clearIndex < KEIRE_MAX_PLANET_ATMOSPHERES; ++clearIndex)
    {
        intervals[clearIndex] = 0.0F;
    }
    [loop]
    for (uint index = 0U; index < planets.Controls.x; ++index)
    {
        const AtmosphereMedium medium = planets.Media[index];
        const float3 origin = medium.ObserverRadius.xyz + observerOffset;
        float2 interval;
        if (AtmosphereSphereInterval(origin, direction, medium.ObserverRadius.w + medium.SunHeight.w, interval))
        {
            intervals[index] = max(interval, 0.0F);
        }
        if (occludeGround)
        {
            const bool inside = length(origin) < medium.ObserverRadius.w;
            float2 ground;
            const bool hit = AtmosphereMediumGroundIntersection(origin, direction, medium, ground);
            const float distance = inside ? 0.0F : (hit ? ground.x : maximumDistance + 1.0F);
            if ((inside || hit) && distance <= endDistance &&
                (groundPlanet == KEIRE_MAX_PLANET_ATMOSPHERES || distance < endDistance))
            {
                groundPlanet = index;
                endDistance = distance;
            }
        }
    }
    float current = 0.0F;
    // Every iteration crosses an entry or exit boundary, so at most 2N+1 intervals exist.
    [loop]
    for (uint region = 0U; region <= 2U * planets.Controls.x && current < endDistance; ++region)
    {
        float next = endDistance;
        [loop]
        for (uint index = 0U; index < planets.Controls.x; ++index)
        {
            if (intervals[index].x > current)
            {
                next = min(next, intervals[index].x);
            }
            if (intervals[index].y > current)
            {
                next = min(next, intervals[index].y);
            }
        }
        const float midpoint = current + 0.5F * (next - current);
        uint samples = 0U;
        [loop]
        for (uint index = 0U; index < planets.Controls.x; ++index)
        {
            if (midpoint > intervals[index].x && midpoint < intervals[index].y)
            {
                samples = max(samples, (uint)planets.Media[index].Controls.y);
            }
        }
        const float stepLength = (next - current) / max(samples, 1U);
        [loop]
        for (uint sampleIndex = 0U; sampleIndex < samples; ++sampleIndex)
        {
            const float3 offset = observerOffset + direction * (current + (sampleIndex + 0.5F) * stepLength);
            float3 extinction = 0.0F;
            float3 source = 0.0F;
            [loop]
            for (uint index = 0U; index < planets.Controls.x; ++index)
            {
                if (midpoint <= intervals[index].x || midpoint >= intervals[index].y)
                {
                    continue;
                }
                const AtmosphereMedium medium = planets.Media[index];
                const float3 density = AtmosphereMediumDensity(medium.ObserverRadius.xyz + offset, medium);
                extinction += AtmosphereMediumExtinction(density, medium);
                if (includeRadiance)
                {
                    const float cosine = dot(direction, medium.SunHeight.xyz);
                    const float rayleigh = 3.0F * (1.0F + cosine * cosine) / (16.0F * 3.14159265359F);
                    const float g = medium.Mie.z;
                    const float mie = (1.0F - g * g) /
                        (4.0F * 3.14159265359F * pow(max(1.0F + g * g - 2.0F * g * cosine, 1.0e-5F), 1.5F));
                    source += medium.SolarWidth.xyz *
                        (medium.RayleighScale.xyz * density.x * rayleigh + medium.Mie.x * density.y * mie);
                }
            }
            if (includeRadiance && any(source > 0.0F))
            {
                const float3 opticalDepth = extinction * stepLength;
                const float3 weight = float3(VolumeSegmentWeight(opticalDepth.x),
                    VolumeSegmentWeight(opticalDepth.y), VolumeSegmentWeight(opticalDepth.z));
                result.Radiance += result.Transmission * source * stepLength * weight *
                    PlanetAtmosphereSunTransmission(offset, planets);
            }
            result.Transmission *= exp(-extinction * stepLength);
        }
        current = next;
    }
    return result;
}
