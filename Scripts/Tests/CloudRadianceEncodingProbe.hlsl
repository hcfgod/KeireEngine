RWStructuredBuffer<float4> Pixels : register(u0, space1);
Texture3D<float4> Noise : register(t0, space0);
SamplerState NoiseSampler : register(s0, space0);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
    float4 Weather;
    float4 Lighting;
};
#include "CloudRadianceEncoding.hlsli"
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    const uint index = id.y * Quality.x + id.x;
    const float fraction = float(index) / float(Quality.x * Quality.y - 1U);
    float value = exp2(lerp(-26.575424759F, 26.575424759F, fraction));
    if (index == 0U)
    {
        value = 0.0F;
    }
    if (index == 1U)
    {
        value = asfloat(0x7f7fffffU);
    }
    const float decoded = DecodeCloudRadianceChannel(f16tof32(f32tof16(EncodeCloudRadianceChannel(value))));
    bool failed = !isfinite(decoded) || decoded < 0.0F;
    failed = failed || (value == 0.0F ? decoded != 0.0F : abs(decoded - value) > value * 0.006F);
    // Keep the host probe's sampler binding present without affecting the tested positive-density path.
    if (Density.x < 0.0F)
    {
        failed = Noise.SampleLevel(NoiseSampler, fraction.xxx, 0.0F).x > 0.5F;
    }
    Pixels[index] = failed ? 1.0F.xxxx : 0.0F.xxxx;
}
