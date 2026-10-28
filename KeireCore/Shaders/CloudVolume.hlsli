#pragma once

#include "AtmosphereMedium.hlsli"
#include "CloudVolumeGeometry.hlsli"
#include "CloudVolumeLighting.hlsli"
#include "CloudVolumeDensity.hlsli"
#include "CloudWeather.hlsli"

#ifndef KEIRE_CLOUD_NOISE_SPACE
#define KEIRE_CLOUD_NOISE_SPACE space2
#endif
Texture3D<float4> VolumeCloudNoiseTexture : register(KEIRE_CLOUD_NOISE_TEXTURE, KEIRE_CLOUD_NOISE_SPACE);
SamplerState VolumeCloudNoiseSampler : register(KEIRE_CLOUD_NOISE_SAMPLER, KEIRE_CLOUD_NOISE_SPACE);

// Local-frame, metre-valued volume kernel. The host must clip the ray to opaque scene depth and the planet.
// The background and volume-aware surface shaders share this kernel; layered clouds are a separate fallback.
struct CloudVolumeParameters
{
    float BaseHeight;
    float Thickness;
    float PatternSize;
    float Coverage;
    float Extinction;
    float DetailAmount;
    float ErosionAmount;
    float Anisotropy;
    uint Seed;
    float3 WindOffset;
    uint ViewSteps;
    float3 ScatteringAlbedo;
    uint ShadowSteps;
    float MaximumDistance;
    float MultipleScatteringContribution;
    float MultipleScatteringOcclusion;
    float AmbientOcclusionStrength;
    float4 ShapeControls;
    float4 RegionalControls;
    float4 HeightProfile;
    float4 WeatherControls;
    // Zero retains deterministic midpoint integration. Positive values require temporal reconstruction.
    float RaySamplePhase;
};

uint VolumeCloudHash(uint3 cell, uint seed)
{
    uint value = cell.x * 0x8da6b343U ^ cell.y * 0xd8163841U ^ cell.z * 0xcb1ab31fU ^ seed;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

float VolumeCloudBaseHeightVariation(CloudVolumeParameters parameters)
{
    return parameters.WeatherControls.x > 0.5F ? clamp(parameters.WeatherControls.w, 0.0F, 100000.0F) : 0.0F;
}

float VolumeCloudDensityPoint(float3 position, float sampleSpacing, CloudVolumeParameters parameters)
{
    const float heightVariation = VolumeCloudBaseHeightVariation(parameters);
    if (position.y <= parameters.BaseHeight - heightVariation ||
        position.y >= parameters.BaseHeight + parameters.Thickness + heightVariation)
    {
        return 0.0F;
    }
    const float localBaseOffset = VolumeCloudWeatherBaseOffset(
        (position.x + parameters.WindOffset.x) / parameters.PatternSize,
        (position.z + parameters.WindOffset.z) / parameters.PatternSize,
        parameters.RegionalControls.y, parameters.Seed, heightVariation);
    const float height = (position.y - parameters.BaseHeight - localBaseOffset) / parameters.Thickness;
    if (height <= 0.0F || height >= 1.0F)
    {
        return 0.0F;
    }

    // The atlas has four base cells per period. Seed changes translate its coordinates without a CPU rebuild.
    const float3 seedOffset = float3(VolumeCloudHash(uint3(0U, 0U, 0U), parameters.Seed) >> 8U,
        VolumeCloudHash(uint3(1U, 0U, 0U), parameters.Seed) >> 8U,
        VolumeCloudHash(uint3(0U, 1U, 0U), parameters.Seed) >> 8U) / 16777216.0F;
    const float3 pattern = (position + parameters.WindOffset) / (parameters.PatternSize * 4.0F) + seedOffset;
    // Explicit LOD follows the ray interval; Sample derivatives are undefined inside divergent ray marches.
    const float footprint = max(sampleSpacing, 0.0F) * 16.0F / max(parameters.PatternSize, 0.0001F);
    float regionalCoverage = parameters.Coverage;
    float cloudType = 1.0F;
    if (parameters.WeatherControls.x > 0.5F)
    {
        const float weatherX = (position.x + parameters.WindOffset.x) / parameters.PatternSize;
        const float weatherZ = (position.z + parameters.WindOffset.z) / parameters.PatternSize;
        const float weatherSpacing = sampleSpacing / parameters.PatternSize;
        const float weather = parameters.RegionalControls.x > 0.0F ?
            VolumeCloudWeatherField(weatherX, weatherZ, weatherSpacing,
                parameters.RegionalControls.y, parameters.Seed) : 0.5F;
        const float typeWeather = parameters.WeatherControls.z > 0.0F ?
            VolumeCloudWeatherField(weatherX, weatherZ, weatherSpacing,
                parameters.RegionalControls.y, parameters.Seed ^ 0xa511e9b3U) : 0.5F;
        regionalCoverage = VolumeCloudWeatherCoverage(parameters.Coverage, weather, parameters.RegionalControls.x);
        cloudType = saturate(parameters.WeatherControls.y +
            (2.0F * typeWeather - 1.0F) * parameters.WeatherControls.z);
    }
    else if (parameters.RegionalControls.x > 0.0F)
    {
        const float regionalPeriod = clamp(parameters.RegionalControls.y, 1.0F, 64.0F);
        const float regionalMip = clamp(log2(max(footprint / regionalPeriod, 1.0F)), 0.0F, 6.0F);
        const float weather = VolumeCloudNoiseTexture.SampleLevel(VolumeCloudNoiseSampler,
            float3(pattern.x / regionalPeriod, 0.5F, pattern.z / regionalPeriod), regionalMip).r;
        regionalCoverage = VolumeCloudRegionalCoverage(parameters.Coverage, weather, parameters.RegionalControls.x);
    }
    const float4 profile = parameters.HeightProfile;
    // Weather type changes vertical development inside the authored layer, independently of extinction.
    float profileHeight = height;
    if (parameters.WeatherControls.x > 0.5F)
    {
        profileHeight = profile.x + (height - profile.x) / (0.35F + 0.65F * cloudType);
    }
    const float envelope = VolumeCloudAuthoredHeightProfile(profileHeight, profile.x, profile.y, profile.z, profile.w);
    // Weather and profile can prove this point empty before any warped body/detail atlas fetches.
    // The density remap returns exactly zero for either condition, independent of those samples.
    if (regionalCoverage <= 0.0F || envelope <= 0.0F)
    {
        return 0.0F;
    }

    const float detailFrequency = clamp(parameters.ShapeControls.x, 1.0F, 16.0F);
    const float variationStrength = saturate(parameters.ShapeControls.y);
    const float variationPeriod = clamp(parameters.ShapeControls.z, 1.0F, 64.0F);
    const float stretchBound = VolumeCloudStretchBound(variationStrength, variationPeriod);
    const float warpedFootprint = footprint * stretchBound;
    const float warpedSpacing = sampleSpacing * stretchBound;
    const float mip = clamp(log2(max(warpedFootprint, 1.0F)), 0.0F, 6.0F);
    // Validated power-of-two periods divide the host's 256-pattern wrapping domain.
    // Continuous displacement breaks adjacent tile repetition without per-tile seams or a wind-wrap jump.
    float3 warpedPattern = pattern;
    if (variationStrength > 0.0F)
    {
        const float macroMip = clamp(log2(max(footprint / variationPeriod, 1.0F)), 0.0F, 6.0F);
        const float3 macroNoise = VolumeCloudNoiseTexture.SampleLevel(
            VolumeCloudNoiseSampler, pattern / variationPeriod, macroMip).gba;
        warpedPattern += float3(VolumeCloudWarpOffset(macroNoise.x),
            VolumeCloudWarpOffset(macroNoise.y), VolumeCloudWarpOffset(macroNoise.z)) * (variationStrength / 0.75F);
    }
    const float4 noise = VolumeCloudNoiseTexture.SampleLevel(VolumeCloudNoiseSampler, warpedPattern, mip);
    const float coarseErosion = 1.0F - dot(noise.gba, float3(0.625F, 0.25F, 0.125F));
    float erosion = coarseErosion;
    float shapeDetail = 0.5F;
    const float detailWeight = VolumeCloudBandWeight(warpedSpacing, parameters.PatternSize / detailFrequency);
    if ((parameters.DetailAmount > 0.0F || parameters.ErosionAmount > 0.0F) && detailWeight > 0.0F)
    {
        // Erode the body at a distinct scale. Increase LOD with frequency so the extra sample does not alias.
        const float detailMip = clamp(log2(max(warpedFootprint * detailFrequency, 1.0F)), 0.0F, 6.0F);
        const float4 detailNoise = VolumeCloudNoiseTexture.SampleLevel(
            VolumeCloudNoiseSampler, warpedPattern * detailFrequency, detailMip);
        shapeDetail = detailNoise.r;
        const float detailErosion = 1.0F - dot(detailNoise.gba, float3(0.625F, 0.25F, 0.125F));
        erosion = VolumeCloudErosion(coarseErosion, detailErosion, warpedSpacing,
            parameters.PatternSize * 4.0F / detailFrequency);
    }
    // Reuse the detail sample's shape channel so detail changes the body as well as eroding its edges.
    // The same spacing weight fades both effects together when the march cannot resolve them.
    const float body = VolumeCloudDensityWithProfile(noise.r, shapeDetail,
        detailWeight * saturate(parameters.DetailAmount), erosion,
        regionalCoverage, parameters.ErosionAmount, envelope);
    return pow(body, clamp(parameters.ShapeControls.w, 0.25F, 4.0F));
}

// Average density after nonlinear coverage/erosion shaping. Averaging raw noise first erases thin bodies.
// Three subintervals retain more of those bodies while keeping the lighting integration budget explicit.
float VolumeCloudDensity(float3 position, float3 direction, float sampleSpacing, CloudVolumeParameters parameters)
{
    const float subSpacing = sampleSpacing / 3.0F;
    float density = 0.0F;
    [unroll]
    for (uint sampleIndex = 0U; sampleIndex < 3U; ++sampleIndex)
    {
        const float offset = (float(sampleIndex) - 1.0F) * subSpacing;
        density += VolumeCloudDensityPoint(position + direction * offset, subSpacing, parameters);
    }
    return density / 3.0F;
}

// Shared by direct shading and the transmission-cache builder. Directions are normalized.
float VolumeCloudLightOpticalDepth(float3 position, float3 sunDirection,
    CloudVolumeParameters parameters, uint shadowSteps)
{
    const float heightVariation = VolumeCloudBaseHeightVariation(parameters);
    const float shadowDistance = CloudVolumeShadowDistance(position.y, sunDirection.y,
        parameters.BaseHeight - heightVariation, parameters.Thickness + 2.0F * heightVariation,
        parameters.MaximumDistance);
    const float shadowStepLength = shadowDistance / float(shadowSteps);
    float lightOpticalDepth = 0.0F;
    [loop]
    for (uint shadowStep = 0U; shadowStep < shadowSteps; ++shadowStep)
    {
        const float3 lightPosition = position + sunDirection * ((float(shadowStep) + 0.5F) * shadowStepLength);
        lightOpticalDepth += VolumeCloudDensity(lightPosition, sunDirection, shadowStepLength, parameters) *
            parameters.Extinction * shadowStepLength;
    }
    return lightOpticalDepth;
}

// Four equal-solid-angle upper-hemisphere directions with four intervals each.
// This is a bounded artistic skylight approximation, not multiple-scattering transport.
float VolumeCloudAmbientTransmission(float3 position, CloudVolumeParameters parameters)
{
    if (parameters.AmbientOcclusionStrength <= 0.0F)
    {
        return 1.0F;
    }
    float transmission = 0.0F;
    [unroll]
    for (uint azimuth = 0U; azimuth < 4U; ++azimuth)
    {
        const float angle = (float(azimuth) + 0.5F) * 1.57079632679F;
        const float3 direction = float3(cos(angle) * 0.86602540378F, 0.5F, sin(angle) * 0.86602540378F);
        transmission += exp(-VolumeCloudLightOpticalDepth(position, direction, parameters, 4U));
    }
    return transmission * 0.25F;
}

#ifdef KEIRE_CLOUD_LIGHT_CACHE
#include "CloudLightCacheSampling.hlsli"

float4 VolumeCloudCachedTransmission(float3 position, float3 sunDirection,
    CloudVolumeParameters parameters, uint shadowSteps, bool sunlightEnabled)
{
    float cacheWeight;
    const float4 cached = SampleCloudLightCache(CloudLightTransmissionCache, position, CloudCacheOrigin.xyz,
        CloudCacheCellSize.xyz, CloudCacheDimensions.xyz, cacheWeight);
    if (cacheWeight >= 1.0F)
    {
        return cached;
    }
    const float depth = sunlightEnabled ?
        VolumeCloudLightOpticalDepth(position, sunDirection, parameters, shadowSteps) : 0.0F;
    const float occlusion = saturate(parameters.MultipleScatteringOcclusion);
    const float4 direct = float4(exp(-depth * float3(1.0F, occlusion, occlusion * occlusion)),
        VolumeCloudAmbientTransmission(position, parameters));
    return lerp(direct, cached, cacheWeight);
}
#endif

// Call only with a finite, nonnegative interval already intersected against the cloud volume and scene geometry.
// Directions are normalized. Returns premultiplied cloud-correction radiance and opacity, before exposure or bloom.
// The atmosphere is already present in a sky background. For a surface, add (1-opacity)*clearAir.Radiance
// and multiply the surface radiance by (1-opacity)*clearAir.Transmission at that surface's distance.
float4 IntegrateCloudVolumeWithDepth(float3 origin, float3 direction, float entryDistance, float exitDistance,
                                         float3 sunDirection, float3 sunRadiance, float3 ambientRadiance,
                                         CloudVolumeParameters parameters, AtmosphereMedium medium,
                                         bool includeRadiance, out float meanCloudDistance)
{
    // Zero denotes a ray without measurable cloud opacity. Distances are metres along the normalized view ray.
    meanCloudDistance = 0.0F;
    if (exitDistance <= entryDistance || parameters.Thickness <= 0.0F || parameters.PatternSize <= 0.0F ||
        parameters.Extinction <= 0.0F || parameters.Coverage <= 0.0F)
    {
        return 0.0F;
    }

    const bool atmosphereEnabled = medium.Controls.x > 0.5F;
    const float3 airOrigin = medium.ObserverRadius.xyz + origin * 0.001F;
    if (atmosphereEnabled)
    {
        if (length(airOrigin) < medium.ObserverRadius.w)
        {
            return 0.0F;
        }
        float2 groundInterval;
        if (AtmosphereMediumGroundIntersection(airOrigin, direction, medium, groundInterval))
        {
            exitDistance = min(exitDistance, groundInterval.x * 1000.0F);
        }
    }
    const float heightVariation = VolumeCloudBaseHeightVariation(parameters);
    const CloudVolumeInterval slab = IntersectCloudVolumeSlab(
        origin.y, direction.y, parameters.BaseHeight - heightVariation,
        parameters.Thickness + 2.0F * heightVariation, exitDistance);
    entryDistance = max(entryDistance, slab.Entry);
    exitDistance = min(exitDistance, slab.Exit);
    if (exitDistance <= entryDistance)
    {
        return 0.0F;
    }

    const uint steps = clamp(parameters.ViewSteps, 1U, 128U);
    const uint shadowSteps = clamp(parameters.ShadowSteps, 1U, 16U);
    const float stepLength = (exitDistance - entryDistance) / float(steps);
    const float samplePhase = clamp(parameters.RaySamplePhase, 0.0F, 1.0F);
    const bool shiftedSamples = samplePhase > 0.0F;
    const uint intervalCount = steps + (shiftedSamples ? 1U : 0U);
    const float scatteringCosine = dot(direction, sunDirection);
    const bool sunlightEnabled = any(sunRadiance > 0.0F);
    // Both blend passes must march identical density samples even when the compiler removes lighting work.
    precise float transmission = 1.0F;
    float depthWeight = 0.0F;
    float weightedDistance = 0.0F;
    float3 radiance = 0.0F;
    AtmosphereTransport airPrefix;
    airPrefix.Radiance = 0.0F;
    airPrefix.Transmission = 1.0F;
    float previousSampleDistance = entryDistance + 0.5F * stepLength * (shiftedSamples ? samplePhase : 1.0F);
    float3 previousAirPosition = airOrigin + direction * (previousSampleDistance * 0.001F);
    if (atmosphereEnabled && includeRadiance)
    {
        airPrefix = IntegrateAtmosphereMedium(airOrigin, direction,
            previousSampleDistance * 0.001F, medium);
    }
    [loop]
    for (uint step = 0U; step < intervalCount; ++step)
    {
        if (VolumeCloudTransportComplete(transmission))
        {
            transmission = 0.0F;
            break;
        }

        float intervalLength = stepLength;
        float sampleDistance = entryDistance + (float(step) + 0.5F) * stepLength;
        if (shiftedSamples)
        {
            const CloudVolumeInterval interval = CloudVolumeShiftedInterval(
                entryDistance, exitDistance, steps, step, samplePhase);
            intervalLength = interval.Exit - interval.Entry;
            if (intervalLength <= 0.0F)
            {
                continue;
            }
            sampleDistance = 0.5F * (interval.Entry + interval.Exit);
        }
        precise float3 position = origin + direction * sampleDistance;
        const float3 airPosition = medium.ObserverRadius.xyz + position * 0.001F;
        if (atmosphereEnabled && includeRadiance && step > 0U)
        {
            // Advance once through each interval, including intervals without clouds. Reintegrating from the
            // camera at every occupied sample is quadratic work and changes the foreground-air quadrature.
            AtmosphereMedium segmentMedium = medium;
            segmentMedium.Controls.y = 1.0F;
            const AtmosphereTransport segment = IntegrateAtmosphereMedium(
                previousAirPosition, direction,
                (shiftedSamples ? sampleDistance - previousSampleDistance : stepLength) * 0.001F, segmentMedium);
            airPrefix.Radiance += airPrefix.Transmission * segment.Radiance;
            airPrefix.Transmission *= segment.Transmission;
        }
        previousAirPosition = airPosition;
        previousSampleDistance = sampleDistance;
        precise float extinction = VolumeCloudDensity(position, direction, intervalLength, parameters) * parameters.Extinction;
        if (extinction <= 0.0F)
        {
            continue;
        }

        // The RGB multiplication pass needs cloud optical depth, but no shadow march or radiance evaluation.
        // Bounding optical depth also avoids Inf * zero in segment integration.
        precise float opticalDepth = min(extinction * intervalLength, 80.0F);
        const float eventWeight = transmission * opticalDepth * VolumeSegmentWeight(opticalDepth);
        const float eventFraction = VolumeCloudMeanEventFraction(extinction * intervalLength);
        const float eventDistance = clamp(sampleDistance + (eventFraction - 0.5F) * intervalLength,
            entryDistance, exitDistance);
        weightedDistance += eventWeight * eventDistance;
        depthWeight += eventWeight;
        if (!includeRadiance)
        {
            transmission *= exp(-opticalDepth);
            continue;
        }

#ifdef KEIRE_CLOUD_LIGHT_CACHE
        const float4 cachedTransmission = sunlightEnabled || parameters.AmbientOcclusionStrength > 0.0F ?
            VolumeCloudCachedTransmission(position, sunDirection, parameters, shadowSteps, sunlightEnabled) : 1.0F.xxxx;
        const float ambientTransmission = cachedTransmission.a;
        const float contribution = saturate(parameters.MultipleScatteringContribution);
        const float incident = sunlightEnabled ? dot(cachedTransmission.rgb, float3(
            VolumeCloudPhase(scatteringCosine, parameters.Anisotropy),
            contribution * VolumeCloudPhase(scatteringCosine, parameters.Anisotropy * 0.5F),
            contribution * contribution * VolumeCloudPhase(scatteringCosine, parameters.Anisotropy * 0.25F))) : 0.0F;
#else
        const float ambientTransmission = VolumeCloudAmbientTransmission(position, parameters);
        const float lightOpticalDepth = sunlightEnabled ? VolumeCloudLightOpticalDepth(
            position, sunDirection, parameters, shadowSteps) : 0.0F;

        const float incident = sunlightEnabled ?
            VolumeCloudIncidentLight(scatteringCosine, parameters.Anisotropy, lightOpticalDepth,
                parameters.MultipleScatteringContribution, parameters.MultipleScatteringOcclusion) : 0.0F;
#endif
        const float3 solarTransmission = atmosphereEnabled && sunlightEnabled ?
            AtmosphereMediumSunTransmission(airPosition, medium) : 1.0F;
        const float ambientWeight = lerp(1.0F, ambientTransmission, saturate(parameters.AmbientOcclusionStrength));
        const float3 illumination = ambientRadiance * ambientWeight + sunRadiance * incident * solarTransmission;
        // Replacing background sky at this opacity event must preserve the air in front of the cloud.
        // The caller adds the remaining clear-air integral for an opaque/transparent surface background.
        radiance += transmission * (airPrefix.Radiance + airPrefix.Transmission * illumination *
            saturate(parameters.ScatteringAlbedo)) *
            (opticalDepth * VolumeSegmentWeight(opticalDepth));
        transmission *= exp(-opticalDepth);
    }
    if (depthWeight > 0.0F && transmission < 1.0F)
    {
        meanCloudDistance = clamp(weightedDistance / depthWeight, entryDistance, exitDistance);
    }
    return float4(radiance, 1.0F - transmission);
}

// Compatibility path for consumers that do not reconstruct cloud history. Unused depth work is eliminated.
float4 IntegrateCloudVolumeWithAtmosphere(float3 origin, float3 direction, float entryDistance, float exitDistance,
                                         float3 sunDirection, float3 sunRadiance, float3 ambientRadiance,
                                         CloudVolumeParameters parameters, AtmosphereMedium medium,
                                         bool includeRadiance)
{
    float unusedDistance;
    return IntegrateCloudVolumeWithDepth(origin, direction, entryDistance, exitDistance,
        sunDirection, sunRadiance, ambientRadiance, parameters, medium, includeRadiance, unusedDistance);
}

float4 IntegrateCloudVolume(float3 origin, float3 direction, float entryDistance, float exitDistance,
                            float3 sunDirection, float3 sunRadiance, float3 ambientRadiance,
                            CloudVolumeParameters parameters)
{
    const AtmosphereMedium disabledMedium = (AtmosphereMedium)0;
    return IntegrateCloudVolumeWithAtmosphere(origin, direction, entryDistance, exitDistance,
        sunDirection, sunRadiance, ambientRadiance, parameters, disabledMedium, true);
}
