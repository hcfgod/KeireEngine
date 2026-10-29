#pragma once

// Scalar form is shared with numerical tests. Inputs are validated linear HDR radiance, before exposure.
float BlendSkyColorChannel(float physical, float horizon, float zenith, float cosineZenith,
                          float weight, float exponent)
{
    if (weight <= 0.0F)
    {
        return physical;
    }
    const float gradient = pow(clamp(cosineZenith, 0.0F, 1.0F), exponent);
    return lerp(physical, lerp(horizon, zenith, gradient), clamp(weight, 0.0F, 1.0F));
}

// Surface-authored colors fade out across the atmosphere; physical scattering remains visible from space.
float SkyGradientAtmosphereWeight(float observerRadius, float planetRadius, float atmosphereHeight)
{
    if (atmosphereHeight <= 0.0F || observerRadius < planetRadius)
    {
        return 0.0F;
    }
    const float altitudeFraction = clamp((observerRadius - planetRadius) / atmosphereHeight, 0.0F, 1.0F);
    const float transition = altitudeFraction * altitudeFraction * (3.0F - 2.0F * altitudeFraction);
    return 1.0F - transition;
}
