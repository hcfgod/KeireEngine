#pragma once

#include "CloudVolumeGeometry.hlsli"
#include "FogDensity.hlsli"

// Clip against one box axis without dividing by a zero ray component.
CloudVolumeInterval ClipVolumeRayAxis(CloudVolumeInterval interval, float origin, float direction,
    float center, float halfExtent)
{
    if (direction == 0.0F)
    {
        if (abs(origin - center) >= halfExtent)
        {
            interval.Exit = interval.Entry;
        }
        return interval;
    }
    const float first = (center - halfExtent - origin) / direction;
    const float second = (center + halfExtent - origin) / direction;
    interval.Entry = max(interval.Entry, min(first, second));
    interval.Exit = max(interval.Entry, min(interval.Exit, max(first, second)));
    return interval;
}

CloudVolumeInterval IntersectFogBox(float3 origin, float3 direction, FogLocalDensity box, float maximumDistance)
{
    CloudVolumeInterval interval;
    interval.Entry = 0.0F;
    interval.Exit = maximumDistance;
    for (uint axis = 0U; axis < 3U; ++axis)
    {
        interval = ClipVolumeRayAxis(interval, origin[axis], direction[axis],
            box.CenterExtinction[axis], box.HalfExtentEdge[axis]);
    }
    return interval;
}

// Include local box edges in the quadrature: a thin volume must not fall between two coarse samples.
// The caller precomputes intersections once per ray and uses strict progress rather than an epsilon,
// which could erase valid thin intervals at long distances.
float NextVolumeIntervalBoundary(float cursor, float candidate, CloudVolumeInterval interval)
{
    if (interval.Exit <= interval.Entry)
    {
        return candidate;
    }
    if (interval.Entry > cursor)
    {
        candidate = min(candidate, interval.Entry);
    }
    if (interval.Exit > cursor)
    {
        candidate = min(candidate, interval.Exit);
    }
    return candidate;
}
