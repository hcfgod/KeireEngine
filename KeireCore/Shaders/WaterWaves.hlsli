#ifndef KEIRE_WATER_WAVES_INCLUDED
#define KEIRE_WATER_WAVES_INCLUDED

#define KEIRE_MAX_WATER_WAVES 16

// Matches WaterWaveShaderParameters. Phase is captured at a nearby patch origin on the CPU.
struct WaterWaveShaderEntry
{
    float4 DirectionFrequencyPhase;
    float4 AmplitudesVelocity;
};

struct WaterWaveShaderParameters
{
    uint Count;
    uint3 Padding;
    WaterWaveShaderEntry Waves[KEIRE_MAX_WATER_WAVES];
};

struct WaterWaveEvaluation
{
    float3 Displacement;
    float3 Normal;
    float3 Velocity;
    float Compression;
};

struct WaterSurfaceDetail
{
    float3 Normal;
    float Compression;
    float UnresolvedSlopeVariance;
};

// Filter in parametric patch coordinates, so detail follows the same moving Gerstner surface.
// Derivatives are supplied by the caller; compute probes can exercise the exact fragment filter.
WaterSurfaceDetail EvaluateWaterSurfaceDetail(float2 patchOffset, float2 pixelDx, float2 pixelDy,
    WaterWaveShaderParameters parameters)
{
    WaterSurfaceDetail result = (WaterSurfaceDetail)0;
    float3 tangentX = float3(1.0F, 0.0F, 0.0F);
    float3 tangentZ = float3(0.0F, 0.0F, 1.0F);
    const uint count = min(parameters.Count, (uint)KEIRE_MAX_WATER_WAVES);
    [loop]
    for (uint index = 0U; index < count; ++index)
    {
        const WaterWaveShaderEntry wave = parameters.Waves[index];
        const float2 direction = wave.DirectionFrequencyPhase.xy;
        const float frequency = wave.DirectionFrequencyPhase.z;
        const float phaseSpan = frequency *
            (abs(dot(direction, pixelDx)) + abs(dot(direction, pixelDy)));
        // Fade before the pixel Nyquist limit instead of letting unresolved ripples sparkle.
        const float resolved = 1.0F - smoothstep(1.57079632679F, 3.14159265359F, phaseSpan);
        const float slope = wave.AmplitudesVelocity.x * frequency;
        result.UnresolvedSlopeVariance += 0.5F * slope * slope * (1.0F - resolved * resolved);
        if (resolved <= 0.0F)
        {
            continue;
        }
        const float phase = dot(direction, patchOffset) * frequency + wave.DirectionFrequencyPhase.w;
        float sine;
        float cosine;
        sincos(phase, sine, cosine);
        const float horizontalDerivative = -wave.AmplitudesVelocity.y * frequency * sine * resolved;
        const float verticalDerivative = slope * cosine * resolved;
        tangentX += float3(horizontalDerivative * direction.x * direction.x, verticalDerivative * direction.x,
            horizontalDerivative * direction.x * direction.y);
        tangentZ += float3(horizontalDerivative * direction.x * direction.y, verticalDerivative * direction.y,
            horizontalDerivative * direction.y * direction.y);
    }
    result.Normal = normalize(cross(tangentZ, tangentX));
    result.Compression = saturate(1.0F - (tangentX.x * tangentZ.z - tangentX.z * tangentZ.x));
    return result;
}

WaterWaveEvaluation EvaluateWaterWaves(float2 patchOffset, WaterWaveShaderParameters parameters)
{
    WaterWaveEvaluation result = (WaterWaveEvaluation)0;
    float3 tangentX = float3(1.0F, 0.0F, 0.0F);
    float3 tangentZ = float3(0.0F, 0.0F, 1.0F);
    const uint count = min(parameters.Count, (uint)KEIRE_MAX_WATER_WAVES);
    [loop]
    for (uint index = 0U; index < count; ++index)
    {
        const WaterWaveShaderEntry wave = parameters.Waves[index];
        const float2 direction = wave.DirectionFrequencyPhase.xy;
        const float frequency = wave.DirectionFrequencyPhase.z;
        const float phase = dot(direction, patchOffset) * frequency + wave.DirectionFrequencyPhase.w;
        float sine;
        float cosine;
        sincos(phase, sine, cosine);
        const float amplitude = wave.AmplitudesVelocity.x;
        const float horizontalAmplitude = wave.AmplitudesVelocity.y;
        result.Displacement += float3(direction.x * horizontalAmplitude * cosine, amplitude * sine,
            direction.y * horizontalAmplitude * cosine);
        const float horizontalDerivative = -horizontalAmplitude * frequency * sine;
        const float verticalDerivative = amplitude * frequency * cosine;
        tangentX += float3(horizontalDerivative * direction.x * direction.x, verticalDerivative * direction.x,
            horizontalDerivative * direction.x * direction.y);
        tangentZ += float3(horizontalDerivative * direction.x * direction.y, verticalDerivative * direction.y,
            horizontalDerivative * direction.y * direction.y);
        result.Velocity += float3(direction.x * wave.AmplitudesVelocity.w * sine,
            -wave.AmplitudesVelocity.z * cosine, direction.y * wave.AmplitudesVelocity.w * sine);
    }
    result.Normal = normalize(cross(tangentZ, tangentX));
    // Horizontal area loss identifies converging crests, unlike height which also marks gentle swells.
    const float horizontalJacobian = tangentX.x * tangentZ.z - tangentX.z * tangentZ.x;
    result.Compression = saturate(1.0F - horizontalJacobian);
    return result;
}

#endif
