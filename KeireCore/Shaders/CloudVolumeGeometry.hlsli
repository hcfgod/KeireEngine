#pragma once

struct CloudVolumeInterval
{
    float Entry;
    float Exit;
};

// Shift interval boundaries while retaining both clipped end segments. A positive phase uses
// steps + 1 intervals; discarding the first/last partial segment would change optical thickness.
CloudVolumeInterval CloudVolumeShiftedInterval(float entry, float exitDistance, uint steps, uint index, float phase)
{
    const float stepLength = (exitDistance - entry) / float(max(steps, 1U));
    CloudVolumeInterval interval;
    interval.Entry = max(entry, entry + (float(index) - 1.0F + phase) * stepLength);
    interval.Exit = min(exitDistance, entry + (float(index) + phase) * stepLength);
    return interval;
}

// A zero-length interval is a miss. maximumDistance is the smaller of scene depth and the view budget.
// Shared scalar implementation is also compiled by the numerical regression tests.
CloudVolumeInterval IntersectCloudVolumeSlab(float originHeight, float directionHeight, float baseHeight,
                                            float thickness, float maximumDistance)
{
    CloudVolumeInterval interval;
    interval.Entry = 0.0F;
    interval.Exit = 0.0F;
    const float topHeight = baseHeight + thickness;
    if (!isfinite(originHeight) || !isfinite(directionHeight) || !isfinite(baseHeight) ||
        !isfinite(thickness) || !isfinite(topHeight) || !isfinite(maximumDistance) ||
        thickness <= 0.0F || topHeight <= baseHeight || maximumDistance <= 0.0F)
    {
        return interval;
    }

    if (directionHeight == 0.0F)
    {
        if (originHeight > baseHeight && originHeight < topHeight)
        {
            interval.Exit = maximumDistance;
        }
        return interval;
    }

    const float first = (baseHeight - originHeight) / directionHeight;
    const float second = (topHeight - originHeight) / directionHeight;
    const float entry = max(0.0F, min(first, second));
    const float exitDistance = min(maximumDistance, max(first, second));
    if (exitDistance > entry)
    {
        interval.Entry = entry;
        interval.Exit = exitDistance;
    }
    return interval;
}

// Shadow rays start at an interior density sample. Use the same explicit distance budget as view rays;
// a near-horizontal sun must not switch abruptly to an unrelated pattern-size cutoff.
float CloudVolumeShadowDistance(float sampleHeight, float sunHeight, float baseHeight,
                                float thickness, float maximumDistance)
{
    const CloudVolumeInterval interval =
        IntersectCloudVolumeSlab(sampleHeight, sunHeight, baseHeight, thickness, maximumDistance);
    return interval.Exit;
}
