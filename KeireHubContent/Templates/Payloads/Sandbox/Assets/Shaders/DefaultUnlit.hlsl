static const float Pi = 3.14159265359F;

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV0 : TEXCOORD2;
    float4 Color : TEXCOORD3;
    float4 Tangent : TEXCOORD4;
    float2 UV1 : TEXCOORD5;
};

struct VertexOutput
{
    float3 Normal : TEXCOORD0;
    float3 Tangent : TEXCOORD1;
    float3 Bitangent : TEXCOORD2;
    float3 ViewDirection : TEXCOORD3;
    float2 UV0 : TEXCOORD4;
    float4 Color : TEXCOORD5;
    float3 WorldPosition : TEXCOORD6;
    float ViewDepth : TEXCOORD7;
    float2 UV1 : TEXCOORD8;
    float4 Position : SV_Position;
};

cbuffer ObjectData : register(b0, space1)
{
    float4x4 Model;
    float4x4 View;
    float4x4 Projection;
    float4x4 NormalMatrix;
};

struct InstanceData
{
    float4x4 Model;
    float4x4 NormalMatrix;
    float4 Tint;
};

StructuredBuffer<InstanceData> Instances : register(t0, space0);

struct LocalLightData
{
    float4 PositionRange;
    float4 DirectionOuter;
    float4 ColorIntensity;
    float4 Parameters;
};

cbuffer SceneData : register(b0, space3)
{
    float4 AmbientColorIntensity;
    float4 DirectionalColorIntensity;
    float4 DirectionalDirectionExposure;
    float4 SurfaceParameters;
    float4 LocalLightCounts;
    LocalLightData LocalLights[62];
};

cbuffer MaterialData : register(b1, space3)
{
    float4 Tint;
    float4 MetallicFactor;
    float4 RoughnessFactor;
    float4 NormalScale;
    float4 OcclusionStrength;
    float4 EmissiveFactor;
};

cbuffer ShadowData : register(b2, space3)
{
    float4 DirectionalShadowParameters;
    float4 DirectionalCascadeSplits;
    float4x4 DirectionalShadowMatrices[4];
    float4x4 LocalShadowMatrices[20];
    float4 LocalShadowParameters[62];
    float4 LocalShadowSampleBounds[20];
};

struct SpatialReflectionProbeData
{
    float4x4 WorldToLocal;
    float4x4 LocalToWorld;
    float4 ExtentsWeight;
    float4 Parameters;
};

static const uint InvalidSpatialSelectionIndex = 0xffffffffU;
static const uint SpatialSelectionHasLightProbe = 1U << 0U;
static const uint SpatialSelectionHasReflectionProbe0 = 1U << 1U;
static const uint SpatialSelectionHasReflectionProbe1 = 1U << 2U;

struct SpatialSelectionData
{
    float4 ProbeIrradiance[9];
    SpatialReflectionProbeData ReflectionProbes[2];
    uint4 Metadata;
};

cbuffer EnvironmentData : register(b3, space3)
{
    float4 DiffuseIrradiance[9];
    float4 EnvironmentParameters;
    float4 EnvironmentEncoding;
    float4 LightmapScaleOffset;
    float4 LightmapParameters;
    float4 ShadowMaskParameters;
    float4 ProbeIrradiance[9];
    SpatialReflectionProbeData ReflectionProbes[2];
    float4 CookieTransforms[8];
    float4 CookieRotations[2];
    float4 DirectionalCookieAndContact;
    float4x4 SpatialViewProjection;
    uint4 SpatialSelection;
};

Texture2D MainTexture : register(t0, space2);
SamplerState MainSampler : register(s0, space2);
Texture2D NormalTexture : register(t1, space2);
SamplerState NormalSampler : register(s1, space2);
Texture2D MetallicRoughnessTexture : register(t2, space2);
SamplerState MetallicRoughnessSampler : register(s2, space2);
Texture2D OcclusionTexture : register(t3, space2);
SamplerState OcclusionSampler : register(s3, space2);
Texture2D EmissiveTexture : register(t4, space2);
SamplerState EmissiveSampler : register(s4, space2);
Texture2D MetallicTexture : register(t5, space2);
SamplerState MetallicSampler : register(s5, space2);
Texture2D RoughnessTexture : register(t6, space2);
SamplerState RoughnessSampler : register(s6, space2);
Texture2DArray<float> DirectionalShadowTexture : register(t7, space2);
SamplerState DirectionalShadowSampler : register(s7, space2);
Texture2DArray<float> LocalShadowTexture : register(t8, space2);
SamplerState LocalShadowSampler : register(s8, space2);
Texture2D EnvironmentTexture : register(t9, space2);
SamplerState EnvironmentSampler : register(s9, space2);
Texture2D BrdfIntegrationLut : register(t10, space2);
SamplerState BrdfIntegrationSampler : register(s10, space2);
Texture2DArray BakedLightmapTexture : register(t11, space2);
SamplerState BakedLightmapSampler : register(s11, space2);
Texture2DArray BakedDirectionalityTexture : register(t12, space2);
SamplerState BakedDirectionalitySampler : register(s12, space2);
Texture2DArray BakedShadowMaskTexture : register(t13, space2);
SamplerState BakedShadowMaskSampler : register(s13, space2);
TextureCubeArray BakedReflectionTexture : register(t14, space2);
SamplerState BakedReflectionSampler : register(s14, space2);
Texture2D LightCookieAtlas : register(t15, space2);
SamplerState LightCookieAtlasSampler : register(s15, space2);
StructuredBuffer<LocalLightData> ForwardPlusLights : register(t16, space2);
StructuredBuffer<uint4> ForwardPlusTiles : register(t17, space2);
StructuredBuffer<uint4> ForwardPlusLightIndices : register(t18, space2);
StructuredBuffer<SpatialSelectionData> SpatialSelections : register(t19, space2);

uint ForwardPlusLightIndex(const uint index)
{
    const uint4 indices = ForwardPlusLightIndices[index >> 2U];
    return indices[index & 3U];
}

float3 SafeNormal(const float3 value, const float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-12F && all(isfinite(value)) ? value * rsqrt(lengthSquared) : fallback;
}

float3 OrthogonalTangent(const float3 normal)
{
    const float3 axis = abs(normal.z) < 0.999F ? float3(0.0F, 0.0F, 1.0F) : float3(0.0F, 1.0F, 0.0F);
    return SafeNormal(cross(axis, normal), float3(1.0F, 0.0F, 0.0F));
}

VertexOutput VSMain(VertexInput input, const uint instanceId : SV_InstanceID)
{
    VertexOutput output;
    const InstanceData instance = Instances[instanceId];
    const float4 worldPosition = mul(instance.Model, float4(input.Position, 1.0F));
    const float4 viewPosition = mul(View, worldPosition);
    output.Position = mul(Projection, viewPosition);
    output.Normal = SafeNormal(mul((float3x3)instance.NormalMatrix, input.Normal), float3(0.0F, 0.0F, 1.0F));
    float3 tangent = mul((float3x3)instance.Model, input.Tangent.xyz);
    tangent -= output.Normal * dot(output.Normal, tangent);
    output.Tangent = SafeNormal(tangent, OrthogonalTangent(output.Normal));
    const float modelHandedness = determinant((float3x3)instance.Model) < 0.0F ? -1.0F : 1.0F;
    const float tangentHandedness = abs(input.Tangent.w) > 0.0001F ? input.Tangent.w : 1.0F;
    output.Bitangent = SafeNormal(cross(output.Normal, output.Tangent) * tangentHandedness * modelHandedness,
                                  cross(output.Normal, OrthogonalTangent(output.Normal)));
    output.ViewDirection = normalize(mul(-viewPosition.xyz, (float3x3)View));
    output.UV0 = input.UV0;
    output.Color = input.Color * instance.Tint;
    output.WorldPosition = worldPosition.xyz;
    output.ViewDepth = viewPosition.z;
    output.UV1 = input.UV1;
    return output;
}

float SampleShadowPcf(Texture2DArray<float> textureValue, SamplerState samplerValue, const float2 uv,
                      const float layer, const float depth, const float inverseResolution, const bool soft,
                      const float4 sampleBounds, const bool clampSamples)
{
    if (any(uv < sampleBounds.xy) || any(uv > sampleBounds.zw) || depth <= 0.0F || depth >= 1.0F)
        return 1.0F;
    if (!soft)
        return depth <= textureValue.SampleLevel(samplerValue, float3(uv, layer), 0.0F) ? 1.0F : 0.0F;
    float visibility = 0.0F;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 unclampedUv = uv + float2(x, y) * inverseResolution;
            if (!clampSamples &&
                (any(unclampedUv < sampleBounds.xy) || any(unclampedUv > sampleBounds.zw)))
            {
                visibility += 1.0F;
            }
            else
            {
                const float2 sampleUv = clamp(unclampedUv, sampleBounds.xy, sampleBounds.zw);
                const float storedDepth =
                    textureValue.SampleLevel(samplerValue, float3(sampleUv, layer), 0.0F);
                visibility += depth <= storedDepth ? 1.0F : 0.0F;
            }
        }
    }
    return visibility / 9.0F;
}

float EvaluateDirectionalShadow(const float3 worldPosition, const float viewDepth)
{
    const float cascadeCount = abs(DirectionalShadowParameters.x);
    if (SurfaceParameters.z < 0.5F || cascadeCount < 0.5F)
        return 1.0F;
    uint cascade = 0U;
    cascade += viewDepth > DirectionalCascadeSplits.x;
    cascade += viewDepth > DirectionalCascadeSplits.y;
    cascade += viewDepth > DirectionalCascadeSplits.z;
    cascade = min(cascade, (uint)cascadeCount - 1U);
    const float4 clip = mul(DirectionalShadowMatrices[cascade], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    const float visibility = SampleShadowPcf(DirectionalShadowTexture, DirectionalShadowSampler, uv, cascade,
                                             projected.z - DirectionalShadowParameters.z,
                                             DirectionalShadowParameters.w, DirectionalShadowParameters.x > 0.0F,
                                             float4(0.0F, 0.0F, 1.0F, 1.0F), false);
    return lerp(1.0F, visibility, saturate(DirectionalShadowParameters.y));
}

float2 PointShadowCoordinates(const float3 direction, out uint face, out float majorDistance)
{
    const float3 absoluteDirection = abs(direction);
    float2 projected;
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        majorDistance = absoluteDirection.x;
        if (direction.x >= 0.0F)
        {
            face = 0U;
            projected = float2(-direction.z, direction.y) / majorDistance;
        }
        else
        {
            face = 1U;
            projected = float2(direction.z, direction.y) / majorDistance;
        }
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        majorDistance = absoluteDirection.y;
        if (direction.y >= 0.0F)
        {
            face = 2U;
            projected = float2(direction.x, -direction.z) / majorDistance;
        }
        else
        {
            face = 3U;
            projected = float2(direction.x, direction.z) / majorDistance;
        }
    }
    else
    {
        majorDistance = absoluteDirection.z;
        if (direction.z >= 0.0F)
        {
            face = 4U;
            projected = float2(direction.x, direction.y) / majorDistance;
        }
        else
        {
            face = 5U;
            projected = float2(-direction.x, direction.y) / majorDistance;
        }
    }
    return float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
}

float EvaluateLocalShadow(const uint lightIndex, const float3 worldPosition)
{
    if (SurfaceParameters.z < 0.5F || LocalShadowParameters[lightIndex].x < 0.0F)
        return 1.0F;
    const bool spot = LocalLights[lightIndex].Parameters.y > 0.5F;
    uint matrixIndex = (uint)LocalShadowParameters[lightIndex].x;
    if (!spot)
    {
        const float3 fromLight = worldPosition - LocalLights[lightIndex].PositionRange.xyz;
        uint face = 0U;
        float majorDistance = 0.0F;
        PointShadowCoordinates(fromLight, face, majorDistance);
        matrixIndex += face;
    }
    const float4 clip = mul(LocalShadowMatrices[matrixIndex], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    const float visibility = SampleShadowPcf(LocalShadowTexture, LocalShadowSampler, uv, 0.0F,
                                             projected.z - LocalShadowParameters[lightIndex].w, 1.0F / 4096.0F,
                                             LocalShadowParameters[lightIndex].z > 0.5F,
                                             LocalShadowSampleBounds[matrixIndex], true);
    return lerp(1.0F, visibility, saturate(LocalShadowParameters[lightIndex].y));
}

float DistributionGgx(const float3 normal, const float3 halfway, const float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float normalHalfway = saturate(dot(normal, halfway));
    const float denominator = normalHalfway * normalHalfway * (alphaSquared - 1.0F) + 1.0F;
    return alphaSquared / max(Pi * denominator * denominator, 0.0001F);
}

float GeometrySchlickGgx(const float normalDirection, const float roughness)
{
    const float radius = roughness + 1.0F;
    const float factor = radius * radius / 8.0F;
    return normalDirection / max(normalDirection * (1.0F - factor) + factor, 0.0001F);
}

float3 FresnelSchlick(const float directionHalfway, const float3 baseReflectance)
{
    return baseReflectance + (1.0F - baseReflectance) * pow(1.0F - directionHalfway, 5.0F);
}

float3 FresnelSchlickRoughness(const float normalView, const float3 baseReflectance, const float roughness)
{
    return baseReflectance + (max((1.0F - roughness).xxx, baseReflectance) - baseReflectance) *
                                 pow(1.0F - normalView, 5.0F);
}

float3 RotateEnvironmentDirection(float3 direction)
{
    const float rotation = radians(EnvironmentParameters.x);
    const float sineRotation = sin(rotation);
    const float cosineRotation = cos(rotation);
    direction.xz = float2(direction.x * cosineRotation - direction.z * sineRotation,
                          direction.x * sineRotation + direction.z * cosineRotation);
    return direction;
}

float2 CubemapAtlasUv(float3 direction, const int layout)
{
    const float3 absoluteDirection = abs(direction);
    int face = 0;
    float2 local;
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        const float inverse = 1.0F / absoluteDirection.x;
        if (direction.x >= 0.0F)
        {
            face = 0;
            local = float2(-direction.z, -direction.y) * inverse;
        }
        else
        {
            face = 1;
            local = float2(direction.z, -direction.y) * inverse;
        }
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        const float inverse = 1.0F / absoluteDirection.y;
        if (direction.y >= 0.0F)
        {
            face = 2;
            local = float2(direction.x, direction.z) * inverse;
        }
        else
        {
            face = 3;
            local = float2(direction.x, -direction.z) * inverse;
        }
    }
    else
    {
        const float inverse = 1.0F / absoluteDirection.z;
        if (direction.z >= 0.0F)
        {
            face = 4;
            local = float2(direction.x, -direction.y) * inverse;
        }
        else
        {
            face = 5;
            local = float2(-direction.x, -direction.y) * inverse;
        }
    }
    local = local * 0.5F + 0.5F;
    uint textureWidth;
    uint textureHeight;
    EnvironmentTexture.GetDimensions(textureWidth, textureHeight);
    const float2 grid = layout == 4 ? float2(6.0F, 1.0F)
                        : layout == 5 ? float2(1.0F, 6.0F)
                        : layout == 2 ? float2(4.0F, 3.0F)
                                      : float2(3.0F, 4.0F);
    const float2 cellPixels = max(float2(textureWidth, textureHeight) / grid, 1.0F);
    const float2 inset = 0.5F / cellPixels;
    local = clamp(local, inset, 1.0F - inset);
    if (layout == 4)
        return float2((face + local.x) / 6.0F, local.y);
    if (layout == 5)
        return float2(local.x, (face + local.y) / 6.0F);
    const int2 horizontalCells[6] = {int2(2, 1), int2(0, 1), int2(1, 0), int2(1, 2), int2(1, 1), int2(3, 1)};
    const int2 verticalCells[6] = {int2(2, 1), int2(0, 1), int2(1, 0), int2(1, 2), int2(1, 1), int2(1, 3)};
    if (layout == 2)
        return (float2(horizontalCells[face]) + local) / float2(4.0F, 3.0F);
    return (float2(verticalCells[face]) + local) / float2(3.0F, 4.0F);
}

float3 DecodeRgbe(const float4 sampleValue)
{
    const float exponent = sampleValue.a * 255.0F - 136.0F;
    return sampleValue.rgb * (255.0F * exp2(exponent));
}

float3 SampleEnvironment(float3 direction, const float level)
{
    direction = RotateEnvironmentDirection(direction);
    const int encoding = (int)EnvironmentEncoding.x;
    const int layout = encoding & 15;
    const float2 uv = layout <= 1
                          ? float2(0.5F + atan2(direction.x, direction.z) / (2.0F * Pi),
                                   0.5F - asin(clamp(direction.y, -1.0F, 1.0F)) / Pi)
                          : CubemapAtlasUv(direction, layout);
    const float4 sampleValue = EnvironmentTexture.SampleLevel(EnvironmentSampler, uv, level);
    return encoding >= 16 ? DecodeRgbe(sampleValue) : sampleValue.rgb;
}

float3 EvaluateDiffuseEnvironment(float3 normal)
{
    normal = RotateEnvironmentDirection(normalize(normal));
    const float x = normal.x;
    const float y = normal.y;
    const float z = normal.z;
    return max(DiffuseIrradiance[0].rgb * 0.282095F + DiffuseIrradiance[1].rgb * (0.488603F * y) +
                   DiffuseIrradiance[2].rgb * (0.488603F * z) + DiffuseIrradiance[3].rgb * (0.488603F * x) +
                   DiffuseIrradiance[4].rgb * (1.092548F * x * y) +
                   DiffuseIrradiance[5].rgb * (1.092548F * y * z) +
                   DiffuseIrradiance[6].rgb * (0.315392F * (3.0F * y * y - 1.0F)) +
                   DiffuseIrradiance[7].rgb * (1.092548F * x * z) +
                   DiffuseIrradiance[8].rgb * (0.546274F * (z * z - x * x)),
               0.0F.xxx);
}

float2 BakedUv(const float2 uv1)
{
    return uv1 * LightmapScaleOffset.xy + LightmapScaleOffset.zw;
}

float SampleBakedShadowMask(const float encodedChannel, const float2 uv1)
{
    if (encodedChannel < 0.5F || ((uint)round(LightmapParameters.z) & 1U) == 0U)
        return 1.0F;
    const uint channel = min((uint)encodedChannel - 1U, 7U);
    const float layer = LightmapParameters.y + (channel >= 4U ? ShadowMaskParameters.x : 0.0F);
    const float4 mask = BakedShadowMaskTexture.SampleLevel(
        BakedShadowMaskSampler, float3(BakedUv(uv1), layer), 0.0F);
    return mask[channel & 3U];
}

float3 EvaluateProbeIrradiance(const float3 normal, const float4 coefficients[9])
{
    const float x = normal.x;
    const float y = normal.y;
    const float z = normal.z;
    return max(coefficients[0].rgb * 0.282095F + coefficients[1].rgb * (0.488603F * y) +
                   coefficients[2].rgb * (0.488603F * z) + coefficients[3].rgb * (0.488603F * x) +
                   coefficients[4].rgb * (1.092548F * x * y) + coefficients[5].rgb * (1.092548F * y * z) +
                   coefficients[6].rgb * (0.315392F * (3.0F * z * z - 1.0F)) +
                   coefficients[7].rgb * (1.092548F * x * z) +
                   coefficients[8].rgb * (0.546274F * (x * x - y * y)),
               0.0F.xxx);
}

float3 EvaluateSpatialDiffuse(const float3 normal, const float2 uv1)
{
    const uint flags = (uint)round(LightmapParameters.z);
    if ((flags & 1U) != 0U)
    {
        const float4 encoded = BakedLightmapTexture.SampleLevel(
            BakedLightmapSampler, float3(BakedUv(uv1), LightmapParameters.x), 0.0F);
        const float3 radiance = DecodeRgbe(encoded);
        const float4 directionality = BakedDirectionalityTexture.SampleLevel(
            BakedDirectionalitySampler, float3(BakedUv(uv1), LightmapParameters.x), 0.0F);
        const float3 dominantDirection = SafeNormal(directionality.xyz * 2.0F - 1.0F, normal);
        return radiance * lerp(1.0F, saturate(dot(normal, dominantDirection)) * 2.0F,
                               saturate(directionality.w * 0.5F));
    }
    if (SpatialSelection.x == InvalidSpatialSelectionIndex)
        return (flags & 2U) != 0U ? EvaluateProbeIrradiance(normal, ProbeIrradiance) : 0.0F.xxx;
    const SpatialSelectionData selection = SpatialSelections[SpatialSelection.x];
    return (selection.Metadata.x & SpatialSelectionHasLightProbe) != 0U
               ? EvaluateProbeIrradiance(normal, selection.ProbeIrradiance)
               : 0.0F.xxx;
}

SpatialReflectionProbeData SelectedReflectionProbe(const uint probeIndex, out bool enabled)
{
    if (SpatialSelection.x == InvalidSpatialSelectionIndex)
    {
        enabled = ReflectionProbes[probeIndex].ExtentsWeight.w > 0.0F;
        return ReflectionProbes[probeIndex];
    }
    const SpatialSelectionData selection = SpatialSelections[SpatialSelection.x];
    const uint flag = probeIndex == 0U ? SpatialSelectionHasReflectionProbe0 : SpatialSelectionHasReflectionProbe1;
    enabled = (selection.Metadata.x & flag) != 0U;
    return selection.ReflectionProbes[probeIndex];
}

float3 BoxProjectedProbeDirection(const SpatialReflectionProbeData probe, const float3 worldPosition,
                                  const float3 worldDirection)
{
    if (probe.Parameters.z < 0.5F)
        return worldDirection;
    const float3 localPosition = mul(probe.WorldToLocal, float4(worldPosition, 1.0F)).xyz;
    const float3 localDirection = mul((float3x3)probe.WorldToLocal, worldDirection);
    const float3 safeDirection =
        float3(abs(localDirection.x) > 1.0e-6F ? localDirection.x : 1.0e-6F,
               abs(localDirection.y) > 1.0e-6F ? localDirection.y : 1.0e-6F,
               abs(localDirection.z) > 1.0e-6F ? localDirection.z : 1.0e-6F);
    const float3 first = (-probe.ExtentsWeight.xyz - localPosition) / safeDirection;
    const float3 second = (probe.ExtentsWeight.xyz - localPosition) / safeDirection;
    const float3 intersections = max(first, second);
    const float distance = min(intersections.x, min(intersections.y, intersections.z));
    const float3 localHit = localPosition + localDirection * max(distance, 0.0F);
    const float3 worldHit = mul(probe.LocalToWorld, float4(localHit, 1.0F)).xyz;
    const float3 worldCenter = mul(probe.LocalToWorld, float4(0.0F, 0.0F, 0.0F, 1.0F)).xyz;
    return SafeNormal(worldHit - worldCenter, worldDirection);
}

float3 SampleSpatialReflection(const float3 worldPosition, const float3 reflection, const float roughness,
                               out float totalWeight)
{
    float3 radiance = 0.0F.xxx;
    totalWeight = 0.0F;
    [unroll]
    for (uint probeIndex = 0U; probeIndex < 2U; ++probeIndex)
    {
        bool enabled = false;
        const SpatialReflectionProbeData probe = SelectedReflectionProbe(probeIndex, enabled);
        if (!enabled || probe.ExtentsWeight.w <= 0.0F)
            continue;
        const float3 direction = BoxProjectedProbeDirection(probe, worldPosition, reflection);
        const float4 encoded = BakedReflectionTexture.SampleLevel(
            BakedReflectionSampler, float4(direction, probe.Parameters.x), roughness * probe.Parameters.w);
        radiance += DecodeRgbe(encoded) * probe.Parameters.y * probe.ExtentsWeight.w;
        totalWeight += probe.ExtentsWeight.w;
    }
    return totalWeight > 0.0F ? radiance / totalWeight : 0.0F.xxx;
}

float CookieRotation(const uint slot)
{
    return CookieRotations[slot >> 2U][slot & 3U];
}

float2 TransformCookieUv(float2 uv, const uint slot)
{
    const float4 transform = CookieTransforms[slot];
    const float angle = radians(CookieRotation(slot));
    const float sine = sin(angle);
    const float cosine = cos(angle);
    uv -= 0.5F.xx;
    uv = float2(uv.x * cosine - uv.y * sine, uv.x * sine + uv.y * cosine);
    uv /= max(abs(transform.xy), 0.0001F.xx);
    return uv + 0.5F.xx + transform.zw;
}

float3 SampleCookieTexture(const uint slot, const float2 uv)
{
    const float2 atlasGrid = float2(4.0F, 2.0F);
    const float2 cell = float2(slot & 3U, slot >> 2U);
    const float2 atlasUv = (cell + clamp(uv, 0.005F.xx, 0.995F.xx)) / atlasGrid;
    return LightCookieAtlas.SampleLevel(LightCookieAtlasSampler, atlasUv, 0.0F).rgb;
}

float3 DirectionalCookie(const float3 worldPosition, const float3 lightDirection)
{
    if (DirectionalCookieAndContact.x < 0.5F)
        return 1.0F.xxx;
    const uint slot = min((uint)DirectionalCookieAndContact.x - 1U, 7U);
    const float3 upReference = abs(lightDirection.y) > 0.95F ? float3(0.0F, 0.0F, 1.0F)
                                                             : float3(0.0F, 1.0F, 0.0F);
    const float3 right = SafeNormal(cross(upReference, lightDirection), float3(1.0F, 0.0F, 0.0F));
    const float3 up = SafeNormal(cross(lightDirection, right), float3(0.0F, 1.0F, 0.0F));
    const float2 uv = TransformCookieUv(float2(dot(worldPosition, right), dot(worldPosition, up)) + 0.5F.xx, slot);
    return any(uv < 0.0F.xx) || any(uv > 1.0F.xx) ? 0.0F.xxx : SampleCookieTexture(slot, uv);
}

float3 LocalCookie(const LocalLightData light, const float3 worldPosition)
{
    const uint encoded = (uint)light.Parameters.w;
    const uint cookie = encoded & 15U;
    if (cookie == 0U)
        return 1.0F.xxx;
    const uint slot = min(cookie - 1U, 7U);
    const float3 fromLight = worldPosition - light.PositionRange.xyz;
    float2 uv;
    if (light.Parameters.y > 0.5F)
    {
        const float3 forward = SafeNormal(light.DirectionOuter.xyz, float3(0.0F, 0.0F, 1.0F));
        const float3 upReference = abs(forward.y) > 0.95F ? float3(0.0F, 0.0F, 1.0F)
                                                          : float3(0.0F, 1.0F, 0.0F);
        const float3 right = SafeNormal(cross(upReference, forward), float3(1.0F, 0.0F, 0.0F));
        const float3 up = SafeNormal(cross(forward, right), float3(0.0F, 1.0F, 0.0F));
        const float forwardDistance = max(dot(fromLight, forward), 0.0001F);
        const float outerCosine = clamp(light.DirectionOuter.w, 0.001F, 0.9999F);
        const float coneRadius = forwardDistance * sqrt(1.0F - outerCosine * outerCosine) / outerCosine;
        uv = float2(dot(fromLight, right), -dot(fromLight, up)) / max(coneRadius * 2.0F, 0.0001F) + 0.5F.xx;
    }
    else
    {
        const float3 direction = SafeNormal(fromLight, float3(0.0F, 0.0F, 1.0F));
        uv = float2(0.5F + atan2(direction.x, direction.z) / (2.0F * Pi),
                    0.5F - asin(clamp(direction.y, -1.0F, 1.0F)) / Pi);
    }
    uv = TransformCookieUv(uv, slot);
    return any(uv < 0.0F.xx) || any(uv > 1.0F.xx) ? 0.0F.xxx : SampleCookieTexture(slot, uv);
}

float EvaluateDirectionalContactShadow(const float3 worldPosition, const float viewDepth, const float3 towardLight,
                                       const bool enabled)
{
    if (!enabled)
        return 1.0F;
    const float maximumDistance = max(DirectionalCookieAndContact.z, 0.01F);
    [unroll]
    for (uint stepIndex = 1U; stepIndex <= 4U; ++stepIndex)
    {
        const float3 samplePosition = worldPosition + towardLight * maximumDistance * (stepIndex / 4.0F);
        if (EvaluateDirectionalShadow(samplePosition, viewDepth) < 0.5F)
            return 0.35F;
    }
    return 1.0F;
}

float EvaluateLocalContactShadow(const uint lightIndex, const float3 worldPosition, const float3 towardLight,
                                 const bool enabled)
{
    if (!enabled)
        return 1.0F;
    const float maximumDistance = max(DirectionalCookieAndContact.z, 0.01F);
    [unroll]
    for (uint stepIndex = 1U; stepIndex <= 4U; ++stepIndex)
    {
        const float3 samplePosition = worldPosition + towardLight * maximumDistance * (stepIndex / 4.0F);
        if (EvaluateLocalShadow(lightIndex, samplePosition) < 0.5F)
            return 0.35F;
    }
    return 1.0F;
}

float3 EvaluateDirectLighting(const float3 normal, const float3 viewDirection, const float3 lightDirection,
                              const float3 radiance, const float3 baseColor, const float metallic,
                              const float roughness)
{
    const float normalLight = saturate(dot(normal, lightDirection));
    const float normalView = saturate(dot(normal, viewDirection));
    if (normalLight <= 0.0F || normalView <= 0.0F)
        return 0.0F.xxx;
    const float3 halfway = SafeNormal(viewDirection + lightDirection, normal);
    const float3 reflectance = lerp(0.04F.xxx, baseColor, metallic);
    const float3 fresnel = FresnelSchlick(saturate(dot(viewDirection, halfway)), reflectance);
    const float distribution = DistributionGgx(normal, halfway, roughness);
    const float geometry = GeometrySchlickGgx(normalView, roughness) * GeometrySchlickGgx(normalLight, roughness);
    const float3 specular = distribution * geometry * fresnel / max(4.0F * normalView * normalLight, 0.0001F);
    const float3 diffuse = (1.0F - fresnel) * (1.0F - metallic) * baseColor / Pi;
    return (diffuse + specular) * radiance * normalLight;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float4 baseColor = MainTexture.Sample(MainSampler, input.UV0) * input.Color * Tint;
    if (SurfaceParameters.y > 0.5F && SurfaceParameters.y < 1.5F)
        clip(baseColor.a - SurfaceParameters.x);
    float3 tangentNormal = NormalTexture.Sample(NormalSampler, input.UV0).xyz * 2.0F - 1.0F;
    tangentNormal.xy *= NormalScale.x;
    const float3 normal = normalize(input.Tangent * tangentNormal.x + input.Bitangent * tangentNormal.y +
                                    input.Normal * tangentNormal.z);
    const float3 viewDirection = normalize(input.ViewDirection);
    const float3 lightDirection = normalize(-DirectionalDirectionExposure.xyz);
    const float4 metallicRoughness = MetallicRoughnessTexture.Sample(MetallicRoughnessSampler, input.UV0);
    const float metallicSample = MetallicTexture.Sample(MetallicSampler, input.UV0).r;
    const float roughnessSample = RoughnessTexture.Sample(RoughnessSampler, input.UV0).r;
    const float metallic = saturate(max(metallicRoughness.b, metallicSample) * MetallicFactor.x);
    const float roughness = clamp(min(metallicRoughness.g, roughnessSample) * RoughnessFactor.x, 0.045F, 1.0F);
    const float occlusionSample = OcclusionTexture.Sample(OcclusionSampler, input.UV0).r;
    const float occlusion = lerp(1.0F, occlusionSample, saturate(OcclusionStrength.x));
    const float3 emissive = EmissiveTexture.Sample(EmissiveSampler, input.UV0).rgb * EmissiveFactor.rgb;

    float3 direct = EvaluateDirectLighting(normal, viewDirection, lightDirection,
                                           DirectionalColorIntensity.rgb * DirectionalColorIntensity.a *
                                               DirectionalCookie(input.WorldPosition, lightDirection),
                                           baseColor.rgb, metallic, roughness);
    direct *= min(EvaluateDirectionalShadow(input.WorldPosition, input.ViewDepth),
                  SampleBakedShadowMask(LightmapParameters.w, input.UV1));
    direct *= EvaluateDirectionalContactShadow(input.WorldPosition, input.ViewDepth, lightDirection,
                                               DirectionalCookieAndContact.y > 0.5F);
    uint2 tile = 0U.xx;
    if (LocalLightCounts.x > 0.5F)
    {
        const uint tileColumns = max((uint)LocalLightCounts.y, 1U);
        const uint tileIndex = (uint(input.Position.y) >> 4U) * tileColumns + (uint(input.Position.x) >> 4U);
        tile = ForwardPlusTiles[tileIndex].xy;
    }
    for (uint tileLightIndex = 0U; tileLightIndex < tile.y; ++tileLightIndex)
    {
        const uint lightIndex = ForwardPlusLightIndex(tile.x + tileLightIndex);
        const LocalLightData light = ForwardPlusLights[lightIndex];
        const float3 toLight = light.PositionRange.xyz - input.WorldPosition;
        const float distanceSquared = dot(toLight, toLight);
        const float distance = sqrt(max(distanceSquared, 1.0e-8F));
        const float range = max(light.PositionRange.w, 0.0001F);
        if (distance >= range)
            continue;
        const float3 localDirection = toLight / distance;
        const float normalizedDistance = distance / range;
        const float rangeFade = saturate(1.0F - normalizedDistance * normalizedDistance * normalizedDistance *
                                                    normalizedDistance);
        float attenuation = rangeFade * rangeFade / max(distanceSquared, 0.01F);
        if (light.Parameters.y > 0.5F)
        {
            const float3 spotDirection =
                SafeNormal(light.DirectionOuter.xyz, float3(0.0F, 0.0F, 1.0F));
            const float coneCosine = dot(spotDirection, -localDirection);
            const float outerCosine = light.DirectionOuter.w;
            const float innerCosine = max(light.Parameters.x, outerCosine + 0.0001F);
            attenuation *= smoothstep(outerCosine, innerCosine, coneCosine);
        }
        const float realtimeShadow = lightIndex < 62U ? EvaluateLocalShadow(lightIndex, input.WorldPosition) : 1.0F;
        const float bakedShadow = SampleBakedShadowMask(light.Parameters.z, input.UV1);
        const bool contactShadows = ((uint)light.Parameters.w & 16U) != 0U;
        const float contactShadow =
            EvaluateLocalContactShadow(lightIndex, input.WorldPosition, localDirection, contactShadows);
        const float3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.a * attenuation *
                                min(realtimeShadow, bakedShadow) * contactShadow *
                                LocalCookie(light, input.WorldPosition);
        direct += EvaluateDirectLighting(normal, viewDirection, localDirection, radiance, baseColor.rgb, metallic,
                                         roughness);
    }
    const float normalView = saturate(dot(normal, viewDirection));
    const float3 baseReflectance = lerp(0.04F.xxx, baseColor.rgb, metallic);
    const float3 diffuseEnvironment = EvaluateDiffuseEnvironment(normal) * baseColor.rgb * (1.0F - metallic) / Pi;
    const float3 spatialDiffuse =
        EvaluateSpatialDiffuse(normal, input.UV1) * baseColor.rgb * (1.0F - metallic) / Pi;
    const float3 reflection = reflect(-viewDirection, normal);
    float spatialReflectionWeight = 0.0F;
    const float3 spatialReflection =
        SampleSpatialReflection(input.WorldPosition, reflection, roughness, spatialReflectionWeight);
    const float3 specularRadiance = spatialReflectionWeight > 0.0F
                                        ? spatialReflection
                                        : SampleEnvironment(reflection, roughness * EnvironmentParameters.w);
    const float2 integratedBrdf =
        BrdfIntegrationLut.SampleLevel(BrdfIntegrationSampler, float2(normalView, roughness), 0.0F).rg;
    const float3 specularEnvironment =
        specularRadiance *
        (FresnelSchlickRoughness(normalView, baseReflectance, roughness) * integratedBrdf.x + integratedBrdf.y);
    const float3 flatAmbient = baseColor.rgb * AmbientColorIntensity.rgb * AmbientColorIntensity.a;
    const float3 ambient =
        (flatAmbient + diffuseEnvironment * EnvironmentParameters.y + spatialDiffuse +
         specularEnvironment * EnvironmentParameters.z) *
        occlusion;
    const float3 color = (ambient + direct + emissive) * DirectionalDirectionExposure.w;
    if (SurfaceParameters.y > 1.5F)
        return float4(color * baseColor.a, baseColor.a);
    return float4(color, 1.0F);
}
