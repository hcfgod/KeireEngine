struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Color : TEXCOORD1;
    float3 Normal : TEXCOORD2;
};

struct VertexOutput
{
    float4 AmbientColor : TEXCOORD0;
    float4 DirectColor : TEXCOORD1;
    float3 WorldPosition : TEXCOORD2;
    float3 WorldNormal : TEXCOORD3;
    float4 BaseColor : TEXCOORD4;
    float ViewDepth : TEXCOORD5;
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
};

cbuffer ShadowConstants : register(b0, space3)
{
    float4 DirectionalShadowParameters;
    float4 DirectionalCascadeSplits;
    float4x4 DirectionalShadowMatrices[4];
    float4x4 LocalShadowMatrices[20];
    float4 LocalShadowParameters[62];
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

Texture2DArray<float> DirectionalShadowTexture : register(t0, space2);
SamplerState DirectionalShadowSampler : register(s0, space2);
Texture2DArray<float> LocalShadowTexture : register(t1, space2);
SamplerState LocalShadowSampler : register(s1, space2);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float3 worldNormal = normalize(mul((float3x3)NormalMatrix, input.Normal));
    const float4 worldPosition = mul(Model, float4(input.Position, 1.0F));
    const float4 viewPosition = mul(View, worldPosition);
    output.Position = mul(ModelViewProjection, float4(input.Position, 1.0F));
    output.WorldPosition = worldPosition.xyz;
    output.WorldNormal = worldNormal;
    output.ViewDepth = viewPosition.z;
    const float4 baseColor = float4(input.Color, 1.0F) * Tint;
    output.BaseColor = baseColor;
    if (LightingParameters.x < 0.5F)
    {
        output.AmbientColor = baseColor;
        output.DirectColor = 0.0F.xxxx;
        return output;
    }

    output.AmbientColor = float4(baseColor.rgb * AmbientAndExposure.rgb * AmbientAndExposure.a, baseColor.a);
    output.DirectColor = 0.0F.xxxx;
    if (LightDirection.w > 0.5F)
    {
        const float diffuse = saturate(dot(worldNormal, normalize(-LightDirection.xyz)));
        output.DirectColor =
            float4(baseColor.rgb * LightColor.rgb * LightColor.a * diffuse * AmbientAndExposure.a, 0.0F);
    }
    return output;
}

float SampleShadowPcf(Texture2DArray<float> textureValue, SamplerState samplerValue, const float2 uv,
                      const float layer, const float depth, const float inverseResolution, const bool soft)
{
    if (any(uv < 0.0F.xx) || any(uv > 1.0F.xx) || depth <= 0.0F || depth >= 1.0F)
        return 1.0F;
    const int radius = soft ? 1 : 0;
    float visibility = 0.0F;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float storedDepth = textureValue.SampleLevel(
                samplerValue, float3(uv + float2(x, y) * inverseResolution, layer), 0.0F);
            visibility += depth <= storedDepth ? 1.0F : 0.0F;
        }
    }
    return visibility / (float)((radius * 2 + 1) * (radius * 2 + 1));
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
        DirectionalShadowParameters.x > 0.0F);
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
    uint layer = (uint)LocalShadowParameters[lightIndex].x;
    if (!spot)
        layer += PointShadowFace(worldPosition - LocalLights[lightIndex].PositionRange.xyz);
    const float4 clip = mul(LocalShadowMatrices[layer], float4(worldPosition, 1.0F));
    const float3 projected = clip.xyz / clip.w;
    const float2 uv = float2(projected.x * 0.5F + 0.5F, -projected.y * 0.5F + 0.5F);
    const float visibility = SampleShadowPcf(
        LocalShadowTexture, LocalShadowSampler, uv, layer,
        projected.z - LocalShadowParameters[lightIndex].w, 1.0F / 1024.0F,
        LocalShadowParameters[lightIndex].z > 0.5F);
    return lerp(1.0F, visibility, saturate(LocalShadowParameters[lightIndex].y));
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float shadow = SampleDirectionalShadow(input.WorldPosition, input.ViewDepth);
    float3 localLighting = 0.0F.xxx;
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
            const float coneCosine = dot(normalize(LocalLights[lightIndex].DirectionOuter.xyz), -direction);
            attenuation *= smoothstep(LocalLights[lightIndex].DirectionOuter.w,
                                      LocalLights[lightIndex].Parameters.x, coneCosine);
        }
        const float diffuse = saturate(dot(normalize(input.WorldNormal), direction));
        localLighting += input.BaseColor.rgb * LocalLights[lightIndex].ColorIntensity.rgb *
                         LocalLights[lightIndex].ColorIntensity.a * attenuation * diffuse *
                         SampleLocalShadow(lightIndex, input.WorldPosition);
    }
    return float4(saturate(input.AmbientColor.rgb + input.DirectColor.rgb * shadow + localLighting),
                  input.AmbientColor.a);
}
