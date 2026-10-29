#pragma once

#ifdef KEIRE_CLOUD_NOISE_BUFFER

// Matches PackCloudNoiseBuffer. Read-only storage avoids consuming a sampled-texture slot.
#ifndef KEIRE_CLOUD_NOISE_BUFFER_SPACE
#define KEIRE_CLOUD_NOISE_BUFFER_SPACE space2
#endif
StructuredBuffer<uint> VolumeCloudNoiseBuffer : register(KEIRE_CLOUD_NOISE_BUFFER, KEIRE_CLOUD_NOISE_BUFFER_SPACE);

float4 LoadVolumeCloudNoiseBuffer(uint offset, uint dimension, int3 coordinate)
{
    const uint3 wrapped = uint3(coordinate) & (dimension - 1U);
    const uint index = offset + (wrapped.z * dimension + wrapped.y) * dimension + wrapped.x;
    const uint packed = VolumeCloudNoiseBuffer[index];
    return float4(packed & 255U, (packed >> 8U) & 255U,
                  (packed >> 16U) & 255U, packed >> 24U) / 255.0F;
}

float4 SampleVolumeCloudNoiseBufferMip(float3 position, uint level)
{
    const uint descriptor = 4U + level * 4U;
    const uint dimension = VolumeCloudNoiseBuffer[descriptor];
    const uint offset = VolumeCloudNoiseBuffer[descriptor + 1U];
    // Normalized repeat addressing and half-texel centers match the existing linear 3D texture sampler.
    const float3 texel = frac(position) * float(dimension) - 0.5F;
    const int3 lower = int3(floor(texel));
    const float3 weight = frac(texel);
    const float4 nearLower = lerp(
        LoadVolumeCloudNoiseBuffer(offset, dimension, lower),
        LoadVolumeCloudNoiseBuffer(offset, dimension, lower + int3(1, 0, 0)), weight.x);
    const float4 nearUpper = lerp(
        LoadVolumeCloudNoiseBuffer(offset, dimension, lower + int3(0, 1, 0)),
        LoadVolumeCloudNoiseBuffer(offset, dimension, lower + int3(1, 1, 0)), weight.x);
    const float4 farLower = lerp(
        LoadVolumeCloudNoiseBuffer(offset, dimension, lower + int3(0, 0, 1)),
        LoadVolumeCloudNoiseBuffer(offset, dimension, lower + int3(1, 0, 1)), weight.x);
    const float4 farUpper = lerp(
        LoadVolumeCloudNoiseBuffer(offset, dimension, lower + int3(0, 1, 1)),
        LoadVolumeCloudNoiseBuffer(offset, dimension, lower + int3(1, 1, 1)), weight.x);
    return lerp(lerp(nearLower, nearUpper, weight.y), lerp(farLower, farUpper, weight.y), weight.z);
}

float4 SampleVolumeCloudNoiseBuffer(float3 position, float level)
{
    const uint maximumLevel = VolumeCloudNoiseBuffer[1] - 1U;
    const float boundedLevel = clamp(level, 0.0F, float(maximumLevel));
    const uint lower = uint(floor(boundedLevel));
    const float weight = frac(boundedLevel);
    const float4 first = SampleVolumeCloudNoiseBufferMip(position, lower);
    if (weight == 0.0F)
    {
        return first;
    }
    return lerp(first, SampleVolumeCloudNoiseBufferMip(position, min(lower + 1U, maximumLevel)), weight);
}

#endif // KEIRE_CLOUD_NOISE_BUFFER
