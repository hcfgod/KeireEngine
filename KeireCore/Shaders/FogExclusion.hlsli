#pragma once

struct FogExclusionDensity
{
    float4 CenterStrength;
    float4 HalfExtentEdge;
};

float FogExclusionWeight(float3 position, FogExclusionDensity region)
{
    const float3 normalized = abs(position - region.CenterStrength.xyz) / region.HalfExtentEdge.xyz;
    const float distance = max(normalized.x, max(normalized.y, normalized.z));
    if (distance >= 1.0F)
    {
        return 0.0F;
    }
    const float edge = region.HalfExtentEdge.w == 0.0F ? 1.0F :
        saturate((1.0F - distance) / region.HalfExtentEdge.w);
    return region.CenterStrength.w * edge * edge * (3.0F - 2.0F * edge);
}

// Strongest exclusion wins: order-independent and invariant under duplicated regions.
float FogExclusionRetention(float3 position, uint count, FogExclusionDensity regions[16])
{
    float exclusion = 0.0F;
    for (uint index = 0U; index < min(count, 16U); ++index)
    {
        exclusion = max(exclusion, FogExclusionWeight(position, regions[index]));
        if (exclusion >= 1.0F)
        {
            return 0.0F;
        }
    }
    return 1.0F - exclusion;
}
