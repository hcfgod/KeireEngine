struct SpatialReflectionProbeData
{
    float4x4 WorldToLocal;
    float4x4 LocalToWorld;
    float4 ExtentsWeight;
    float4 Parameters;
};

struct SpatialSelectionData
{
    float4 ProbeIrradiance[9];
    SpatialReflectionProbeData ReflectionProbes[2];
    uint4 Metadata;
};

struct SpatialSelectionDraw
{
    uint4 Ranges;
};

struct SpatialReflectionCandidate
{
    SpatialReflectionProbeData Descriptor;
    uint4 Metadata;
};

struct SpatialLightProbeCandidate
{
    float4 ProbeIrradiance[9];
    uint4 Metadata;
};

StructuredBuffer<SpatialSelectionDraw> Draws : register(t0, space0);
StructuredBuffer<SpatialReflectionCandidate> ReflectionCandidates : register(t1, space0);
StructuredBuffer<SpatialLightProbeCandidate> LightProbeCandidates : register(t2, space0);
StructuredBuffer<uint> SpatialVolumeVisibilityMask : register(t3, space0);
RWStructuredBuffer<SpatialSelectionData> OutputRecords : register(u0, space1);

cbuffer SpatialSelectionDispatch : register(b0, space2)
{
    uint4 DispatchCounts;
};

static const uint InvalidSpatialSelectionIndex = 0xffffffffU;
static const uint SpatialSelectionHasLightProbe = 1U << 0U;
static const uint SpatialSelectionHasReflectionProbe0 = 1U << 1U;
static const uint SpatialSelectionHasReflectionProbe1 = 1U << 2U;

[numthreads(64, 1, 1)] void CSSelectSpatialLighting(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint drawIndex = dispatchThreadId.x;
    if (drawIndex >= DispatchCounts.x)
        return;

    SpatialSelectionData result = (SpatialSelectionData)0;
    result.Metadata = uint4(0U, InvalidSpatialSelectionIndex, InvalidSpatialSelectionIndex,
                            InvalidSpatialSelectionIndex);
    const SpatialSelectionDraw draw = Draws[drawIndex];

    uint reflectionCount = 0U;
    int selectedImportance = 0;
    float totalWeight = 0.0F;
    for (uint offset = 0U; offset < draw.Ranges.y; ++offset)
    {
        const SpatialReflectionCandidate candidate = ReflectionCandidates[draw.Ranges.x + offset];
        const uint maskIndex = candidate.Metadata.x;
        if (maskIndex >= DispatchCounts.y || SpatialVolumeVisibilityMask[maskIndex] == 0U)
            continue;
        const int importance = asint(candidate.Metadata.y);
        if (reflectionCount != 0U && importance != selectedImportance)
            break;
        if (reflectionCount == 0U)
            selectedImportance = importance;
        if (reflectionCount < 2U)
        {
            result.ReflectionProbes[reflectionCount] = candidate.Descriptor;
            result.Metadata[reflectionCount + 1U] = maskIndex;
            totalWeight += candidate.Descriptor.ExtentsWeight.w;
            ++reflectionCount;
        }
    }
    if (reflectionCount != 0U && totalWeight > 0.0F)
    {
        result.Metadata.x |= SpatialSelectionHasReflectionProbe0;
        result.ReflectionProbes[0].ExtentsWeight.w /= totalWeight;
        if (reflectionCount > 1U)
        {
            result.Metadata.x |= SpatialSelectionHasReflectionProbe1;
            result.ReflectionProbes[1].ExtentsWeight.w /= totalWeight;
        }
    }

    for (uint offset = 0U; offset < draw.Ranges.w; ++offset)
    {
        const SpatialLightProbeCandidate candidate = LightProbeCandidates[draw.Ranges.z + offset];
        const uint maskIndex = candidate.Metadata.x;
        if (maskIndex >= DispatchCounts.y || SpatialVolumeVisibilityMask[maskIndex] == 0U)
            continue;
        [unroll]
        for (uint coefficient = 0U; coefficient < 9U; ++coefficient)
            result.ProbeIrradiance[coefficient] = candidate.ProbeIrradiance[coefficient];
        result.Metadata.x |= SpatialSelectionHasLightProbe;
        result.Metadata.w = maskIndex;
        break;
    }
    OutputRecords[drawIndex] = result;
}
