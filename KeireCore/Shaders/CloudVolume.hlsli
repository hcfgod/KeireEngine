#pragma once

#include "VolumeTransport.hlsli"
#include "CloudVolumeGeometry.hlsli"

// Local-frame, metre-valued volume kernel. The host must clip the ray to opaque scene depth and the planet.
// This is not the layered-cloud fallback. Integration into render passes and authored settings is pending.
struct CloudVolumeParameters
{
    float BaseHeight;
    float Thickness;
    float PatternSize;
    float Coverage;
    float Extinction;
    float DetailAmount;
    float Anisotropy;
    uint Seed;
    float3 WindOffset;
    uint ViewSteps;
    float3 ScatteringAlbedo;
    uint ShadowSteps;
};

uint VolumeCloudHash(uint3 cell, uint seed)
{
    uint value = cell.x * 0x8da6b343U ^ cell.y * 0xd8163841U ^ cell.z * 0xcb1ab31fU ^ seed;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

float VolumeCloudNoise(float3 position, uint seed)
{
    const float3 base = floor(position);
    const float3 fraction = frac(position);
    const float3 blend = fraction * fraction * (3.0F - 2.0F * fraction);
    // Periodic cells keep integer conversion bounded even after negative wind displacement.
    const uint3 cell = uint3(base - floor(base / 256.0F) * 256.0F);
    float result = 0.0F;
    [unroll]
    for (uint corner = 0U; corner < 8U; ++corner)
    {
        const uint3 offset = uint3(corner & 1U, (corner >> 1U) & 1U, (corner >> 2U) & 1U);
        const float3 weight = lerp(1.0F - blend, blend, float3(offset));
        const float sampleValue = float(VolumeCloudHash((cell + offset) & 255U, seed) >> 8U) / 16777216.0F;
        result += sampleValue * weight.x * weight.y * weight.z;
    }
    return result;
}

float VolumeCloudDensity(float3 position, CloudVolumeParameters parameters)
{
    const float height = (position.y - parameters.BaseHeight) / parameters.Thickness;
    const float envelope = smoothstep(0.0F, 0.15F, height) * (1.0F - smoothstep(0.7F, 1.0F, height));
    const float3 pattern = (position + parameters.WindOffset) / parameters.PatternSize;
    const float shape = VolumeCloudNoise(pattern, parameters.Seed);
    const float detail = VolumeCloudNoise(pattern * 4.0F, parameters.Seed + 71U);
    const float coverage = saturate(parameters.Coverage);
    const float density = saturate((shape - (1.0F - coverage)) / max(coverage, 0.0001F));
    return saturate(density - detail * parameters.DetailAmount) * envelope;
}

float VolumeCloudPhase(float cosine, float anisotropy)
{
    const float g = clamp(anisotropy, -0.95F, 0.95F);
    const float denominator = max(1.0F + g * g - 2.0F * g * cosine, 0.0001F);
    return (1.0F - g * g) / (12.5663706144F * denominator * sqrt(denominator));
}

// Call only with a finite, nonnegative interval already intersected against the cloud volume and scene geometry.
// Directions are normalized. Returns premultiplied linear radiance and opacity, before exposure or bloom.
float4 IntegrateCloudVolume(float3 origin, float3 direction, float entryDistance, float exitDistance,
                            float3 sunDirection, float3 sunRadiance, float3 ambientRadiance,
                            CloudVolumeParameters parameters)
{
    if (exitDistance <= entryDistance || parameters.Thickness <= 0.0F || parameters.PatternSize <= 0.0F ||
        parameters.Extinction <= 0.0F || parameters.Coverage <= 0.0F)
    {
        return 0.0F;
    }

    const CloudVolumeInterval slab = IntersectCloudVolumeSlab(
        origin.y, direction.y, parameters.BaseHeight, parameters.Thickness, exitDistance);
    entryDistance = max(entryDistance, slab.Entry);
    exitDistance = min(exitDistance, slab.Exit);
    if (exitDistance <= entryDistance)
    {
        return 0.0F;
    }

    const uint steps = clamp(parameters.ViewSteps, 1U, 128U);
    const uint shadowSteps = clamp(parameters.ShadowSteps, 1U, 16U);
    const float stepLength = (exitDistance - entryDistance) / float(steps);
    const float phase = VolumeCloudPhase(dot(direction, sunDirection), parameters.Anisotropy);
    float transmission = 1.0F;
    float3 radiance = 0.0F;
    [loop]
    for (uint step = 0U; step < steps; ++step)
    {
        const float3 position = origin + direction * (entryDistance + (float(step) + 0.5F) * stepLength);
        const float extinction = VolumeCloudDensity(position, parameters) * parameters.Extinction;
        if (extinction <= 0.0F)
        {
            continue;
        }

        const float boundaryHeight = sunDirection.y >= 0.0F
            ? parameters.BaseHeight + parameters.Thickness : parameters.BaseHeight;
        const float shadowDistance = abs(sunDirection.y) > 0.0001F
            ? max(0.0F, (boundaryHeight - position.y) / sunDirection.y) : parameters.PatternSize;
        const float shadowStepLength = min(shadowDistance, parameters.PatternSize * 4.0F) / float(shadowSteps);
        float lightOpticalDepth = 0.0F;
        [loop]
        for (uint shadowStep = 0U; shadowStep < shadowSteps; ++shadowStep)
        {
            const float3 lightPosition = position + sunDirection * ((float(shadowStep) + 0.5F) * shadowStepLength);
            lightOpticalDepth += VolumeCloudDensity(lightPosition, parameters) * parameters.Extinction * shadowStepLength;
        }

        // Beyond this depth transmission is negligible; bounding also avoids Inf * zero in segment integration.
        const float opticalDepth = min(extinction * stepLength, 80.0F);
        const float3 illumination = ambientRadiance + sunRadiance * (phase * exp(-lightOpticalDepth));
        radiance += transmission * illumination * saturate(parameters.ScatteringAlbedo) *
            (opticalDepth * VolumeSegmentWeight(opticalDepth));
        transmission *= exp(-opticalDepth);
    }
    return float4(radiance, 1.0F - transmission);
}
