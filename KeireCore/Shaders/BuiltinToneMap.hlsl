struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

Texture2D HdrSceneTexture : register(t0, space2);
SamplerState HdrSceneSampler : register(s0, space2);
Texture2D TemporalHistoryTexture : register(t1, space2);
SamplerState TemporalHistorySampler : register(s1, space2);
Texture2D VelocityTexture : register(t2, space2);
SamplerState VelocitySampler : register(s2, space2);

cbuffer ToneMapData : register(b0, space3)
{
    float4 ToneMapParameters;
};

VertexOutput VSMain(const uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

float3 AcesFitted(const float3 color)
{
    const float3 numerator = color * (2.51F * color + 0.03F);
    const float3 denominator = color * (2.43F * color + 0.59F) + 0.14F;
    return saturate(numerator / denominator);
}

float3 SampleToneMapped(const float2 uv)
{
    return AcesFitted(max(HdrSceneTexture.SampleLevel(HdrSceneSampler, uv, 0.0F).rgb, 0.0F.xxx));
}

float Luminance(const float3 color)
{
    return dot(color, float3(0.299F, 0.587F, 0.114F));
}

float3 ApplyFxaa(const float2 uv, const float3 center)
{
    const float2 texel = ToneMapParameters.xy;
    const float3 northWest = SampleToneMapped(uv + texel * float2(-1.0F, -1.0F));
    const float3 northEast = SampleToneMapped(uv + texel * float2(1.0F, -1.0F));
    const float3 southWest = SampleToneMapped(uv + texel * float2(-1.0F, 1.0F));
    const float3 southEast = SampleToneMapped(uv + texel * float2(1.0F, 1.0F));
    const float lumaNorthWest = Luminance(northWest);
    const float lumaNorthEast = Luminance(northEast);
    const float lumaSouthWest = Luminance(southWest);
    const float lumaSouthEast = Luminance(southEast);
    const float lumaCenter = Luminance(center);
    const float lumaMinimum = min(lumaCenter, min(min(lumaNorthWest, lumaNorthEast), min(lumaSouthWest, lumaSouthEast)));
    const float lumaMaximum = max(lumaCenter, max(max(lumaNorthWest, lumaNorthEast), max(lumaSouthWest, lumaSouthEast)));

    float2 direction;
    direction.x = -((lumaNorthWest + lumaNorthEast) - (lumaSouthWest + lumaSouthEast));
    direction.y = (lumaNorthWest + lumaSouthWest) - (lumaNorthEast + lumaSouthEast);
    const float directionReduce = max((lumaNorthWest + lumaNorthEast + lumaSouthWest + lumaSouthEast) *
                                          (0.25F * (1.0F / 8.0F)),
                                      1.0F / 128.0F);
    const float reciprocalMinimum = 1.0F / (min(abs(direction.x), abs(direction.y)) + directionReduce);
    direction = clamp(direction * reciprocalMinimum, -8.0F.xx, 8.0F.xx) * texel;

    const float3 resultA = 0.5F * (SampleToneMapped(uv + direction * (1.0F / 3.0F - 0.5F)) +
                                   SampleToneMapped(uv + direction * (2.0F / 3.0F - 0.5F)));
    const float3 resultB = resultA * 0.5F +
                           0.25F * (SampleToneMapped(uv + direction * -0.5F) +
                                    SampleToneMapped(uv + direction * 0.5F));
    const float lumaResultB = Luminance(resultB);
    return lumaResultB < lumaMinimum || lumaResultB > lumaMaximum ? resultA : resultB;
}

float2 ClampSceneUv(const float2 uv)
{
    const float2 halfTexel = ToneMapParameters.xy * 0.5F;
    return clamp(uv, halfTexel, 1.0F.xx - halfTexel);
}

float3 ApplyTaa(const float2 uv, const float3 center)
{
    if (ToneMapParameters.w < 0.5F)
        return center;

    const float2 velocity = VelocityTexture.SampleLevel(VelocitySampler, uv, 0.0F).xy;
    const float2 previousUv = uv - velocity;
    if (any(previousUv <= 0.0F.xx) || any(previousUv >= 1.0F.xx))
        return center;

    float3 neighborhoodMinimum = center;
    float3 neighborhoodMaximum = center;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float3 sampleValue = SampleToneMapped(ClampSceneUv(uv + ToneMapParameters.xy * float2(x, y)));
            neighborhoodMinimum = min(neighborhoodMinimum, sampleValue);
            neighborhoodMaximum = max(neighborhoodMaximum, sampleValue);
        }
    }

    const float3 neighborhoodExtent = neighborhoodMaximum - neighborhoodMinimum;
    const float3 clippingPadding = max(neighborhoodExtent * 0.5F, 0.02F.xxx);
    const float3 rawHistory = TemporalHistoryTexture.SampleLevel(TemporalHistorySampler, previousUv, 0.0F).rgb;
    const float3 history = clamp(rawHistory, neighborhoodMinimum - clippingPadding,
                                 neighborhoodMaximum + clippingPadding);
    const float3 nearestNeighborhood = clamp(rawHistory, neighborhoodMinimum, neighborhoodMaximum);
    const float historyMismatch = max(max(abs(rawHistory.r - nearestNeighborhood.r),
                                          abs(rawHistory.g - nearestNeighborhood.g)),
                                      abs(rawHistory.b - nearestNeighborhood.b));
    const float velocityPixels = length(velocity / max(ToneMapParameters.xy, 1.0e-6F.xx));
    const float motionWeight = lerp(0.92F, 0.72F, saturate(velocityPixels * (1.0F / 32.0F)));
    const float historyValidity = 1.0F - saturate(historyMismatch * 32.0F);
    return lerp(center, history, motionWeight * historyValidity);
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    const bool taa = ToneMapParameters.z > 1.5F;
    const float4 hdr = HdrSceneTexture.SampleLevel(HdrSceneSampler, input.UV, 0.0F);
    const float3 toneMapped = AcesFitted(max(hdr.rgb, 0.0F.xxx));
    const float3 color = taa ? ApplyTaa(input.UV, toneMapped)
                             : ToneMapParameters.z > 0.5F ? ApplyFxaa(input.UV, toneMapped) : toneMapped;
    return float4(color, saturate(hdr.a));
}
