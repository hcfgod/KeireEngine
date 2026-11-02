#include "DisplayTransfer.hlsli"

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
Texture2D BloomTexture : register(t3, space2);
SamplerState BloomSampler : register(s3, space2);

cbuffer ToneMapData : register(b0, space3)
{
    float4 ToneMapParameters;
    float4 BloomParameters;
    float4 PostGrade;
    float4 PostGradeGain;
    float4 PostLens;
    float4 PostGrain;
};

#include "BuiltinPostProcess.hlsli"

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

float3 ComposeBloom(const float2 uv, const float3 scene)
{
    if (BloomParameters.x <= 0.0F)
    {
        return max(scene, 0.0F.xxx);
    }
    const float3 bloom = BloomTexture.SampleLevel(BloomSampler, uv, 0.0F).rgb;
    return min(max(scene, 0.0F.xxx) + max(bloom, 0.0F.xxx) * BloomParameters.x, 65504.0F.xxx);
}

float3 SampleToneMapped(const float2 uv)
{
    return AcesFitted(ComposeBloom(uv, HdrSceneTexture.SampleLevel(HdrSceneSampler, uv, 0.0F).rgb));
}

float3 SampleTemporalColor(const float2 uv)
{
    return ComposeBloom(uv, HdrSceneTexture.SampleLevel(HdrSceneSampler, uv, 0.0F).rgb);
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

    // Reuse the existing center only at identical UVs; retain the clamped edge sample otherwise.
    const bool centerUvUnchanged = all(ClampSceneUv(uv) == uv);

    // Extend foreground motion across the one-pixel silhouette neighborhood. Background zero velocity
    // otherwise leaves stale edge history behind when a moving object reveals a pixel.
    float2 velocity = VelocityTexture.SampleLevel(VelocitySampler, uv, 0.0F).xy;
    [unroll]
    for (int vy = -1; vy <= 1; ++vy)
    {
        [unroll]
        for (int vx = -1; vx <= 1; ++vx)
        {
            if (vx == 0 && vy == 0 && centerUvUnchanged)
                continue;
            const float2 candidate = VelocityTexture.SampleLevel(VelocitySampler,
                ClampSceneUv(uv + ToneMapParameters.xy * float2(vx, vy)), 0.0F).xy;
            if (dot(candidate, candidate) > dot(velocity, velocity))
                velocity = candidate;
        }
    }
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
            if (x == 0 && y == 0 && centerUvUnchanged)
                continue;
            const float3 sampleValue = SampleTemporalColor(ClampSceneUv(uv + ToneMapParameters.xy * float2(x, y)));
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
    // Reject history in display-relative units, but retain and blend the unclipped HDR radiance.
    // Otherwise a bright highlight would reject every subpixel change or be lost before lens filtering.
    const float3 perceptualDifference = abs(AcesFitted(max(rawHistory, 0.0F.xxx)) -
        AcesFitted(max(nearestNeighborhood, 0.0F.xxx)));
    const float historyMismatch = max(max(perceptualDifference.r, perceptualDifference.g), perceptualDifference.b);
    const float velocityPixels = length(velocity / max(ToneMapParameters.xy, 1.0e-6F.xx));
    const float motionWeight = lerp(0.92F, 0.72F, saturate(velocityPixels * (1.0F / 32.0F)));
    const float historyValidity = 1.0F - saturate(historyMismatch * 32.0F);
    return clamp(lerp(center, history, motionWeight * historyValidity), 0.0F.xxx, 65504.0F.xxx);
}

// Lens depth is the material-aware depth/velocity pass, not geometry-only shadow depth.
// Reconstruct view distance locally so planetary world coordinates never lose precision here.
float LensViewDistance(const float2 uv)
{
    const int2 pixel = int2(ClampSceneUv(uv) / ToneMapParameters.xy);
    const float depth = TemporalHistoryTexture.Load(int3(pixel, 0)).r;
    const float4 clip = float4(uv * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), depth, 1.0F);
    const float4 view = PostGrade * clip.x + PostGradeGain * clip.y + PostLens * clip.z + PostGrain;
    return max(abs(view.z) / max(abs(view.w), 1.0e-8F), 1.0e-4F);
}

float LensRadius(const float distance)
{
    return clamp(BloomParameters.x / distance - BloomParameters.y, -BloomParameters.z, BloomParameters.z);
}

float3 LensColor(const float2 uv)
{
    const float3 scene = max(HdrSceneTexture.SampleLevel(HdrSceneSampler, uv, 0.0F).rgb, 0.0F.xxx);
    const float3 bloom = ToneMapParameters.w > 0.0F ?
        max(BloomTexture.SampleLevel(BloomSampler, uv, 0.0F).rgb, 0.0F.xxx) * ToneMapParameters.w : 0.0F.xxx;
    return min(scene + bloom, 65504.0F.xxx);
}

float4 ApplyHdrMotionBlur(const float2 uv)
{
    const float centerDepth = LensViewDistance(uv);
    float2 velocity = VelocityTexture.SampleLevel(VelocitySampler, uv, 0.0F).xy * BloomParameters.w;
    const float velocityPixels = length(velocity / ToneMapParameters.xy);
    velocity *= min(1.0F, 32.0F / max(velocityPixels, 1.0e-5F));
    if (velocityPixels < 0.5F)
    {
        return float4(LensColor(uv), HdrSceneTexture.SampleLevel(HdrSceneSampler, uv, 0.0F).a);
    }
    float3 sum = LensColor(uv);
    float total = 1.0F;
    // Fixed bounded work, independent of aperture, shutter length, or world coordinate magnitude.
    [unroll]
    for (int index = 0; index < 24; ++index)
    {
        const float fraction = (float(index) + 0.5F) / 24.0F;
        const float2 offset = velocity * (fraction - 0.5F);
        const float2 sampleUv = uv + offset;
        if (any(sampleUv < ToneMapParameters.xy * 0.5F) || any(sampleUv > 1.0F.xx - ToneMapParameters.xy * 0.5F))
        {
            continue;
        }
        const float sampleDepth = LensViewDistance(sampleUv);
        // Visible-surface rejection prevents streaks across disocclusion edges.
        const float relativeDepth = abs(sampleDepth - centerDepth) / max(centerDepth, 0.01F);
        const float sampleWeight = 1.0F - smoothstep(0.01F, 0.05F, relativeDepth);
        sum += LensColor(sampleUv) * sampleWeight;
        total += sampleWeight;
    }
    return float4(sum / total, HdrSceneTexture.SampleLevel(HdrSceneSampler, uv, 0.0F).a);
}

#include "BuiltinDepthOfField.hlsli"

float4 PSMain(const VertexOutput input) : SV_Target0
{
    if (ToneMapParameters.z > 14.5F)
    {
        const float4 source = HdrSceneTexture.SampleLevel(HdrSceneSampler, input.UV, 0.0F);
        return float4(ComposeBloom(input.UV, source.rgb), source.a);
    }
    if (ToneMapParameters.z > 13.5F)
    {
        return DofComposite(input.UV);
    }
    if (ToneMapParameters.z > 11.5F)
    {
        return DofGather(input.UV, ToneMapParameters.z < 12.5F);
    }
    if (ToneMapParameters.z > 10.5F)
    {
        return DofDilateBounds(int2(input.Position.xy));
    }
    if (ToneMapParameters.z > 9.5F)
    {
        return DofTileBounds(int2(input.Position.xy));
    }
    if (ToneMapParameters.z > 7.5F)
    {
        return DofPrefilter(int2(input.Position.xy), ToneMapParameters.z < 8.5F);
    }
    if (ToneMapParameters.z > 5.5F)
    {
        return ApplyHdrMotionBlur(input.UV);
    }
    if (ToneMapParameters.z > 4.5F)
    {
        const float4 source = HdrSceneTexture.SampleLevel(HdrSceneSampler, input.UV, 0.0F);
        return float4(ApplyTaa(input.UV, ComposeBloom(input.UV, source.rgb)), saturate(source.a));
    }
    if (ToneMapParameters.z > 3.5F)
    {
        return ApplyBuiltinPostProcessing(input.UV, uint2(input.Position.xy));
    }
    if (ToneMapParameters.z > 2.5F)
    {
        const float4 source = HdrSceneTexture.SampleLevel(HdrSceneSampler, input.UV, 0.0F);
        const uint2 pixel = uint2(input.Position.xy);
        uint hash = pixel.x * 1973U + pixel.y * 9277U + 0x68bc21ebU;
        hash = (hash ^ (hash >> 16U)) * 0x7feb352dU;
        hash = (hash ^ (hash >> 15U)) * 0x846ca68bU;
        hash ^= hash >> 16U;
        const float noise = (float(hash & 65535U) + 0.5F) / 65536.0F - 0.5F;
        const float3 encoded = float3(DitherDisplaySrgb(source.r, noise), DitherDisplaySrgb(source.g, noise),
            DitherDisplaySrgb(source.b, noise));
        return float4(encoded, source.a);
    }
    const float4 hdr = HdrSceneTexture.SampleLevel(HdrSceneSampler, input.UV, 0.0F);
    const float3 toneMapped = AcesFitted(ComposeBloom(input.UV, hdr.rgb));
    const float3 color = ToneMapParameters.z > 0.5F ? ApplyFxaa(input.UV, toneMapped) : toneMapped;
    return float4(color, saturate(hdr.a));
}
