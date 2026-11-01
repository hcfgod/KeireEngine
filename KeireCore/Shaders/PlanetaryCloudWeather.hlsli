#pragma once

uint PlanetaryCloudWeatherHash(uint x, uint y, uint z, uint repeat, uint seed)
{
    // Host-validated regional periods are powers of two from 1 to 64; repeat is 256 / period.
    const uint mask = repeat - 1U;
    uint value = (x & mask) * 0x8da6b343U ^ (y & mask) * 0xd8163841U ^ (z & mask) * 0xcb1ab31fU ^ seed;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

// A continuous three-dimensional field restricted to the shell has no chart seams or poles.
// Period divides the CPU phase domain, preserving continuity when its wrapped origin changes.
#ifndef __cplusplus
float PlanetaryCloudWeather(float3 position, float period, uint seed)
{
    const float boundedPeriod = clamp(period, 1.0F, 64.0F);
    const float3 coordinate = frac(position / 256.0F) * (256.0F / boundedPeriod);
    const uint repeat = uint(256.0F / boundedPeriod);
    const uint3 cell = uint3(floor(coordinate));
    const float3 fraction = frac(coordinate);
    const float3 blend = fraction * fraction * (3.0F - 2.0F * fraction);
    float value = 0.0F;
    [unroll]
    for (uint corner = 0U; corner < 8U; ++corner)
    {
        const uint3 offset = uint3(corner & 1U, (corner >> 1U) & 1U, (corner >> 2U) & 1U);
        const uint3 cornerCell = cell + offset;
        const float3 weight = lerp(1.0F - blend, blend, float3(offset));
        value += weight.x * weight.y * weight.z *
            (float(PlanetaryCloudWeatherHash(cornerCell.x, cornerCell.y, cornerCell.z, repeat, seed) >> 8U) / 16777216.0F);
    }
    return value;
}
#endif
