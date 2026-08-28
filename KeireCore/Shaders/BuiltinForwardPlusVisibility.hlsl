StructuredBuffer<uint> LocalLightVisibilityMask : register(t0, space0);
RWStructuredBuffer<uint4> ForwardPlusTiles : register(u0, space1);
RWStructuredBuffer<uint> ForwardPlusLightIndices : register(u1, space1);

cbuffer ForwardPlusVisibilityDispatch : register(b0, space2)
{
    uint4 DispatchCounts;
};

[numthreads(64, 1, 1)] void CSCompactForwardPlusTiles(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint tileIndex = dispatchThreadId.x;
    if (tileIndex >= DispatchCounts.x)
        return;

    uint4 tile = ForwardPlusTiles[tileIndex];
    const uint first = tile.x;
    const uint count = min(tile.y, 128U);
    uint visibleCount = 0U;
    for (uint offset = 0U; offset < count; ++offset)
    {
        const uint lightIndex = ForwardPlusLightIndices[first + offset];
        const bool visible = lightIndex >= DispatchCounts.y || LocalLightVisibilityMask[lightIndex] != 0U;
        if (!visible)
            continue;
        ForwardPlusLightIndices[first + visibleCount] = lightIndex;
        ++visibleCount;
    }
    tile.y = visibleCount;
    ForwardPlusTiles[tileIndex] = tile;
}
