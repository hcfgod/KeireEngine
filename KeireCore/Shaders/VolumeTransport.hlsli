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
