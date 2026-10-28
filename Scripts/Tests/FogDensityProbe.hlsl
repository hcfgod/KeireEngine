#include "FogDensity.hlsli"

RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    FogViewDensity fog = (FogViewDensity)0;
    fog.HeightDensity = float4(0.0F, 0.01F, 0.002F, 10000.0F);
    fog.Controls = uint4(1U, 64U, 2U, 0U);
    fog.Local[0].CenterExtinction = float4(0.0F, 0.0F, 0.0F, 0.02F);
    fog.Local[0].HalfExtentEdge = float4(10.0F, 10.0F, 10.0F, 0.2F);
    fog.Local[1] = fog.Local[0];
    const float x = float(id.x) * 24.0F / float(Quality.x - 1) - 12.0F;
    const float height = float(id.y);
    const float sampled = SampleFogDensity(float3(x, height, 0.0F), fog);
    const float distance = max(abs(x), height);
    const float edge = saturate((10.0F - distance) / 2.0F);
    const float expected = 0.002F * exp(-height * 0.01F) + 0.04F * edge * edge * (3.0F - 2.0F * edge);
    bool valid = abs(sampled - expected) < 1.0e-6F;
    valid = valid && abs(SampleFogDensity(float3(9, 0, 0), fog) - 0.022F) < 1.0e-6F;
    valid = valid && abs(SampleFogDensity(float3(10, 0, 0), fog) - 0.002F) < 1.0e-6F;
    fog.Controls.x = 0U;
    valid = valid && SampleFogDensity(float3(x, height, 0), fog) == 0.0F;
    Pixels[id.y * Quality.x + id.x] = float4(sampled, expected, 0.0F, valid ? 0.0F : 1.0F);
}
