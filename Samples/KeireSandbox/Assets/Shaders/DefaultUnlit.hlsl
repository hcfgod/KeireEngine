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
    float4 Position : SV_Position;
};

cbuffer ObjectData : register(b0, space1)
{
    float4x4 Model;
    float4x4 View;
    float4x4 Projection;
    float4x4 NormalMatrix;
};

cbuffer SceneData : register(b0, space3)
{
    float4 AmbientColorIntensity;
    float4 DirectionalColorIntensity;
    float4 DirectionalDirectionExposure;
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

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(Model, float4(input.Position, 1.0F));
    const float4 viewPosition = mul(View, worldPosition);
    output.Position = mul(Projection, viewPosition);
    output.Normal = normalize(mul((float3x3)NormalMatrix, input.Normal));
    output.Tangent = normalize(mul((float3x3)Model, input.Tangent.xyz));
    output.Bitangent = normalize(cross(output.Normal, output.Tangent) * input.Tangent.w);
    output.ViewDirection = normalize(-viewPosition.xyz);
    output.UV0 = input.UV0;
    output.Color = input.Color;
    return output;
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

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float4 baseColor = MainTexture.Sample(MainSampler, input.UV0) * input.Color * Tint;
    float3 tangentNormal = NormalTexture.Sample(NormalSampler, input.UV0).xyz * 2.0F - 1.0F;
    tangentNormal.xy *= NormalScale.x;
    const float3 normal = normalize(input.Tangent * tangentNormal.x + input.Bitangent * tangentNormal.y +
                                    input.Normal * tangentNormal.z);
    const float3 viewDirection = normalize(input.ViewDirection);
    const float3 lightDirection = normalize(-DirectionalDirectionExposure.xyz);
    const float3 halfway = normalize(viewDirection + lightDirection);
    const float4 metallicRoughness = MetallicRoughnessTexture.Sample(MetallicRoughnessSampler, input.UV0);
    const float metallic = saturate(metallicRoughness.b * MetallicFactor.x);
    const float roughness = clamp(metallicRoughness.g * RoughnessFactor.x, 0.045F, 1.0F);
    const float occlusionSample = OcclusionTexture.Sample(OcclusionSampler, input.UV0).r;
    const float occlusion = lerp(1.0F, occlusionSample, saturate(OcclusionStrength.x));
    const float3 emissive = EmissiveTexture.Sample(EmissiveSampler, input.UV0).rgb * EmissiveFactor.rgb;

    const float normalLight = saturate(dot(normal, lightDirection));
    const float normalView = saturate(dot(normal, viewDirection));
    const float3 reflectance = lerp(0.04F.xxx, baseColor.rgb, metallic);
    const float3 fresnel = FresnelSchlick(saturate(dot(viewDirection, halfway)), reflectance);
    const float distribution = DistributionGgx(normal, halfway, roughness);
    const float geometry = GeometrySchlickGgx(normalView, roughness) *
                           GeometrySchlickGgx(normalLight, roughness);
    const float3 specular = distribution * geometry * fresnel / max(4.0F * normalView * normalLight, 0.0001F);
    const float3 diffuse = (1.0F - fresnel) * (1.0F - metallic) * baseColor.rgb / Pi;
    const float3 direct = (diffuse + specular) * DirectionalColorIntensity.rgb *
                          DirectionalColorIntensity.a * normalLight;
    const float3 ambient = baseColor.rgb * AmbientColorIntensity.rgb * AmbientColorIntensity.a * occlusion;
    const float3 color = (ambient + direct + emissive) * DirectionalDirectionExposure.w;
    return float4(color, baseColor.a);
}
