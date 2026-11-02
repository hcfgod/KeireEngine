#include "BuiltinVfxCompute.hlsl"

#define KEIRE_CLOUD_NOISE_TEXTURE t1
#define KEIRE_CLOUD_NOISE_SAMPLER s1
#define KEIRE_PLANET_VOLUME_SET 1
#include "CloudVolumeView.hlsli"

// Built-in particle pipelines use straight-alpha blending for every fallback renderer.
float4 ComposeVfxCloudVolume(float4 surface, float4 position, CloudVolumeView view)
{
    const CloudVolumeViewTransport volume = EvaluateCloudVolumeView(
        position.xy * view.CameraControls.zw, position.z, view, true);
    return float4(VolumeStraightSurfaceSource(surface.r, volume.Radiance.r, volume.Transmission.r),
                  VolumeStraightSurfaceSource(surface.g, volume.Radiance.g, volume.Transmission.g),
                  VolumeStraightSurfaceSource(surface.b, volume.Radiance.b, volume.Transmission.b), surface.a);
}

StructuredBuffer<GpuParticle> RenderParticles : register(t0, space0);
StructuredBuffer<uint> RenderAliveIndices : register(t1, space0);

cbuffer VfxCamera : register(b0, space1)
{
    float4x4 ViewProjection;
    float4 CameraRight;
    float4 CameraUp;
};

cbuffer VfxMaterial : register(b0, space3)
{
    float4 RenderParameters;
    float4 AmbientColor;
    float4 DirectionalColor;
    float4 DirectionalDirection;
    float4 MaterialTint;
    float4 SurfaceParameters;
    CloudVolumeView VfxCloudVolume;
};

Texture2D<float4> RenderTexture : register(t0, space2);
SamplerState RenderSampler : register(s0, space2);

struct VfxVertexOutput
{
    float4 Tint : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float4 Position : SV_Position;
};

VfxVertexOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] = {float2(-0.5F, -0.5F), float2(0.5F, -0.5F), float2(0.5F, 0.5F),
                                      float2(-0.5F, -0.5F), float2(0.5F, 0.5F),  float2(-0.5F, 0.5F)};
    const GpuParticle particle = RenderParticles[RenderAliveIndices[instanceId]];
    const float2 corner = corners[vertexId];
    const float angle = particle.SizeRotation.w * 0.01745329251994329577F;
    float sine = 0.0F;
    float cosine = 0.0F;
    sincos(angle, sine, cosine);
    const float2 rotatedCorner = float2(corner.x * cosine - corner.y * sine, corner.x * sine + corner.y * cosine);
    const float3 world =
        particle.PositionAge.xyz +
        (CameraRight.xyz * rotatedCorner.x + CameraUp.xyz * rotatedCorner.y) * max(particle.SizeRotation.x, 0.0F);
    VfxVertexOutput output;
    output.Position = mul(ViewProjection, float4(world, 1.0F));
    output.Tint = particle.Tint;
    output.UV = corner + 0.5F.xx;
    return output;
}

VfxVertexOutput VSRibbon(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 topology[6] = {float2(0.0F, -0.5F), float2(0.0F, 0.5F), float2(1.0F, 0.5F),
                                       float2(0.0F, -0.5F), float2(1.0F, 0.5F), float2(1.0F, -0.5F)};
    const GpuParticle particle = RenderParticles[RenderAliveIndices[instanceId]];
    float3 start = particle.PositionAge.xyz;
    bool connected = false;
    if (particle.Identity.z > 0U)
    {
        const GpuParticle previous = RenderParticles[particle.Identity.z - 1U];
        connected = previous.PositionAge.w >= 0.0F && all(previous.Identity.xy == particle.Identity.xy) &&
                    Equal64(Add64(previous.SequenceIdentity.xy, uint2(1U, 0U)), particle.SequenceIdentity.xy);
        if (connected)
            start = previous.PositionAge.xyz;
    }
    const float3 end = particle.PositionAge.xyz;
    const float3 segment = end - start;
    const float segmentLength = length(segment);
    const float3 direction = segmentLength > 0.000001F ? segment / segmentLength : CameraUp.xyz;
    const float3 cameraForward = cross(CameraRight.xyz, CameraUp.xyz);
    const float3 candidateSide = cross(direction, cameraForward);
    const float candidateLengthSquared = dot(candidateSide, candidateSide);
    const float3 side = candidateLengthSquared > 0.000001F
                            ? candidateSide * rsqrt(candidateLengthSquared)
                            : CameraRight.xyz;
    const float2 coordinate = topology[vertexId];
    const float3 world = lerp(start, end, coordinate.x) + side * coordinate.y * max(particle.SizeRotation.x, 0.0F);
    VfxVertexOutput output;
    output.Position = mul(ViewProjection, float4(world, 1.0F));
    output.Tint = particle.Tint;
    output.Tint.a *= connected && segmentLength > 0.000001F ? 1.0F : 0.0F;
    output.UV = float2(coordinate.x, coordinate.y + 0.5F);
    return output;
}

float4 PSMain(VfxVertexOutput input) : SV_Target0
{
    float4 surface = input.Tint * MaterialTint;
    if (RenderParameters.x > 0.5F)
        surface *= RenderTexture.Sample(RenderSampler, input.UV);
    const float2 centered = input.UV * 2.0F - 1.0F;
    if (RenderParameters.y > 0.5F)
    {
        const float radiusSquared = dot(centered, centered);
        const float density = sqrt(saturate(1.0F - radiusSquared));
        surface.rgb *= density;
        surface.a = 1.0F - exp(-density * 4.0F * max(surface.a, 0.0F));
    }
    else if (RenderParameters.z > 0.5F)
    {
        const float alpha = saturate((1.0F - abs(centered.y)) * 4.0F);
        surface.a *= alpha;
    }
    else if (RenderParameters.x <= 0.5F)
    {
        const float alpha = saturate((1.0F - dot(centered, centered)) * 4.0F);
        surface.a *= alpha;
    }
    if (SurfaceParameters.z > 0.5F)
    {
        if (SurfaceParameters.y > 0.5F && SurfaceParameters.y < 1.5F)
            clip(surface.a - SurfaceParameters.x);
        else if (SurfaceParameters.y < 0.5F)
            surface.a = 1.0F;
    }
    return ComposeVfxCloudVolume(surface, input.Position, VfxCloudVolume);
}

struct VfxMeshVertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float4 Color : TEXCOORD3;
    float4 Tangent : TEXCOORD4;
};

struct VfxMeshVertexOutput
{
    float4 Tint : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Position : SV_Position;
};


VfxMeshVertexOutput VSMesh(VfxMeshVertexInput input, uint instanceId : SV_InstanceID)
{
    const GpuParticle particle = RenderParticles[RenderAliveIndices[instanceId]];
    const float4 rotation = QuaternionFromEulerDegrees(particle.SizeRotation.yzw);
    const float size = max(particle.SizeRotation.x, 0.0F);
    const float3 worldPosition = particle.PositionAge.xyz + RotateByQuaternion(input.Position, rotation) * size;
    VfxMeshVertexOutput output;
    output.Position = mul(ViewProjection, float4(worldPosition, 1.0F));
    output.Normal = normalize(RotateByQuaternion(input.Normal, rotation));
    output.Tint = input.Color * particle.Tint;
    return output;
}

float4 PSMesh(VfxMeshVertexOutput input) : SV_Target0
{
    const float diffuse = DirectionalDirection.w > 0.5F
                              ? saturate(dot(normalize(input.Normal), normalize(-DirectionalDirection.xyz)))
                              : 0.0F;
    const float3 lighting = AmbientColor.rgb + DirectionalColor.rgb * DirectionalColor.a * diffuse;
    const float4 tint = input.Tint * MaterialTint;
    return ComposeVfxCloudVolume(float4(tint.rgb * max(lighting, 0.0001F.xxx), tint.a),
                                input.Position, VfxCloudVolume);
}

struct VfxCpuVertexInput
{
    float3 Position : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float3 Data : TEXCOORD2;
};

cbuffer VfxCpuCamera : register(b0, space1)
{
    float4x4 CpuViewProjection;
};

cbuffer VfxCpuMaterial : register(b0, space3)
{
    float4 CpuTint;
    float4 CpuSurfaceParameters;
    CloudVolumeView CpuCloudVolume;
};

Texture2D<float4> CpuRenderTexture : register(t0, space2);
SamplerState CpuRenderSampler : register(s0, space2);

struct VfxCpuVertexOutput
{
    float4 Tint : TEXCOORD0;
    float3 Data : TEXCOORD1;
    float4 Position : SV_Position;
};

VfxCpuVertexOutput VSCpu(VfxCpuVertexInput input)
{
    VfxCpuVertexOutput output;
    output.Position = mul(CpuViewProjection, float4(input.Position, 1.0F));
    output.Tint = input.Color;
    output.Data = input.Data;
    return output;
}

float4 PSCpu(VfxCpuVertexOutput input) : SV_Target0
{
    float4 surface = input.Tint * CpuTint;
    if (CpuSurfaceParameters.x > 0.5F)
        surface *= CpuRenderTexture.Sample(CpuRenderSampler, input.Data.xy);
    const float2 centered = input.Data.xy * 2.0F - 1.0F;
    if (input.Data.z > 1.5F)
    {
        const float density = sqrt(saturate(1.0F - dot(centered, centered)));
        surface.rgb *= density;
        surface.a = 1.0F - exp(-density * 4.0F * max(surface.a, 0.0F));
    }
    else if (input.Data.z > 0.5F)
    {
        const float alpha = saturate((1.0F - abs(centered.y)) * 4.0F);
        surface.a *= alpha;
    }
    else if (CpuSurfaceParameters.x <= 0.5F)
    {
        const float alpha = saturate((1.0F - dot(centered, centered)) * 4.0F);
        surface.a *= alpha;
    }
    if (CpuSurfaceParameters.w > 0.5F)
    {
        if (CpuSurfaceParameters.z > 0.5F && CpuSurfaceParameters.z < 1.5F)
            clip(surface.a - CpuSurfaceParameters.y);
        else if (CpuSurfaceParameters.z < 0.5F)
            surface.a = 1.0F;
    }
    return ComposeVfxCloudVolume(surface, input.Position, CpuCloudVolume);
}
