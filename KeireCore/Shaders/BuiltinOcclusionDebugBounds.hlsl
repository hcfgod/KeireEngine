struct InstanceData
{
    float4x4 Model;
    float4x4 NormalMatrix;
    float4 Tint;
};

struct OcclusionCandidate
{
    float4 BoundsMinimum;
    float4 BoundsMaximum;
    uint4 Metadata;
};

struct VertexOutput
{
    float4 Color : TEXCOORD0;
    float4 Position : SV_Position;
};

StructuredBuffer<OcclusionCandidate> Candidates : register(t0, space0);
StructuredBuffer<InstanceData> InputInstances : register(t1, space0);
StructuredBuffer<uint> Visibility : register(t2, space0);

cbuffer DebugBoundsData : register(b0, space1)
{
    float4x4 ViewProjection;
};

VertexOutput VSDebugBounds(const uint vertexId : SV_VertexID, const uint instanceId : SV_InstanceID)
{
    static const uint cornerIndices[24] = {0U, 1U, 0U, 2U, 0U, 4U, 1U, 3U, 1U, 5U, 2U, 3U,
                                           2U, 6U, 3U, 7U, 4U, 5U, 4U, 6U, 5U, 7U, 6U, 7U};
    const OcclusionCandidate candidate = Candidates[instanceId];
    const uint corner = cornerIndices[vertexId];
    const float displacementRadius = max(candidate.BoundsMinimum.w, 0.0F);
    float3 worldCorners[8];
    float3 worldMinimum = float3(3.402823466e+38F, 3.402823466e+38F, 3.402823466e+38F);
    float3 worldMaximum = -worldMinimum;
    [unroll] for (uint candidateCorner = 0U; candidateCorner < 8U; ++candidateCorner)
    {
        const float3 local =
            float3((candidateCorner & 1U) != 0U ? candidate.BoundsMaximum.x : candidate.BoundsMinimum.x,
                   (candidateCorner & 2U) != 0U ? candidate.BoundsMaximum.y : candidate.BoundsMinimum.y,
                   (candidateCorner & 4U) != 0U ? candidate.BoundsMaximum.z : candidate.BoundsMinimum.z);
        worldCorners[candidateCorner] = mul(InputInstances[instanceId].Model, float4(local, 1.0F)).xyz;
        worldMinimum = min(worldMinimum, worldCorners[candidateCorner]);
        worldMaximum = max(worldMaximum, worldCorners[candidateCorner]);
    }
    worldMinimum -= displacementRadius.xxx;
    worldMaximum += displacementRadius.xxx;
    const float3 worldPosition =
        displacementRadius > 0.0F
            ? float3((corner & 1U) != 0U ? worldMaximum.x : worldMinimum.x,
                     (corner & 2U) != 0U ? worldMaximum.y : worldMinimum.y,
                     (corner & 4U) != 0U ? worldMaximum.z : worldMinimum.z)
            : worldCorners[corner];
    const float4 world = float4(worldPosition, 1.0F);
    VertexOutput output;
    output.Position = mul(ViewProjection, world);
    output.Color = Visibility[instanceId] != 0U ? float4(0.1F, 1.0F, 0.25F, 0.9F)
                                                : float4(1.0F, 0.12F, 0.08F, 0.9F);
    return output;
}

float4 PSDebugBounds(const VertexOutput input) : SV_Target0
{
    return input.Color;
}
