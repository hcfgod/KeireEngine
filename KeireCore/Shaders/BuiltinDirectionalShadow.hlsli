#pragma once

float SampleShadowPcf(Texture2DArray<float> textureValue, SamplerState samplerValue, const float2 uv,
                      const float layer, const float depth, const float inverseResolution, const bool soft,
                      const float4 sampleBounds, const bool clampSamples)
{
    if (any(uv < sampleBounds.xy) || any(uv > sampleBounds.zw) || depth <= 0.0F || depth >= 1.0F)
        return 1.0F;
    if (!soft)
        return depth <= textureValue.SampleLevel(samplerValue, float3(uv, layer), 0.0F) ? 1.0F : 0.0F;

    float visibility = 0.0F;
    float totalWeight = 0.0F;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float weight = (2.0F - abs((float)x)) * (2.0F - abs((float)y));
            const float2 unclampedUv = uv + float2(x, y) * inverseResolution;
            if (!clampSamples &&
                (any(unclampedUv < sampleBounds.xy) || any(unclampedUv > sampleBounds.zw)))
            {
                visibility += weight;
            }
            else
            {
                const float2 sampleUv = clamp(unclampedUv, sampleBounds.xy, sampleBounds.zw);
                const float storedDepth =
                    textureValue.SampleLevel(samplerValue, float3(sampleUv, layer), 0.0F);
                visibility += depth <= storedDepth ? weight : 0.0F;
            }
            totalWeight += weight;
        }
    }
    return visibility / max(totalWeight, 1.0F);
}

float SampleDirectionalShadow(const float3 worldPosition, const float viewDepth)
{
    const float cascadeCount = abs(DirectionalShadowParameters.x);
    if (cascadeCount < 0.5F)
        return 1.0F;
    uint cascade = 0U;
    cascade += viewDepth > DirectionalCascadeSplits.x;
    cascade += viewDepth > DirectionalCascadeSplits.y;
    cascade += viewDepth > DirectionalCascadeSplits.z;
    cascade = min(cascade, (uint)cascadeCount - 1U);
    const float4 clip = mul(DirectionalShadowMatrices[cascade], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    if (any(uv < 0.0F.xx) || any(uv > 1.0F.xx) || projected.z <= 0.0F || projected.z >= 1.0F)
        return 1.0F;
    const float visibility = SampleShadowPcf(
        DirectionalShadowTexture, DirectionalShadowSampler, uv, cascade,
        projected.z - DirectionalShadowParameters.z, DirectionalShadowParameters.w,
        DirectionalShadowParameters.x > 0.0F, float4(0.0F, 0.0F, 1.0F, 1.0F), false);
    return lerp(1.0F, visibility, saturate(DirectionalShadowParameters.y));
}
