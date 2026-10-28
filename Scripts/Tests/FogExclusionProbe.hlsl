#include "FogExclusion.hlsli"

RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    FogExclusionDensity regions[16];
    for (uint index = 0U; index < 16U; ++index)
    {
        regions[index].CenterStrength = float4(0, 0, 0, 0.7F);
        regions[index].HalfExtentEdge = float4(10, 20, 30, 0.2F);
    }
    const float x = float(id.x) / float(Quality.x - 1U) * 24.0F - 12.0F;
    const float strength = float(id.y) / float(Quality.y - 1U);
    regions[0].CenterStrength.w = strength;
    const float edge = saturate((10.0F - abs(x)) / 2.0F);
    const float expected = 1.0F - strength * edge * edge * (3.0F - 2.0F * edge);
    const float3 position = float3(x, 0, 0);
    const float single = FogExclusionRetention(position, 1U, regions);
    bool valid = abs(single - expected) < 1.0e-6F;
    valid = valid && FogExclusionRetention(position, 0U, regions) == 1.0F;
    regions[1] = regions[0];
    valid = valid && FogExclusionRetention(position, 2U, regions) == single;
    regions[1].CenterStrength.w = 1.0F;
    const float overlap = FogExclusionRetention(position, 2U, regions);
    FogExclusionDensity temporary = regions[0];
    regions[0] = regions[1];
    regions[1] = temporary;
    valid = valid && FogExclusionRetention(position, 2U, regions) == overlap;
    regions[0].HalfExtentEdge.w = 0.0F;
    valid = valid && FogExclusionRetention(float3(9.99F, 0, 0), 1U, regions) == 0.0F;
    valid = valid && FogExclusionRetention(float3(10.0F, 0, 0), 1U, regions) == 1.0F;
    // The last authored slot must participate; earlier inactive slots cannot terminate the scan.
    for (uint regionIndex = 0U; regionIndex < 16U; ++regionIndex)
    {
        regions[regionIndex].CenterStrength = float4(0, 0, 0, 0);
        regions[regionIndex].HalfExtentEdge = float4(10, 20, 30, 0.2F);
    }
    regions[15].CenterStrength.w = strength;
    valid = valid && abs(FogExclusionRetention(position, 16U, regions) - expected) < 1.0e-6F;
    // Defensive count clamping must not read beyond the fixed uniform array.
    valid = valid && FogExclusionRetention(position, 17U, regions) ==
        FogExclusionRetention(position, 16U, regions);
    Pixels[id.y * Quality.x + id.x] = float4(single.xxx, valid ? 0.0F : 1.0F);
}
