Texture2D<float> SourceDepth : register(t0, space0);
SamplerState SourceSampler : register(s0, space0);
RWTexture2D<float> TargetDepth : register(u0, space1);

cbuffer PyramidDispatch : register(b0, space2)
{
    uint4 SourceTargetSize;
};

float LoadSource(uint2 coordinate)
{
    const uint2 clamped = min(coordinate, SourceTargetSize.xy - 1U.xx);
    const float2 uv = (float2(clamped) + 0.5F.xx) / float2(SourceTargetSize.xy);
    return SourceDepth.SampleLevel(SourceSampler, uv, 0.0F);
}

float ReduceSource(uint2 targetCoordinate)
{
    const uint2 source = targetCoordinate * 2U;
    float result = LoadSource(source);
    result = max(result, LoadSource(source + uint2(1U, 0U)));
    result = max(result, LoadSource(source + uint2(0U, 1U)));
    result = max(result, LoadSource(source + uint2(1U, 1U)));
    return result;
}

[numthreads(8, 8, 1)] void CSBuildBase(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= SourceTargetSize.zw))
        return;
    TargetDepth[dispatchThreadId.xy] = ReduceSource(dispatchThreadId.xy);
}

[numthreads(8, 8, 1)] void CSReduce(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= SourceTargetSize.zw))
        return;
    TargetDepth[dispatchThreadId.xy] = ReduceSource(dispatchThreadId.xy);
}
