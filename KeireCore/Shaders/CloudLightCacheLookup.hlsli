#pragma once

// Matches CloudLightCacheLookupUniforms: near, medium and distant world-aligned grids.
struct CloudLightCacheLookupLevel
{
    float4 Origin;
    float4 CellSize;
    uint4 Dimensions;
};
