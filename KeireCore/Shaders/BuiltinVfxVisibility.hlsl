struct GpuParticle
{
    float4 PositionAge;
    float4 VelocityLifetime;
    float4 Tint;
    float4 SizeRotation;
    float4 AccelerationSizeEnd;
    float4 ColorStart;
    float4 ColorEnd;
    float4 PreviousPositionStrip;
    uint4 Identity;
    uint4 SequenceIdentity;
};

struct VfxInstanceData
{
    float4x4 Model;
    float4x4 NormalMatrix;
    float4 Tint;
};

struct VfxOcclusionCandidate
{
    float4 BoundsMinimum;
    float4 BoundsMaximum;
    uint4 Metadata;
};

// Both visibility entrypoints share this dense five-read/five-write ABI. Keeping the resources in portable SDL
// compute spaces avoids exposing the fourteen-buffer simulation ABI to backends with an eight-RW-buffer limit.
StructuredBuffer<GpuParticle> SourceParticles : register(t0, space0);
StructuredBuffer<uint> SourceIndices : register(t1, space0);
ByteAddressBuffer SourceIndirectArguments : register(t2, space0);
StructuredBuffer<VfxInstanceData> SourceInstances : register(t3, space0);
StructuredBuffer<uint> SourceVisibilityMask : register(t4, space0);

RWStructuredBuffer<VfxOcclusionCandidate> OutputCandidates : register(u0, space1);
RWStructuredBuffer<VfxInstanceData> OutputCandidateInstances : register(u1, space1);
RWStructuredBuffer<uint> OutputIndices : register(u2, space1);
RWByteAddressBuffer OutputIndirectArguments : register(u3, space1);
RWStructuredBuffer<VfxInstanceData> OutputInstances : register(u4, space1);

cbuffer VfxVisibilityDispatch : register(b0, space2)
{
    // Absolute unified-candidate first, VFX mask first, VFX range count, output primitive count.
    uint4 VisibilityMetadata;
    // World particle capacity, emitter render capacity, renderer type, reserved.
    uint4 VisibilityExecution;
    float4 VisibilityBoundsMinimum;
    float4 VisibilityBoundsMaximum;
};

static const uint VfxRendererSprite = 0U;
static const uint VfxRendererMesh = 1U;
static const uint VfxRendererRibbon = 2U;
static const uint VfxVisibilityConsumer = 1U;
static const uint VfxForceVisible = 1U;

bool Equal64(uint2 left, uint2 right) { return all(left == right); }

uint2 Add64(uint2 left, uint2 right)
{
    const uint low = left.x + right.x;
    return uint2(low, left.y + right.y + (low < left.x ? 1U : 0U));
}

bool IsFiniteVfxFloat3(float3 value)
{
    return !any(isnan(value)) && !any(isinf(value));
}

bool IsFiniteVfxFloat(float value)
{
    return !isnan(value) && !isinf(value);
}

VfxInstanceData IdentityVfxInstance()
{
    VfxInstanceData identity;
    identity.Model = float4x4(1.0F, 0.0F, 0.0F, 0.0F,
                              0.0F, 1.0F, 0.0F, 0.0F,
                              0.0F, 0.0F, 1.0F, 0.0F,
                              0.0F, 0.0F, 0.0F, 1.0F);
    identity.NormalMatrix = identity.Model;
    identity.Tint = 1.0F.xxxx;
    return identity;
}

void StoreVfxVisibilityCandidate(uint localIndex, float3 boundsMinimum, float3 boundsMaximum,
                                 VfxInstanceData instance, uint visibilityClass, bool forceVisible,
                                 float maximumWorldPositionDisplacementRadius)
{
    if (localIndex >= VisibilityMetadata.z)
        return;
    const uint candidateIndex = VisibilityMetadata.x + localIndex;
    VfxOcclusionCandidate candidate;
    candidate.BoundsMinimum = float4(boundsMinimum, maximumWorldPositionDisplacementRadius);
    candidate.BoundsMaximum = float4(boundsMaximum, maximumWorldPositionDisplacementRadius);
    candidate.Metadata =
        uint4(forceVisible ? VfxForceVisible : 0U, visibilityClass, VisibilityMetadata.y + localIndex,
              VfxVisibilityConsumer);
    OutputCandidates[candidateIndex] = candidate;
    OutputCandidateInstances[candidateIndex] = instance;
}

[numthreads(256, 1, 1)] void CSBuildVisibilityCandidates(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint sourceCount = min(SourceIndirectArguments.Load(4), VisibilityExecution.y);
    if (VisibilityExecution.z == VfxRendererRibbon)
    {
        if (dispatchThreadId.x != 0U)
            return;
        float3 boundsMinimum = float3(3.402823466e+38F, 3.402823466e+38F, 3.402823466e+38F);
        float3 boundsMaximum = -boundsMinimum;
        bool valid = sourceCount > 0U;
        [loop] for (uint source = 0U; source < sourceCount; ++source)
        {
            const uint particleIndex = SourceIndices[source];
            const GpuParticle particle = SourceParticles[particleIndex];
            const float halfWidth = max(particle.SizeRotation.x, 0.0F) * 0.5F;
            valid = valid && particle.PositionAge.w >= 0.0F && IsFiniteVfxFloat3(particle.PositionAge.xyz) &&
                    IsFiniteVfxFloat(halfWidth);
            if (!valid)
                break;
            boundsMinimum = min(boundsMinimum, particle.PositionAge.xyz - halfWidth.xxx);
            boundsMaximum = max(boundsMaximum, particle.PositionAge.xyz + halfWidth.xxx);
            if (particle.Identity.z > 0U && particle.Identity.z <= VisibilityExecution.x)
            {
                const GpuParticle previous = SourceParticles[particle.Identity.z - 1U];
                const bool connected = previous.PositionAge.w >= 0.0F &&
                                       all(previous.Identity.xy == particle.Identity.xy) &&
                                       Equal64(Add64(previous.SequenceIdentity.xy, uint2(1U, 0U)),
                                               particle.SequenceIdentity.xy);
                if (connected)
                {
                    const float previousHalfWidth = max(previous.SizeRotation.x, 0.0F) * 0.5F;
                    const float segmentHalfWidth = max(halfWidth, previousHalfWidth);
                    valid = valid && IsFiniteVfxFloat3(previous.PositionAge.xyz) &&
                            IsFiniteVfxFloat(previousHalfWidth) && IsFiniteVfxFloat(segmentHalfWidth);
                    if (!valid)
                        break;
                    boundsMinimum = min(boundsMinimum, particle.PositionAge.xyz - segmentHalfWidth.xxx);
                    boundsMaximum = max(boundsMaximum, particle.PositionAge.xyz + segmentHalfWidth.xxx);
                    boundsMinimum = min(boundsMinimum, previous.PositionAge.xyz - segmentHalfWidth.xxx);
                    boundsMaximum = max(boundsMaximum, previous.PositionAge.xyz + segmentHalfWidth.xxx);
                }
            }
        }
        const VfxInstanceData identity = IdentityVfxInstance();
        StoreVfxVisibilityCandidate(0U, valid ? boundsMinimum : 0.0F.xxx,
                                    valid ? boundsMaximum : 0.0F.xxx, identity, 4U, !valid, 0.0F);
        return;
    }

    const uint source = dispatchThreadId.x;
    if (source >= sourceCount || source >= VisibilityMetadata.z)
        return;
    const uint particleIndex = SourceIndices[source];
    const GpuParticle particle = SourceParticles[particleIndex];
    const bool alive = particle.PositionAge.w >= 0.0F && IsFiniteVfxFloat3(particle.PositionAge.xyz);
    if (VisibilityExecution.z == VfxRendererSprite)
    {
        const float extent = max(particle.SizeRotation.x, 0.0F) * 0.7071067811865475F;
        const VfxInstanceData identity = IdentityVfxInstance();
        const bool valid = alive && IsFiniteVfxFloat(extent);
        StoreVfxVisibilityCandidate(source, valid ? particle.PositionAge.xyz - extent.xxx : 0.0F.xxx,
                                    valid ? particle.PositionAge.xyz + extent.xxx : 0.0F.xxx, identity, 3U, !valid,
                                    0.0F);
        return;
    }
    if (VisibilityExecution.z == VfxRendererMesh)
    {
        const VfxInstanceData instance = SourceInstances[source];
        const float3 localMinimum = VisibilityBoundsMinimum.xyz;
        const float3 localMaximum = VisibilityBoundsMaximum.xyz;
        const float3 localCenter = (localMinimum + localMaximum) * 0.5F;
        const float3 localExtents = (localMaximum - localMinimum) * 0.5F;
        const float3 worldCenter = mul(instance.Model, float4(localCenter, 1.0F)).xyz;
        const float3 worldExtents =
            float3(dot(abs(instance.Model[0].xyz), localExtents),
                   dot(abs(instance.Model[1].xyz), localExtents),
                   dot(abs(instance.Model[2].xyz), localExtents));
        const float3 worldMinimum = worldCenter - worldExtents;
        const float3 worldMaximum = worldCenter + worldExtents;
        const bool valid = alive && IsFiniteVfxFloat3(localMinimum) && IsFiniteVfxFloat3(localMaximum) &&
                           all(localMinimum <= localMaximum) && IsFiniteVfxFloat3(localCenter) &&
                           IsFiniteVfxFloat3(localExtents) && IsFiniteVfxFloat3(worldCenter) &&
                           IsFiniteVfxFloat3(worldExtents) && IsFiniteVfxFloat3(worldMinimum) &&
                           IsFiniteVfxFloat3(worldMaximum) && all(worldMinimum <= worldMaximum);
        const VfxInstanceData identity = IdentityVfxInstance();
        StoreVfxVisibilityCandidate(source, valid ? worldMinimum : 0.0F.xxx,
                                    valid ? worldMaximum : 0.0F.xxx, identity, 2U, !valid,
                                    VisibilityBoundsMinimum.w);
    }
}

[numthreads(1, 1, 1)] void CSCompactVisibility(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint sourceCount = min(SourceIndirectArguments.Load(4), VisibilityExecution.y);
    const bool ribbonVisible =
        VisibilityExecution.z != VfxRendererRibbon || SourceVisibilityMask[VisibilityMetadata.y] != 0U;
    uint outputCount = 0U;
    [loop] for (uint source = 0U; source < sourceCount; ++source)
    {
        const bool visible = ribbonVisible &&
                             (VisibilityExecution.z == VfxRendererRibbon ||
                              (source < VisibilityMetadata.z &&
                               SourceVisibilityMask[VisibilityMetadata.y + source] != 0U));
        if (!visible)
            continue;
        OutputIndices[outputCount] = SourceIndices[source];
        if (VisibilityExecution.z != VfxRendererRibbon)
            OutputInstances[outputCount] = SourceInstances[source];
        ++outputCount;
    }
    OutputIndirectArguments.Store4(0, uint4(VisibilityMetadata.w, outputCount, 0U, 0U));
    OutputIndirectArguments.Store(16, 0U);
}
