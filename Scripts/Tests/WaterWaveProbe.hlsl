#include "WaterWaves.hlsli"

RWStructuredBuffer<float4> Results : register(u0, space1);

// One thread, two records. All components should be within 1e-5 of zero.
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id != 0U))
    {
        return;
    }
    WaterWaveShaderParameters parameters = (WaterWaveShaderParameters)0;
    const WaterWaveEvaluation flat = EvaluateWaterWaves(float2(0.0F, 0.0F), parameters);
    Results[0] = float4(abs(flat.Displacement),
        length(flat.Normal - float3(0.0F, 1.0F, 0.0F)) + abs(flat.Compression));
    parameters.Count = 1U;
    parameters.Waves[0].DirectionFrequencyPhase = float4(1.0F, 0.0F, 1.0F, 0.0F);
    parameters.Waves[0].AmplitudesVelocity = float4(0.5F, 0.25F, 1.0F, 0.5F);
    const WaterWaveEvaluation wave = EvaluateWaterWaves(float2(0.0F, 0.0F), parameters);
    Results[1] = float4(length(wave.Displacement - float3(0.25F, 0.0F, 0.0F)),
        length(wave.Normal - normalize(float3(-0.5F, 1.0F, 0.0F))),
        length(wave.Velocity - float3(0.0F, -1.0F, 0.0F)), abs(wave.Compression));
    const WaterWaveEvaluation crest = EvaluateWaterWaves(float2(1.57079632679F, 0.0F), parameters);
    Results[1].w += abs(crest.Compression - 0.25F);
    const WaterSurfaceDetail resolved = EvaluateWaterSurfaceDetail(float2(0.0F, 0.0F),
        float2(0.1F, 0.0F), float2(0.0F, 0.1F), parameters);
    Results[0].x += length(resolved.Normal - wave.Normal) + abs(resolved.Compression - wave.Compression) +
        abs(resolved.UnresolvedSlopeVariance);
    const WaterSurfaceDetail resolvedCrest = EvaluateWaterSurfaceDetail(float2(1.57079632679F, 0.0F),
        float2(0.1F, 0.0F), float2(0.0F, 0.1F), parameters);
    Results[0].y += length(resolvedCrest.Normal - crest.Normal) + abs(resolvedCrest.Compression - crest.Compression);
    const WaterSurfaceDetail unresolved = EvaluateWaterSurfaceDetail(float2(1.57079632679F, 0.0F),
        float2(4.0F, 0.0F), float2(0.0F, 0.1F), parameters);
    Results[0].z += length(unresolved.Normal - float3(0.0F, 1.0F, 0.0F)) + abs(unresolved.Compression) +
        abs(unresolved.UnresolvedSlopeVariance - 0.125F);
    // A long footprint perpendicular to propagation must not erase a resolved wave.
    const WaterSurfaceDetail perpendicular = EvaluateWaterSurfaceDetail(float2(0.0F, 0.0F),
        float2(0.0F, 4.0F), float2(0.1F, 0.0F), parameters);
    Results[0].w += length(perpendicular.Normal - wave.Normal) + abs(perpendicular.UnresolvedSlopeVariance);
    parameters.Waves[0].AmplitudesVelocity.y = 0.0F;
    const WaterWaveEvaluation uncompressed = EvaluateWaterWaves(float2(1.57079632679F, 0.0F), parameters);
    Results[1].w += abs(uncompressed.Compression);
}
