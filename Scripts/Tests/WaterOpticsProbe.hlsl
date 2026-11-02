#include "WaterOptics.hlsli"

RWStructuredBuffer<float4> Results : register(u0, space1);

// Three records of absolute error. Explicit analytic cases include underwater total reflection.
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id != 0U))
    {
        return;
    }
    const float waterIndex = 1.333F;
    const float ratio = (waterIndex - 1.0F) / (waterIndex + 1.0F);
    Results[0] = float4(abs(WaterDielectricReflectance(1.0F, 1.0F, waterIndex) - ratio * ratio),
        abs(WaterDielectricReflectance(0.0F, 1.0F, waterIndex) - 1.0F),
        abs(WaterDielectricReflectance(0.5F, waterIndex, 1.0F) - 1.0F),
        abs(WaterDielectricReflectance(0.0F, waterIndex, waterIndex)));
    const float3 coefficients = float3(0.1F, 0.2F, 0.4F);
    const float3 first = WaterPathTransmission(coefficients, 2.0F);
    const float3 second = WaterPathTransmission(coefficients, 3.0F);
    Results[1] = float4(abs(first * second - WaterPathTransmission(coefficients, 5.0F)),
        length(WaterPathTransmission(coefficients, 0.0F) - 1.0F));
    const float3 radiance = float3(0.1F, 2.0F, 8.0F);
    const float reflection = WaterDielectricReflectance(0.6F, 1.0F, waterIndex);
    const float3 conserved = ComposeWaterSurfaceRadiance(radiance, radiance, 0.0F, 1.0F, reflection);
    const float3 absorption = ComposeWaterSurfaceRadiance(0.0F, radiance, 0.0F, first, 0.0F);
    Results[2] = float4(abs(conserved - radiance), length(absorption - radiance * first));
}
