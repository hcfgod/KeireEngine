#pragma once

#include "VolumeTransport.hlsli"

// Validated single-scattering medium: lengths in kilometres, coefficients in inverse kilometres.
// No bound resources or pixel derivatives, so sky, surface and compute paths can share this transport.
struct AtmosphereMedium
{
    float4 ObserverRadius;
    float4 SunHeight;
    float4 RayleighScale;
    float4 Mie;
    float4 AbsorptionPeak;
    float4 SolarWidth;
    float4 Controls;
};

bool AtmosphereSphereInterval(float3 origin, float3 direction, float radius, out float2 interval)
{
    const float along = dot(origin, direction);
    const float originRadius = length(origin);
    // Factoring the radial difference preserves surface roots; subtracting two squared radii loses metres.
    const float constant = (originRadius - radius) * (originRadius + radius);
    const float discriminant = mad(along, along, -constant);
    const float roundoff = 4.0F * 1.192092896e-7F * max(along * along, abs(constant));
    interval = 0.0F;
    if (discriminant < -roundoff)
    {
        return false;
    }
    const float root = sqrt(max(discriminant, 0.0F));
    const float stableRoot = -along - (along >= 0.0F ? root : -root);
    const float otherRoot = stableRoot != 0.0F ? constant / stableRoot : 0.0F;
    interval = float2(min(stableRoot, otherRoot), max(stableRoot, otherRoot));
    return interval.y >= 0.0F;
}

bool AtmosphereMediumGroundIntersection(float3 origin, float3 direction, AtmosphereMedium medium, out float2 interval)
{
    // Surface-outward and exact tangent rays see the atmosphere; inward surface rays stop at distance zero.
    return AtmosphereSphereInterval(origin, direction, medium.ObserverRadius.w, interval) &&
           dot(origin, direction) < 0.0F && interval.x >= 0.0F;
}

float3 AtmosphereMediumDensity(float3 position, AtmosphereMedium medium)
{
    const float altitude = max(length(position) - medium.ObserverRadius.w, 0.0F);
    return float3(exp(-altitude / medium.RayleighScale.w),
                  exp(-altitude / medium.Mie.w),
                  saturate(1.0F - abs(altitude - medium.AbsorptionPeak.w) / medium.SolarWidth.w));
}

float3 AtmosphereMediumExtinction(float3 integratedDensity, AtmosphereMedium medium)
{
    return medium.RayleighScale.xyz * integratedDensity.x + medium.Mie.y * integratedDensity.y +
           medium.AbsorptionPeak.xyz * integratedDensity.z;
}

float3 AtmosphereMediumSunTransmission(float3 position, AtmosphereMedium medium)
{
    const float3 sunDirection = medium.SunHeight.xyz;
    float2 groundInterval;
    if (AtmosphereMediumGroundIntersection(position, sunDirection, medium, groundInterval))
    {
        return 0.0F;
    }
    float2 atmosphereInterval;
    if (!AtmosphereSphereInterval(position, sunDirection,
                                  medium.ObserverRadius.w + medium.SunHeight.w, atmosphereInterval))
    {
        return 1.0F;
    }
    const uint samples = (uint)medium.Controls.z;
    // Space observers can enter the atmosphere later along the ray. Vacuum must not consume quadrature samples.
    const float startDistance = max(atmosphereInterval.x, 0.0F);
    const float stepLength = max(atmosphereInterval.y - startDistance, 0.0F) / samples;
    float3 integratedDensity = 0.0F;
    [loop]
    for (uint index = 0; index < samples; ++index)
    {
        integratedDensity += AtmosphereMediumDensity(
            position + sunDirection * (startDistance + (index + 0.5F) * stepLength), medium) * stepLength;
    }
    return exp(-AtmosphereMediumExtinction(integratedDensity, medium));
}

struct AtmosphereTransport
{
    float3 Radiance;
    float3 Transmission;
};

// Normalized direction, finite nonnegative maximum distance in kilometres, validated medium parameters.
// Includes single-scattered sunlight and planet occlusion, but no solar disc or background radiance.
// A caller composes this interval as Radiance + Transmission * radianceBehindInterval.
AtmosphereTransport IntegrateAtmosphereMedium(float3 origin, float3 direction, float maximumDistance,
                                               AtmosphereMedium medium, bool includeRadiance, bool occludeGround)
{
    AtmosphereTransport result;
    result.Radiance = 0.0F;
    result.Transmission = 1.0F;
    if (medium.Controls.x <= 0.5F || maximumDistance <= 0.0F)
    {
        return result;
    }
    if (occludeGround && length(origin) < medium.ObserverRadius.w)
    {
        result.Transmission = 0.0F;
        return result;
    }

    float2 atmosphereInterval;
    if (!AtmosphereSphereInterval(origin, direction, medium.ObserverRadius.w + medium.SunHeight.w,
                                  atmosphereInterval))
    {
        return result;
    }
    float2 groundInterval;
    const bool groundHit = occludeGround && AtmosphereMediumGroundIntersection(origin, direction, medium, groundInterval) &&
                           groundInterval.x <= maximumDistance;
    const float startDistance = max(atmosphereInterval.x, 0.0F);
    const float endDistance = min(maximumDistance,
        groundHit ? min(atmosphereInterval.y, groundInterval.x) : atmosphereInterval.y);
    const uint samples = (uint)medium.Controls.y;
    const float stepLength = max(endDistance - startDistance, 0.0F) / samples;
    // Extinction still applies at night; only the solar source and its shadow rays disappear.
    const bool integrateSolarRadiance = includeRadiance && any(medium.SolarWidth.xyz > 0.0F);
    const float pi = 3.14159265359F;
    const float cosineTheta = dot(direction, medium.SunHeight.xyz);
    const float rayleighPhase = 3.0F * (1.0F + cosineTheta * cosineTheta) / (16.0F * pi);
    const float anisotropy = medium.Mie.z;
    const float miePhase = (1.0F - anisotropy * anisotropy) /
        (4.0F * pi * pow(max(1.0F + anisotropy * anisotropy - 2.0F * anisotropy * cosineTheta, 1.0e-5F), 1.5F));
    float3 integratedDensity = 0.0F;
    if (stepLength > 0.0F)
    {
        [loop]
        for (uint index = 0U; index < samples; ++index)
        {
            const float3 position = origin + direction * (startDistance + (index + 0.5F) * stepLength);
            const float3 segmentDensity = AtmosphereMediumDensity(position, medium) * stepLength;
            if (integrateSolarRadiance)
            {
                const float3 segmentExtinction = AtmosphereMediumExtinction(segmentDensity, medium);
                const float3 segmentWeight = float3(VolumeSegmentWeight(segmentExtinction.x),
                    VolumeSegmentWeight(segmentExtinction.y), VolumeSegmentWeight(segmentExtinction.z));
                const float3 transmission = exp(-AtmosphereMediumExtinction(integratedDensity, medium)) * segmentWeight;
                const float3 scattering = medium.RayleighScale.xyz * segmentDensity.x * rayleighPhase +
                                          medium.Mie.x * segmentDensity.y * miePhase;
                result.Radiance += transmission * AtmosphereMediumSunTransmission(position, medium) * scattering;
            }
            integratedDensity += segmentDensity;
        }
    }
    result.Radiance *= medium.SolarWidth.xyz;
    result.Transmission = groundHit ? 0.0F : exp(-AtmosphereMediumExtinction(integratedDensity, medium));
    return result;
}

// Authored surfaces supply visibility through scene depth. The atmosphere reference sphere is not
// hidden terrain: below-sea-level geometry receives finite sea-level medium density instead of black.
AtmosphereTransport IntegrateAtmosphereToSurface(float3 origin, float3 direction, float maximumDistance,
                                                 AtmosphereMedium medium, bool includeRadiance)
{
    return IntegrateAtmosphereMedium(origin, direction, maximumDistance, medium, includeRadiance, false);
}

AtmosphereTransport IntegrateAtmosphereMedium(float3 origin, float3 direction, float maximumDistance,
                                               AtmosphereMedium medium, bool includeRadiance)
{
    return IntegrateAtmosphereMedium(origin, direction, maximumDistance, medium, includeRadiance, true);
}

AtmosphereTransport IntegrateAtmosphereMedium(float3 origin, float3 direction, float maximumDistance,
                                               AtmosphereMedium medium)
{
    return IntegrateAtmosphereMedium(origin, direction, maximumDistance, medium, true);
}
