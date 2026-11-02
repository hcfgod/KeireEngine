#pragma once

// Shared default-forward and deferred lighting math. Keep radiance linear until the tone-map pass.
static const float Pi = 3.14159265359F;

float3 SafeNormalize(const float3 value, const float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-10F ? value * rsqrt(lengthSquared) : fallback;
}

float3 FresnelSchlick(const float cosine, const float3 f0)
{
    return f0 + (1.0F - f0) * pow(1.0F - saturate(cosine), 5.0F);
}

float DistributionGgx(const float noH, const float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator = noH * noH * (alphaSquared - 1.0F) + 1.0F;
    return alphaSquared / max(Pi * denominator * denominator, 1.0e-6F);
}

float GeometrySchlickGgx(const float noX, const float roughness)
{
    const float adjusted = roughness + 1.0F;
    const float k = adjusted * adjusted * 0.125F;
    return noX / max(noX * (1.0F - k) + k, 1.0e-6F);
}

float3 EvaluateDirectLighting(const float3 normal, const float3 viewDirection, const float3 lightDirection,
                              const float3 radiance, const float3 baseColor, const float metallic,
                              const float roughness, const float specularLevel)
{
    const float noL = saturate(dot(normal, lightDirection));
    const float noV = saturate(dot(normal, viewDirection));
    if (noL <= 0.0F || noV <= 0.0F)
        return 0.0F.xxx;
    const float3 halfVector = SafeNormalize(viewDirection + lightDirection, normal);
    const float noH = saturate(dot(normal, halfVector));
    const float voH = saturate(dot(viewDirection, halfVector));
    const float3 f0 = lerp((0.08F * specularLevel).xxx, baseColor, metallic);
    const float3 fresnel = FresnelSchlick(voH, f0);
    const float distribution = DistributionGgx(noH, roughness);
    const float geometry = GeometrySchlickGgx(noV, roughness) * GeometrySchlickGgx(noL, roughness);
    const float3 specular = distribution * geometry * fresnel / max(4.0F * noV * noL, 1.0e-5F);
    const float3 diffuse = (1.0F - fresnel) * (1.0F - metallic) * baseColor / Pi;
    return (diffuse + specular) * radiance * noL;
}

float3 FresnelSchlickRoughness(const float cosine, const float3 f0, const float roughness)
{
    return f0 + (max((1.0F - roughness).xxx, f0) - f0) * pow(1.0F - saturate(cosine), 5.0F);
}

float2 ApproximateIntegratedBrdf(const float noV, const float roughness)
{
    const float4 scale = float4(-1.0F, -0.0275F, -0.572F, 0.022F);
    const float4 bias = float4(1.0F, 0.0425F, 1.04F, -0.04F);
    const float4 fit = roughness * scale + bias;
    const float grazing = min(fit.x * fit.x, exp2(-9.28F * saturate(noV))) * fit.x + fit.y;
    return float2(-1.04F, 1.04F) * grazing + fit.zw;
}
