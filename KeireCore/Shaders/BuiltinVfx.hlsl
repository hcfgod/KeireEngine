struct GpuParticle
{
    float4 PositionAge;
    float4 VelocityLifetime;
    float4 Tint;
    float4 SizeRotation;
    float4 AccelerationSizeEnd;
    float4 ColorStart;
    float4 ColorEnd;
    uint4 Identity;
};

RWStructuredBuffer<GpuParticle> Particles : register(u0, space1);
RWStructuredBuffer<uint> FreeIndices : register(u1, space1);
RWStructuredBuffer<uint> AliveIndices : register(u2, space1);
RWByteAddressBuffer Counters : register(u3, space1);
RWByteAddressBuffer IndirectArguments : register(u4, space1);

cbuffer VfxDispatch : register(b0, space2)
{
    uint ParticleCapacity;
    uint SpawnCount;
    float DeltaSeconds;
    uint RandomSeed;
    float4 EmitterPosition;
    float4 EmitterRotation;
    float4 ShapeExtentRadius;
    float4 VelocityMinimumLifetime;
    float4 VelocityMaximumLifetime;
    float4 AccelerationShape;
    float4 ColorStart;
    float4 ColorEnd;
    float4 SizeParameters;
    float4 PreviousEmitterPosition;
    float4 PreviousEmitterRotation;
    uint4 EmitterIdentity;
};

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

float Random01(inout uint state)
{
    state = Hash(state);
    return (state & 0x00ffffffU) * (1.0F / 16777216.0F);
}

float3 RotateByQuaternion(float3 value, float4 rotation)
{
    return value + 2.0F * cross(rotation.xyz, cross(rotation.xyz, value) + rotation.w * value);
}

bool PopFreeIndex(out uint particleIndex)
{
    for (;;)
    {
        const uint current = Counters.Load(0);
        if (current == 0)
        {
            particleIndex = 0;
            return false;
        }
        uint observed = 0;
        Counters.InterlockedCompareExchange(0, current, current - 1, observed);
        if (observed == current)
        {
            particleIndex = FreeIndices[current - 1];
            return true;
        }
    }
}

void PushFreeIndex(uint particleIndex)
{
    uint destination = 0;
    Counters.InterlockedAdd(0, 1, destination);
    if (destination < ParticleCapacity)
        FreeIndices[destination] = particleIndex;
}

void AppendAlive(uint particleIndex)
{
    uint destination = 0;
    Counters.InterlockedAdd(4, 1, destination);
    if (destination < ParticleCapacity)
        AliveIndices[destination] = particleIndex;
}

[numthreads(256, 1, 1)]
void CSInitialize(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index < ParticleCapacity)
    {
        GpuParticle particle = (GpuParticle)0;
        particle.PositionAge.w = -1.0F;
        Particles[index] = particle;
        FreeIndices[index] = ParticleCapacity - index - 1;
    }
    if (index == 0)
    {
        Counters.Store(0, ParticleCapacity);
        Counters.Store(4, 0);
        Counters.Store(8, 0);
        Counters.Store(12, 0);
        Counters.Store(16, 0);
        IndirectArguments.Store4(0, uint4(6, 0, 0, 0));
    }
}

[numthreads(1, 1, 1)]
void CSReset(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Counters.Store(4, 0);
    IndirectArguments.Store4(0, uint4(6, 0, 0, 0));
}

[numthreads(256, 1, 1)]
void CSKill(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= ParticleCapacity)
        return;
    GpuParticle particle = Particles[index];
    if (particle.PositionAge.w < 0.0F || any(particle.Identity.xy != EmitterIdentity.xy))
        return;

    particle.PositionAge.w = -1.0F;
    Particles[index] = particle;
    PushFreeIndex(index);
    uint ignored = 0;
    Counters.InterlockedAdd(12, 1, ignored);
}

[numthreads(256, 1, 1)]
void CSTransform(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= ParticleCapacity)
        return;
    GpuParticle particle = Particles[index];
    if (particle.PositionAge.w < 0.0F || any(particle.Identity.xy != EmitterIdentity.xy))
        return;

    const float4 inversePreviousRotation = float4(-PreviousEmitterRotation.xyz, PreviousEmitterRotation.w);
    const float3 localPosition =
        RotateByQuaternion(particle.PositionAge.xyz - PreviousEmitterPosition.xyz, inversePreviousRotation);
    particle.PositionAge.xyz = EmitterPosition.xyz + RotateByQuaternion(localPosition, EmitterRotation);
    particle.VelocityLifetime.xyz = RotateByQuaternion(
        RotateByQuaternion(particle.VelocityLifetime.xyz, inversePreviousRotation), EmitterRotation);
    particle.AccelerationSizeEnd.xyz = RotateByQuaternion(
        RotateByQuaternion(particle.AccelerationSizeEnd.xyz, inversePreviousRotation), EmitterRotation);
    Particles[index] = particle;
}

[numthreads(256, 1, 1)]
void CSSimulate(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= ParticleCapacity)
        return;
    GpuParticle particle = Particles[index];
    if (particle.PositionAge.w < 0.0F)
        return;

    particle.PositionAge.w += DeltaSeconds;
    if (particle.PositionAge.w >= particle.VelocityLifetime.w)
    {
        particle.PositionAge.w = -1.0F;
        Particles[index] = particle;
        PushFreeIndex(index);
        uint ignored = 0;
        Counters.InterlockedAdd(12, 1, ignored);
        return;
    }

    particle.VelocityLifetime.xyz += particle.AccelerationSizeEnd.xyz * DeltaSeconds;
    particle.PositionAge.xyz += particle.VelocityLifetime.xyz * DeltaSeconds;
    const float age = saturate(particle.PositionAge.w / max(particle.VelocityLifetime.w, 0.0001F));
    particle.Tint = lerp(particle.ColorStart, particle.ColorEnd, age);
    particle.SizeRotation.x = lerp(particle.SizeRotation.y, particle.AccelerationSizeEnd.w, age);
    Particles[index] = particle;
    AppendAlive(index);
}

[numthreads(256, 1, 1)]
void CSSpawn(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint spawnIndex = dispatchThreadId.x;
    if (spawnIndex >= SpawnCount)
        return;
    uint particleIndex = 0;
    if (!PopFreeIndex(particleIndex))
    {
        uint ignored = 0;
        Counters.InterlockedAdd(16, 1, ignored);
        return;
    }

    uint random = Hash(RandomSeed ^ spawnIndex ^ EmitterIdentity.z);
    float3 localPosition = 0.0F.xxx;
    const uint shape = asuint(AccelerationShape.w);
    if (shape == 1)
    {
        localPosition =
            (float3(Random01(random), Random01(random), Random01(random)) * 2.0F - 1.0F) * ShapeExtentRadius.xyz;
    }
    else if (shape == 2 || shape == 3)
    {
        const float3 direction =
            normalize(float3(Random01(random), Random01(random), Random01(random)) * 2.0F - 1.0F + 0.0001F);
        localPosition = direction * (pow(Random01(random), 1.0F / 3.0F) * ShapeExtentRadius.w);
    }
    const float3 localVelocity =
        lerp(VelocityMinimumLifetime.xyz, VelocityMaximumLifetime.xyz,
             float3(Random01(random), Random01(random), Random01(random)));
    const float lifetime =
        lerp(VelocityMinimumLifetime.w, VelocityMaximumLifetime.w, Random01(random));

    GpuParticle particle = (GpuParticle)0;
    particle.PositionAge = float4(EmitterPosition.xyz + RotateByQuaternion(localPosition, EmitterRotation), 0.0F);
    particle.VelocityLifetime =
        float4(RotateByQuaternion(localVelocity, EmitterRotation), max(lifetime, 0.0001F));
    particle.Tint = ColorStart;
    particle.SizeRotation =
        float4(max(SizeParameters.x, 0.0F), max(SizeParameters.x, 0.0F), 0.0F, 0.0F);
    const bool localSpace = asuint(SizeParameters.z) == 0;
    const float3 acceleration =
        localSpace ? RotateByQuaternion(AccelerationShape.xyz, EmitterRotation) : AccelerationShape.xyz;
    particle.AccelerationSizeEnd = float4(acceleration, max(SizeParameters.y, 0.0F));
    particle.ColorStart = ColorStart;
    particle.ColorEnd = ColorEnd;
    particle.Identity = EmitterIdentity;
    Particles[particleIndex] = particle;
    AppendAlive(particleIndex);
    uint ignored = 0;
    Counters.InterlockedAdd(8, 1, ignored);
}

[numthreads(1, 1, 1)]
void CSFinalize(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint aliveCount = min(Counters.Load(4), ParticleCapacity);
    IndirectArguments.Store4(0, uint4(6, aliveCount, 0, 0));
}

StructuredBuffer<GpuParticle> RenderParticles : register(t0, space0);
StructuredBuffer<uint> RenderAliveIndices : register(t1, space0);

cbuffer VfxCamera : register(b0, space1)
{
    float4x4 ViewProjection;
    float4 CameraRight;
    float4 CameraUp;
};

struct VfxVertexOutput
{
    float4 Tint : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float4 Position : SV_Position;
};

VfxVertexOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] = {
        float2(-0.5F, -0.5F), float2(0.5F, -0.5F), float2(0.5F, 0.5F),
        float2(-0.5F, -0.5F), float2(0.5F, 0.5F), float2(-0.5F, 0.5F)};
    const GpuParticle particle = RenderParticles[RenderAliveIndices[instanceId]];
    const float2 corner = corners[vertexId];
    const float3 world =
        particle.PositionAge.xyz +
        (CameraRight.xyz * corner.x + CameraUp.xyz * corner.y) * max(particle.SizeRotation.x, 0.0F);
    VfxVertexOutput output;
    output.Position = mul(ViewProjection, float4(world, 1.0F));
    output.Tint = particle.Tint;
    output.UV = corner + 0.5F.xx;
    return output;
}

float4 PSMain(VfxVertexOutput input) : SV_Target0
{
    const float2 centered = input.UV * 2.0F - 1.0F;
    const float alpha = saturate((1.0F - dot(centered, centered)) * 4.0F);
    return float4(input.Tint.rgb, input.Tint.a * alpha);
}
