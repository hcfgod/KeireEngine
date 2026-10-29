#pragma once

#include "FogExclusion.hlsli"

struct FogLocalDensity
{
    float4 CenterExtinction;
    float4 HalfExtentEdge;
};

struct FogViewDensity
{
    float4 HeightDensity;
    float4 Scattering;
    uint4 Controls;
    FogLocalDensity Local[16];
    FogExclusionDensity Exclusions[16];
};

float SampleLocalFogDensity(float3 cameraRelativePosition, FogViewDensity fog)
{
    float extinction = 0.0F;
    for (uint index = 0U; index < min(fog.Controls.z, 16U); ++index)
    {
        const FogLocalDensity local = fog.Local[index];
        const float3 normalized = abs(cameraRelativePosition - local.CenterExtinction.xyz) / local.HalfExtentEdge.xyz;
        const float distance = max(normalized.x, max(normalized.y, normalized.z));
        if (distance < 1.0F)
        {
            const float edge = local.HalfExtentEdge.w == 0.0F ? 1.0F :
                saturate((1.0F - distance) / local.HalfExtentEdge.w);
            extinction += local.CenterExtinction.w * edge * edge * (3.0F - 2.0F * edge);
        }
    }
    return extinction;
}

float FogHeightOffset(float3 cameraRelativePosition, FogViewDensity fog, float3 planetObserverKilometres)
{
    if (fog.Controls.x != 2U)
    {
        return cameraRelativePosition.y;
    }
    // The host computes camera altitude in double precision. Only evaluate its local change here;
    // subtracting two planet-sized float radii would erase small surface heights.
    const float3 displacement = cameraRelativePosition * 0.001F;
    const float denominator = length(planetObserverKilometres + displacement) + length(planetObserverKilometres);
    return denominator > 0.0F ?
        1000.0F * dot(displacement, 2.0F * planetObserverKilometres + displacement) / denominator : 0.0F;
}

float SampleHeightFogDensity(float3 position, FogViewDensity fog, float3 planetObserverKilometres)
{
    const float height = max(0.0F, FogHeightOffset(position, fog, planetObserverKilometres) - fog.HeightDensity.x);
    return fog.HeightDensity.z * exp(-height * fog.HeightDensity.y);
}

float SampleFogDensity(float3 cameraRelativePosition, FogViewDensity fog, float3 planetObserverKilometres)
{
    if (fog.Controls.x == 0U)
    {
        return 0.0F;
    }
    const float extinction = SampleHeightFogDensity(cameraRelativePosition, fog, planetObserverKilometres) +
        SampleLocalFogDensity(cameraRelativePosition, fog);
    return extinction * FogExclusionRetention(cameraRelativePosition, fog.Controls.w, fog.Exclusions);
}

// Flat-height callers have no planet descriptor. Planetary callers must supply the observer explicitly.
float SampleFogDensity(float3 cameraRelativePosition, FogViewDensity fog)
{
    return SampleFogDensity(cameraRelativePosition, fog, float3(0.0F, 0.0F, 0.0F));
}

// Integrate exponential height density exactly, including segments crossing the constant-density base.
// The small-span series avoids cancellation for horizontal rays and very small height falloff.
float AverageHeightFogDensity(float firstHeight, float secondHeight, FogViewDensity fog)
{
    const float low = min(firstHeight, secondHeight) - fog.HeightDensity.x;
    const float high = max(firstHeight, secondHeight) - fog.HeightDensity.x;
    if (high <= 0.0F)
    {
        return fog.HeightDensity.z;
    }
    const float aboveLow = max(low, 0.0F);
    const float span = (high - aboveLow) * fog.HeightDensity.y;
    const float exponentialAverage = span < 0.01F ?
        1.0F - span * 0.5F + span * span / 6.0F : (1.0F - exp(-span)) / span;
    const float aboveAverage = exp(-aboveLow * fog.HeightDensity.y) * exponentialAverage;
    const float belowFraction = low < 0.0F ? -low / (high - low) : 0.0F;
    return fog.HeightDensity.z * lerp(aboveAverage, 1.0F, belowFraction);
}

float AverageClampedSmoothstep(float first, float second)
{
    const float low = min(first, second);
    const float high = max(first, second);
    const float width = high - low;
    if (width < 1.0e-5F)
    {
        const float value = saturate(0.5F * (first + second));
        return value * value * (3.0F - 2.0F * value);
    }
    const float a = saturate(low);
    const float b = saturate(high);
    // Divided polynomial integral avoids subtracting nearly equal fourth powers.
    const float rampAverage = a * a + a * b + b * b -
        0.5F * (a * a * a + a * a * b + a * b * b + b * b * b);
    return ((b - a) * rampAverage + max(0.0F, high - max(low, 1.0F))) / width;
}

float AverageLocalFogDensity(float3 midpoint, float3 segmentSpan, FogLocalDensity local)
{
    const float3 center = (midpoint - local.CenterExtinction.xyz) / local.HalfExtentEdge.xyz;
    const float3 halfSpan = segmentSpan * 0.5F / local.HalfExtentEdge.xyz;
    if (any(abs(center) - abs(halfSpan) >= 1.0F))
    {
        return 0.0F;
    }
    const float softness = local.HalfExtentEdge.w;
    if (all(abs(center) + abs(halfSpan) <= 1.0F - softness))
    {
        return local.CenterExtinction.w;
    }
    // The box distance is the maximum of six affine face distances. Each face dominates
    // at most one interval; integrate its cubic ramp exactly over that interval.
    float intercepts[6];
    float slopes[6];
    for (uint axis = 0U; axis < 3U; ++axis)
    {
        intercepts[axis * 2U] = center[axis] - halfSpan[axis];
        slopes[axis * 2U] = 2.0F * halfSpan[axis];
        intercepts[axis * 2U + 1U] = -intercepts[axis * 2U];
        slopes[axis * 2U + 1U] = -slopes[axis * 2U];
    }
    float average = 0.0F;
    for (uint face = 0U; face < 6U; ++face)
    {
        float entry = 0.0F;
        float exit = 1.0F;
        for (uint other = 0U; other < 6U; ++other)
        {
            if (other == face)
            {
                continue;
            }
            const float difference = intercepts[face] - intercepts[other];
            const float slope = slopes[face] - slopes[other];
            if (slope > 0.0F)
            {
                entry = max(entry, -difference / slope);
            }
            else if (slope < 0.0F)
            {
                exit = min(exit, -difference / slope);
            }
            else if (difference < 0.0F || (difference == 0.0F && other < face))
            {
                exit = entry;
            }
        }
        if (exit <= entry)
        {
            continue;
        }
        const float firstDistance = intercepts[face] + slopes[face] * entry;
        const float secondDistance = intercepts[face] + slopes[face] * exit;
        if (softness > 0.0F)
        {
            average += (exit - entry) * AverageClampedSmoothstep(
                (1.0F - firstDistance) / softness, (1.0F - secondDistance) / softness);
        }
        else
        {
            const float low = min(firstDistance, secondDistance);
            const float high = max(firstDistance, secondDistance);
            const float fraction = high == low ? (low < 1.0F ? 1.0F : 0.0F) :
                saturate((1.0F - low) / (high - low));
            average += (exit - entry) * fraction;
        }
    }
    return saturate(average) * local.CenterExtinction.w;
}

float IntegrateExcludedRadialInterval(float low, float high, float3 closest, float3 direction,
                                     FogViewDensity fog, float3 observer)
{
    if (high <= low)
    {
        return 0.0F;
    }
    const float nodes[4] = {0.1834346425F, 0.5255324099F, 0.7966664774F, 0.9602898565F};
    const float weights[4] = {0.3626837834F, 0.3137066459F, 0.2223810345F, 0.1012285363F};
    float result = 0.0F;
    for (uint part = 0U; part < 4U; ++part)
    {
        const float halfWidth = (high - low) * 0.125F;
        const float center = low + (float(part) + 0.5F) * (high - low) * 0.25F;
        for (uint node = 0U; node < 4U; ++node)
        {
            const float offset = halfWidth * nodes[node];
            const float3 left = closest + direction * (center - offset);
            const float3 right = closest + direction * (center + offset);
            result += halfWidth * weights[node] *
                (SampleHeightFogDensity(left, fog, observer) *
                 FogExclusionRetention(left, fog.Controls.w, fog.Exclusions) +
                 SampleHeightFogDensity(right, fog, observer) *
                 FogExclusionRetention(right, fog.Controls.w, fog.Exclusions));
        }
    }
    return result;
}

float SampleExcludedRadialHeight(float3 midpoint, float3 span, FogViewDensity fog, float3 observer)
{
    const float segmentLength = length(span);
    if (segmentLength <= 0.0F)
    {
        return SampleHeightFogDensity(midpoint, fog, observer) *
            FogExclusionRetention(midpoint, fog.Controls.w, fog.Exclusions);
    }
    const float3 direction = span / segmentLength;
    if (fog.HeightDensity.y == 0.0F)
    {
        return IntegrateExcludedRadialInterval(-segmentLength * 0.5F, segmentLength * 0.5F,
                                              midpoint, direction, fog, observer) / segmentLength;
    }
    const float closestDistance = -dot(observer * 1000.0F + midpoint, direction);
    const float3 closest = midpoint + direction * closestDistance;
    const float radius = length(observer + closest * 0.001F) * 1000.0F;
    const float baseDelta = fog.HeightDensity.x - FogHeightOffset(closest, fog, observer);
    const float outerDelta = baseDelta + 24.0F / fog.HeightDensity.y;
    if (outerDelta <= 0.0F)
    {
        return 0.0F;
    }
    const float outerHalf = sqrt(outerDelta * (2.0F * radius + outerDelta));
    const float coreHalf = baseDelta > 0.0F ? sqrt(baseDelta * (2.0F * radius + baseDelta)) : 0.0F;
    const float first = max(-segmentLength * 0.5F - closestDistance, -outerHalf);
    const float last = min(segmentLength * 0.5F - closestDistance, outerHalf);
    return (IntegrateExcludedRadialInterval(first, min(last, -coreHalf), closest, direction, fog, observer) +
            IntegrateExcludedRadialInterval(max(first, -coreHalf), min(last, coreHalf), closest,
                                            direction, fog, observer) +
            IntegrateExcludedRadialInterval(max(first, coreHalf), last, closest, direction, fog, observer)) /
           segmentLength;
}

float SampleUnexcludedFogSegmentDensity(float3 midpoint, float3 segmentSpan, FogViewDensity fog,
                                       float3 planetObserverKilometres)
{
    const float halfSpan = abs(segmentSpan.y) * 0.5F;
    float extinction = AverageHeightFogDensity(midpoint.y - halfSpan, midpoint.y + halfSpan, fog);
    if (fog.Controls.x == 2U)
    {
        // Radial altitude is nonlinear along a tangent ray; the flat exponential integral does not apply.
        const float firstHeight = FogHeightOffset(midpoint - segmentSpan * 0.5F, fog, planetObserverKilometres);
        const float lastHeight = FogHeightOffset(midpoint + segmentSpan * 0.5F, fog, planetObserverKilometres);
        const float middleHeight = FogHeightOffset(midpoint, fog, planetObserverKilometres);
        const float curvature = abs(middleHeight - 0.5F * (firstHeight + lastHeight));
        if (curvature * fog.HeightDensity.y <= 1.0e-5F)
        {
            // Radial rays are linear even with extremely steep falloff. An analytic average avoids
            // missing their thin endpoint layer and saves eight density samples on locally flat rays.
            extinction = AverageHeightFogDensity(firstHeight, lastHeight, fog);
        }
        else
        {
            const float segmentLength = length(segmentSpan);
            const float3 direction = segmentSpan / segmentLength;
            const float closestDistance = -dot(planetObserverKilometres * 1000.0F + midpoint, direction);
            const float3 closestPosition = midpoint + direction * closestDistance;
            const float closestHeight = FogHeightOffset(closestPosition, fog, planetObserverKilometres);
            const float closestRadius = length(planetObserverKilometres + closestPosition * 0.001F) * 1000.0F;
            const float baseDelta = fog.HeightDensity.x - closestHeight;
            // Beyond 24 scale heights the density contributes less than 4e-11 of the base density.
            const float outerDelta = baseDelta + 24.0F / fog.HeightDensity.y;
            extinction = 0.0F;
            if (outerDelta > 0.0F)
            {
                const float outerHalf = sqrt(outerDelta * (2.0F * closestRadius + outerDelta));
                const float coreHalf = baseDelta > 0.0F ?
                    sqrt(baseDelta * (2.0F * closestRadius + baseDelta)) : 0.0F;
                const float first = max(-segmentLength * 0.5F - closestDistance, -outerHalf);
                const float last = min(segmentLength * 0.5F - closestDistance, outerHalf);
                const float coreLength = max(0.0F, min(last, coreHalf) - max(first, -coreHalf));
                extinction = coreLength * fog.HeightDensity.z / segmentLength;
                const float nodes[4] = {0.1834346425F, 0.5255324099F, 0.7966664774F, 0.9602898565F};
                const float weights[4] = {0.3626837834F, 0.3137066459F, 0.2223810345F, 0.1012285363F};
                for (uint side = 0U; side < 2U; ++side)
                {
                    const float low = side == 0U ? first : max(first, coreHalf);
                    const float high = side == 0U ? min(last, -coreHalf) : last;
                    if (high <= low)
                    {
                        continue;
                    }
                    const float halfWidth = (high - low) * 0.5F;
                    const float center = (high + low) * 0.5F;
                    for (uint node = 0U; node < 4U; ++node)
                    {
                        const float offset = halfWidth * nodes[node];
                        extinction += halfWidth * weights[node] / segmentLength *
                            (SampleHeightFogDensity(closestPosition + direction * (center - offset), fog,
                                                    planetObserverKilometres) +
                             SampleHeightFogDensity(closestPosition + direction * (center + offset), fog,
                                                    planetObserverKilometres));
                    }
                }
            }
        }
    }
    for (uint index = 0U; index < min(fog.Controls.z, 16U); ++index)
    {
        extinction += AverageLocalFogDensity(midpoint, segmentSpan, fog.Local[index]);
    }
    return extinction;
}

float SampleFogSegmentDensity(float3 midpoint, float3 segmentSpan, FogViewDensity fog,
                             float3 planetObserverKilometres)
{
    float original = SampleUnexcludedFogSegmentDensity(midpoint, segmentSpan, fog, planetObserverKilometres);
    if (fog.Controls.w == 0U || original <= 0.0F)
    {
        return original;
    }
    // The marcher splits outer and core boundaries first. Integrate the remaining soft transition with
    // bounded Gauss-Legendre quadrature; retain the exact unexcluded integral outside affected segments.
    bool affected = false;
    float coreStrength = 0.0F;
    float maximumStrength = 0.0F;
    for (uint index = 0U; index < min(fog.Controls.w, 16U); ++index)
    {
        const FogExclusionDensity region = fog.Exclusions[index];
        const float3 nearest = abs(midpoint - region.CenterStrength.xyz) - abs(segmentSpan) * 0.5F;
        if (region.CenterStrength.w <= 0.0F || any(nearest >= region.HalfExtentEdge.xyz))
        {
            continue;
        }
        affected = true;
        maximumStrength = max(maximumStrength, region.CenterStrength.w);
        const float3 farthest = abs(midpoint - region.CenterStrength.xyz) + abs(segmentSpan) * 0.5F;
        if (all(farthest <= region.HalfExtentEdge.xyz * (1.0F - region.HalfExtentEdge.w)))
        {
            coreStrength = max(coreStrength, region.CenterStrength.w);
            if (coreStrength >= 1.0F)
            {
                return 0.0F;
            }
        }
    }
    if (!affected)
    {
        return original;
    }
    if (coreStrength >= maximumStrength)
    {
        return original * (1.0F - coreStrength);
    }
    float radialHeight = 0.0F;
    if (fog.Controls.x == 2U && fog.HeightDensity.z > 0.0F)
    {
        radialHeight = SampleExcludedRadialHeight(midpoint, segmentSpan, fog, planetObserverKilometres);
        // Local boxes retain their own interval integration; their density can lie outside the radial shell.
        fog.HeightDensity.z = 0.0F;
        original = 0.0F;
        for (uint index = 0U; index < min(fog.Controls.z, 16U); ++index)
        {
            original += AverageLocalFogDensity(midpoint, segmentSpan, fog.Local[index]);
        }
        if (original == 0.0F)
        {
            return radialHeight;
        }
    }
    const float nodes[4] = {0.1834346425F, 0.5255324099F, 0.7966664774F, 0.9602898565F};
    const float weights[4] = {0.3626837834F, 0.3137066459F, 0.2223810345F, 0.1012285363F};
    float retained = 0.0F;
    float reference = 0.0F;
    // Subdivision resolves changes in the strongest region and dominant box face inside a soft interval.
    for (uint subdivision = 0U; subdivision < 4U; ++subdivision)
    {
        for (uint node = 0U; node < 4U; ++node)
        {
            for (uint side = 0U; side < 2U; ++side)
            {
                const float fraction = (float(subdivision) + 0.5F +
                    nodes[node] * (side == 0U ? -0.5F : 0.5F)) * 0.25F - 0.5F;
                const float3 position = midpoint + segmentSpan * fraction;
                const float density = SampleHeightFogDensity(position, fog, planetObserverKilometres) +
                    SampleLocalFogDensity(position, fog);
                const float weighted = weights[node] * density;
                reference += weighted;
                retained += weighted * FogExclusionRetention(position, fog.Controls.w, fog.Exclusions);
            }
        }
    }
    return radialHeight + (reference > 0.0F ? original * saturate(retained / reference) : 0.0F);
}
