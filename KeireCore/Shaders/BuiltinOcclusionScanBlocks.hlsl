struct OcclusionChunk
{
    uint CandidateFirst;
    uint CandidateCount;
    uint BatchIndex;
    uint Padding;
};

StructuredBuffer<uint> Visibility : register(t0, space0);
StructuredBuffer<OcclusionChunk> Chunks : register(t1, space0);
RWStructuredBuffer<uint> LocalOffsets : register(u0, space1);
RWStructuredBuffer<uint> ChunkCounts : register(u1, space1);

cbuffer ScanBlockDispatch : register(b0, space2)
{
    uint4 DispatchCounts;
};

groupshared uint PrefixValues[256];

[numthreads(256, 1, 1)] void CSScanBlocks(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    const uint chunkIndex = groupId.x + DispatchCounts.z;
    if (chunkIndex >= DispatchCounts.x)
        return;
    const OcclusionChunk chunk = Chunks[chunkIndex];
    const uint visible = groupIndex < chunk.CandidateCount ? Visibility[chunk.CandidateFirst + groupIndex] : 0U;
    PrefixValues[groupIndex] = visible;
    GroupMemoryBarrierWithGroupSync();

    [unroll] for (uint offset = 1U; offset < 256U; offset <<= 1U)
    {
        const uint addend = groupIndex >= offset ? PrefixValues[groupIndex - offset] : 0U;
        GroupMemoryBarrierWithGroupSync();
        PrefixValues[groupIndex] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex < chunk.CandidateCount)
        LocalOffsets[chunk.CandidateFirst + groupIndex] = PrefixValues[groupIndex] - visible;
    if (groupIndex == 0U)
        ChunkCounts[chunkIndex] =
            chunk.CandidateCount == 0U ? 0U : PrefixValues[min(chunk.CandidateCount, 256U) - 1U];
}
