// Reusable include. Append matching CloudGeometry/CloudPattern/CloudColor vectors for each layer to the host cbuffer.
// Returns premultiplied linear radiance and opacity; composite before exposure/tone mapping.
// A lit periodic sheet with procedural erosion, not volumetric transport or self-shadowing.
uint CloudNoiseHash(uint x, uint z, uint seed)
{
    uint value = x * 0x8da6b343U ^ z * 0xd8163841U ^ seed * 0xcb1ab31fU;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

float CloudNoiseCorner(uint2 cell, uint period, uint seed)
{
    return float(CloudNoiseHash(cell.x % period, cell.y % period, seed) >> 8U) / 16777216.0F;
}

float CloudPeriodicNoise(float2 uv, uint period, uint seed)
{
    const float2 position = frac(uv) * period;
    const uint2 cell = uint2(floor(position));
    const float2 fraction = frac(position);
    const float2 blend = fraction * fraction * (3.0F - 2.0F * fraction);
    return lerp(lerp(CloudNoiseCorner(cell, period, seed), CloudNoiseCorner(cell + uint2(1, 0), period, seed), blend.x),
                lerp(CloudNoiseCorner(cell + uint2(0, 1), period, seed),
                     CloudNoiseCorner(cell + uint2(1, 1), period, seed), blend.x), blend.y);
}

float SampleNaturalCloudPattern(float2 uv, uint seed, float footprint)
{
    const float2 warp = float2(CloudPeriodicNoise(uv, 4U, seed + 11U),
                               CloudPeriodicNoise(uv, 4U, seed + 29U));
    const float2 warped = uv + (warp - 0.5F) * 0.12F;
    float density = 0.0F;
    float weight = 0.533333333F;
    [unroll]
    for (uint octave = 0; octave < 4U; ++octave)
    {
        const uint period = 16U << octave;
        const float resolved = saturate(1.0F - footprint * period);
        density += lerp(0.5F, CloudPeriodicNoise(warped, period, seed + octave), resolved) * weight;
        weight *= 0.5F;
    }
    const float erosion = lerp(0.5F, CloudPeriodicNoise(warped, 128U, seed + 71U), saturate(1.0F - footprint * 128.0F));
    return saturate((density - 0.22F) * 1.55F - erosion * 0.12F);
}

float3 EvaluateNaturalCloudLight(float density, float sunElevation, float viewSunCosine,
                                 float3 ambient, float3 sunlight)
{
    const float core = lerp(1.0F, 0.45F, density);
    const float rim = 0.15F * pow(saturate(viewSunCosine), 8.0F) * (1.0F - density);
    const float direct = ((0.2F + 0.8F * saturate(sunElevation)) * core + rim) / 3.14159265359F;
    return ambient * (0.65F + 0.35F * (1.0F - density)) + sunlight * direct;
}

float4 EvaluateCloudLayer(float3 localDirection, float4 geometry, float4 pattern, float4 color,
                           uint4 noise, float3 ambient, float3 sunlight, float3 sunDirection)
{
    // Evaluate derivatives before any lane-varying horizon, distance or coverage rejection.
    const float safeVertical = abs(localDirection.y) >= 0.001F ? localDirection.y
                                : localDirection.y < 0.0F ? -0.001F : 0.001F;
    const float distanceToSheet = geometry.x / safeVertical;
    const float2 unwrappedUv = pattern.xy + localDirection.xz * distanceToSheet * geometry.y;
    const float footprint = max(length(ddx(unwrappedUv)), length(ddy(unwrappedUv)));
    if (geometry.w <= 0.0F || geometry.z <= 0.0F || abs(localDirection.y) < 0.001F)
    {
        return 0.0F;
    }
    // No infinite tangent projection; the local sheet model is bounded to 100 km.
    if (distanceToSheet <= 0.0F || distanceToSheet >= 100000.0F)
    {
        return 0.0F;
    }
    const float2 uv = frac(unwrappedUv);
    const float tau = 6.28318530718F;
    const float legacyDensity = saturate(0.5F + 0.25F * sin(tau * uv.x) * cos(tau * uv.y) +
                                  0.15F * sin(tau * (2.0F * uv.x + uv.y)) +
                                  0.1F * cos(tau * (uv.x - 3.0F * uv.y)));
    const float density = noise.x == 1U ? SampleNaturalCloudPattern(uv, noise.y, footprint) : legacyDensity;
    const float threshold = 1.0F - geometry.z;
    const float mask = geometry.z >= 1.0F ? 1.0F : smoothstep(threshold - pattern.z, threshold + pattern.z, density);
    float alpha = mask * geometry.w;
    alpha *= smoothstep(0.001F, pattern.w, abs(localDirection.y));
    alpha *= 1.0F - smoothstep(80000.0F, 100000.0F, distanceToSheet);
    const float3 illumination = noise.x == 1U
        ? EvaluateNaturalCloudLight(density, sunDirection.y, dot(localDirection, sunDirection), ambient, sunlight)
        : 1.0F.xxx;
    // Bound before multiplying: a valid extreme light and tint must not turn transparent pixels into Inf * 0 NaNs.
    const float3 naturalRadiance = min(illumination, 65504.0F / max(color.rgb, 0.0001F.xxx)) * color.rgb;
    return float4((noise.x == 1U ? naturalRadiance : color.rgb) * alpha, alpha);
}
