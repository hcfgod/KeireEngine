#define KEIRE_CLOUD_NOISE_BUFFER t1
#define KEIRE_CLOUD_NOISE_BUFFER_SPACE space0
#include "CloudVolume.hlsli"

Texture3D<float4> NoiseTexture : register(t0, space0);
SamplerState NoiseSampler : register(s0, space0);
RWStructuredBuffer<float4> Results : register(u0, space1);

[numthreads(64, 1, 1)]
void CSMain(uint3 thread : SV_DispatchThreadID)
{
    const uint index = thread.x;
    if (index >= 512U)
    {
        return;
    }
    const float sample = float(index % 32U);
    float3 samplePosition = float3(sample * 0.137F - 1.3F, sample * 0.071F + 0.23F, sample * 0.193F - 0.91F);
    float level = float(index / 32U) * 0.5F;
    if (index >= 416U)
    {
        // Exact texel centers, repeat seams and negative periods at several fractional/clamped LODs.
        const float boundary = float(index % 16U) / 16.0F - 1.0F;
        samplePosition = float3(boundary, -boundary, (float(index % 64U) + 0.5F) / 64.0F);
        level = float((index - 416U) / 16U) * 1.5F - 0.25F;
    }
    Results[index] = SampleVolumeCloudNoise(samplePosition, level) - NoiseTexture.SampleLevel(NoiseSampler, samplePosition, level);
}
