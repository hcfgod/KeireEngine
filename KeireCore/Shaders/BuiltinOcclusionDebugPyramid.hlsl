struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

Texture2D<float> HierarchicalDepth : register(t0, space2);
SamplerState HierarchicalDepthSampler : register(s0, space2);

VertexOutput VSDebugPyramid(const uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

float4 PSDebugPyramid(const VertexOutput input) : SV_Target0
{
    const float depth = saturate(HierarchicalDepth.SampleLevel(HierarchicalDepthSampler, input.UV, 0.0F));
    // Standard device depth clusters distant geometry near one. Invert and apply display-only contrast so the
    // untouched far clear remains black while nearer HZB values remain inspectable.
    const float visualization = pow(saturate(1.0F - depth), 0.25F);
    return float4(visualization.xxx, 1.0F);
}
