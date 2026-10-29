// Diagnostic image only: production volume integration over a synthetic background, without Editor post effects.
#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#define KEIRE_CLOUD_NOISE_SPACE space0
#include "CloudVolume.hlsli"

RWStructuredBuffer<float4> Pixels : register(u0, space1);

cbuffer ProbeConstants : register(b0, space2)
{
    float4 ProbeDensity;
    float4 ProbeShape;
    uint4 ProbeQuality;
    float4 ProbeRegional;
    // Enabled, cloud type, type variation, regional base-height variation in metres.
    float4 ProbeWeather;
    float4 ProbeLighting;
};

AtmosphereMedium ProbeAtmosphere()
{
    AtmosphereMedium medium;
    medium.ObserverRadius = float4(0.0F, 6360.01F, 0.0F, 6360.0F);
    medium.SunHeight = float4(normalize(float3(0.2F, 0.7F, -0.4F)), 100.0F);
    medium.RayleighScale = float4(0.005802F, 0.013558F, 0.0331F, 8.0F);
    medium.Mie = float4(0.003996F, 0.00444F, 0.8F, 1.2F);
    medium.AbsorptionPeak = float4(0.00065F, 0.001881F, 0.000085F, 25.0F);
    medium.SolarWidth = float4(20.0F, 20.0F, 20.0F, 15.0F);
    medium.Controls = float4(1.0F, 8.0F, 4.0F, 0.004675F);
    return medium;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= ProbeQuality.x || id.y >= ProbeQuality.y)
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5F) / float2(ProbeQuality.xy);
    const float2 screen = uv * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F);
    const float aspect = float(ProbeQuality.x) / float(ProbeQuality.y);
    const float3 forward = float3(0.0F, 0.342020143F, -0.939692621F);
    const float3 up = float3(0.0F, 0.939692621F, 0.342020143F);
    const float3 ray = normalize(forward + up * screen.y * 0.466307658F +
        float3(screen.x * aspect * 0.466307658F, 0.0F, 0.0F));

    CloudVolumeParameters parameters = (CloudVolumeParameters)0;
    parameters.RaySamplePhase = 0.0F;
    parameters.WeatherControls = ProbeWeather;
    parameters.HeightProfile = float4(0.0F, 0.15F, 0.7F, 1.0F);
    parameters.BaseHeight = 1490.0F;
    parameters.Thickness = 1200.0F;
    parameters.PatternSize = 3000.0F;
    parameters.Coverage = ProbeDensity.w;
    parameters.Extinction = 0.004F * ProbeDensity.x;
    parameters.DetailAmount = ProbeDensity.y;
    parameters.ErosionAmount = VolumeCloudErosionAmount(ProbeRegional.w, ProbeDensity.y);
    parameters.Anisotropy = 0.65F;
    parameters.Seed = 1U;
    parameters.WindOffset = float3(0.0F, 10.0F, 9.0F);
    parameters.ViewSteps = ProbeQuality.z;
    parameters.ScatteringAlbedo = 0.95F.xxx;
    parameters.ShadowSteps = ProbeQuality.w;
    parameters.MaximumDistance = 100000.0F;
    parameters.MultipleScatteringContribution = 0.5F;
    parameters.MultipleScatteringOcclusion = 0.5F;
    parameters.AmbientOcclusionStrength = ProbeLighting.x;
    parameters.ShapeControls = ProbeShape;
    parameters.RegionalControls = ProbeRegional;
#ifdef KEIRE_TEST_CLOUD_AMBIENT
    const float3 sunDirection = normalize(float3(0.2F, 0.7F, -0.4F));
    const float4 unoccluded = IntegrateCloudVolume(0.0F.xxx, ray, 0.0F, parameters.MaximumDistance,
        sunDirection, 0.0F.xxx, 1.0F.xxx, parameters);
    parameters.AmbientOcclusionStrength = 0.5F;
    const float4 partial = IntegrateCloudVolume(0.0F.xxx, ray, 0.0F, parameters.MaximumDistance,
        sunDirection, 0.0F.xxx, 1.0F.xxx, parameters);
    parameters.AmbientOcclusionStrength = 1.0F;
    const float4 occluded = IntegrateCloudVolume(0.0F.xxx, ray, 0.0F, parameters.MaximumDistance,
        sunDirection, 0.0F.xxx, 1.0F.xxx, parameters);
    const bool failed = !all(isfinite(unoccluded)) || !all(isfinite(partial)) || !all(isfinite(occluded)) ||
        any(occluded < 0.0F) || occluded.a > 1.0F ||
        unoccluded.a != partial.a || unoccluded.a != occluded.a ||
        any(partial.rgb > unoccluded.rgb + 0.000001F) || any(occluded.rgb > partial.rgb + 0.000001F) ||
        any(abs(partial.rgb - 0.5F * (unoccluded.rgb + occluded.rgb)) > 0.00001F);
    Pixels[id.y * ProbeQuality.x + id.x] = failed ? 1.0F.xxxx : 0.0F.xxxx;
    return;
#endif
    if (ProbeRegional.z > 0.5F)
    {
        const AtmosphereMedium medium = ProbeAtmosphere();
        if (ProbeRegional.z > 3.5F)
        {
            parameters.RaySamplePhase = float(VolumeCloudHash(id, 7927U) >> 8U) / 16777216.0F;
            float originalDistance;
            const float4 original = IntegrateCloudVolumeWithDepth(0.0F.xxx, ray, 0.0F,
                parameters.MaximumDistance, medium.SunHeight.xyz, 3.0F.xxx, ProbeDensity.z.xxx,
                parameters, (AtmosphereMedium)0, false, originalDistance);
            parameters.Extinction *= 3.0F;
            float denserDistance;
            const float4 denser = IntegrateCloudVolumeWithDepth(0.0F.xxx, ray, 0.0F,
                parameters.MaximumDistance, medium.SunHeight.xyz, 3.0F.xxx, ProbeDensity.z.xxx,
                parameters, (AtmosphereMedium)0, false, denserDistance);
            const float variation = VolumeCloudBaseHeightVariation(parameters);
            const CloudVolumeInterval interval = IntersectCloudVolumeSlab(0.0F, ray.y,
                parameters.BaseHeight - variation, parameters.Thickness + 2.0F * variation,
                parameters.MaximumDistance);
            const bool originalBounds = original.a > 0.0F
                ? originalDistance >= interval.Entry && originalDistance <= interval.Exit : originalDistance == 0.0F;
            const bool denserBounds = denser.a > 0.0F
                ? denserDistance >= interval.Entry && denserDistance <= interval.Exit : denserDistance == 0.0F;
            const float distanceTolerance = max(0.01F, originalDistance * 0.00001F);
            const bool movedBack = original.a > 0.0F && denser.a > 0.0F &&
                denserDistance > originalDistance + distanceTolerance;
            const bool failed = !all(isfinite(original)) || !all(isfinite(denser)) ||
                !isfinite(originalDistance) || !isfinite(denserDistance) || !originalBounds || !denserBounds ||
                movedBack || original.a < 0.0F || denser.a > 1.0F || denser.a + 0.000001F < original.a;
            Pixels[id.y * ProbeQuality.x + id.x] = failed ? 1.0F.xxxx : 0.0F.xxxx;
            return;
        }
        if (ProbeRegional.z > 2.5F)
        {
            const float4 original = IntegrateCloudVolumeWithAtmosphere(0.0F.xxx, ray, 0.0F,
                parameters.MaximumDistance, medium.SunHeight.xyz, 3.0F.xxx, ProbeDensity.z.xxx,
                parameters, medium, false);
            parameters.Extinction *= 3.0F;
            const float4 denser = IntegrateCloudVolumeWithAtmosphere(0.0F.xxx, ray, 0.0F,
                parameters.MaximumDistance, medium.SunHeight.xyz, 3.0F.xxx, ProbeDensity.z.xxx,
                parameters, medium, false);
            const bool failed = !all(isfinite(original)) || !all(isfinite(denser)) ||
                original.a < 0.0F || original.a > 1.0F || denser.a < 0.0F || denser.a > 1.0F ||
                denser.a + 0.000001F < original.a;
            Pixels[id.y * ProbeQuality.x + id.x] = failed ? 1.0F.xxxx : 0.0F.xxxx;
            return;
        }
        const float4 cloud = IntegrateCloudVolumeWithAtmosphere(0.0F.xxx, ray, 0.0F, parameters.MaximumDistance,
            medium.SunHeight.xyz, 3.0F.xxx, ProbeDensity.z.xxx, parameters, medium, true);
        if (ProbeRegional.z > 1.5F)
        {
            const float4 attenuation = IntegrateCloudVolumeWithAtmosphere(0.0F.xxx, ray, 0.0F,
                parameters.MaximumDistance, medium.SunHeight.xyz, 3.0F.xxx, ProbeDensity.z.xxx,
                parameters, medium, false);
            const bool failed = !all(isfinite(cloud)) || any(cloud < 0.0F) || cloud.a > 1.0F ||
                !all(isfinite(attenuation)) || any(attenuation.rgb != 0.0F) || abs(attenuation.a - cloud.a) > 0.000001F;
            Pixels[id.y * ProbeQuality.x + id.x] = failed ? 1.0F.xxxx : 0.0F.xxxx;
            return;
        }
        const float3 background = IntegrateAtmosphereMedium(
            medium.ObserverRadius.xyz, ray, 2000.0F, medium).Radiance;
        Pixels[id.y * ProbeQuality.x + id.x] = float4(cloud.rgb + (1.0F - cloud.a) * background, cloud.a);
        return;
    }
    float4 cloud = 0.0F;
    const bool averagePhases = ProbeLighting.y > 0.5F;
    const uint phaseCount = averagePhases ? 8U : 1U;
    const float pixelPhase = float(VolumeCloudHash(id, 7927U) >> 8U) / 16777216.0F;
    for (uint phaseIndex = 0U; phaseIndex < phaseCount; ++phaseIndex)
    {
        parameters.RaySamplePhase = averagePhases ? frac(pixelPhase + float(phaseIndex) / 8.0F) : 0.0F;
        cloud += IntegrateCloudVolume(0.0F.xxx, ray, 0.0F, parameters.MaximumDistance,
            normalize(float3(0.2F, 0.7F, -0.4F)), 3.0F.xxx, ProbeDensity.z.xxx, parameters) / float(phaseCount);
    }
    const float3 background = lerp(float3(0.5F, 0.7F, 0.9F), float3(0.04F, 0.15F, 0.4F), saturate(ray.y));
    Pixels[id.y * ProbeQuality.x + id.x] = float4(cloud.rgb + (1.0F - cloud.a) * background, cloud.a);
}
