// Reduced texels represent exact source blocks. A partial final block has its own center;
// treating ceil-divided images as the same normalized UV space shifts silhouettes on odd sizes.
float DofFullToReducedAxis(const float uv, const float fullExtent, const float divisor)
{
    const float count = ceil(fullExtent / divisor);
    if (count <= 1.0F)
    {
        return 0.5F;
    }
    const float coordinate = uv * fullExtent;
    const float remainder = fullExtent - (count - 1.0F) * divisor;
    const float previousCenter = (count - 1.5F) * divisor;
    const float lastCenter = (count - 1.0F) * divisor + remainder * 0.5F;
    if (remainder < divisor && coordinate > previousCenter)
    {
        const float index = count - 2.0F + (coordinate - previousCenter) / (lastCenter - previousCenter);
        return (index + 0.5F) / count;
    }
    return coordinate / (divisor * count);
}

float DofReducedToFullAxis(const float uv, const float fullExtent, const float divisor)
{
    const float count = ceil(fullExtent / divisor);
    if (count <= 1.0F)
    {
        return 0.5F;
    }
    const float index = uv * count - 0.5F;
    const float remainder = fullExtent - (count - 1.0F) * divisor;
    const float previousCenter = (count - 1.5F) * divisor;
    const float lastCenter = (count - 1.0F) * divisor + remainder * 0.5F;
    const float coordinate = remainder < divisor && index > count - 2.0F ?
        lerp(previousCenter, lastCenter, index - (count - 2.0F)) : (index + 0.5F) * divisor;
    return clamp(coordinate, 0.5F, fullExtent - 0.5F) / fullExtent;
}

float2 DofFullToReducedUv(const float2 uv, const float divisor)
{
    const float2 size = round(1.0F.xx / ToneMapParameters.xy);
    return float2(DofFullToReducedAxis(uv.x, size.x, divisor), DofFullToReducedAxis(uv.y, size.y, divisor));
}

float2 DofReducedToFullUv(const float2 uv, const float divisor)
{
    const float2 size = round(1.0F.xx / ToneMapParameters.xy);
    return float2(DofReducedToFullAxis(uv.x, size.x, divisor), DofReducedToFullAxis(uv.y, size.y, divisor));
}

// Signed CoC stays in full-resolution pixels throughout this pipeline.
// RGB is linear HDR; near/far layer RGB is premultiplied by its validity/coverage.
float4 DofPrefilter(const int2 outputPixel, const bool firstLevel)
{
    uint width;
    uint height;
    HdrSceneTexture.GetDimensions(width, height);
    const int2 limit = int2(width, height) - 1;
    float4 samples[4];
    float reference = -65504.0F;
    [unroll]
    for (int index = 0; index < 4; ++index)
    {
        const int2 pixel = min(outputPixel * 2 + int2(index & 1, index >> 1), limit);
        samples[index] = HdrSceneTexture.Load(int3(pixel, 0));
        if (firstLevel)
        {
            const float2 uv = (float2(pixel) + 0.5F) * ToneMapParameters.xy;
            samples[index].a = LensRadius(LensViewDistance(uv));
        }
        reference = max(reference, samples[index].a);
    }
    float3 color = 0.0F.xxx;
    float total = 0.0F;
    [unroll]
    for (int index = 0; index < 4; ++index)
    {
        // Never average across unrelated focal layers while reducing bright highlights.
        const float weight = saturate(1.0F - abs(samples[index].a - reference));
        color += samples[index].rgb * weight;
        total += weight;
    }
    return float4(color / max(total, 1.0e-5F), reference);
}

float4 DofTileBounds(const int2 tile)
{
    const int2 limit = int2(round(1.0F.xx / ToneMapParameters.xy)) - 1;
    float nearRadius = 0.0F;
    [unroll]
    for (int y = 0; y < 8; ++y)
    {
        [unroll]
        for (int x = 0; x < 8; ++x)
        {
            const int2 pixel = min(tile * 8 + int2(x, y), limit);
            const float radius = LensRadius(LensViewDistance((float2(pixel) + 0.5F) * ToneMapParameters.xy));
            nearRadius = max(nearRadius, radius);
        }
    }
    return float4(nearRadius, 0.0F, 0.0F, 0.0F);
}

float4 DofDilateBounds(const int2 tile)
{
    uint width;
    uint height;
    HdrSceneTexture.GetDimensions(width, height);
    float radius = 0.0F;
    // Max blur is 32px and a tile is 8px. Exact tile bounds conservatively include every possible caster.
    [loop]
    for (int y = -4; y <= 4; ++y)
    {
        [loop]
        for (int x = -4; x <= 4; ++x)
        {
            const int2 source = tile + int2(x, y);
            if (any(source < 0) || any(source >= int2(width, height)))
            {
                continue;
            }
            const float candidate = HdrSceneTexture.Load(int3(source, 0)).r;
            const float2 gap = max(abs(float2(x, y)) * 8.0F - 8.0F, 0.0F.xx);
            if (dot(gap, gap) <= candidate * candidate)
            {
                radius = max(radius, candidate);
            }
        }
    }
    return float4(radius, 0.0F, 0.0F, 0.0F);
}

float4 DofPyramidSample(const float2 uv, const float footprint)
{
    const float level = clamp(log2(max(footprint * 0.5F, 1.0F)), 0.0F, 2.0F);
    const float4 fine = HdrSceneTexture.SampleLevel(HdrSceneSampler, DofFullToReducedUv(uv, 2.0F), 0.0F);
    if (level < 0.001F)
    {
        return fine;
    }
    float4 filtered;
    if (level < 1.0F)
    {
        const float4 medium = TemporalHistoryTexture.SampleLevel(TemporalHistorySampler, DofFullToReducedUv(uv, 4.0F), 0.0F);
        filtered = lerp(fine, medium, level);
    }
    else
    {
        const float4 coarse = VelocityTexture.SampleLevel(VelocitySampler, DofFullToReducedUv(uv, 8.0F), 0.0F);
        if (level > 1.999F)
        {
            filtered = coarse;
        }
        else
        {
            const float4 medium = TemporalHistoryTexture.SampleLevel(TemporalHistorySampler, DofFullToReducedUv(uv, 4.0F), 0.0F);
            filtered = lerp(medium, coarse, level - 1.0F);
        }
    }
    // Bilateral levels can select a different thin foreground surface: retain the fine layer there.
    return abs(filtered.a - fine.a) < 1.0F ? filtered : fine;
}

float4 DofGather(const float2 reducedUv, const bool nearField)
{
    const float2 uv = DofReducedToFullUv(reducedUv, 2.0F);
    const float centerCoc = HdrSceneTexture.SampleLevel(HdrSceneSampler, reducedUv, 0.0F).a;
    const int2 tile = int2(uv / ToneMapParameters.xy) / 8;
    const float searchRadius = nearField ? BloomTexture.Load(int3(tile, 0)).r : max(-centerCoc, 0.0F);
    if (searchRadius < 0.5F)
    {
        return 0.0F.xxxx;
    }
    float3 sum = 0.0F.xxx;
    float total = 0.0F;
    // Filter each sample over its quadrature footprint instead of replicating point highlights.
    const float footprint = max(2.0F, searchRadius * 0.40824829F);
    [unroll]
    for (int index = 0; index < 24; ++index)
    {
        const float fraction = (float(index) + 0.5F) / 24.0F;
        const float angle = float(index) * 2.39996323F;
        const float2 offset = float2(cos(angle), sin(angle)) * sqrt(fraction) * searchRadius;
        const float2 sampleUv = uv + offset * ToneMapParameters.xy;
        if (any(sampleUv < 0.0F.xx) || any(sampleUv > 1.0F.xx))
        {
            continue;
        }
        const float4 gathered = DofPyramidSample(sampleUv, footprint);
        const float sampleRadius = abs(gathered.a);
        const float support = saturate(sampleRadius - length(offset) + 1.0F);
        float weight;
        if (nearField)
        {
            // Source CoC controls coverage, including on in-focus and background destinations.
            // A farther foreground layer cannot cover a closer foreground center.
            weight = gathered.a > 0.5F && gathered.a >= centerCoc - 1.0F ? support : 0.0F;
            weight *= (searchRadius * searchRadius) / max(sampleRadius * sampleRadius, 1.0F);
        }
        else
        {
            weight = gathered.a < -0.5F && gathered.a <= centerCoc + 1.0F ? support : 0.0F;
        }
        sum += gathered.rgb * weight;
        total += weight;
    }
    if (total < 1.0e-5F)
    {
        return 0.0F.xxxx;
    }
    const float coverage = nearField ? saturate(total / 24.0F) : 1.0F;
    return float4(sum / total * coverage, coverage);
}

float4 DofComposite(const float2 uv)
{
    const float4 sharp = HdrSceneTexture.SampleLevel(HdrSceneSampler, uv, 0.0F);
    const float coc = LensRadius(LensViewDistance(uv));
    const float4 farLayer = BloomTexture.SampleLevel(BloomSampler, DofFullToReducedUv(uv, 2.0F), 0.0F);
    float3 color = lerp(sharp.rgb, farLayer.rgb / max(farLayer.a, 1.0e-5F),
        smoothstep(0.5F, 2.0F, -coc) * saturate(farLayer.a));
    float4 nearLayer = 0.0F.xxxx;
    uint nearWidth;
    uint nearHeight;
    VelocityTexture.GetDimensions(nearWidth, nearHeight);
    const float2 nearUv = DofFullToReducedUv(uv, 2.0F);
    // Coverage filtering closes quadrature holes and softens silhouettes without adding stochastic noise.
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            nearLayer += VelocityTexture.SampleLevel(VelocitySampler,
                nearUv + float2(x, y) / float2(nearWidth, nearHeight), 0.0F);
        }
    }
    nearLayer /= 9.0F;
    const float opacity = smoothstep(0.0F, 1.0F, nearLayer.a);
    color = lerp(color, nearLayer.rgb / max(nearLayer.a, 1.0e-5F), opacity);
    return float4(clamp(color, 0.0F.xxx, 65504.0F.xxx), sharp.a);
}
