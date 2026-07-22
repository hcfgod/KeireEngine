struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

cbuffer SkyConstants : register(b0, space3)
{
    float4x4 InverseProjection;
    float4x4 InverseView;
    float4 SkyParameters;
};

Texture2D EnvironmentTexture : register(t0, space2);
SamplerState EnvironmentSampler : register(s0, space2);

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1) & 2, vertexId & 2);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

float3 DecodeRgbe(float4 sampleValue)
{
    const float exponent = sampleValue.a * 255.0F - 136.0F;
    return sampleValue.rgb * (255.0F * exp2(exponent));
}

float3 AcesFitted(float3 value)
{
    const float3 numerator = value * (2.51F * value + 0.03F);
    const float3 denominator = value * (2.43F * value + 0.59F) + 0.14F;
    return saturate(numerator / denominator);
}

float2 CubemapAtlasUv(float3 direction, int layout)
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

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float2 clip = input.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F);
    const float4 viewPosition = mul(InverseProjection, float4(clip, 1.0F, 1.0F));
    float3 direction = normalize(mul((float3x3)InverseView, viewPosition.xyz / max(abs(viewPosition.w), 1.0e-6F)));
    const float rotation = radians(SkyParameters.x);
    const float sineRotation = sin(rotation);
    const float cosineRotation = cos(rotation);
    direction.xz = float2(direction.x * cosineRotation - direction.z * sineRotation,
                          direction.x * sineRotation + direction.z * cosineRotation);
    const int layout = ((int)SkyParameters.w) & 15;
    const float2 uv = layout <= 1
                          ? float2(0.5F + atan2(direction.x, direction.z) / 6.28318530718F,
                                   0.5F - asin(clamp(direction.y, -1.0F, 1.0F)) / 3.14159265359F)
                          : CubemapAtlasUv(direction, layout);
    const float4 sampleValue = EnvironmentTexture.SampleLevel(EnvironmentSampler, uv, 0.0F);
    const float3 radiance = SkyParameters.w >= 16.0F ? DecodeRgbe(sampleValue) : sampleValue.rgb;
    return float4(AcesFitted(radiance * SkyParameters.y * SkyParameters.z), 1.0F);
}
