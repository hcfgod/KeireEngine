struct VertexInput
{
    float3 Position : TEXCOORD0;
};

cbuffer ShadowObjectData : register(b0, space1)
{
    float4x4 LightViewProjection;
};

float4 VSMain(VertexInput input) : SV_Position
{
    return mul(LightViewProjection, float4(input.Position, 1.0F));
}

void PSMain()
{
}
