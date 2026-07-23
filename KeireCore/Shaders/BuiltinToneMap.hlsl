struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

Texture2D HdrSceneTexture : register(t0, space2);
SamplerState HdrSceneSampler : register(s0, space2);

VertexOutput VSMain(const uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

float3 AcesFitted(const float3 color)
{
    const float3 numerator = color * (2.51F * color + 0.03F);
    const float3 denominator = color * (2.43F * color + 0.59F) + 0.14F;
    return saturate(numerator / denominator);
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    const float4 hdr = HdrSceneTexture.SampleLevel(HdrSceneSampler, input.UV, 0.0F);
    return float4(AcesFitted(max(hdr.rgb, 0.0F.xxx)), saturate(hdr.a));
}
