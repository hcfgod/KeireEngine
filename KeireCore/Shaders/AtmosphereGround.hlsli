#pragma once

#include "AtmosphereMedium.hlsli"

// The atmosphere's opaque reference sphere is a radiance boundary, not scene geometry.
// Irradiance is linear RGB; reflectance is in [0, 1]. Distances use the medium's kilometres.
float3 AtmosphereGroundRadiance(float3 origin, float3 direction, float groundDistance,
                               AtmosphereMedium medium, float3 reflectance, float3 skyIrradiance)
{
    const float inversePi = 0.31830988618F;
    const float3 groundNormal = normalize(origin + direction * groundDistance);
    const float sunCosine = saturate(dot(groundNormal, medium.SunHeight.xyz));
    float3 directIrradiance = 0.0F;
    if (sunCosine > 0.0F && any(medium.SolarWidth.xyz > 0.0F))
    {
        // Move one metre above the reference sphere to avoid float surface self-intersection.
        const float3 lightingPosition = groundNormal * (medium.ObserverRadius.w + 0.001F);
        directIrradiance = medium.SolarWidth.xyz * sunCosine *
            AtmosphereMediumSunTransmission(lightingPosition, medium);
    }
    const float3 boundaryRadiance = reflectance * inversePi * (directIrradiance + skyIrradiance);
    // The sky integrator deliberately zeros transmission at opaque ground. Surface transport
    // retains the air transmission needed to compose the ground boundary exactly once.
    const AtmosphereTransport air = IntegrateAtmosphereToSurface(
        origin, direction, groundDistance, medium, true);
    return air.Radiance + air.Transmission * boundaryRadiance;
}
