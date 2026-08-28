struct InstanceData
{
    float4x4 Model;
    float4x4 NormalMatrix;
    float4 Tint;
};

struct OcclusionCandidate
{
    float4 BoundsMinimum;
    float4 BoundsMaximum;
    uint4 Metadata;
};

Texture2D<float> Hierarchy0 : register(t0, space0);
Texture2D<float> Hierarchy1 : register(t1, space0);
Texture2D<float> Hierarchy2 : register(t2, space0);
Texture2D<float> Hierarchy3 : register(t3, space0);
Texture2D<float> Hierarchy4 : register(t4, space0);
Texture2D<float> Hierarchy5 : register(t5, space0);
Texture2D<float> Hierarchy6 : register(t6, space0);
Texture2D<float> Hierarchy7 : register(t7, space0);
Texture2D<float> Hierarchy8 : register(t8, space0);
Texture2D<float> Hierarchy9 : register(t9, space0);
Texture2D<float> Hierarchy10 : register(t10, space0);
Texture2D<float> Hierarchy11 : register(t11, space0);
Texture2D<float> Hierarchy12 : register(t12, space0);
Texture2D<float> Hierarchy13 : register(t13, space0);
SamplerState HierarchySampler0 : register(s0, space0);
SamplerState HierarchySampler1 : register(s1, space0);
SamplerState HierarchySampler2 : register(s2, space0);
SamplerState HierarchySampler3 : register(s3, space0);
SamplerState HierarchySampler4 : register(s4, space0);
SamplerState HierarchySampler5 : register(s5, space0);
SamplerState HierarchySampler6 : register(s6, space0);
SamplerState HierarchySampler7 : register(s7, space0);
SamplerState HierarchySampler8 : register(s8, space0);
SamplerState HierarchySampler9 : register(s9, space0);
SamplerState HierarchySampler10 : register(s10, space0);
SamplerState HierarchySampler11 : register(s11, space0);
SamplerState HierarchySampler12 : register(s12, space0);
SamplerState HierarchySampler13 : register(s13, space0);
StructuredBuffer<OcclusionCandidate> Candidates : register(t14, space0);
StructuredBuffer<InstanceData> InputInstances : register(t15, space0);
RWStructuredBuffer<uint> GeometryVisibility : register(u0, space1);
RWStructuredBuffer<uint> VfxVisibilityMask : register(u1, space1);
RWStructuredBuffer<uint> LocalLightVisibilityMask : register(u2, space1);
RWStructuredBuffer<uint> SpatialVolumeVisibilityMask : register(u3, space1);

cbuffer ClassifyDispatch : register(b0, space2)
{
    float4x4 ViewProjection;
    float4 ViewportBiasLevels;
    uint4 DispatchCounts;
    uint4 HierarchySizes[14];
};

static const uint ForceVisible = 1U;
static const uint IndexedIndirectConsumer = 0U;
static const uint VfxVisibilityConsumer = 1U;
static const uint ForwardPlusLightConsumer = 2U;
static const uint SpatialVolumeConsumer = 3U;

void StoreVisibility(OcclusionCandidate candidate, uint visible)
{
    const uint outputIndex = candidate.Metadata.z;
    switch (candidate.Metadata.w)
    {
    case VfxVisibilityConsumer:
        VfxVisibilityMask[outputIndex] = visible;
        break;
    case ForwardPlusLightConsumer:
        LocalLightVisibilityMask[outputIndex] = visible;
        break;
    case SpatialVolumeConsumer:
        SpatialVolumeVisibilityMask[outputIndex] = visible;
        break;
    case IndexedIndirectConsumer:
    default:
        GeometryVisibility[outputIndex] = visible;
        break;
    }
}

float SampleHierarchy(uint level, float2 uv)
{
    switch (level)
    {
    case 0U:
        return Hierarchy0.SampleLevel(HierarchySampler0, uv, 0.0F);
    case 1U:
        return Hierarchy1.SampleLevel(HierarchySampler1, uv, 0.0F);
    case 2U:
        return Hierarchy2.SampleLevel(HierarchySampler2, uv, 0.0F);
    case 3U:
        return Hierarchy3.SampleLevel(HierarchySampler3, uv, 0.0F);
    case 4U:
        return Hierarchy4.SampleLevel(HierarchySampler4, uv, 0.0F);
    case 5U:
        return Hierarchy5.SampleLevel(HierarchySampler5, uv, 0.0F);
    case 6U:
        return Hierarchy6.SampleLevel(HierarchySampler6, uv, 0.0F);
    case 7U:
        return Hierarchy7.SampleLevel(HierarchySampler7, uv, 0.0F);
    case 8U:
        return Hierarchy8.SampleLevel(HierarchySampler8, uv, 0.0F);
    case 9U:
        return Hierarchy9.SampleLevel(HierarchySampler9, uv, 0.0F);
    case 10U:
        return Hierarchy10.SampleLevel(HierarchySampler10, uv, 0.0F);
    case 11U:
        return Hierarchy11.SampleLevel(HierarchySampler11, uv, 0.0F);
    case 12U:
        return Hierarchy12.SampleLevel(HierarchySampler12, uv, 0.0F);
    default:
        return Hierarchy13.SampleLevel(HierarchySampler13, uv, 0.0F);
    }
}

[numthreads(256, 1, 1)] void CSClassify(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint candidateIndex = dispatchThreadId.x;
    if (candidateIndex >= DispatchCounts.x)
        return;

    const OcclusionCandidate candidate = Candidates[candidateIndex];
    if ((candidate.Metadata.x & ForceVisible) != 0U)
    {
        StoreVisibility(candidate, 1U);
        return;
    }

    const InstanceData instance = InputInstances[candidateIndex];
    float2 minimumPixel = ViewportBiasLevels.xy;
    float2 maximumPixel = 0.0F.xx;
    float nearestDepth = 1.0F;
    bool ambiguous = false;
    [unroll] for (uint corner = 0U; corner < 8U; ++corner)
    {
        const float3 local = float3((corner & 1U) != 0U ? candidate.BoundsMaximum.x : candidate.BoundsMinimum.x,
                                    (corner & 2U) != 0U ? candidate.BoundsMaximum.y : candidate.BoundsMinimum.y,
                                    (corner & 4U) != 0U ? candidate.BoundsMaximum.z : candidate.BoundsMinimum.z);
        const float4 world = mul(instance.Model, float4(local, 1.0F));
        const float4 clip = mul(ViewProjection, world);
        if (clip.w <= 0.00001F || any(isnan(clip)) || any(isinf(clip)))
        {
            ambiguous = true;
            break;
        }
        const float3 ndc = clip.xyz / clip.w;
        if (any(isnan(ndc)) || any(isinf(ndc)) || ndc.z < 0.0F || ndc.z > 1.0F)
        {
            ambiguous = true;
            break;
        }
        const float2 pixel = (ndc.xy * float2(0.5F, -0.5F) + 0.5F.xx) * ViewportBiasLevels.xy;
        minimumPixel = min(minimumPixel, pixel);
        maximumPixel = max(maximumPixel, pixel);
        nearestDepth = min(nearestDepth, ndc.z);
    }

    if (ambiguous || any(maximumPixel < 0.0F.xx) || any(minimumPixel > ViewportBiasLevels.xy))
    {
        StoreVisibility(candidate, 1U);
        return;
    }

    minimumPixel = clamp(floor(minimumPixel) - 1.0F.xx, 0.0F.xx, ViewportBiasLevels.xy - 1.0F.xx);
    maximumPixel = clamp(ceil(maximumPixel) + 1.0F.xx, minimumPixel, ViewportBiasLevels.xy - 1.0F.xx);

    const uint levelCount = max(DispatchCounts.y, 1U);
    uint selectedLevel = levelCount - 1U;
    uint2 minimumTexel = 0U.xx;
    uint2 maximumTexel = 0U.xx;
    [loop] for (uint level = 0U; level < levelCount; ++level)
    {
        const uint2 size = max(HierarchySizes[level].xy, 1U.xx);
        const float texelCoverage = float(1U << (level + 1U));
        const uint2 candidateMinimum = min(uint2(floor(minimumPixel / texelCoverage)), size - 1U.xx);
        const uint2 candidateMaximum = min(uint2(floor(maximumPixel / texelCoverage)), size - 1U.xx);
        selectedLevel = level;
        minimumTexel = candidateMinimum;
        maximumTexel = candidateMaximum;
        if (all(candidateMaximum - candidateMinimum < 2U.xx))
            break;
    }

    const uint2 selectedSize = max(HierarchySizes[selectedLevel].xy, 1U.xx);
    float farthestDepth = 0.0F;
    [loop] for (uint y = minimumTexel.y; y <= maximumTexel.y; ++y)
    {
        [loop] for (uint x = minimumTexel.x; x <= maximumTexel.x; ++x)
        {
            const float2 uv = (float2(x, y) + 0.5F.xx) / float2(selectedSize);
            farthestDepth = max(farthestDepth, SampleHierarchy(selectedLevel, uv));
        }
    }
    StoreVisibility(candidate, nearestDepth > farthestDepth + ViewportBiasLevels.z ? 0U : 1U);
}
