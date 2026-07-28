struct AssetVertex
{
    float3 Position;
    float3 Normal;
    float2 UV0;
    float4 VertexColor;
    float4 Tangent;
};

struct BuiltinVertex
{
    float3 Position;
    float3 Color;
    float3 Normal;
};

struct SkinInfluence
{
    uint4 Bones0;
    uint4 Bones1;
    float4 Weights0;
    float4 Weights1;
};

StructuredBuffer<AssetVertex> SourceVertices : register(t0, space0);
StructuredBuffer<SkinInfluence> Influences : register(t1, space0);
StructuredBuffer<float4x4> SkinPalette : register(t2, space0);
RWStructuredBuffer<AssetVertex> DeformedVertices : register(u0, space1);
RWStructuredBuffer<BuiltinVertex> DeformedBuiltinVertices : register(u1, space1);

cbuffer SkinDispatch : register(b0, space2)
{
    uint VertexCount;
    uint InfluenceCount;
    uint SkinningMode;
    uint Padding;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint vertexIndex = dispatchThreadId.x;
    if (vertexIndex >= VertexCount)
        return;

    const AssetVertex source = SourceVertices[vertexIndex];
    const SkinInfluence influence = Influences[vertexIndex];
    float4 position = 0.0F;
    float3 normal = 0.0F;
    float3 tangent = 0.0F;
    float totalWeight = 0.0F;

    [unroll]
    for (uint index = 0; index < 8; ++index)
    {
        if (index >= InfluenceCount)
            break;
        const uint bone = index < 4 ? influence.Bones0[index] : influence.Bones1[index - 4];
        const float weight = index < 4 ? influence.Weights0[index] : influence.Weights1[index - 4];
        if (weight <= 0.0F)
            continue;
        const float4x4 skin = SkinPalette[bone];
        position += mul(skin, float4(source.Position, 1.0F)) * weight;
        normal += mul((float3x3)skin, source.Normal) * weight;
        tangent += mul((float3x3)skin, source.Tangent.xyz) * weight;
        totalWeight += weight;
    }

    AssetVertex output = source;
    if (totalWeight > 0.000001F)
    {
        const float inverseWeight = rcp(totalWeight);
        output.Position = position.xyz * inverseWeight;
        output.Normal = normalize(normal * inverseWeight);
        output.Tangent.xyz = normalize(tangent * inverseWeight);
    }
    DeformedVertices[vertexIndex] = output;

    BuiltinVertex builtinOutput;
    builtinOutput.Position = output.Position;
    builtinOutput.Color = output.VertexColor.rgb;
    builtinOutput.Normal = output.Normal;
    DeformedBuiltinVertices[vertexIndex] = builtinOutput;
}
