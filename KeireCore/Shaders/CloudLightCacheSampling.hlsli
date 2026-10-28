#pragma once

// The caller owns a buffer containing dimensions.x * dimensions.y * dimensions.z float4 entries.
// A zero weight requests direct lighting; no buffer access occurs outside the grid.
float4 SampleCloudLightCache(StructuredBuffer<float4> cache, float3 position, float3 origin,
    float3 cellSize, uint3 dimensions, out float cacheWeight)
{
    cacheWeight = 0.0F;
    if (any(dimensions < 2U) || any(cellSize <= 0.0F) || !all(isfinite(cellSize)))
    {
        return 0.0F;
    }

    const float3 grid = (position - origin) / cellSize;
    const float3 extent = float3(dimensions - 1U);
    if (!all(isfinite(grid)) || any(grid < 0.0F) || any(grid >= extent))
    {
        return 0.0F;
    }

    const uint3 cell = uint3(floor(grid));
    const float3 fraction = frac(grid);
    float4 transmission = 0.0F;
    [unroll]
    for (uint corner = 0U; corner < 8U; ++corner)
    {
        const uint3 offset = uint3(corner & 1U, (corner >> 1U) & 1U, (corner >> 2U) & 1U);
        const float3 weight = lerp(1.0F - fraction, fraction, float3(offset));
        const uint3 index = cell + offset;
        transmission += weight.x * weight.y * weight.z *
            cache[(index.z * dimensions.y + index.y) * dimensions.x + index.x];
    }

    // Fade across two cells on every face so the bounded cache joins the direct shadow march continuously.
    const float3 edgeDistance = min(grid, extent - grid);
    cacheWeight = smoothstep(0.0F, 2.0F, min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)));
    return transmission;
}
