#include <string_view>

namespace Keire::Detail
{
    std::string_view ShaderGraphVfxVertexHlsl() noexcept
    {
        return R"HLSL(
#if defined(KEIRE_PASS_VFX_BILLBOARD) || defined(KEIRE_PASS_VFX_RIBBON)
struct VfxParticle
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
StructuredBuffer<VfxParticle> VfxParticles : register(t0, space0);
StructuredBuffer<uint> VfxIndices : register(t1, space0);

VertexInput VfxInput(uint vertexId, uint instanceId)
{
    const VfxParticle particle = VfxParticles[VfxIndices[instanceId]];
    const float3 right = View[0].xyz;
    const float3 up = View[1].xyz;
    VertexInput input = (VertexInput)0;
    input.Color = particle.Tint;
    input.Normal = normalize(cross(right, up));
    input.Tangent = float4(right, 1.0F);
#if defined(KEIRE_PASS_VFX_RIBBON)
    static const float2 corners[6] = {float2(0,-0.5), float2(0,0.5), float2(1,0.5),
                                      float2(0,-0.5), float2(1,0.5), float2(1,-0.5)};
    float3 start = particle.PositionAge.xyz;
    bool connected = false;
    if (particle.Identity.z > 0U)
    {
        const VfxParticle previous = VfxParticles[particle.Identity.z - 1U];
        const uint nextLow = previous.SequenceIdentity.x + 1U;
        const uint nextHigh = previous.SequenceIdentity.y + (nextLow == 0U ? 1U : 0U);
        connected = previous.PositionAge.w >= 0.0F && all(previous.Identity.xy == particle.Identity.xy) &&
                    all(uint2(nextLow, nextHigh) == particle.SequenceIdentity.xy);
        if (connected)
            start = previous.PositionAge.xyz;
    }
    const float3 segment = particle.PositionAge.xyz - start;
    const float3 candidate = cross(segment, input.Normal);
    const float magnitude = dot(candidate, candidate);
    const float3 side = magnitude > 0.000001F ? candidate * rsqrt(magnitude) : right;
    const float2 corner = corners[vertexId];
    input.Position = lerp(start, particle.PositionAge.xyz, corner.x) +
                     side * corner.y * max(particle.SizeRotation.x, 0.0F);
    input.Color.a *= connected && dot(segment, segment) > 0.000000000001F ? 1.0F : 0.0F;
    input.UV0 = float2(corner.x, corner.y + 0.5F);
#else
    static const float2 corners[6] = {float2(-0.5,-0.5), float2(0.5,-0.5), float2(0.5,0.5),
                                      float2(-0.5,-0.5), float2(0.5,0.5), float2(-0.5,0.5)};
    const float2 corner = corners[vertexId];
    float sine, cosine;
    sincos(particle.SizeRotation.w * 0.01745329251994329577F, sine, cosine);
    const float2 rotated = float2(corner.x * cosine - corner.y * sine,
                                  corner.x * sine + corner.y * cosine);
    input.Position = particle.PositionAge.xyz +
                     (right * rotated.x + up * rotated.y) * max(particle.SizeRotation.x, 0.0F);
    input.UV0 = corner + 0.5F;
#endif
    input.UV1 = input.UV0;
    return input;
}
#elif defined(KEIRE_PASS_VFX_CPU)
struct VfxCpuInput
{
    float4 Position : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float4 UV : TEXCOORD2;
};
VertexInput VfxInput(VfxCpuInput vertex)
{
    VertexInput input = (VertexInput)0;
    input.Position = vertex.Position.xyz;
    input.Color = vertex.Color;
    input.UV0 = vertex.UV.xy;
    input.UV1 = vertex.UV.xy;
    input.Normal = normalize(cross(View[0].xyz, View[1].xyz));
    input.Tangent = float4(View[0].xyz, 1.0F);
    return input;
}
#endif
)HLSL";
    }
} // namespace Keire::Detail
