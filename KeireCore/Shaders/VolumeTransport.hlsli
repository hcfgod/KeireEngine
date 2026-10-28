#pragma once

// Mean transmission across a homogeneous segment. Multiply by segment length and source per metre.
// Input is nonnegative optical depth, not distance or an extinction coefficient alone.
float VolumeSegmentWeight(float opticalDepth)
{
    if (opticalDepth < 0.1F)
    {
        return 1.0F - opticalDepth * (0.5F - opticalDepth * (1.0F / 6.0F -
               opticalDepth * (1.0F / 24.0F - opticalDepth * (1.0F / 120.0F - opticalDepth / 720.0F))));
    }
    return (1.0F - exp(-opticalDepth)) / opticalDepth;
}

struct VolumeIntegratedMedium
{
    float Transmittance;
    float Radiance;
};

// Coefficients are finite, nonnegative and summed over all media occupying this segment.
// Source is lit radiance per metre, not albedo. This also supports emissive vacuum.
VolumeIntegratedMedium IntegrateVolumeMedium(float extinction, float source, float distance)
{
    const float opticalDepth = extinction * distance;
    VolumeIntegratedMedium result;
    result.Transmittance = exp(-opticalDepth);
    // Divide by extinction in the thick branch to avoid overflowing source * distance.
    result.Radiance = opticalDepth < 0.1F
        ? source * distance * VolumeSegmentWeight(opticalDepth)
        : (source / extinction) * (1.0F - result.Transmittance);
    return result;
}
