#pragma once

// Shared surface/underwater optics. Inputs are finite linear HDR values; indices are positive,
// extinction is nonnegative inverse metres, and distance is the actual path through water.
float WaterDielectricReflectance(float incidentCosine, float incidentIndex, float transmittedIndex)
{
    if (incidentIndex == transmittedIndex)
    {
        return 0.0F;
    }
    const float cosine = saturate(abs(incidentCosine));
    const float ratio = incidentIndex / transmittedIndex;
    const float transmittedSineSquared = ratio * ratio * max(0.0F, 1.0F - cosine * cosine);
    if (transmittedSineSquared >= 1.0F)
    {
        return 1.0F;
    }
    const float transmittedCosine = sqrt(1.0F - transmittedSineSquared);
    const float perpendicular = (incidentIndex * cosine - transmittedIndex * transmittedCosine) /
        (incidentIndex * cosine + transmittedIndex * transmittedCosine);
    const float parallel = (transmittedIndex * cosine - incidentIndex * transmittedCosine) /
        (transmittedIndex * cosine + incidentIndex * transmittedCosine);
    return saturate(0.5F * (perpendicular * perpendicular + parallel * parallel));
}

float3 WaterPathTransmission(float3 extinctionPerMetre, float pathMetres)
{
    return exp(-extinctionPerMetre * pathMetres);
}

// Depth tint is an authored asymptotic radiance approximation, not a multiple-scattering solver.
// A zero tint produces absorption only. Reflection and transmission retain complementary weights.
float3 ComposeWaterSurfaceRadiance(float3 reflectedRadiance, float3 transmittedRadiance,
    float3 depthTintRadiance, float3 pathTransmission, float reflectance)
{
    const float3 underwater = transmittedRadiance * pathTransmission +
        depthTintRadiance * (1.0F - pathTransmission);
    return lerp(underwater, reflectedRadiance, reflectance);
}
