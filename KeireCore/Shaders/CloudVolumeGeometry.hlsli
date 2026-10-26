#pragma once

struct CloudVolumeInterval
{
    float Entry;
    float Exit;
};

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
