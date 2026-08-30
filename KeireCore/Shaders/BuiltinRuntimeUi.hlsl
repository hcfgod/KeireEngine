struct VertexInput
{
    float3 Position : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float2 UV : TEXCOORD2;
};

struct VertexOutput
{
    float4 Color : TEXCOORD0;
    float2 UV : TEXCOORD1;
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
    const float2 normalized = input.Position.xy / max(ViewportSize, 1.0F.xx);
    output.Position = float4(normalized.x * 2.0F - 1.0F, 1.0F - normalized.y * 2.0F, input.Position.z, 1.0F);
    output.Color = input.Color;
    output.UV = input.UV;
    return output;
}

Texture2D<float4> RuntimeUiTexture : register(t0, space2);
SamplerState RuntimeUiSampler : register(s0, space2);

float4 PSMain(const VertexOutput input) : SV_Target0
{
    return RuntimeUiTexture.Sample(RuntimeUiSampler, input.UV) * input.Color;
}
