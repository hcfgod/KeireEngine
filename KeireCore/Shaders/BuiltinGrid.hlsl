struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

struct FragmentOutput
{
    float4 Color : SV_Target0;
    float Depth : SV_Depth;
};

cbuffer GridConstants : register(b0, space3)
{
    float4x4 InverseProjection;
    float4x4 InverseView;
    float4x4 ViewProjection;
    float4 GridParameters;
};

VertexOutput VSMain(const uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

float GridLine(const float2 worldPosition, const float spacing)
{
    const float2 coordinate = worldPosition / spacing;
    const float2 derivative = max(fwidth(coordinate), 1.0e-6F.xx);
    const float2 distanceToLine = abs(frac(coordinate - 0.5F) - 0.5F) / derivative;
    const float coverage = 1.0F - min(min(distanceToLine.x, distanceToLine.y), 1.0F);
    return coverage * saturate(1.0F / max(derivative.x, derivative.y));
}

FragmentOutput PSMain(const VertexOutput input)
{
    const float2 clip = input.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F);
    const float4 cameraWorld = mul(InverseView, float4(0.0F, 0.0F, 0.0F, 1.0F));
    const float4 viewPoint = mul(InverseProjection, float4(clip, 1.0F, 1.0F));
    const float3 rayDirection = normalize(mul((float3x3)InverseView, viewPoint.xyz / max(abs(viewPoint.w), 1.0e-6F)));
    const float rayDenominator = rayDirection.y;
    if (abs(rayDenominator) < 1.0e-6F)
        discard;
    const float rayDistance = -cameraWorld.y / rayDenominator;
    if (rayDistance <= 0.0F)
        discard;

    const float3 worldPosition = cameraWorld.xyz + rayDirection * rayDistance;
    const float minorLine = GridLine(worldPosition.xz, max(GridParameters.x, 1.0e-4F));
    const float majorLine = GridLine(worldPosition.xz, max(GridParameters.y, GridParameters.x));
    const float axisDerivative = max(max(fwidth(worldPosition.x), fwidth(worldPosition.z)), 1.0e-6F);
    const float axisLine = 1.0F - saturate(min(abs(worldPosition.x), abs(worldPosition.z)) / axisDerivative);
    const float opacity = max(max(minorLine * GridParameters.z, majorLine * GridParameters.w), axisLine * 0.9F);
    if (opacity <= 0.001F)
        discard;

    const float3 minorColor = float3(0.24F, 0.27F, 0.32F);
    const float3 majorColor = float3(0.32F, 0.36F, 0.43F);
    const float3 axisColor = float3(0.30F, 0.50F, 0.78F);
    float3 color = lerp(minorColor, majorColor, saturate(majorLine));
    color = lerp(color, axisColor, saturate(axisLine));

    const float4 projected = mul(ViewProjection, float4(worldPosition, 1.0F));
    if (projected.w <= 0.0F)
        discard;
    const float depth = projected.z / projected.w;
    if (depth < 0.0F || depth > 1.0F)
        discard;
    FragmentOutput output;
    output.Color = float4(color, saturate(opacity));
    output.Depth = depth;
    return output;
}
