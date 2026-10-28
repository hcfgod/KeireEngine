#pragma once

// All distances are metres along the corresponding view ray. Negative scene distance denotes sky.
bool CloudTemporalDepthMatches(float expectedDistance, float historyDistance, float absoluteTolerance,
    float relativeTolerance)
{
    if (!isfinite(expectedDistance) || !isfinite(historyDistance) ||
        !isfinite(absoluteTolerance) || !isfinite(relativeTolerance) ||
        expectedDistance <= 0.0F || historyDistance <= 0.0F)
    {
        return false;
    }
    const float tolerance = max(max(absoluteTolerance, 0.0F),
        max(relativeTolerance, 0.0F) * expectedDistance);
    return abs(historyDistance - expectedDistance) <= tolerance;
}

bool CloudTemporalSceneMatches(float currentSceneDistance, float previousSceneDistance,
    float expectedPreviousSceneDistance, float absoluteTolerance, float relativeTolerance)
{
    if (!isfinite(currentSceneDistance) || !isfinite(previousSceneDistance))
    {
        return false;
    }
    if (currentSceneDistance < 0.0F || previousSceneDistance < 0.0F)
    {
        return currentSceneDistance < 0.0F && previousSceneDistance < 0.0F;
    }
    return CloudTemporalDepthMatches(expectedPreviousSceneDistance, previousSceneDistance,
        absoluteTolerance, relativeTolerance);
}
