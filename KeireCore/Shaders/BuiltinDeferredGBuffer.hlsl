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
    float3 WorldNormal : TEXCOORD0;
    float4 BaseColor : TEXCOORD1;
    float2 LightmapUV : TEXCOORD2;
    nointerpolation uint PackedLayers : TEXCOORD3;
    nointerpolation uint PackedSpatial : TEXCOORD4;
    nointerpolation float ReceiveShadows : TEXCOORD5;
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
    const float4 worldPosition = mul(model, float4(input.Position, 1.0F));
    output.Position = LightingParameters.y > 0.5F
                          ? mul(Projection, mul(View, worldPosition))
                          : mul(ModelViewProjection, float4(input.Position, 1.0F));
    output.WorldNormal = normalize(mul((float3x3)normalMatrix, input.Normal));
    output.BaseColor = input.VertexColor * tint;
    output.LightmapUV = input.UV1 * LightDirection.xy + LightDirection.zw;
    const bool hasLightmap = ((uint)round(max(LightColor.z, 0.0F)) & 1U) != 0U;
    const uint lightmapLayer = hasLightmap && LightColor.x < 4095.0F ? (uint)LightColor.x + 1U : 0U;
    const uint shadowMaskLayer = hasLightmap && LightColor.y < 4095.0F ? (uint)LightColor.y + 1U : 0U;
    output.PackedLayers = lightmapLayer + shadowMaskLayer * 4096U;
    output.PackedSpatial = (uint)LightingParameters.w + (uint)LightingParameters.z * 65536U;
    output.ReceiveShadows = LightingParameters.x;
    return output;
}

struct GBufferOutput
{
    float4 BaseColorMetallic : SV_Target0;
    float4 NormalRoughness : SV_Target1;
    float4 Material : SV_Target2;
    float4 Lighting : SV_Target3;
};

GBufferOutput PSMain(VertexOutput input)
{
    GBufferOutput output;
    output.BaseColorMetallic = float4(saturate(input.BaseColor.rgb), 0.0F);
    output.NormalRoughness = float4(normalize(input.WorldNormal) * 0.5F + 0.5F, 0.65F);
    // R=ambient occlusion, G=specular level, B=emissive mask, A=occupied/material flags.
    // Keep occupied flags above the 0.5 cutoff even after UNORM quantization.
    output.Material = float4(1.0F, 0.5F, 0.0F, input.ReceiveShadows > 0.5F ? 1.0F : 0.75F);
    output.Lighting = float4(input.LightmapUV, (float)input.PackedLayers, (float)input.PackedSpatial);
    return output;
}
