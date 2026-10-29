#include "FogDensity.hlsli"

#ifndef PROBE_ORIENTATION
#define PROBE_ORIENTATION 0U
#endif

RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};

float3 RotateProbe(float3 value, uint orientation)
{
    const uint axis = orientation % 3U;
    const float directionSign = 1.0F - 2.0F * float(orientation / 3U);
    return (value * float(axis == 0U) + value.yzx * float(axis == 1U) +
            value.zxy * float(axis == 2U)) * directionSign;
}
float SampleProbe(float3 midpoint, float3 span, FogViewDensity fog, float3 observer, uint orientation)
{
    return SampleFogSegmentDensity(RotateProbe(midpoint, orientation), RotateProbe(span, orientation),
                                   fog, RotateProbe(observer, orientation));
}
// References use 65536-panel double-precision Simpson integration of radial height.
// Cases cover tangent lengths, a capped-density core, an asymmetric ray, a small planet,
// and an oblique descent. The host's air-check mode requires every output lane to be zero.
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    FogViewDensity fog = (FogViewDensity)0;
    fog.Controls.x = 2U;
    fog.HeightDensity.z = 0.002F;
    float actual = 0.0F;
    float expected = 0.0F;
    if (id.x % 8U == 0U)
    {
        fog.HeightDensity.x = 0;
        fog.HeightDensity.y = 10;
        expected = 0.0018763502679469072F;
        actual = SampleProbe(float3(0, 0, 0), float3(1000, 0, 0),
                                         fog, float3(0, 6360, 0), PROBE_ORIENTATION);
    }
    if (id.x % 8U == 1U)
    {
        fog.HeightDensity.x = 0;
        fog.HeightDensity.y = 10;
        expected = 0.00039980524756924016F;
        actual = SampleProbe(float3(0, 0, 0), float3(10000, 0, 0),
                                         fog, float3(0, 6360, 0), PROBE_ORIENTATION);
    }
    if (id.x % 8U == 2U)
    {
        fog.HeightDensity.x = 0;
        fog.HeightDensity.y = 10;
        expected = 3.9980524771393786E-05F;
        actual = SampleProbe(float3(0, 0, 0), float3(100000, 0, 0),
                                         fog, float3(0, 6360, 0), PROBE_ORIENTATION);
    }
    if (id.x % 8U == 3U)
    {
        fog.HeightDensity.x = 0;
        fog.HeightDensity.y = 10;
        expected = 3.998052477139391E-06F;
        actual = SampleProbe(float3(0, 0, 0), float3(1000000, 0, 0),
                                         fog, float3(0, 6360, 0), PROBE_ORIENTATION);
    }
    if (id.x % 8U == 4U)
    {
        fog.HeightDensity.x = 100;
        fog.HeightDensity.y = 0.01;
        expected = 0.0018042632592578263F;
        actual = SampleProbe(float3(0, 0, 0), float3(100000, 0, 0),
                                         fog, float3(0, 6359.9, 0), PROBE_ORIENTATION);
    }
    if (id.x % 8U == 5U)
    {
        fog.HeightDensity.x = 0;
        fog.HeightDensity.y = 10;
        expected = 3.9980524771393684E-05F;
        actual = SampleProbe(float3(30000, 0, 0), float3(100000, 0, 0),
                                         fog, float3(0, 6360, 0), PROBE_ORIENTATION);
    }
    if (id.x % 8U == 6U)
    {
        fog.HeightDensity.x = 0;
        fog.HeightDensity.y = 0.01;
        expected = 0.00016430662823830526F;
        actual = SampleProbe(float3(0, 0, 0), float3(10000, 0, 0),
                                         fog, float3(0, 1, 0), PROBE_ORIENTATION);
    }
    if (id.x % 8U == 7U)
    {
        fog.HeightDensity.x = -1000;
        fog.HeightDensity.y = 0.01;
        expected = 0.001098377995057773F;
        actual = SampleProbe(float3(5000, -1000, 0), float3(10000, -2000, 0),
                                         fog, float3(0, 6361, 0), PROBE_ORIENTATION);
    }
    const bool valid = abs(actual - expected) <= max(1.0e-9F, expected * 0.01F);
    Pixels[id.y * Quality.x + id.x] = valid ? 0.0F : float4(actual, expected, id.x % 8U, 1.0F);
}
