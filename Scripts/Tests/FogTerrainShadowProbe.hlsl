#define KEIRE_FOG_SHADOW_TEXTURE t0
#define KEIRE_FOG_SHADOW_SAMPLER s0
#define KEIRE_FOG_SHADOW_CONSTANTS b0
#include "FogTerrainShadow.hlsli"

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    const float2 uv = float2((vertexId << 1U) & 2U, vertexId & 2U);
    return float4(uv * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
}

float4 PSMain(float4 pixel : SV_Position) : SV_Target0
{
    const uint column = uint(pixel.x) % 8U;
    const uint cascade = column / 2U;
    const bool occluded = (column & 1U) != 0U;
    // Each layer is cleared to 0.15 + 0.2 * layer; the two rays straddle that depth.
    float3 position = float3(0.0F, 0.0F, 0.1F + 0.2F * cascade + (occluded ? 0.08F : 0.0F));
    float expected = occluded ? 1.0F - FogShadowParameters.y : 1.0F;
    const uint row = uint(pixel.y) % 8U;
    if (row == 1U)
    {
        position.x = 3.0F;
        expected = 1.0F;
    }
    else if (row == 2U)
    {
        position.z = -0.1F;
        expected = 1.0F;
    }
    else if (row == 3U)
    {
        position.z = 1.1F;
        expected = 1.0F;
    }
    else if (row >= 4U)
    {
        // Half a texel inside the boundary: one filter column falls outside the map.
        // Its weights total 1/4. At a corner, the union of the row and column is 7/16.
        // Hard shadows must remain fully occluded at both positions.
        const float edge = 1.0F - FogShadowParameters.w;
        position.x = (row & 1U) == 0U ? -edge : edge;
        if (row >= 6U)
        {
            position.y = (row & 1U) == 0U ? -edge : edge;
        }
        if (occluded && FogShadowParameters.x > 0.0F)
        {
            const float unoccludedWeight = row >= 6U ? 7.0F / 16.0F : 1.0F / 4.0F;
            expected = 1.0F - FogShadowParameters.y * (1.0F - unoccludedWeight);
        }
    }
    const float actual = EvaluateFogTerrainShadow(position);
    return float4(actual, expected, 0.0F, 1.0F);
}
