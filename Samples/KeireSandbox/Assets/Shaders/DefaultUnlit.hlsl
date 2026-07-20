struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV0 : TEXCOORD2;
    float4 Color : TEXCOORD3;
};

struct VertexOutput
{
    float3 Normal : TEXCOORD0;
    float2 UV0 : TEXCOORD1;
    float4 Color : TEXCOORD2;
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
};

Texture2D MainTexture : register(t0, space2);
SamplerState MainSampler : register(s0, space2);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(Model, float4(input.Position, 1.0F));
    output.Position = mul(Projection, mul(View, worldPosition));
    output.Normal = normalize(mul((float3x3)NormalMatrix, input.Normal));
    output.UV0 = input.UV0;
    output.Color = input.Color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float3 normal = normalize(input.Normal);
    const float directional = saturate(dot(normal, -DirectionalDirectionExposure.xyz));
    const float3 lighting = AmbientColorIntensity.rgb * AmbientColorIntensity.a +
                            DirectionalColorIntensity.rgb * DirectionalColorIntensity.a * directional;
    const float4 surface = MainTexture.Sample(MainSampler, input.UV0) * input.Color * Tint;
    return float4(surface.rgb * lighting * DirectionalDirectionExposure.w, surface.a);
}
