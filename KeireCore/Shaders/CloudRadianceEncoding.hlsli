// History stores signed log2 radiance in RGB and linear opacity in A.
// Decode before filtering; hardware filtering of this representation is invalid.
float RoundCloudLogToHalf(float value)
{
    if (abs(value) < 0.00006103515625F)
    {
        return round(value * 16777216.0F) / 16777216.0F;
    }
    const uint bits = asuint(value);
    const uint discarded = bits & 8191U;
    uint rounded = bits & 0xffffe000U;
    // Pre-round to nearest-even: render-target downconversion may otherwise truncate.
    if (discarded > 4096U || (discarded == 4096U && (rounded & 8192U) != 0U))
    {
        rounded += 8192U;
    }
    return asfloat(rounded);
}

float EncodeCloudRadianceChannel(float value)
{
    return value > 0.0F ? RoundCloudLogToHalf(log2(value)) : -65504.0F;
}

float DecodeCloudRadianceChannel(float value)
{
    if (value <= -65504.0F)
    {
        return 0.0F;
    }
    if (value >= 128.0F)
    {
        return asfloat(0x7f7fffffU);
    }
    return exp2(value);
}

float4 EncodeCloudRadiance(float4 value)
{
    return float4(EncodeCloudRadianceChannel(value.r), EncodeCloudRadianceChannel(value.g),
        EncodeCloudRadianceChannel(value.b), value.a);
}

float4 DecodeCloudRadiance(float4 value)
{
    return float4(DecodeCloudRadianceChannel(value.r), DecodeCloudRadianceChannel(value.g),
        DecodeCloudRadianceChannel(value.b), value.a);
}
