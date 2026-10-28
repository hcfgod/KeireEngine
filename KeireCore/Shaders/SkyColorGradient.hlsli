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
