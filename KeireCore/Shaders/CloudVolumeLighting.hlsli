#pragma once

// At most this fraction of the background and remaining bounded source radiance is discarded.
// Both cloud blend passes must use the same cutoff and snap the remaining transmission to zero.
static const float VolumeCloudTransmissionCutoff = 0.00001F;

bool VolumeCloudTransportComplete(float transmission)
{
    return transmission <= VolumeCloudTransmissionCutoff;
}

// Mean extinction-event position inside a constant-density interval, normalized to [0,1].
// The small-depth series avoids cancellation; the thick limit avoids overflowing exp.
float VolumeCloudMeanEventFraction(float opticalDepth)
{
    const float depth = max(opticalDepth, 0.0F);
    if (depth < 0.25F)
    {
        const float squared = depth * depth;
        return 0.5F - depth / 12.0F + depth * squared / 720.0F - depth * squared * squared / 30240.0F;
    }
    if (depth >= 20.0F)
    {
        return 1.0F / depth;
    }
    return 1.0F / depth - 1.0F / (exp(depth) - 1.0F);
}

float VolumeCloudPhase(float cosine, float anisotropy)
{
    const float g = clamp(anisotropy, -0.95F, 0.95F);
    const float denominator = max(1.0F + g * g - 2.0F * g * clamp(cosine, -1.0F, 1.0F), 0.0001F);
    return (1.0F - g * g) / (12.5663706144F * denominator * sqrt(denominator));
}

// Two bounded additional scattering orders reuse the sampled light optical depth. Each order reduces
// directional bias and shadow depth. This is an artistic approximation, not an energy-conserving solver.
float VolumeCloudIncidentLight(float cosine, float anisotropy, float lightOpticalDepth,
                               float contribution, float occlusion)
{
    const float depth = max(lightOpticalDepth, 0.0F);
    const float direct = VolumeCloudPhase(cosine, anisotropy) * exp(-depth);
    const float weight = clamp(contribution, 0.0F, 1.0F);
    if (weight == 0.0F)
    {
        return direct;
    }

    const float depthScale = clamp(occlusion, 0.0F, 1.0F);
    const float first = VolumeCloudPhase(cosine, anisotropy * 0.5F) * exp(-depth * depthScale);
    const float second = VolumeCloudPhase(cosine, anisotropy * 0.25F) * exp(-depth * depthScale * depthScale);
    return direct + weight * first + weight * weight * second;
}
