#include "CloudRadianceEncoding.hlsli"

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    const float2 uv = float2((vertexId << 1U) & 2U, vertexId & 2U);
    return float4(uv * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    const uint index = uint(position.y) * 256U + uint(position.x);
    float value = exp2(lerp(-26.575424759F, 26.575424759F, float(index) / 65535.0F));
    if (index == 0U)
    {
        value = 0.0F;
    }
    if (index == 1U)
    {
        value = asfloat(0x7f7fffffU);
    }
    return EncodeCloudRadiance(float4(value, value * 0.5F, value * 0.25F, 0.375F));
}
