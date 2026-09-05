struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV0 : TEXCOORD2;
    float4 VertexColor : TEXCOORD3;
    float4 Tangent : TEXCOORD4;
    float2 UV1 : TEXCOORD5;
};

struct VertexOutput
{
    float3 WorldPosition : TEXCOORD2;
    float3 WorldNormal : TEXCOORD3;
    float4 BaseColor : TEXCOORD4;
    float ViewDepth : TEXCOORD5;
    float2 LightmapUV : TEXCOORD6;
    float4 Position : SV_Position;
};

cbuffer ObjectConstants : register(b0, space1)
{
    float4x4 ModelViewProjection;
    float4x4 NormalMatrix;
    float4 Tint;
    float4 LightDirection;
    float4 LightColor;
    float4 AmbientAndExposure;
    float4 LightingParameters;
    float4x4 Model;
    float4x4 View;
    float4x4 Projection;
};

cbuffer InstanceAddressingData : register(b2, space1)
{
    uint4 InstanceParameters;
};

struct InstanceData
{
    float4x4 Model;
    float4x4 NormalMatrix;
    float4 Tint;
};

StructuredBuffer<InstanceData> Instances : register(t0, space0);

cbuffer ShadowConstants : register(b0, space3)
{
    float4 DirectionalShadowParameters;
    float4 DirectionalCascadeSplits;
    float4x4 DirectionalShadowMatrices[4];
    float4x4 LocalShadowMatrices[20];
    float4 LocalShadowParameters[62];
    float4 LocalShadowSampleBounds[20];
};

struct LocalLightData
{
    float4 PositionRange;
    float4 DirectionOuter;
    float4 ColorIntensity;
    float4 Parameters;
};

cbuffer LocalLightConstants : register(b1, space3)
{
    float4 LocalLightCounts;
    LocalLightData LocalLights[62];
};

cbuffer EnvironmentConstants : register(b2, space3)
{
    float4 DiffuseIrradiance[9];
    // Rotation, diffuse intensity, specular intensity, and maximum environment mip.
    float4 EnvironmentParameters;
    float4 EnvironmentEncoding;
};

cbuffer VertexSpatialLightingConstants : register(b3, space1)
{
    float4 VertexLightmapScaleOffset;
    float4 VertexLightmapParameters;
    float4 VertexShadowMaskParameters;
};

cbuffer FragmentSpatialLightingConstants : register(b3, space3)
{
    float4 FragmentLightmapScaleOffset;
    float4 FragmentLightmapParameters;
    float4 FragmentShadowMaskParameters;
    float4 CameraPositionExposure;
    float4 AmbientColorIntensity;
    float4 DirectionalDirectionEnabled;
    float4 DirectionalColorIntensity;
};

Texture2DArray<float> DirectionalShadowTexture : register(t0, space2);
SamplerState DirectionalShadowSampler : register(s0, space2);
Texture2DArray<float> LocalShadowTexture : register(t1, space2);
SamplerState LocalShadowSampler : register(s1, space2);
Texture2DArray<float4> LightmapTexture : register(t2, space2);
SamplerState LightmapSampler : register(s2, space2);
Texture2DArray<float4> LightmapDirectionalityTexture : register(t3, space2);
SamplerState LightmapDirectionalitySampler : register(s3, space2);
Texture2D<float4> EnvironmentTexture : register(t4, space2);
SamplerState EnvironmentSampler : register(s4, space2);

#include "BuiltinLighting.hlsli"

VertexOutput VSMain(VertexInput input, const uint instanceId : SV_InstanceID)
{
    VertexOutput output;
    float4x4 model = Model;
    float4x4 normalMatrix = NormalMatrix;
    float4 tint = Tint;
    if (LightingParameters.y > 0.5F)
    {
        const InstanceData instance = Instances[InstanceParameters.x + instanceId];
        model = instance.Model;
        normalMatrix = instance.NormalMatrix;
        tint = instance.Tint;
    }
    const float3 worldNormal = normalize(mul((float3x3)normalMatrix, input.Normal));
    const float4 worldPosition = mul(model, float4(input.Position, 1.0F));
    const float4 viewPosition = mul(View, worldPosition);
    output.Position = LightingParameters.y > 0.5F ? mul(Projection, viewPosition)
                                                  : mul(ModelViewProjection, float4(input.Position, 1.0F));
    output.WorldPosition = worldPosition.xyz;
    output.WorldNormal = worldNormal;
    output.ViewDepth = viewPosition.z;
    const float4 baseColor = input.VertexColor * tint;
    output.BaseColor = baseColor;
    output.LightmapUV = input.UV1 * VertexLightmapScaleOffset.xy + VertexLightmapScaleOffset.zw;
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

float SampleDirectionalShadow(const float3 worldPosition, const float viewDepth)
{
    const float cascadeCount = abs(DirectionalShadowParameters.x);
    if (cascadeCount < 0.5F)
        return 1.0F;
    uint cascade = 0U;
    cascade += viewDepth > DirectionalCascadeSplits.x;
    cascade += viewDepth > DirectionalCascadeSplits.y;
    cascade += viewDepth > DirectionalCascadeSplits.z;
    cascade = min(cascade, (uint)cascadeCount - 1U);
    const float4 clip = mul(DirectionalShadowMatrices[cascade], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    if (any(uv < 0.0F.xx) || any(uv > 1.0F.xx) || projected.z <= 0.0F || projected.z >= 1.0F)
        return 1.0F;
    const float visibility = SampleShadowPcf(
        DirectionalShadowTexture, DirectionalShadowSampler, uv, cascade,
        projected.z - DirectionalShadowParameters.z, DirectionalShadowParameters.w,
        DirectionalShadowParameters.x > 0.0F, float4(0.0F, 0.0F, 1.0F, 1.0F), false);
    return lerp(1.0F, visibility, saturate(DirectionalShadowParameters.y));
}

uint PointShadowFace(const float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
        return direction.x >= 0.0F ? 0U : 1U;
    if (absoluteDirection.y >= absoluteDirection.z)
        return direction.y >= 0.0F ? 2U : 3U;
    return direction.z >= 0.0F ? 4U : 5U;
}

float SampleLocalShadow(const uint lightIndex, const float3 worldPosition)
{
    if (LocalShadowParameters[lightIndex].x < 0.0F)
        return 1.0F;
    const bool spot = LocalLights[lightIndex].Parameters.y > 0.5F;
    uint matrixIndex = (uint)LocalShadowParameters[lightIndex].x;
    if (!spot)
        matrixIndex += PointShadowFace(worldPosition - LocalLights[lightIndex].PositionRange.xyz);
    const float4 clip = mul(LocalShadowMatrices[matrixIndex], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    const float visibility = SampleShadowPcf(
        LocalShadowTexture, LocalShadowSampler, uv, 0.0F,
        projected.z - LocalShadowParameters[lightIndex].w, 1.0F / 4096.0F,
        LocalShadowParameters[lightIndex].z > 0.5F, LocalShadowSampleBounds[matrixIndex], true);
    return lerp(1.0F, visibility, saturate(LocalShadowParameters[lightIndex].y));
}


float3 EvaluateBakedDiffuse(const VertexOutput input)
{
    const bool hasLightmap = ((uint)round(max(FragmentLightmapParameters.z, 0.0F)) & 1U) != 0U;
    if (!hasLightmap || FragmentLightmapParameters.x >= 4095.0F)
        return 0.0F.xxx;
    const float layer = FragmentLightmapParameters.x;
    const float4 lightmapSample =
        LightmapTexture.SampleLevel(LightmapSampler, float3(input.LightmapUV, layer), 0.0F);
    float3 irradiance = FragmentShadowMaskParameters.y > 0.5F ? DecodeRgbe(lightmapSample) : lightmapSample.rgb;
    const float4 directionality = LightmapDirectionalityTexture.SampleLevel(
        LightmapDirectionalitySampler, float3(input.LightmapUV, layer), 0.0F);
    const float3 dominantDirection = SafeNormalize(directionality.xyz * 2.0F - 1.0F, input.WorldNormal);
    const float directionalResponse = saturate(dot(normalize(input.WorldNormal), dominantDirection)) * 2.0F;
    irradiance *= lerp(1.0F, directionalResponse, saturate(directionality.w));
    return saturate(input.BaseColor.rgb) * irradiance / Pi;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float3 normal = SafeNormalize(input.WorldNormal, float3(0.0F, 0.0F, 1.0F));
    const float3 viewDirection = SafeNormalize(CameraPositionExposure.xyz - input.WorldPosition, normal);
    const float3 baseColor = saturate(input.BaseColor.rgb);
    // Match the default GBuffer surface: dielectric, roughness 0.65, specular level 0.5, AO 1.
    const float roughness = 0.65F;
    const float shadow = SampleDirectionalShadow(input.WorldPosition, input.ViewDepth);
    float3 directLighting = EvaluateDirectLighting(
        normal, viewDirection,
        SafeNormalize(-DirectionalDirectionEnabled.xyz, float3(0.0F, 1.0F, 0.0F)),
        DirectionalColorIntensity.rgb * DirectionalColorIntensity.a * DirectionalDirectionEnabled.w * shadow,
        baseColor, 0.0F, roughness, 0.5F);
    const uint localLightCount = min((uint)max(LocalLightCounts.x, 0.0F), 62U);
    for (uint lightIndex = 0U; lightIndex < localLightCount; ++lightIndex)
    {
        const float3 toLight = LocalLights[lightIndex].PositionRange.xyz - input.WorldPosition;
        const float distanceSquared = dot(toLight, toLight);
        const float distance = sqrt(max(distanceSquared, 1.0e-8F));
        const float range = max(LocalLights[lightIndex].PositionRange.w, 0.0001F);
        if (distance >= range)
            continue;
        const float3 direction = toLight / distance;
        const float normalizedDistance = distance / range;
        const float rangeFade = saturate(1.0F - pow(normalizedDistance, 4.0F));
        float attenuation = rangeFade * rangeFade / max(distanceSquared, 0.01F);
        if (LocalLights[lightIndex].Parameters.y > 0.5F)
        {
            const float coneCosine = dot(SafeNormalize(LocalLights[lightIndex].DirectionOuter.xyz,
                                                       float3(0.0F, 0.0F, 1.0F)), -direction);
            attenuation *= smoothstep(LocalLights[lightIndex].DirectionOuter.w,
                                      max(LocalLights[lightIndex].Parameters.x,
                                          LocalLights[lightIndex].DirectionOuter.w + 1.0e-4F), coneCosine);
        }
        const float3 radiance = LocalLights[lightIndex].ColorIntensity.rgb *
                                LocalLights[lightIndex].ColorIntensity.a * attenuation *
                                SampleLocalShadow(lightIndex, input.WorldPosition);
        directLighting += EvaluateDirectLighting(normal, viewDirection, direction, radiance,
                                                 baseColor, 0.0F, roughness, 0.5F);
    }
    const bool hasLightmap = ((uint)round(max(FragmentLightmapParameters.z, 0.0F)) & 1U) != 0U;
    const float3 diffuse = hasLightmap ? EvaluateBakedDiffuse(input)
                                      : baseColor * EvaluateEnvironmentDiffuse(normal) * EnvironmentParameters.y / Pi;
    const float noV = saturate(dot(normal, viewDirection));
    const float2 integratedBrdf = ApproximateIntegratedBrdf(noV, roughness);
    const float3 reflectionRadiance = SampleEnvironment(reflect(-viewDirection, normal),
                                                        roughness * EnvironmentParameters.w) * EnvironmentParameters.z;
    const float3 specular = reflectionRadiance *
                           (FresnelSchlickRoughness(noV, 0.04F.xxx, roughness) * integratedBrdf.x + integratedBrdf.y);
    const float3 ambient = baseColor * AmbientColorIntensity.rgb * AmbientColorIntensity.a / Pi;
    return float4((ambient + diffuse + specular + directLighting) * CameraPositionExposure.w, input.BaseColor.a);
}
