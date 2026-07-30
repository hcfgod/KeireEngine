struct AssetVertex
{
    float4 Position;
    float4 Normal;
    float4 UV0;
    float4 VertexColor;
    float4 Tangent;
};

struct BuiltinVertex
{
    float4 Position;
    float4 Color;
    float4 Normal;
};

struct SkinInfluence
{
    uint4 Bones0;
    uint4 Bones1;
    float4 Weights0;
    float4 Weights1;
};

struct SkinMatrix
{
    float4 Column0;
    float4 Column1;
    float4 Column2;
    float4 Column3;
};

StructuredBuffer<AssetVertex> SourceVertices : register(t0, space0);
StructuredBuffer<SkinInfluence> Influences : register(t1, space0);
StructuredBuffer<SkinMatrix> SkinPalette : register(t2, space0);
RWStructuredBuffer<AssetVertex> DeformedVertices : register(u0, space1);
RWStructuredBuffer<BuiltinVertex> DeformedBuiltinVertices : register(u1, space1);

cbuffer SkinDispatch : register(b0, space2)
{
    uint VertexCount;
    uint InfluenceCount;
    uint SkinningMode;
    uint Padding;
};

float4 TransformPosition(SkinMatrix matrix, float4 position)
{
    return matrix.Column0 * position.x + matrix.Column1 * position.y + matrix.Column2 * position.z
        + matrix.Column3 * position.w;
}

float3 TransformDirection(SkinMatrix matrix, float3 direction)
{
    return matrix.Column0.xyz * direction.x + matrix.Column1.xyz * direction.y
        + matrix.Column2.xyz * direction.z;
}

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
    uint paletteCount = 0;
    uint paletteStride = 0;
    SkinPalette.GetDimensions(paletteCount, paletteStride);

    [unroll]
    for (uint index = 0; index < 8; ++index)
    {
        if (index >= InfluenceCount)
            break;
        const uint bone = index < 4 ? influence.Bones0[index] : influence.Bones1[index - 4];
        const float weight = index < 4 ? influence.Weights0[index] : influence.Weights1[index - 4];
        if (weight <= 0.0F || bone >= paletteCount)
            continue;
        const SkinMatrix skin = SkinPalette[bone];
        position += TransformPosition(skin, float4(source.Position.xyz, 1.0F)) * weight;
        normal += TransformDirection(skin, source.Normal.xyz) * weight;
        tangent += TransformDirection(skin, source.Tangent.xyz) * weight;
        totalWeight += weight;
    }

    AssetVertex output = source;
    if (totalWeight > 0.000001F)
    {
        const float inverseWeight = rcp(totalWeight);
        output.Position = float4(position.xyz * inverseWeight, 1.0F);
        output.Normal = float4(normalize(normal * inverseWeight), 0.0F);
        output.Tangent.xyz = normalize(tangent * inverseWeight);
    }
    DeformedVertices[vertexIndex] = output;

    BuiltinVertex builtinOutput;
    builtinOutput.Position = output.Position;
    builtinOutput.Color = output.VertexColor;
    builtinOutput.Normal = output.Normal;
    DeformedBuiltinVertices[vertexIndex] = builtinOutput;
}
