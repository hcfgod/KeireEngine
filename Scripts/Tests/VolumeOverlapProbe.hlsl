#include "VolumeTransport.hlsli"

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
    const float opticalDepth = float(id.x) * 80.0F / float(Quality.x - 1);
    const float distance = 1.0F + float(id.y);
    const float extinction = opticalDepth / distance;
    const float source = extinction * 1.5F;
    const VolumeIntegratedMedium whole = IntegrateVolumeMedium(extinction, source, distance);
    const uint steps = 1 + id.y % 64;
    const VolumeIntegratedMedium segment = IntegrateVolumeMedium(extinction, source, distance / float(steps));
    float transmission = 1.0F;
    float radiance = 0.0F;
    for (uint step = 0; step < steps; ++step)
    {
        radiance += transmission * segment.Radiance;
        transmission *= segment.Transmittance;
    }
    const VolumeIntegratedMedium vacuum = IntegrateVolumeMedium(0.0F, 2.0F, distance);
    const VolumeIntegratedMedium empty = IntegrateVolumeMedium(extinction, source, 0.0F);
    bool valid = abs(transmission - whole.Transmittance) < 1.0e-5F;
    valid = valid && abs(radiance - whole.Radiance) < 2.0e-5F;
    valid = valid && abs(whole.Radiance - 1.5F * (1.0F - exp(-opticalDepth))) < 2.0e-5F;
    valid = valid && vacuum.Transmittance == 1.0F && vacuum.Radiance == 2.0F * distance;
    valid = valid && empty.Transmittance == 1.0F && empty.Radiance == 0.0F;
    Pixels[id.y * Quality.x + id.x] = float4(whole.Transmittance, whole.Radiance, radiance, valid ? 0.0F : 1.0F);
}
