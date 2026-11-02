#pragma once

#ifndef KEIRE_PLANET_VOLUME_RAY_INTERVALS_HLSLI
#define KEIRE_PLANET_VOLUME_RAY_INTERVALS_HLSLI

#include "PlanetVolumeWeatherUniforms.hlsli"
#include "PlanetVolumeIntervalGrid.hlsli"

struct PlanetVolumeRayIntervals
{
    CloudVolumeInterval FirstCloud;
    CloudVolumeInterval SecondCloud;
    CloudVolumeInterval FogRange;
    uint FirstCloudSamples;
    uint SecondCloudSamples;
    uint FogSamples;
    uint LocalCount;
    uint ExclusionCount;
    CloudVolumeInterval Local[16];
    CloudVolumeInterval Exclusions[32];
};

PlanetVolumeRayIntervals MakePlanetVolumeRayIntervals(float3 origin, float3 direction, float maximumDistance,
    CloudVolumeParameters cloud, FogViewDensity fog)
{
    PlanetVolumeRayIntervals result = (PlanetVolumeRayIntervals)0;
    const bool cloudsEnabled = cloud.Thickness > 0.0F && cloud.PatternSize > 0.0F &&
        cloud.Extinction > 0.0F && cloud.Coverage > 0.0F;
    if (cloudsEnabled)
    {
        const float variation = VolumeCloudBaseHeightVariation(cloud);
        const PlanetaryCloudIntervals shell = LimitPlanetaryCloudTravel(
            PlanetaryCloudRayIntervals(origin, direction, cloud.PlanetaryCloud,
                cloud.BaseHeight - variation, cloud.Thickness + 2.0F * variation, maximumDistance),
            cloud.MaximumDistance);
        result.FirstCloud.Entry = shell.FirstEntry;
        result.FirstCloud.Exit = shell.FirstExit;
        result.SecondCloud.Entry = shell.SecondEntry;
        result.SecondCloud.Exit = shell.SecondExit;
        const float firstLength = max(shell.FirstExit - shell.FirstEntry, 0.0F);
        const float secondLength = max(shell.SecondExit - shell.SecondEntry, 0.0F);
        const uint samples = clamp(cloud.ViewSteps, secondLength > 0.0F ? 2U : 1U, 128U);
        result.FirstCloudSamples = samples;
        if (secondLength > 0.0F)
        {
            result.FirstCloudSamples = max(1U, min(samples - 1U,
                uint(float(samples) * firstLength / max(firstLength + secondLength, 0.0001F))));
            result.SecondCloudSamples = samples - result.FirstCloudSamples;
        }
    }
    if (fog.Controls.x != 0U && (fog.HeightDensity.z > 0.0F || fog.Controls.z > 0U))
    {
        result.FogRange.Exit = min(maximumDistance, fog.HeightDensity.w);
        result.FogSamples = clamp(fog.Controls.y, 1U, 128U);
        result.LocalCount = min(fog.Controls.z, 16U);
        result.ExclusionCount = min(fog.Controls.w, 16U) * 2U;
        [loop]
        for (uint index = 0U; index < result.LocalCount; ++index)
        {
            result.Local[index] = IntersectFogBox(origin, direction, fog.Local[index], result.FogRange.Exit);
        }
        [loop]
        for (uint index = 0U; index < result.ExclusionCount / 2U; ++index)
        {
            FogLocalDensity box;
            box.CenterExtinction = fog.Exclusions[index].CenterStrength;
            box.HalfExtentEdge = fog.Exclusions[index].HalfExtentEdge;
            result.Exclusions[index * 2U] = IntersectFogBox(origin, direction, box, result.FogRange.Exit);
            box.HalfExtentEdge.xyz *= max(1.0F - box.HalfExtentEdge.w, 1.0e-6F);
            result.Exclusions[index * 2U + 1U] = IntersectFogBox(origin, direction, box, result.FogRange.Exit);
        }
    }
    return result;
}

float NextPlanetVolumeRayBoundary(float cursor, float candidate, PlanetVolumeRayIntervals intervals,
    float cloudSamplePhase)
{
    candidate = NextPlanetVolumeGridBoundary(cursor, candidate, intervals.FirstCloud,
        intervals.FirstCloudSamples, cloudSamplePhase);
    candidate = NextPlanetVolumeGridBoundary(cursor, candidate, intervals.SecondCloud,
        intervals.SecondCloudSamples, cloudSamplePhase);
    candidate = NextPlanetVolumeGridBoundary(cursor, candidate, intervals.FogRange, intervals.FogSamples, 0.0F);
    [loop]
    for (uint index = 0U; index < intervals.LocalCount; ++index)
    {
        candidate = NextVolumeIntervalBoundary(cursor, candidate, intervals.Local[index]);
    }
    [loop]
    for (uint index = 0U; index < intervals.ExclusionCount; ++index)
    {
        candidate = NextVolumeIntervalBoundary(cursor, candidate, intervals.Exclusions[index]);
    }
    return candidate;
}

bool InsidePlanetVolumeCloud(float entry, float exitDistance, PlanetVolumeRayIntervals intervals)
{
    // Membership follows interval overlap, not a rounded midpoint that may equal an endpoint.
    return min(exitDistance, intervals.FirstCloud.Exit) > max(entry, intervals.FirstCloud.Entry) ||
        min(exitDistance, intervals.SecondCloud.Exit) > max(entry, intervals.SecondCloud.Entry);
}

// Each cloud contributes at most 128 grid exits, two shifted leading segments and two entries.
// Each of nine fog profiles contributes at most 128 grid exits, 32 box edges and 64 exclusion edges.
// Include one final surface boundary. This bounds traversal; it is not a density quality truncation.
#define KEIRE_PLANET_VOLUME_MAX_RAY_INTERVALS (8U * 132U + 9U * 224U + 1U)

#endif // KEIRE_PLANET_VOLUME_RAY_INTERVALS_HLSLI
