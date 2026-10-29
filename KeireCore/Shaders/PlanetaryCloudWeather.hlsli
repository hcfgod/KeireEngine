#pragma once

uint PlanetaryCloudWeatherHash(uint3 cell, uint seed)
{
    uint value = cell.x * 0x8da6b343U ^ cell.y * 0xd8163841U ^ cell.z * 0xcb1ab31fU ^ seed;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

// A continuous three-dimensional field restricted to the shell has no chart seams or poles.
// Period divides the CPU phase domain, preserving continuity when its wrapped origin changes.
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
        const float3 weight = lerp(1.0F - blend, blend, float3(offset));
        value += weight.x * weight.y * weight.z *
            (float(PlanetaryCloudWeatherHash((cell + offset) % repeat, seed) >> 8U) / 16777216.0F);
    }
    return value;
}
