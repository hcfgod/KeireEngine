#pragma once

#ifndef KEIRE_PLANET_VOLUME_INTERVAL_GRID_HLSLI
#define KEIRE_PLANET_VOLUME_INTERVAL_GRID_HLSLI

#include "CloudVolumeGeometry.hlsli"

// Merge independently authored sample grids by taking their next boundary. A caller adds extinction
// and extinction-weighted source from every medium covering the resulting interval before transport.
// Vacuum before a distant shell consumes one boundary, not the shell's density sample budget.
float NextPlanetVolumeGridBoundary(float cursor, float candidate, CloudVolumeInterval interval,
    uint requestedSamples, float samplePhase)
{
    if (interval.Exit <= interval.Entry || cursor >= interval.Exit || candidate <= cursor)
    {
        return candidate;
    }
    if (cursor < interval.Entry)
    {
        return min(candidate, interval.Entry);
    }
    const uint samples = min(max(requestedSamples, 1U), 128U);
    const float length = interval.Exit - interval.Entry;
    const float phase = samplePhase > 0.0F ? min(samplePhase, 1.0F) : 1.0F;
    const float gridCoordinate = (cursor - interval.Entry) / length * float(samples);
    uint index = uint(max(0.0F, floor(gridCoordinate - phase) + 1.0F));
    // Rounded subdivisions can coincide at long ray distances. Advance to a representable boundary;
    // an epsilon would incorrectly skip physically representable thin shells and box intersections.
    while (index <= samples)
    {
        const float boundary = min(interval.Exit,
            interval.Entry + length * ((float(index) + phase) / float(samples)));
        if (boundary > cursor)
        {
            return min(candidate, boundary);
        }
        ++index;
    }
    return min(candidate, interval.Exit);
}

#endif // KEIRE_PLANET_VOLUME_INTERVAL_GRID_HLSLI
