#include <string_view>

namespace Keire::Detail
{
    [[nodiscard]] std::string_view ShaderGraphSpatialLightingHlsl() noexcept
    {
        return R"HLSL(float2 ApproximateSpatialIntegratedBrdf(const float noV, const float roughness)
{
    // Match the portable deferred resolve, which reserves its sampler budget for scene lighting resources.
    const float4 scale = float4(-1.0F, -0.0275F, -0.572F, 0.022F);
    const float4 bias = float4(1.0F, 0.0425F, 1.04F, -0.04F);
    const float4 fit = roughness * scale + bias;
    const float grazing = min(fit.x * fit.x, exp2(-9.28F * saturate(noV))) * fit.x + fit.y;
    return float2(-1.04F, 1.04F) * grazing + fit.zw;
}

float3 DecodeSpatialLightingSample(const float4 sampleValue, const bool rgbe)
{
    return rgbe ? DecodeRgbe(sampleValue) : sampleValue.rgb;
}

float3 EvaluateSpatialProbeDiffuse(const float3 normal, const float2 lightmapUv)
{
    if (((uint)LightmapParameters.z & 1U) != 0U)
    {
        const float layer = max(LightmapParameters.x, 0.0F);
        const float4 lightmapSample =
            LightmapTexture.SampleLevel(LightmapSampler, float3(lightmapUv, layer), 0.0F);
        float3 irradiance = DecodeSpatialLightingSample(lightmapSample, ShadowMaskParameters.y > 0.5F);
        const float4 directionality = LightmapDirectionalityTexture.SampleLevel(
            LightmapDirectionalitySampler, float3(lightmapUv, layer), 0.0F);
        const float3 dominantDirection = SafeNormalize(directionality.xyz * 2.0F - 1.0F, normal);
        irradiance *= lerp(1.0F, saturate(dot(normal, dominantDirection)) * 2.0F, saturate(directionality.w));
        return irradiance;
    }

    const bool selectedRecord = SpatialSelection.x != 0xffffffffU;
    const bool selectedProbe = selectedRecord &&
                               (SpatialSelectionRecords[SpatialSelection.x].Metadata.x & 1U) != 0U;
    const bool embeddedProbe = !selectedRecord && ((uint)LightmapParameters.z & 2U) != 0U;
    if (!selectedProbe && !embeddedProbe)
        return EvaluateDiffuseEnvironment(normal) * EnvironmentParameters.y;

    float3 coefficients[9];
    [unroll]
    for (uint index = 0U; index < 9U; ++index)
    {
        coefficients[index] = selectedProbe ? SpatialSelectionRecords[SpatialSelection.x].ProbeIrradiance[index].rgb
                                            : ProbeIrradiance[index].rgb;
    }
    const float3 direction = SafeNormalize(normal, float3(0.0F, 1.0F, 0.0F));
    const float x = direction.x;
    const float y = direction.y;
    const float z = direction.z;
    return max(coefficients[0] * 0.282095F + coefficients[1] * (0.488603F * y) +
                   coefficients[2] * (0.488603F * z) + coefficients[3] * (0.488603F * x) +
                   coefficients[4] * (1.092548F * x * y) + coefficients[5] * (1.092548F * y * z) +
                   coefficients[6] * (0.315392F * (3.0F * y * y - 1.0F)) +
                   coefficients[7] * (1.092548F * x * z) + coefficients[8] * (0.546274F * (z * z - x * x)),
               0.0F.xxx);
}

ShaderGraphReflectionProbe SpatialReflectionProbe(const uint index)
{
    if (SpatialSelection.x != 0xffffffffU)
        return SpatialSelectionRecords[SpatialSelection.x].ReflectionProbes[index];
    return ReflectionProbes[index];
}

bool HasSpatialReflectionProbe(const uint index)
{
    if (SpatialSelection.x != 0xffffffffU)
    {
        const uint flag = index == 0U ? 2U : 4U;
        return (SpatialSelectionRecords[SpatialSelection.x].Metadata.x & flag) != 0U;
    }
    return ReflectionProbes[index].ExtentsWeight.w > 0.0F;
}

float3 BoxProjectedSpatialReflection(const ShaderGraphReflectionProbe probe, const float3 worldPosition,
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
    const float3 hitWorld =
        mul(probe.LocalToWorld, float4(localPosition + localDirection * distanceToBox, 1.0F)).xyz;
    const float3 probePosition = mul(probe.LocalToWorld, float4(0.0F, 0.0F, 0.0F, 1.0F)).xyz;
    return SafeNormalize(hitWorld - probePosition, reflectionDirection);
}

float3 SampleSpatialReflection(const float3 worldPosition, const float3 reflectionDirection, const float roughness)
{
    float3 result = 0.0F.xxx;
    float totalWeight = 0.0F;
    [unroll]
    for (uint index = 0U; index < 2U; ++index)
    {
        if (!HasSpatialReflectionProbe(index))
            continue;
        const ShaderGraphReflectionProbe probe = SpatialReflectionProbe(index);
        const float weight = max(probe.ExtentsWeight.w, 0.0F);
        const float3 direction = BoxProjectedSpatialReflection(probe, worldPosition, reflectionDirection);
        const float level = roughness * max(probe.Parameters.w, 0.0F);
        const float4 reflectionSample = ReflectionProbeTexture.SampleLevel(
            ReflectionProbeSampler, float4(direction, max(probe.Parameters.x, 0.0F)), level);
        result += DecodeSpatialLightingSample(reflectionSample, ShadowMaskParameters.z > 0.5F) *
                  max(probe.Parameters.y, 0.0F) * weight;
        totalWeight += weight;
    }
    if (totalWeight > 1.0e-5F)
        return result / totalWeight;
    return SampleEnvironment(reflectionDirection, roughness * EnvironmentParameters.w) * EnvironmentParameters.z;
}

float SpatialCookieRotation(const uint slot)
{
    const float4 rotations = CookieRotations[slot >> 2U];
    return slot % 4U == 0U ? rotations.x
           : slot % 4U == 1U ? rotations.y
           : slot % 4U == 2U ? rotations.z
                              : rotations.w;
}

float SampleSpatialCookie(float2 uv, const float oneBasedSlot)
{
    if (oneBasedSlot < 0.5F)
        return 1.0F;
    const uint slot = min((uint)oneBasedSlot - 1U, 7U);
    uv = RotateMaterialUV(uv * CookieTransforms[slot].xy + CookieTransforms[slot].zw, 0.5F.xx,
                          radians(SpatialCookieRotation(slot)));
    if (any(uv < 0.0F.xx) || any(uv > 1.0F.xx))
        return 0.0F;
    const float2 atlasUv = (uv + float2(slot & 3U, slot >> 2U)) / float2(4.0F, 2.0F);
    return dot(CookieAtlasTexture.SampleLevel(CookieAtlasSampler, atlasUv, 0.0F).rgb,
               float3(0.2126F, 0.7152F, 0.0722F));
}

void SpatialLightBasis(const float3 direction, out float3 tangent, out float3 bitangent)
{
    const float3 up = abs(direction.y) < 0.99F ? float3(0.0F, 1.0F, 0.0F) : float3(1.0F, 0.0F, 0.0F);
    tangent = SafeNormalize(cross(up, direction), float3(1.0F, 0.0F, 0.0F));
    bitangent = SafeNormalize(cross(direction, tangent), float3(0.0F, 1.0F, 0.0F));
}

float EvaluateDirectionalSpatialCookie(const float3 worldPosition)
{
    if (DirectionalCookieAndContact.x < 0.5F)
        return 1.0F;
    const float3 direction = SafeNormalize(DirectionalDirectionExposure.xyz, float3(0.0F, -1.0F, 0.0F));
    float3 tangent;
    float3 bitangent;
    SpatialLightBasis(direction, tangent, bitangent);
    return SampleSpatialCookie(float2(dot(worldPosition, tangent), dot(worldPosition, bitangent)) + 0.5F.xx,
                               DirectionalCookieAndContact.x);
}

float EvaluateLocalSpatialCookie(const ShaderGraphLocalLight light, const float3 worldPosition)
{
    const float encoded = fmod(max(light.Parameters.w, 0.0F), 16.0F);
    if (encoded < 0.5F)
        return 1.0F;
    const float3 direction = SafeNormalize(light.DirectionOuter.xyz, float3(0.0F, 0.0F, 1.0F));
    float3 tangent;
    float3 bitangent;
    SpatialLightBasis(direction, tangent, bitangent);
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
    return SampleSpatialCookie(uv, encoded);
}

float SampleSpatialMixedVisibility(const float2 lightmapUv, const float oneBasedChannel)
{
    if (SurfaceParameters.z < 0.5F || ((uint)LightmapParameters.z & 1U) == 0U || oneBasedChannel < 0.5F)
        return 1.0F;
    const uint channel = min((uint)oneBasedChannel - 1U, 7U);
    const uint layer = (uint)max(LightmapParameters.y, 0.0F) * 2U + (channel >> 2U);
    return ShadowMaskTexture.SampleLevel(ShadowMaskSampler, float3(lightmapUv, layer),
                                         0.0F)[channel & 3U];
}

)HLSL";
    }
} // namespace Keire::Detail
