struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

VertexOutput VSMain(const uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1U) & 2U, vertexId & 2U);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

Texture2D<float4> SceneColorTexture : register(t0, space2);
SamplerState SceneColorSampler : register(s0, space2);
Texture2D<float> SceneDepthTexture : register(t1, space2);
SamplerState SceneDepthSampler : register(s1, space2);
Texture2D<float4> NormalRoughnessTexture : register(t2, space2);
SamplerState NormalRoughnessSampler : register(s2, space2);
Texture2D<float4> MaterialTexture : register(t3, space2);
SamplerState MaterialSampler : register(s3, space2);
Texture2D<float2> VelocityTexture : register(t4, space2);
SamplerState VelocitySampler : register(s4, space2);
Texture2D<float4> HistoryTexture : register(t5, space2);
SamplerState HistorySampler : register(s5, space2);
Texture2D<float4> BaseColorMetallicTexture : register(t6, space2);
SamplerState BaseColorMetallicSampler : register(s6, space2);

struct IrradynSceneCard
{
    float4 CenterRadius;
    float4 RadianceDensity;
    float4 ExtentsFlags;
};

cbuffer IrradynConstants : register(b0, space3)
{
    float4x4 InverseViewProjection;
    float4x4 View;
    // stage, strength, ray count, ray steps
    float4 StageStrengthRaysSteps;
    // full-resolution ray radius, maximum world distance, temporal weight, history validity
    float4 RadiusDistanceHistory;
    // frame index, scene-card count, depth thickness, reserved
    float4 FrameCardsThickness;
    // trace resolution divisor, low-frequency receiver strength, reserved, reserved
    float4 TraceLayout;
};

cbuffer IrradynSceneCache : register(b1, space3)
{
    IrradynSceneCard SceneCards[16];
};

static const float Pi = 3.14159265359F;
static const float2 IrradynDisk[8] = {
    float2(0.2521F, 0.1593F),  float2(-0.4204F, 0.3105F), float2(0.1138F, -0.5182F),
    float2(0.6073F, 0.4217F),  float2(-0.6946F, -0.2381F), float2(-0.1837F, 0.7934F),
    float2(0.4862F, -0.7415F), float2(-0.8968F, 0.4026F)};

float3 SafeNormalize(const float3 value, const float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-10F ? value * rsqrt(lengthSquared) : fallback;
}

float3 ReconstructWorldPosition(const float2 uv, const float depth)
{
    const float2 clip = uv * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F);
    const float4 homogeneousWorld = mul(InverseViewProjection, float4(clip, depth, 1.0F));
    return homogeneousWorld.xyz / max(abs(homogeneousWorld.w), 1.0e-6F);
}

float ViewDistance(const float3 worldPosition)
{
    return abs(mul(View, float4(worldPosition, 1.0F)).z);
}

float RayVisibility(const float2 receiverUv, const float2 sourceUv, const float3 receiverWorld,
                    const float3 sourceWorld)
{
    const uint stepCount = min((uint)StageStrengthRaysSteps.w, 6U);
    const float receiverDistance = ViewDistance(receiverWorld);
    const float sourceDistance = ViewDistance(sourceWorld);
    [loop]
    for (uint step = 1U; step <= stepCount; ++step)
    {
        const float amount = (float)step / ((float)stepCount + 1.0F);
        const float2 sampleUv = lerp(receiverUv, sourceUv, amount);
        const float sampleDepth = SceneDepthTexture.SampleLevel(SceneDepthSampler, sampleUv, 0.0F);
        if (sampleDepth <= 0.0F || sampleDepth >= 1.0F)
            continue;
        const float sceneDistance = ViewDistance(ReconstructWorldPosition(sampleUv, sampleDepth));
        const float rayDistance = lerp(receiverDistance, sourceDistance, amount);
        if (sceneDistance + FrameCardsThickness.z < rayDistance)
            return 0.0F;
    }
    return 1.0F;
}

float3 EvaluateSceneCards(const float3 worldPosition, const float3 normal)
{
    float3 irradiance = 0.0F.xxx;
    const uint count = min((uint)FrameCardsThickness.y, 16U);
    [loop]
    for (uint index = 0U; index < count; ++index)
    {
        const IrradynSceneCard card = SceneCards[index];
        const float3 toCard = card.CenterRadius.xyz - worldPosition;
        const float distanceSquared = dot(toCard, toCard);
        const float radius = max(card.CenterRadius.w, 0.05F);
        if (distanceSquared > RadiusDistanceHistory.y * RadiusDistanceHistory.y || distanceSquared < 1.0e-4F)
            continue;
        const float3 direction = toCard * rsqrt(distanceSquared);
        const uint flags = (uint)round(max(card.ExtentsFlags.w, 0.0F));
        float participation = 1.0F;
        if ((flags & 8U) != 0U)
            participation = 0.22F;
        else if ((flags & 16U) != 0U)
            participation = 0.18F;
        else if ((flags & 2U) != 0U)
            participation = 0.32F;
        else if ((flags & 4U) != 0U)
            participation = 0.48F;
        const float geometry = saturate(dot(normal, direction));
        const float solidAngle = saturate(radius * radius / max(distanceSquared, radius * radius));
        irradiance += card.RadianceDensity.rgb * card.RadianceDensity.w * geometry * solidAngle * participation;
    }
    return irradiance;
}

float4 TraceIrradiance(const float2 uv)
{
    const float depth = SceneDepthTexture.SampleLevel(SceneDepthSampler, uv, 0.0F);
    if (depth <= 0.0F || depth >= 1.0F)
        return 0.0F.xxxx;

    uint width;
    uint height;
    SceneDepthTexture.GetDimensions(width, height);
    const uint tracingStride = max((uint)TraceLayout.x / 2U, 1U);
    const uint2 tracePixel = (uint2)(uv * float2(width, height));
    const uint pattern = (tracePixel.x % tracingStride) + (tracePixel.y % tracingStride) * tracingStride;
    const uint framePattern = (uint)FrameCardsThickness.x % (tracingStride * tracingStride);
    if (RadiusDistanceHistory.w > 0.5F && pattern != framePattern)
    {
        const float2 velocity = VelocityTexture.SampleLevel(VelocitySampler, uv, 0.0F).xy;
        const float2 previousUv = uv - velocity;
        if (all(previousUv > 0.0F.xx) && all(previousUv < 1.0F.xx))
            return HistoryTexture.SampleLevel(HistorySampler, previousUv, 0.0F);
    }
    const float2 inverseSize = rcp(max(float2(width, height), 1.0F.xx));
    const float3 worldPosition = ReconstructWorldPosition(uv, depth);
    const float4 material = MaterialTexture.SampleLevel(MaterialSampler, uv, 0.0F);
    float3 normal = SafeNormalize(
        NormalRoughnessTexture.SampleLevel(NormalRoughnessSampler, uv, 0.0F).xyz * 2.0F - 1.0F,
        float3(0.0F, 1.0F, 0.0F));
    if (material.a < 0.5F)
    {
        const float3 dx = ddx(worldPosition);
        const float3 dy = ddy(worldPosition);
        normal = SafeNormalize(cross(dx, dy), normal);
    }

    const float2 pixel = uv * float2(width, height);
    const float angle = frac(sin(dot(floor(pixel) + StageStrengthRaysSteps.xx * FrameCardsThickness.x,
                                     float2(12.9898F, 78.233F))) *
                             43758.5453F) *
                        (2.0F * Pi);
    float sineAngle;
    float cosineAngle;
    sincos(angle, sineAngle, cosineAngle);
    const float2x2 rotation = float2x2(cosineAngle, -sineAngle, sineAngle, cosineAngle);

    float3 irradiance = 0.0F.xxx;
    float totalWeight = 0.0F;
    const uint rayCount = min((uint)StageStrengthRaysSteps.z, 8U);
    [loop]
    for (uint index = 0U; index < rayCount; ++index)
    {
        const float radialScale = 0.35F + 0.65F * ((float)index + 1.0F) / max((float)rayCount, 1.0F);
        const float2 sourceUv =
            uv + mul(rotation, IrradynDisk[index]) * RadiusDistanceHistory.x * radialScale * inverseSize;
        if (any(sourceUv <= 0.0F.xx) || any(sourceUv >= 1.0F.xx))
            continue;
        const float sourceDepth = SceneDepthTexture.SampleLevel(SceneDepthSampler, sourceUv, 0.0F);
        if (sourceDepth <= 0.0F || sourceDepth >= 1.0F)
            continue;
        const float3 sourceWorld = ReconstructWorldPosition(sourceUv, sourceDepth);
        const float3 toSource = sourceWorld - worldPosition;
        const float distanceSquared = dot(toSource, toSource);
        if (distanceSquared <= 1.0e-4F || distanceSquared > RadiusDistanceHistory.y * RadiusDistanceHistory.y)
            continue;
        const float3 direction = toSource * rsqrt(distanceSquared);
        const float4 sourceMaterial = MaterialTexture.SampleLevel(MaterialSampler, sourceUv, 0.0F);
        const float3 sourceNormal = SafeNormalize(
            NormalRoughnessTexture.SampleLevel(NormalRoughnessSampler, sourceUv, 0.0F).xyz * 2.0F - 1.0F,
            -direction);
        const float receiverResponse = saturate(dot(normal, direction));
        const float sourceResponse = sourceMaterial.a >= 0.5F ? saturate(dot(sourceNormal, -direction)) : 0.35F;
        const float visibility = RayVisibility(uv, sourceUv, worldPosition, sourceWorld);
        const float weight = receiverResponse * sourceResponse * visibility / (1.0F + distanceSquared);
        if (weight <= 1.0e-5F)
            continue;
        irradiance += max(SceneColorTexture.SampleLevel(SceneColorSampler, sourceUv, 0.0F).rgb, 0.0F.xxx) * weight;
        totalWeight += weight;
    }
    if (totalWeight > 1.0e-5F)
        irradiance /= totalWeight;
    irradiance += EvaluateSceneCards(worldPosition, normal);
    irradiance *= StageStrengthRaysSteps.y;

    const float2 velocity = VelocityTexture.SampleLevel(VelocitySampler, uv, 0.0F).xy;
    const float2 previousUv = uv - velocity;
    if (RadiusDistanceHistory.w > 0.5F && all(previousUv > 0.0F.xx) && all(previousUv < 1.0F.xx))
    {
        const float4 history = HistoryTexture.SampleLevel(HistorySampler, previousUv, 0.0F);
        const float depthTolerance = max(FrameCardsThickness.z / max(ViewDistance(worldPosition), 1.0F), 0.002F);
        if (abs(history.a - depth) <= depthTolerance)
        {
            const float3 boundedHistory = clamp(history.rgb, irradiance * 0.25F, max(irradiance * 4.0F, 0.02F.xxx));
            irradiance = lerp(irradiance + boundedHistory * 0.04F, boundedHistory, RadiusDistanceHistory.z);
        }
    }
    return float4(max(irradiance, 0.0F.xxx), depth);
}

float3 BilateralResolve(const float2 uv, const float centerDepth, const float3 centerNormal)
{
    uint width;
    uint height;
    HistoryTexture.GetDimensions(width, height);
    const float2 texel = rcp(max(float2(width, height), 1.0F.xx));
    float3 result = 0.0F.xxx;
    float totalWeight = 0.0F;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUv = uv + float2(x, y) * texel;
            const float4 sampleValue = HistoryTexture.SampleLevel(HistorySampler, sampleUv, 0.0F);
            const float3 sampleNormal = SafeNormalize(
                NormalRoughnessTexture.SampleLevel(NormalRoughnessSampler, sampleUv, 0.0F).xyz * 2.0F - 1.0F,
                centerNormal);
            const float depthWeight = exp2(-abs(sampleValue.a - centerDepth) * 256.0F);
            const float normalWeight = pow(saturate(dot(centerNormal, sampleNormal)), 8.0F);
            const float kernelWeight = (x == 0 && y == 0) ? 4.0F : (x == 0 || y == 0) ? 2.0F : 1.0F;
            const float weight = depthWeight * normalWeight * kernelWeight;
            result += sampleValue.rgb * weight;
            totalWeight += weight;
        }
    }
    return totalWeight > 1.0e-5F ? result / totalWeight : 0.0F.xxx;
}

float4 ResolveIrradiance(const float2 uv)
{
    const float depth = SceneDepthTexture.SampleLevel(SceneDepthSampler, uv, 0.0F);
    if (depth <= 0.0F || depth >= 1.0F)
        return 0.0F.xxxx;
    const float4 material = MaterialTexture.SampleLevel(MaterialSampler, uv, 0.0F);
    const float3 normal = SafeNormalize(
        NormalRoughnessTexture.SampleLevel(NormalRoughnessSampler, uv, 0.0F).xyz * 2.0F - 1.0F,
        float3(0.0F, 1.0F, 0.0F));
    const float3 irradiance = BilateralResolve(uv, depth, normal);
    if (material.a >= 0.5F)
    {
        const float4 baseColorMetallic = BaseColorMetallicTexture.SampleLevel(BaseColorMetallicSampler, uv, 0.0F);
        const float ao = saturate(material.r);
        return float4(irradiance * baseColorMetallic.rgb * (1.0F - saturate(baseColorMetallic.a)) * ao / Pi, 0.0F);
    }
    // Forward-only hair, translucency, and volume/VFX pixels receive a deliberately low-frequency approximation.
    return float4(irradiance * TraceLayout.y, 0.0F);
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return StageStrengthRaysSteps.x < 1.5F ? TraceIrradiance(input.UV) : ResolveIrradiance(input.UV);
}
