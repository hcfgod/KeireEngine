struct VertexInput
{
    float3 Position : TEXCOORD0;
};

struct InstanceData
{
    float4x4 Model;
    float4x4 NormalMatrix;
    float4 Tint;
};

StructuredBuffer<InstanceData> Instances : register(t0, space0);

cbuffer OcclusionDepthData : register(b0, space1)
{
    float4x4 ViewProjection;
    uint4 InstanceParameters;
};

float4 VSDepth(VertexInput input, uint instanceId : SV_InstanceID) : SV_Position
{
    const InstanceData instance = Instances[InstanceParameters.x + instanceId];
    return mul(ViewProjection, mul(instance.Model, float4(input.Position, 1.0F)));
}

void PSDepth()
{
}
