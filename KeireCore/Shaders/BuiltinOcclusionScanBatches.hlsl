struct OcclusionBatch
{
    uint CandidateFirst;
    uint CandidateCount;
    uint OutputFirst;
    uint ChunkFirst;
    uint ChunkCount;
    uint IndirectByteOffset;
    uint TriangleCount;
    uint Padding;
};

StructuredBuffer<OcclusionBatch> Batches : register(t0, space0);
StructuredBuffer<uint> ChunkCounts : register(t1, space0);
RWStructuredBuffer<uint> ChunkOffsets : register(u0, space1);
RWByteAddressBuffer IndirectArguments : register(u1, space1);
RWByteAddressBuffer Status : register(u2, space1);

cbuffer ScanBatchDispatch : register(b0, space2)
{
    uint4 DispatchCounts;
};

groupshared uint PrefixValues[256];

void MultiplyExtended(uint left, uint right, out uint low, out uint high)
{
    const uint leftLow = left & 0xffffU;
    const uint leftHigh = left >> 16U;
    const uint rightLow = right & 0xffffU;
    const uint rightHigh = right >> 16U;

    uint product = leftLow * rightLow;
    const uint lowLow = product & 0xffffU;
    uint carry = product >> 16U;
    product = leftHigh * rightLow + carry;
    const uint middleLow = product & 0xffffU;
    const uint highLow = product >> 16U;
    product = leftLow * rightHigh + middleLow;
    carry = product >> 16U;
    low = (product << 16U) + lowLow;
    high = leftHigh * rightHigh + highLow + carry;
}

[numthreads(256, 1, 1)] void CSScanBatches(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    const uint batchIndex = groupId.x + DispatchCounts.z;
    if (batchIndex >= DispatchCounts.x)
        return;
    const OcclusionBatch batch = Batches[batchIndex];
    const uint count = groupIndex < batch.ChunkCount ? ChunkCounts[batch.ChunkFirst + groupIndex] : 0U;
    PrefixValues[groupIndex] = count;
    GroupMemoryBarrierWithGroupSync();

    [unroll] for (uint offset = 1U; offset < 256U; offset <<= 1U)
    {
        const uint addend = groupIndex >= offset ? PrefixValues[groupIndex - offset] : 0U;
        GroupMemoryBarrierWithGroupSync();
        PrefixValues[groupIndex] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex < batch.ChunkCount)
        ChunkOffsets[batch.ChunkFirst + groupIndex] = PrefixValues[groupIndex] - count;
    if (groupIndex == 0U)
    {
        const uint visible = batch.ChunkCount == 0U ? 0U : PrefixValues[min(batch.ChunkCount, 256U) - 1U];
        IndirectArguments.Store(batch.IndirectByteOffset + 4U, visible);
        uint ignoredVisible = 0U;
        Status.InterlockedAdd(0U, visible, ignoredVisible);
        uint triangleLow = 0U;
        uint triangleHigh = 0U;
        MultiplyExtended(visible, batch.TriangleCount, triangleLow, triangleHigh);
        uint previousLow = 0U;
        Status.InterlockedAdd(4U, triangleLow, previousLow);
        const uint carry = previousLow > 0xffffffffU - triangleLow ? 1U : 0U;
        uint ignoredHigh = 0U;
        Status.InterlockedAdd(8U, triangleHigh + carry, ignoredHigh);
        if (batch.ChunkCount > 256U)
        {
            uint ignoredError = 0U;
            Status.InterlockedOr(12U, 1U, ignoredError);
        }
    }
}
