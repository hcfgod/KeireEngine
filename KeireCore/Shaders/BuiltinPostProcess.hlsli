float3 ApplyPostProcessGrade(const float3 source)
{
    if (PostGradeGain.w <= 0.0F)
    {
        return source;
    }
    const float3 balance = max(float3(1.0F + PostGrade.x * 0.25F - PostGrade.y * 0.1F,
                                      1.0F + PostGrade.y * 0.2F,
                                      1.0F - PostGrade.x * 0.25F - PostGrade.y * 0.1F), 0.0F.xxx);
    float3 graded = max(source * balance * PostGradeGain.rgb, 0.0F.xxx);
    graded = max((graded - 0.18F.xxx) * PostGrade.z + 0.18F.xxx, 0.0F.xxx);
    const float luminance = dot(graded, float3(0.2126F, 0.7152F, 0.0722F));
    graded = lerp(luminance.xxx, graded, PostGrade.w);
    return saturate(lerp(source, graded, PostGradeGain.w));
}

float3 ApplyPostProcessLookup(const float3 source)
{
    if (BloomParameters.y <= 0.0F || BloomParameters.x < 2.0F)
    {
        return source;
    }
    // Mode 4 reuses slot 1 for a linear-data horizontal cube strip: R across a tile, G down, B across tiles.
    const float edge = BloomParameters.x;
    const float3 lattice = saturate(source) * (edge - 1.0F);
    const float lowSlice = floor(lattice.b);
    const float highSlice = min(lowSlice + 1.0F, edge - 1.0F);
    const float2 lowUv = float2((lowSlice * edge + lattice.r + 0.5F) * BloomParameters.z,
                                (lattice.g + 0.5F) * BloomParameters.w);
    const float2 highUv = float2((highSlice * edge + lattice.r + 0.5F) * BloomParameters.z, lowUv.y);
    const float3 lowColor = TemporalHistoryTexture.SampleLevel(TemporalHistorySampler, lowUv, 0.0F).rgb;
    const float3 highColor = TemporalHistoryTexture.SampleLevel(TemporalHistorySampler, highUv, 0.0F).rgb;
    return lerp(source, lerp(lowColor, highColor, frac(lattice.b)), BloomParameters.y);
}

float4 ApplyBuiltinPostProcessing(const float2 uv, const uint2 pixel)
{
    const float4 source = HdrSceneTexture.SampleLevel(HdrSceneSampler, uv, 0.0F);
    float3 color = source.rgb;
    if (PostLens.w > 0.0F)
    {
        const float2 offset = (uv * 2.0F - 1.0F.xx) * ToneMapParameters.xy * PostLens.w;
        color.r = HdrSceneTexture.SampleLevel(HdrSceneSampler, saturate(uv + offset), 0.0F).r;
        color.b = HdrSceneTexture.SampleLevel(HdrSceneSampler, saturate(uv - offset), 0.0F).b;
    }
    color = ApplyPostProcessGrade(color);
    color = ApplyPostProcessLookup(color);
    if (PostLens.x > 0.0F)
    {
        float2 position = uv * 2.0F - 1.0F.xx;
        const float aspect = ToneMapParameters.y / max(ToneMapParameters.x, 1.0e-8F);
        position.x *= lerp(1.0F, aspect, PostLens.z);
        const float edge = smoothstep(1.0F - PostLens.y, 1.0F, length(position));
        color *= 1.0F - edge * PostLens.x;
    }
    if (PostGrain.x > 0.0F)
    {
        uint hash = pixel.x * 1973U + pixel.y * 9277U + uint(PostGrain.z) * 26699U + 0x68bc21ebU;
        hash = (hash ^ (hash >> 16U)) * 0x7feb352dU;
        hash = (hash ^ (hash >> 15U)) * 0x846ca68bU;
        hash ^= hash >> 16U;
        const float noise = (float(hash & 65535U) + 0.5F) / 65536.0F - 0.5F;
        const float luminance = saturate(dot(color, float3(0.2126F, 0.7152F, 0.0722F)));
        color += noise * PostGrain.x * 0.1F * (1.0F - luminance * PostGrain.y);
    }
    return float4(saturate(color), source.a);
}
