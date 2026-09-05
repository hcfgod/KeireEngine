struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

VertexOutput VSMain(const uint vertexId : SV_VertexID)
{
    VertexOutput output;
    const float2 position = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.UV = position;
    output.Position = float4(position * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

Texture2D<float4> GBufferBaseColorMetallic : register(t0, space2);
SamplerState GBufferBaseColorMetallicSampler : register(s0, space2);
Texture2D<float4> GBufferNormalRoughness : register(t1, space2);
SamplerState GBufferNormalRoughnessSampler : register(s1, space2);
Texture2D<float4> GBufferMaterial : register(t2, space2);
SamplerState GBufferMaterialSampler : register(s2, space2);
Texture2D<float4> GBufferLighting : register(t3, space2);
SamplerState GBufferLightingSampler : register(s3, space2);
Texture2D<float> SceneDepth : register(t4, space2);
SamplerState SceneDepthSampler : register(s4, space2);
Texture2D<float4> DBufferBaseColor : register(t5, space2);
SamplerState DBufferBaseColorSampler : register(s5, space2);
Texture2D<float4> DBufferNormal : register(t6, space2);
SamplerState DBufferNormalSampler : register(s6, space2);
Texture2D<float4> DBufferMaterial : register(t7, space2);
SamplerState DBufferMaterialSampler : register(s7, space2);
Texture2DArray<float> DirectionalShadowTexture : register(t8, space2);
SamplerState DirectionalShadowSampler : register(s8, space2);
Texture2DArray<float> LocalShadowTexture : register(t9, space2);
SamplerState LocalShadowSampler : register(s9, space2);
Texture2D<float4> EnvironmentTexture : register(t10, space2);
SamplerState EnvironmentSampler : register(s10, space2);
Texture2DArray<float4> LightmapTexture : register(t11, space2);
SamplerState LightmapSampler : register(s11, space2);
Texture2DArray<float4> LightmapDirectionalityTexture : register(t12, space2);
SamplerState LightmapDirectionalitySampler : register(s12, space2);
Texture2DArray<float4> ShadowMaskTexture : register(t13, space2);
SamplerState ShadowMaskSampler : register(s13, space2);
TextureCubeArray<float4> ReflectionProbeTexture : register(t14, space2);
SamplerState ReflectionProbeSampler : register(s14, space2);
Texture2D<float4> CookieAtlasTexture : register(t15, space2);
SamplerState CookieAtlasSampler : register(s15, space2);

struct DeferredLocalLight
{
    float4 PositionRange;
    float4 DirectionOuter;
    float4 ColorIntensity;
    float4 Parameters;
};

struct DeferredReflectionProbe
{
    float4x4 WorldToLocal;
    float4x4 LocalToWorld;
    float4 ExtentsWeight;
    float4 Parameters;
};

struct DeferredSpatialSelectionRecord
{
    float4 ProbeIrradiance[9];
    DeferredReflectionProbe ReflectionProbes[2];
    uint4 Metadata;
};

StructuredBuffer<DeferredLocalLight> ForwardPlusLights : register(t16, space2);
StructuredBuffer<uint4> ForwardPlusTiles : register(t17, space2);
StructuredBuffer<uint4> ForwardPlusLightIndices : register(t18, space2);
StructuredBuffer<DeferredSpatialSelectionRecord> SpatialSelectionRecords : register(t19, space2);

cbuffer DeferredLightingConstants : register(b0, space3)
{
    float4 ClearColor;
    float4 AmbientColorIntensity;
    float4 DirectionalColorIntensity;
    float4 DirectionalDirectionExposure;
    float4x4 InverseViewProjection;
    float4x4 View;
    float4x4 ViewProjection;
    float4 CameraPositionAndLocalLightCount;
    float4 ForwardPlusGrid;
    float4 ContactShadowParameters;
    float4 GlobalIlluminationChannels;
};

cbuffer ShadowData : register(b1, space3)
{
    float4 DirectionalShadowParameters;
    float4 DirectionalCascadeSplits;
    float4x4 DirectionalShadowMatrices[4];
    float4x4 LocalShadowMatrices[20];
    float4 LocalShadowParameters[62];
    float4 LocalShadowSampleBounds[20];
};

cbuffer DeferredSpatialLightingData : register(b2, space3)
{
    float4 DiffuseIrradiance[9];
    float4 EnvironmentParameters;
    float4 EnvironmentEncoding;
    float4 CookieTransforms[8];
    float4 CookieRotations[2];
    float4 DirectionalCookieAndContact;
    float4 SpatialContext;
};

#include "BuiltinLighting.hlsli"

uint ForwardPlusLightIndex(const uint index)
{
    const uint4 indices = ForwardPlusLightIndices[index >> 2U];
    return indices[index & 3U];
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
    float totalWeight = 0.0F;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float weight = (2.0F - abs((float)x)) * (2.0F - abs((float)y));
            const float2 unclampedUv = uv + float2(x, y) * inverseResolution;
            if (!clampSamples &&
                (any(unclampedUv < sampleBounds.xy) || any(unclampedUv > sampleBounds.zw)))
            {
                visibility += weight;
            }
            else
            {
                const float2 sampleUv = clamp(unclampedUv, sampleBounds.xy, sampleBounds.zw);
                const float storedDepth =
                    textureValue.SampleLevel(samplerValue, float3(sampleUv, layer), 0.0F);
                visibility += depth <= storedDepth ? weight : 0.0F;
            }
            totalWeight += weight;
        }
    }
    return visibility / max(totalWeight, 1.0F);
}

float EvaluateDirectionalShadow(const float3 worldPosition, const float viewDepth)
{
    const float cascadeCount = abs(DirectionalShadowParameters.x);
    if (cascadeCount < 0.5F)
        return 1.0F;
    uint cascade = 0U;
    cascade += viewDepth > DirectionalCascadeSplits.x;
    cascade += viewDepth > DirectionalCascadeSplits.y;
    cascade += viewDepth > DirectionalCascadeSplits.z;
    cascade = min(cascade, (uint)cascadeCount - 1U);
    const float4 shadowClip = mul(DirectionalShadowMatrices[cascade], float4(worldPosition, 1.0F));
    const float3 projected = shadowClip.xyz / max(abs(shadowClip.w), 1.0e-6F);
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    if (any(uv < 0.0F.xx) || any(uv > 1.0F.xx) || projected.z <= 0.0F || projected.z >= 1.0F)
        return 1.0F;
    const float visibility = SampleShadowPcf(
        DirectionalShadowTexture, DirectionalShadowSampler, uv, cascade,
        projected.z - DirectionalShadowParameters.z, DirectionalShadowParameters.w,
        DirectionalShadowParameters.x > 0.0F, float4(0.0F, 0.0F, 1.0F, 1.0F), false);
    return lerp(1.0F, visibility, saturate(DirectionalShadowParameters.y));
}

void PointShadowCoordinates(const float3 direction, out uint face, out float majorDistance)
{
    const float3 absoluteDirection = abs(direction);
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        face = direction.x >= 0.0F ? 0U : 1U;
        majorDistance = absoluteDirection.x;
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        face = direction.y >= 0.0F ? 2U : 3U;
        majorDistance = absoluteDirection.y;
    }
    else
    {
        face = direction.z >= 0.0F ? 4U : 5U;
        majorDistance = absoluteDirection.z;
    }
}

float EvaluateLocalShadow(const uint lightIndex, const DeferredLocalLight light, const float3 worldPosition)
{
    if (lightIndex >= 62U || LocalShadowParameters[lightIndex].x < 0.0F)
        return 1.0F;
    uint matrixIndex = (uint)LocalShadowParameters[lightIndex].x;
    if (light.Parameters.y < 0.5F)
    {
        uint face = 0U;
        float majorDistance = 0.0F;
        PointShadowCoordinates(worldPosition - light.PositionRange.xyz, face, majorDistance);
        matrixIndex += face;
    }
    const float4 clip = mul(LocalShadowMatrices[matrixIndex], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / max(abs(clip.w), 1.0e-6F);
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    const float visibility = SampleShadowPcf(
        LocalShadowTexture, LocalShadowSampler, uv, 0.0F,
        projected.z - LocalShadowParameters[lightIndex].w, 1.0F / 4096.0F,
        LocalShadowParameters[lightIndex].z > 0.5F, LocalShadowSampleBounds[matrixIndex], true);
    return lerp(1.0F, visibility, saturate(LocalShadowParameters[lightIndex].y));
}


float3 EvaluateProbeDiffuse(const DeferredSpatialSelectionRecord selection, const float3 normal)
{
    if ((selection.Metadata.x & 1U) == 0U)
        return 0.0F.xxx;
    float3 coefficients[9];
    [unroll]
    for (uint index = 0U; index < 9U; ++index)
        coefficients[index] = selection.ProbeIrradiance[index].rgb;
    return EvaluateSphericalHarmonics(coefficients, normal);
}

float3 BoxProjectedReflectionDirection(const DeferredReflectionProbe probe, const float3 worldPosition,
                                       const float3 reflectionDirection)
{
    if (probe.Parameters.z < 0.5F)
        return reflectionDirection;
    const float3 localPosition = mul(probe.WorldToLocal, float4(worldPosition, 1.0F)).xyz;
    const float3 localDirection = mul((float3x3)probe.WorldToLocal, reflectionDirection);
    // sign(0) is zero: choose a nonzero sign even for rays parallel to a box face.
    const float3 directionSign = float3(localDirection.x < 0.0F ? -1.0F : 1.0F,
                                         localDirection.y < 0.0F ? -1.0F : 1.0F,
                                         localDirection.z < 0.0F ? -1.0F : 1.0F);
    const float3 safeDirection = directionSign * max(abs(localDirection), 1.0e-5F.xxx);
    const float3 boundary = sign(safeDirection) * probe.ExtentsWeight.xyz;
    const float3 distances = float3(
        abs(localDirection.x) < 1.0e-5F ? 1.0e30F : (boundary.x - localPosition.x) / safeDirection.x,
        abs(localDirection.y) < 1.0e-5F ? 1.0e30F : (boundary.y - localPosition.y) / safeDirection.y,
        abs(localDirection.z) < 1.0e-5F ? 1.0e30F : (boundary.z - localPosition.z) / safeDirection.z);
    const float distanceToBox = min(distances.x, min(distances.y, distances.z));
    const float3 hitWorld = mul(probe.LocalToWorld, float4(localPosition + localDirection * distanceToBox, 1.0F)).xyz;
    const float3 probePosition = mul(probe.LocalToWorld, float4(0.0F, 0.0F, 0.0F, 1.0F)).xyz;
    return SafeNormalize(hitWorld - probePosition, reflectionDirection);
}

float3 SampleProbeReflections(const DeferredSpatialSelectionRecord selection, const float3 worldPosition,
                              const float3 reflectionDirection, const float roughness)
{
    float3 result = 0.0F.xxx;
    float totalWeight = 0.0F;
    if (GlobalIlluminationChannels.z > 0.5F)
    {
        [unroll]
        for (uint index = 0U; index < 2U; ++index)
        {
            const uint flag = index == 0U ? 2U : 4U;
            if ((selection.Metadata.x & flag) == 0U)
                continue;
            const DeferredReflectionProbe probe = selection.ReflectionProbes[index];
            const float weight = max(probe.ExtentsWeight.w, 0.0F);
            const float3 direction = BoxProjectedReflectionDirection(probe, worldPosition, reflectionDirection);
            const float level = roughness * max(probe.Parameters.w, 0.0F);
            const float4 sampleValue = ReflectionProbeTexture.SampleLevel(
                ReflectionProbeSampler, float4(direction, max(probe.Parameters.x, 0.0F)), level);
            result += DecodeLightingSample(sampleValue, SpatialContext.w > 0.5F) *
                      max(probe.Parameters.y, 0.0F) * weight;
            totalWeight += weight;
        }
    }
    if (totalWeight > 1.0e-5F)
        return result / totalWeight;
    if (GlobalIlluminationChannels.y > 0.5F)
    {
        return SampleEnvironment(reflectionDirection, roughness * EnvironmentParameters.w) *
               EnvironmentParameters.z;
    }
    return 0.0F.xxx;
}

float3 ReconstructWorldPosition(const float2 uv, const float depth)
{
    const float2 clip = uv * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F);
    const float4 homogeneousWorld = mul(InverseViewProjection, float4(clip, depth, 1.0F));
    return homogeneousWorld.xyz / max(abs(homogeneousWorld.w), 1.0e-6F);
}

float CookieRotation(const uint slot)
{
    const float4 rotations = CookieRotations[slot >> 2U];
    return slot % 4U == 0U ? rotations.x
           : slot % 4U == 1U ? rotations.y
           : slot % 4U == 2U ? rotations.z
                              : rotations.w;
}

float2 RotateCookieUv(const float2 uv, const float degrees)
{
    const float angle = radians(degrees);
    const float sine = sin(angle);
    const float cosine = cos(angle);
    const float2 local = uv - 0.5F.xx;
    return float2(local.x * cosine - local.y * sine, local.x * sine + local.y * cosine) + 0.5F.xx;
}

float SampleCookieSlot(float2 uv, const float oneBasedSlot)
{
    if (oneBasedSlot < 0.5F)
        return 1.0F;
    const uint slot = min((uint)oneBasedSlot - 1U, 7U);
    uv = RotateCookieUv(uv * CookieTransforms[slot].xy + CookieTransforms[slot].zw, CookieRotation(slot));
    if (any(uv < 0.0F.xx) || any(uv > 1.0F.xx))
        return 0.0F;
    const float2 atlasUv = (uv + float2(slot & 3U, slot >> 2U)) / float2(4.0F, 2.0F);
    return dot(CookieAtlasTexture.SampleLevel(CookieAtlasSampler, atlasUv, 0.0F).rgb,
               float3(0.2126F, 0.7152F, 0.0722F));
}

void LightBasis(const float3 direction, out float3 tangent, out float3 bitangent)
{
    const float3 up = abs(direction.y) < 0.99F ? float3(0.0F, 1.0F, 0.0F) : float3(1.0F, 0.0F, 0.0F);
    tangent = SafeNormalize(cross(up, direction), float3(1.0F, 0.0F, 0.0F));
    bitangent = SafeNormalize(cross(direction, tangent), float3(0.0F, 1.0F, 0.0F));
}

float EvaluateDirectionalCookie(const float3 worldPosition)
{
    if (DirectionalCookieAndContact.x < 0.5F)
        return 1.0F;
    const float3 direction = SafeNormalize(DirectionalDirectionExposure.xyz, float3(0.0F, -1.0F, 0.0F));
    float3 tangent;
    float3 bitangent;
    LightBasis(direction, tangent, bitangent);
    return SampleCookieSlot(float2(dot(worldPosition, tangent), dot(worldPosition, bitangent)) + 0.5F.xx,
                            DirectionalCookieAndContact.x);
}

float EvaluateLocalCookie(const DeferredLocalLight light, const float3 worldPosition)
{
    const float encoded = fmod(max(light.Parameters.w, 0.0F), 16.0F);
    if (encoded < 0.5F)
        return 1.0F;
    const float3 direction = SafeNormalize(light.DirectionOuter.xyz, float3(0.0F, 0.0F, 1.0F));
    float3 tangent;
    float3 bitangent;
    LightBasis(direction, tangent, bitangent);
    const float3 fromLight = worldPosition - light.PositionRange.xyz;
    float2 uv;
    if (light.Parameters.y > 0.5F)
    {
        const float forward = max(dot(fromLight, direction), 1.0e-4F);
        const float coneRadius = forward * max(sqrt(max(1.0F - light.DirectionOuter.w * light.DirectionOuter.w, 0.0F)) /
                                                   max(abs(light.DirectionOuter.w), 1.0e-4F),
                                               1.0e-4F);
        uv = float2(dot(fromLight, tangent), dot(fromLight, bitangent)) / (2.0F * coneRadius) + 0.5F.xx;
    }
    else
    {
        const float3 unitDirection = SafeNormalize(fromLight, direction);
        uv = float2(0.5F + atan2(dot(unitDirection, tangent), dot(unitDirection, direction)) / (2.0F * Pi),
                    0.5F - asin(clamp(dot(unitDirection, bitangent), -1.0F, 1.0F)) / Pi);
    }
    return SampleCookieSlot(uv, encoded);
}

float EvaluateContactShadow(const float3 worldPosition, const float3 normal, const float3 directionToLight,
                            const float maximumDistance)
{
    const uint steps = clamp((uint)ContactShadowParameters.z, 1U, 16U);
    const float rayLength = min(maximumDistance, max(ContactShadowParameters.x, 0.0F));
    if (rayLength <= 1.0e-4F)
        return 1.0F;
    const float3 origin = worldPosition + normal * max(ContactShadowParameters.y, 1.0e-5F);
    [loop]
    for (uint stepIndex = 1U; stepIndex <= steps; ++stepIndex)
    {
        const float distanceAlongRay = rayLength * ((float)stepIndex / (float)steps);
        const float3 samplePosition = origin + directionToLight * distanceAlongRay;
        const float4 clip = mul(ViewProjection, float4(samplePosition, 1.0F));
        if (clip.w <= 1.0e-6F)
            continue;
        const float3 projected = clip.xyz / clip.w;
        const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
        if (any(uv <= 0.0F.xx) || any(uv >= 1.0F.xx) || projected.z <= 0.0F || projected.z >= 1.0F)
            continue;
        const float sceneDepth = SceneDepth.SampleLevel(SceneDepthSampler, uv, 0.0F);
        if (sceneDepth + ContactShadowParameters.y < projected.z)
            return 0.0F;
    }
    return 1.0F;
}

float SampleMixedVisibility(const float2 lightmapUv, const uint shadowMaskLayer, const float oneBasedChannel)
{
    if (shadowMaskLayer == 0U || oneBasedChannel < 0.5F)
        return 1.0F;
    const uint channel = min((uint)oneBasedChannel - 1U, 7U);
    const uint layer = (shadowMaskLayer - 1U) * 2U + (channel >> 2U);
    const float4 visibility = ShadowMaskTexture.SampleLevel(ShadowMaskSampler, float3(lightmapUv, layer), 0.0F);
    return visibility[channel & 3U];
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    float4 material = GBufferMaterial.SampleLevel(GBufferMaterialSampler, input.UV, 0.0F);
    if (material.a < 0.5F)
        discard;
    const bool receiveShadows = material.a > 0.875F;

    float4 baseColorMetallic = GBufferBaseColorMetallic.SampleLevel(GBufferBaseColorMetallicSampler, input.UV, 0.0F);
    float4 normalRoughness = GBufferNormalRoughness.SampleLevel(GBufferNormalRoughnessSampler, input.UV, 0.0F);
    const float4 lightingPayload = GBufferLighting.SampleLevel(GBufferLightingSampler, input.UV, 0.0F);
    const uint packedLayers = (uint)round(max(lightingPayload.z, 0.0F));
    const uint packedSpatial = (uint)round(max(lightingPayload.w, 0.0F));
    const uint contribution = packedSpatial >> 16U;
    if (contribution != (uint)SpatialContext.x)
        discard;
    const uint lightmapLayer = packedLayers & 0xfffU;
    const uint shadowMaskLayer = packedLayers >> 12U;
    const uint spatialRecord = packedSpatial & 0xffffU;
    DeferredSpatialSelectionRecord selection = (DeferredSpatialSelectionRecord)0;
    if (spatialRecord != 0U)
        selection = SpatialSelectionRecords[spatialRecord - 1U];

    const float4 decalBaseColor = DBufferBaseColor.SampleLevel(DBufferBaseColorSampler, input.UV, 0.0F);
    const float4 decalNormal = DBufferNormal.SampleLevel(DBufferNormalSampler, input.UV, 0.0F);
    const float4 decalMaterial = DBufferMaterial.SampleLevel(DBufferMaterialSampler, input.UV, 0.0F);
    baseColorMetallic.rgb = lerp(baseColorMetallic.rgb, decalBaseColor.rgb, saturate(decalBaseColor.a));
    normalRoughness.xyz = lerp(normalRoughness.xyz, decalNormal.xyz, saturate(decalNormal.a));
    baseColorMetallic.a = lerp(baseColorMetallic.a, decalMaterial.r, saturate(decalMaterial.a));
    normalRoughness.a = lerp(normalRoughness.a, decalMaterial.g, saturate(decalMaterial.a));
    material.g = lerp(material.g, decalMaterial.b, saturate(decalMaterial.a));

    const float depth = SceneDepth.SampleLevel(SceneDepthSampler, input.UV, 0.0F);
    const float3 worldPosition = ReconstructWorldPosition(input.UV, depth);
    const float3 normal = SafeNormalize(normalRoughness.xyz * 2.0F - 1.0F, float3(0.0F, 0.0F, 1.0F));
    const float3 viewDirection = SafeNormalize(CameraPositionAndLocalLightCount.xyz - worldPosition, normal);
    const float3 baseColor = baseColorMetallic.rgb;
    const float metallic = saturate(baseColorMetallic.a);
    const float roughness = clamp(normalRoughness.a, 0.04F, 1.0F);
    const float specularLevel = saturate(material.g);
    const float ao = saturate(material.r);
    const float3 directionalLightDirection =
        SafeNormalize(-DirectionalDirectionExposure.xyz, float3(0.0F, 1.0F, 0.0F));

    float directionalVisibility = receiveShadows
        ? min(EvaluateDirectionalShadow(worldPosition, mul(View, float4(worldPosition, 1.0F)).z),
              SampleMixedVisibility(lightingPayload.xy, shadowMaskLayer, SpatialContext.y)) : 1.0F;
    if (receiveShadows && DirectionalCookieAndContact.y > 0.5F)
        directionalVisibility *= EvaluateContactShadow(worldPosition, normal, directionalLightDirection,
                                                       ContactShadowParameters.x);
    directionalVisibility *= EvaluateDirectionalCookie(worldPosition);
    float3 directLighting = EvaluateDirectLighting(
        normal, viewDirection, directionalLightDirection,
        DirectionalColorIntensity.rgb * DirectionalColorIntensity.a * directionalVisibility, baseColor, metallic,
        roughness, specularLevel);
    if (CameraPositionAndLocalLightCount.w > 0.5F)
    {
        const uint columns = max((uint)ForwardPlusGrid.x, 1U);
        const uint rows = max((uint)ForwardPlusGrid.y, 1U);
        const uint tileSize = max((uint)ForwardPlusGrid.z, 1U);
        const uint2 tile = min(uint2(input.Position.xy) / tileSize, uint2(columns - 1U, rows - 1U));
        const uint2 tileRange = ForwardPlusTiles[tile.y * columns + tile.x].xy;
        [loop]
        for (uint tileLight = 0U; tileLight < tileRange.y; ++tileLight)
        {
            const uint lightIndex = ForwardPlusLightIndex(tileRange.x + tileLight);
            if (lightIndex >= (uint)CameraPositionAndLocalLightCount.w)
                continue;
            const DeferredLocalLight light = ForwardPlusLights[lightIndex];
            const uint lightContribution = (uint)max(light.Parameters.w, 0.0F) >> 5U;
            if (lightContribution != (uint)SpatialContext.x)
                continue;
            const float3 toLight = light.PositionRange.xyz - worldPosition;
            const float distanceSquared = dot(toLight, toLight);
            const float distanceToLight = sqrt(max(distanceSquared, 1.0e-8F));
            const float range = max(light.PositionRange.w, 1.0e-4F);
            if (distanceToLight >= range)
                continue;
            const float3 lightDirection = toLight / distanceToLight;
            const float normalizedDistance = distanceToLight / range;
            const float rangeFade = saturate(1.0F - pow(normalizedDistance, 4.0F));
            float attenuation = rangeFade * rangeFade / max(distanceSquared, 0.01F);
            if (light.Parameters.y > 0.5F)
            {
                const float3 spotDirection = SafeNormalize(light.DirectionOuter.xyz, float3(0.0F, 0.0F, 1.0F));
                const float coneCosine = dot(spotDirection, -lightDirection);
                attenuation *= smoothstep(light.DirectionOuter.w,
                                          max(light.Parameters.x, light.DirectionOuter.w + 1.0e-4F), coneCosine);
            }
            float visibility = receiveShadows
                ? min(EvaluateLocalShadow(lightIndex, light, worldPosition),
                      SampleMixedVisibility(lightingPayload.xy, shadowMaskLayer, light.Parameters.z)) : 1.0F;
            if (receiveShadows && ((uint)max(light.Parameters.w, 0.0F) & 16U) != 0U)
                visibility *= EvaluateContactShadow(worldPosition, normal, lightDirection, distanceToLight);
            visibility *= EvaluateLocalCookie(light, worldPosition);
            const float3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.a * attenuation * visibility;
            directLighting += EvaluateDirectLighting(normal, viewDirection, lightDirection, radiance, baseColor,
                                                     metallic, roughness, specularLevel);
        }
    }

    float3 diffuseIrradiance = 0.0F.xxx;
    bool bakedDiffuseResolved = false;
    if (GlobalIlluminationChannels.z > 0.5F && lightmapLayer != 0U)
    {
        const float4 lightmapSample =
            LightmapTexture.SampleLevel(LightmapSampler, float3(lightingPayload.xy, lightmapLayer - 1U), 0.0F);
        diffuseIrradiance = DecodeLightingSample(lightmapSample, SpatialContext.z > 0.5F);
        const float4 directionality = LightmapDirectionalityTexture.SampleLevel(
            LightmapDirectionalitySampler, float3(lightingPayload.xy, lightmapLayer - 1U), 0.0F);
        const float3 dominantDirection = SafeNormalize(directionality.xyz * 2.0F - 1.0F, normal);
        const float directionalResponse = saturate(dot(normal, dominantDirection)) * 2.0F;
        diffuseIrradiance *= lerp(1.0F, directionalResponse, saturate(directionality.w));
        bakedDiffuseResolved = true;
    }
    else if (GlobalIlluminationChannels.z > 0.5F && (selection.Metadata.x & 1U) != 0U)
    {
        diffuseIrradiance = EvaluateProbeDiffuse(selection, normal);
        bakedDiffuseResolved = true;
    }
    if (!bakedDiffuseResolved && GlobalIlluminationChannels.x > 0.5F)
        diffuseIrradiance = EvaluateEnvironmentDiffuse(normal) * EnvironmentParameters.y;
    const float noV = saturate(dot(normal, viewDirection));
    const float3 f0 = lerp((0.08F * specularLevel).xxx, baseColor, metallic);
    const float3 reflectionDirection = reflect(-viewDirection, normal);
    const float3 reflectionRadiance =
        SampleProbeReflections(selection, worldPosition, reflectionDirection, roughness);
    const float2 integratedBrdf = ApproximateIntegratedBrdf(noV, roughness);
    const float3 specularEnvironment =
        reflectionRadiance *
        (FresnelSchlickRoughness(noV, f0, roughness) * integratedBrdf.x + integratedBrdf.y);
    const float3 flatAmbient = AmbientColorIntensity.rgb * AmbientColorIntensity.a;
    const float3 diffuseEnvironment =
        (flatAmbient + diffuseIrradiance) * baseColor * (1.0F - metallic) / Pi;
    const float3 ambient = (diffuseEnvironment + specularEnvironment) * ao;
    const float3 litColor = (ambient + directLighting) * DirectionalDirectionExposure.w;
    return float4(lerp(litColor, baseColor, saturate(material.b)), 1.0F);
}
