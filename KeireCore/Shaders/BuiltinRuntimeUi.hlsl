struct VertexInput
{
    float2 Position : TEXCOORD0;
    float4 Color : TEXCOORD1;
};

struct VertexOutput
{
    float4 Color : TEXCOORD0;
    float4 Position : SV_Position;
};

cbuffer RuntimeUiConstants : register(b0, space1)
{
    float2 ViewportSize;
    float2 Padding;
};

VertexOutput VSMain(const VertexInput input)
{
    VertexOutput output;
    const float2 normalized = input.Position / max(ViewportSize, 1.0F.xx);
    output.Position = float4(normalized.x * 2.0F - 1.0F, 1.0F - normalized.y * 2.0F, 0.0F, 1.0F);
    output.Color = input.Color;
    return output;
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    return input.Color;
}
