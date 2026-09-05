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

float3 DecodeRgbe(const float4 sampleValue)
{
    return sampleValue.rgb * (255.0F * exp2(sampleValue.a * 255.0F - 136.0F));
}

float3 DecodeLightingSample(const float4 sampleValue, const bool rgbe)
{
    return rgbe ? DecodeRgbe(sampleValue) : sampleValue.rgb;
}

float3 EvaluateSphericalHarmonics(const float3 coefficients[9], float3 normal)
{
    normal = SafeNormalize(normal, float3(0.0F, 1.0F, 0.0F));
    const float x = normal.x;
    const float y = normal.y;
    const float z = normal.z;
    return max(coefficients[0] * 0.282095F + coefficients[1] * (0.488603F * y) +
                   coefficients[2] * (0.488603F * z) + coefficients[3] * (0.488603F * x) +
                   coefficients[4] * (1.092548F * x * y) + coefficients[5] * (1.092548F * y * z) +
                   coefficients[6] * (0.315392F * (3.0F * y * y - 1.0F)) +
                   coefficients[7] * (1.092548F * x * z) + coefficients[8] * (0.546274F * (z * z - x * x)),
               0.0F.xxx);
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
        const float inverse = rcp(max(absoluteDirection.x, 1.0e-6F));
        face = direction.x >= 0.0F ? 0 : 1;
        local = direction.x >= 0.0F ? float2(-direction.z, -direction.y) * inverse
                                    : float2(direction.z, -direction.y) * inverse;
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        const float inverse = rcp(max(absoluteDirection.y, 1.0e-6F));
        face = direction.y >= 0.0F ? 2 : 3;
        local = direction.y >= 0.0F ? float2(direction.x, direction.z) * inverse
                                    : float2(direction.x, -direction.z) * inverse;
    }
    else
    {
        const float inverse = rcp(max(absoluteDirection.z, 1.0e-6F));
        face = direction.z >= 0.0F ? 4 : 5;
        local = direction.z >= 0.0F ? float2(direction.x, -direction.y) * inverse
                                    : float2(-direction.x, -direction.y) * inverse;
    }
    local = local * 0.5F + 0.5F;
    uint textureWidth;
    uint textureHeight;
    EnvironmentTexture.GetDimensions(textureWidth, textureHeight);
    const float2 grid = layout == 4 ? float2(6.0F, 1.0F)
                        : layout == 5 ? float2(1.0F, 6.0F)
                        : layout == 2 ? float2(4.0F, 3.0F)
                                      : float2(3.0F, 4.0F);
    const float2 cellPixels = max(float2(textureWidth, textureHeight) / grid, 1.0F.xx);
    local = clamp(local, 0.5F / cellPixels, 1.0F - 0.5F / cellPixels);
    if (layout == 4)
        return float2((face + local.x) / 6.0F, local.y);
    if (layout == 5)
        return float2(local.x, (face + local.y) / 6.0F);
    const int2 horizontalCells[6] = {int2(2, 1), int2(0, 1), int2(1, 0), int2(1, 2), int2(1, 1), int2(3, 1)};
    const int2 verticalCells[6] = {int2(2, 1), int2(0, 1), int2(1, 0), int2(1, 2), int2(1, 1), int2(1, 3)};
    return layout == 2 ? (float2(horizontalCells[face]) + local) / float2(4.0F, 3.0F)
                       : (float2(verticalCells[face]) + local) / float2(3.0F, 4.0F);
}

float3 SampleEnvironment(float3 direction, const float level)
{
    direction = RotateEnvironmentDirection(SafeNormalize(direction, float3(0.0F, 0.0F, 1.0F)));
    const int encoding = (int)EnvironmentEncoding.x;
    const int layout = encoding & 15;
    const float2 uv = layout <= 1
                          ? float2(0.5F + atan2(direction.x, direction.z) / (2.0F * Pi),
                                   0.5F - asin(clamp(direction.y, -1.0F, 1.0F)) / Pi)
                          : CubemapAtlasUv(direction, layout);
    return DecodeLightingSample(EnvironmentTexture.SampleLevel(EnvironmentSampler, uv, max(level, 0.0F)),
                                encoding >= 16);
}

float3 EvaluateEnvironmentDiffuse(const float3 normal)
{
    float3 coefficients[9];
    [unroll]
    for (uint index = 0U; index < 9U; ++index)
        coefficients[index] = DiffuseIrradiance[index].rgb;
    return EvaluateSphericalHarmonics(coefficients, RotateEnvironmentDirection(normal));
}
