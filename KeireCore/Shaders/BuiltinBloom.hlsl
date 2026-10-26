struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

Texture2D BloomSource : register(t0, space2);
SamplerState BloomSampler : register(s0, space2);

cbuffer BloomData : register(b0, space3)
{
    float4 BloomParameters;
    float4 BloomDirection;
    float4 BloomWeights[7];
};

VertexOutput VSMain(const uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

float3 FiniteBloomColor(const float3 color)
{
    return float3(isfinite(color.r) ? clamp(color.r, 0.0F, 65504.0F) : 0.0F,
                  isfinite(color.g) ? clamp(color.g, 0.0F, 65504.0F) : 0.0F,
                  isfinite(color.b) ? clamp(color.b, 0.0F, 65504.0F) : 0.0F);
}

float3 ExtractBloomSample(const float2 uv)
{
    const float3 color = FiniteBloomColor(BloomSource.SampleLevel(BloomSampler, uv, 0.0F).rgb);
    const float brightness = max(color.r, max(color.g, color.b));
    if (brightness <= 0.0F)
    {
        return 0.0F;
    }
    const float knee = BloomParameters.w;
    float contribution = max(brightness - BloomParameters.z, 0.0F);
    if (knee > 0.0F)
    {
        const float transition = clamp(brightness - BloomParameters.z + knee, 0.0F, 2.0F * knee);
        contribution = max(contribution, transition * (transition / knee) * 0.25F);
    }
    return color * saturate(contribution / brightness);
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    if (BloomDirection.w < 0.5F)
    {
        return 0.0F;
    }
    float3 result = 0.0F;
    if (BloomDirection.z < 0.5F)
    {
        // Half-resolution destination: threshold each bilinear sample before combining the 2x2 footprint.
        const float2 offset = BloomParameters.xy * 0.5F;
        result += ExtractBloomSample(input.UV + float2(-offset.x, -offset.y)) * 0.25F;
        result += ExtractBloomSample(input.UV + float2(offset.x, -offset.y)) * 0.25F;
        result += ExtractBloomSample(input.UV + float2(-offset.x, offset.y)) * 0.25F;
        result += ExtractBloomSample(input.UV + float2(offset.x, offset.y)) * 0.25F;
    }
    else
    {
        [unroll]
        for (uint index = 0; index < 25; ++index)
        {
            const float2 uv = input.UV + BloomDirection.xy * ((float)index - 12.0F);
            const float3 color = FiniteBloomColor(BloomSource.SampleLevel(BloomSampler, uv, 0.0F).rgb);
            result += color * BloomWeights[index / 4][index % 4];
        }
    }
    // Intensity is applied only when composing this buffer with exposed HDR before tone mapping.
    return float4(FiniteBloomColor(result), 1.0F);
}
