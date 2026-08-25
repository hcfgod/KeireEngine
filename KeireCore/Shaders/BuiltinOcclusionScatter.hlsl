struct InstanceData
{
    float4x4 Model;
    float4x4 NormalMatrix;
    float4 Tint;
};

struct OcclusionChunk
{
    uint CandidateFirst;
    uint CandidateCount;
    uint BatchIndex;
    uint Padding;
};

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

StructuredBuffer<uint> Visibility : register(t0, space0);
StructuredBuffer<uint> LocalOffsets : register(t1, space0);
StructuredBuffer<OcclusionChunk> Chunks : register(t2, space0);
StructuredBuffer<uint> ChunkOffsets : register(t3, space0);
StructuredBuffer<OcclusionBatch> Batches : register(t4, space0);
StructuredBuffer<InstanceData> InputInstances : register(t5, space0);
RWStructuredBuffer<InstanceData> VisibleInstances : register(u0, space1);

cbuffer ScatterDispatch : register(b0, space2)
{
    uint4 DispatchCounts;
};

[numthreads(256, 1, 1)] void CSScatter(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    const uint chunkIndex = groupId.x + DispatchCounts.z;
    if (chunkIndex >= DispatchCounts.x)
        return;
    const OcclusionChunk chunk = Chunks[chunkIndex];
    if (groupIndex >= chunk.CandidateCount)
        return;
    const uint candidateIndex = chunk.CandidateFirst + groupIndex;
    if (Visibility[candidateIndex] == 0U)
        return;
    const OcclusionBatch batch = Batches[chunk.BatchIndex];
    const uint outputIndex = batch.OutputFirst + ChunkOffsets[chunkIndex] + LocalOffsets[candidateIndex];
    VisibleInstances[outputIndex] = InputInstances[candidateIndex];
}
