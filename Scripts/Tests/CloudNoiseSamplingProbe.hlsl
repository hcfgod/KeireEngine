Texture3D<float4> Noise : register(t0, space0);
SamplerState NoiseSampler : register(s0, space0);
RWStructuredBuffer<float4> Samples : register(u0, space1);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 416U)
    {
        return;
    }
    const float index = float(id.x % 32U);
    const float3 position = float3(index * 0.137F - 1.3F, index * 0.071F + 0.23F, index * 0.193F - 0.91F);
    Samples[id.x] = Noise.SampleLevel(NoiseSampler, position, float(id.x / 32U) * 0.5F);
}
