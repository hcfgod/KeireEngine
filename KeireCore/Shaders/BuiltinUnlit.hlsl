struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Color : TEXCOORD1;
};

struct VertexOutput
{
    float4 Color : TEXCOORD0;
    float4 Position : SV_Position;
};

cbuffer ObjectConstants : register(b0, space1)
{
    float4x4 ModelViewProjection;
    float4 Tint;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.Color = float4(input.Color, 1.0F) * Tint;
    output.Position = mul(ModelViewProjection, float4(input.Position, 1.0F));
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return input.Color;
}
