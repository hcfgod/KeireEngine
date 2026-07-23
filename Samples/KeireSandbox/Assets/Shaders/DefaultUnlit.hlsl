static const float Pi = 3.14159265359F;

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV0 : TEXCOORD2;
    float4 Color : TEXCOORD3;
    float4 Tangent : TEXCOORD4;
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
    float4 Position : SV_Position;
};

cbuffer ObjectData : register(b0, space1)
{
    float4x4 Model;
    float4x4 View;
    float4x4 Projection;
    float4x4 NormalMatrix;
};

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

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(Model, float4(input.Position, 1.0F));
    const float4 viewPosition = mul(View, worldPosition);
    output.Position = mul(Projection, viewPosition);
    output.Normal = SafeNormal(mul((float3x3)NormalMatrix, input.Normal), float3(0.0F, 0.0F, 1.0F));
    float3 tangent = mul((float3x3)Model, input.Tangent.xyz);
    tangent -= output.Normal * dot(output.Normal, tangent);
    output.Tangent = SafeNormal(tangent, OrthogonalTangent(output.Normal));
    const float modelHandedness = determinant((float3x3)Model) < 0.0F ? -1.0F : 1.0F;
    const float tangentHandedness = abs(input.Tangent.w) > 0.0001F ? input.Tangent.w : 1.0F;
    output.Bitangent = SafeNormal(cross(output.Normal, output.Tangent) * tangentHandedness * modelHandedness,
                                  cross(output.Normal, OrthogonalTangent(output.Normal)));
    output.ViewDirection = normalize(mul(-viewPosition.xyz, (float3x3)View));
    output.UV0 = input.UV0;
    output.Color = input.Color;
    output.WorldPosition = worldPosition.xyz;
    output.ViewDepth = viewPosition.z;
    return output;
}

float SampleShadowPcf(Texture2DArray<float> textureValue, SamplerState samplerValue, const float2 uv,
                      const float layer, const float depth, const float inverseResolution, const bool soft)
{
    if (any(uv < 0.0F.xx) || any(uv > 1.0F.xx) || depth <= 0.0F || depth >= 1.0F)
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
            const float storedDepth =
                textureValue.SampleLevel(samplerValue, float3(uv + float2(x, y) * inverseResolution, layer), 0.0F);
            visibility += depth <= storedDepth ? 1.0F : 0.0F;
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
                                             DirectionalShadowParameters.w, DirectionalShadowParameters.x > 0.0F);
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
    uint layer = (uint)LocalShadowParameters[lightIndex].x;
    if (!spot)
    {
        const float3 fromLight = worldPosition - LocalLights[lightIndex].PositionRange.xyz;
        uint face = 0U;
        float majorDistance = 0.0F;
        PointShadowCoordinates(fromLight, face, majorDistance);
        layer += face;
    }
    const float4 clip = mul(LocalShadowMatrices[layer], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    const float visibility = SampleShadowPcf(LocalShadowTexture, LocalShadowSampler, uv, layer,
                                             projected.z - LocalShadowParameters[lightIndex].w, 1.0F / 1024.0F,
                                             LocalShadowParameters[lightIndex].z > 0.5F);
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
                                           DirectionalColorIntensity.rgb * DirectionalColorIntensity.a,
                                           baseColor.rgb, metallic, roughness);
    direct *= EvaluateDirectionalShadow(input.WorldPosition, input.ViewDepth);
    const uint localLightCount = min((uint)max(LocalLightCounts.x, 0.0F), 62U);
    for (uint lightIndex = 0U; lightIndex < localLightCount; ++lightIndex)
    {
        const float3 toLight = LocalLights[lightIndex].PositionRange.xyz - input.WorldPosition;
        const float distanceSquared = dot(toLight, toLight);
        const float distance = sqrt(max(distanceSquared, 1.0e-8F));
        const float range = max(LocalLights[lightIndex].PositionRange.w, 0.0001F);
        if (distance >= range)
            continue;
        const float3 localDirection = toLight / distance;
        const float normalizedDistance = distance / range;
        const float rangeFade = saturate(1.0F - normalizedDistance * normalizedDistance * normalizedDistance *
                                                    normalizedDistance);
        float attenuation = rangeFade * rangeFade / max(distanceSquared, 0.01F);
        if (LocalLights[lightIndex].Parameters.y > 0.5F)
        {
            const float3 spotDirection =
                SafeNormal(LocalLights[lightIndex].DirectionOuter.xyz, float3(0.0F, 0.0F, 1.0F));
            const float coneCosine = dot(spotDirection, -localDirection);
            const float outerCosine = LocalLights[lightIndex].DirectionOuter.w;
            const float innerCosine = max(LocalLights[lightIndex].Parameters.x, outerCosine + 0.0001F);
            attenuation *= smoothstep(outerCosine, innerCosine, coneCosine);
        }
        const float3 radiance =
            LocalLights[lightIndex].ColorIntensity.rgb * LocalLights[lightIndex].ColorIntensity.a * attenuation *
            EvaluateLocalShadow(lightIndex, input.WorldPosition);
        direct += EvaluateDirectLighting(normal, viewDirection, localDirection, radiance, baseColor.rgb, metallic,
                                         roughness);
    }
    const float3 ambient = baseColor.rgb * AmbientColorIntensity.rgb * AmbientColorIntensity.a * occlusion;
    const float3 color = (ambient + direct + emissive) * DirectionalDirectionExposure.w;
    if (SurfaceParameters.y > 1.5F)
        return float4(color * baseColor.a, baseColor.a);
    return float4(color, 1.0F);
}
