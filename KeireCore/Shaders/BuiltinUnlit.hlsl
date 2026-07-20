struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Color : TEXCOORD1;
    float3 Normal : TEXCOORD2;
};

struct VertexOutput
{
    float4 Color : TEXCOORD0;
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
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float3 worldNormal = normalize(mul((float3x3)NormalMatrix, input.Normal));
    output.Position = mul(ModelViewProjection, float4(input.Position, 1.0F));
    const float4 baseColor = float4(input.Color, 1.0F) * Tint;
    if (LightingParameters.x < 0.5F)
    {
        output.Color = baseColor;
        return output;
    }

    float3 lighting = AmbientAndExposure.rgb;
    if (LightDirection.w > 0.5F)
    {
        const float diffuse = saturate(dot(worldNormal, normalize(-LightDirection.xyz)));
        lighting += LightColor.rgb * LightColor.a * diffuse;
    }
    output.Color = float4(saturate(baseColor.rgb * lighting * AmbientAndExposure.a), baseColor.a);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return input.Color;
}
