struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV0 : TEXCOORD2;
    float4 Color : TEXCOORD3;
    float4 Tangent : TEXCOORD4;
    float2 UV1 : TEXCOORD5;
};

struct VertexOutput
{
    float2 UV0 : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float4 Position : SV_Position;
};

cbuffer ObjectData : register(b0, space1)
{
    float4x4 Model;
    float4x4 View;
    float4x4 Projection;
    float4x4 NormalMatrix;
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

cbuffer MaterialData : register(b1, space3)
{
    float4 Tint;
};

Texture2D MainTexture : register(t0, space2);
SamplerState MainSampler : register(s0, space2);

VertexOutput VSMain(VertexInput input, const uint instanceId : SV_InstanceID)
{
    VertexOutput output;
    const InstanceData instance = Instances[InstanceParameters.x + instanceId];
    const float4 worldPosition = mul(instance.Model, float4(input.Position, 1.0F));
    output.Position = mul(Projection, mul(View, worldPosition));
    output.UV0 = input.UV0;
    output.Color = input.Color * instance.Tint;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return MainTexture.Sample(MainSampler, input.UV0) * input.Color * Tint;
}
