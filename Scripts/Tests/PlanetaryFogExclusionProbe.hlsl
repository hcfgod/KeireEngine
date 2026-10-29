#include "FogDensity.hlsli"
RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    FogViewDensity fog = (FogViewDensity)0;
    fog.Controls = uint4(2U, 1U, 0U, 0U);
    fog.HeightDensity = float4(0, 10, 0.002F, 1000000);
    // Symmetric density and complementary soft retention must preserve half the integral.
    if (id.x % 4U == 1U || id.x % 4U == 3U)
    {
        fog.Controls.z = 1U;
        fog.Local[0].CenterExtinction = float4(0, 0, 0, 0.001F);
        fog.Local[0].HalfExtentEdge = float4(1000000, 1000000, 1000000, 0);
    }
    if (id.x % 4U == 2U)
    {
        fog.HeightDensity.y = 0.0F;
    }
    if (id.x % 4U == 3U)
    {
        fog.HeightDensity.z = 0.0F;
    }
    const float3 span = float3(1000000, 0, 0);
    const float3 observer = float3(0, 1, 0);
    const float unexcluded = SampleFogSegmentDensity(0.0F, span, fog, observer);
    fog.Controls.w = 1U;
    fog.Exclusions[0].CenterStrength = float4(500000, 0, 0, 1);
    fog.Exclusions[0].HalfExtentEdge = float4(1000000, 1000000, 1000000, 1);
    const float retained = SampleFogSegmentDensity(0.0F, span, fog, observer);
    const bool valid = unexcluded > 0.0F && abs(retained - unexcluded * 0.5F) < unexcluded * 0.01F;
    Pixels[id.y * Quality.x + id.x] = valid ? 0.0F : float4(unexcluded, retained, 0, 1);
}