#pragma once

#include "AtmosphereMedium.hlsli"
#include "CloudVolumeGeometry.hlsli"
#include "CloudVolumeLighting.hlsli"
#include "CloudVolumeDensity.hlsli"
#include "CloudWeather.hlsli"
#include "PlanetaryCloudGeometry.hlsli"
#include "PlanetaryCloudWeather.hlsli"
#include "FogRayIntervals.hlsli"
#include "CloudNoiseBuffer.hlsli"

#ifndef KEIRE_FOG_TERRAIN_VISIBILITY
#define KEIRE_FOG_TERRAIN_VISIBILITY(position) 1.0F
#endif

#ifndef KEIRE_CLOUD_NOISE_SPACE
#define KEIRE_CLOUD_NOISE_SPACE space2
#endif
#ifndef KEIRE_CLOUD_NOISE_BUFFER
Texture3D<float4> VolumeCloudNoiseTexture : register(KEIRE_CLOUD_NOISE_TEXTURE, KEIRE_CLOUD_NOISE_SPACE);
SamplerState VolumeCloudNoiseSampler : register(KEIRE_CLOUD_NOISE_SAMPLER, KEIRE_CLOUD_NOISE_SPACE);
#endif

float4 SampleVolumeCloudNoise(float3 position, float level)
{
#ifdef KEIRE_CLOUD_NOISE_BUFFER
    return SampleVolumeCloudNoiseBuffer(position, level);
#else
    return VolumeCloudNoiseTexture.SampleLevel(VolumeCloudNoiseSampler, position, level);
#endif
}

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
    PlanetaryCloudGeometry PlanetaryCloud;
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
    const bool planetary = parameters.PlanetaryCloud.Controls.x != 0U;
    const float altitude = planetary ? PlanetaryCloudAltitude(position, parameters.PlanetaryCloud) : position.y;
    const float3 weatherPosition = planetary ? PlanetaryCloudToPlanet(position, parameters.PlanetaryCloud) : position;
    const float3 weatherCoordinate = (weatherPosition + parameters.WindOffset) / parameters.PatternSize;
    if (altitude <= parameters.BaseHeight - heightVariation ||
        altitude >= parameters.BaseHeight + parameters.Thickness + heightVariation)
    {
        return 0.0F;
    }
    const float localBaseOffset = planetary ?
        (2.0F * PlanetaryCloudWeather(weatherCoordinate, parameters.RegionalControls.y, parameters.Seed ^ 0x3912a51U) - 1.0F) * heightVariation : VolumeCloudWeatherBaseOffset(
        (position.x + parameters.WindOffset.x) / parameters.PatternSize,
        (position.z + parameters.WindOffset.z) / parameters.PatternSize,
        parameters.RegionalControls.y, parameters.Seed, heightVariation);
    const float height = (altitude - parameters.BaseHeight - localBaseOffset) / parameters.Thickness;
    if (height <= 0.0F || height >= 1.0F)
    {
        return 0.0F;
    }

    // The atlas has four base cells per period. Seed changes translate its coordinates without a CPU rebuild.
    const float3 seedOffset = float3(VolumeCloudHash(uint3(0U, 0U, 0U), parameters.Seed) >> 8U,
        VolumeCloudHash(uint3(1U, 0U, 0U), parameters.Seed) >> 8U,
        VolumeCloudHash(uint3(0U, 1U, 0U), parameters.Seed) >> 8U) / 16777216.0F;
    const float3 pattern = weatherCoordinate / 4.0F + seedOffset;
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
            (planetary ? PlanetaryCloudWeather(weatherCoordinate, parameters.RegionalControls.y, parameters.Seed) :
            VolumeCloudWeatherField(weatherX, weatherZ, weatherSpacing,
                parameters.RegionalControls.y, parameters.Seed)) : 0.5F;
        const float typeWeather = parameters.WeatherControls.z > 0.0F ?
            (planetary ? PlanetaryCloudWeather(weatherCoordinate, parameters.RegionalControls.y,
                parameters.Seed ^ 0xa511e9b3U) : VolumeCloudWeatherField(weatherX, weatherZ, weatherSpacing,
                parameters.RegionalControls.y, parameters.Seed ^ 0xa511e9b3U)) : 0.5F;
        regionalCoverage = VolumeCloudWeatherCoverage(parameters.Coverage, weather, parameters.RegionalControls.x);
        cloudType = saturate(parameters.WeatherControls.y +
            (2.0F * typeWeather - 1.0F) * parameters.WeatherControls.z);
    }
    else if (parameters.RegionalControls.x > 0.0F)
    {
        const float regionalPeriod = clamp(parameters.RegionalControls.y, 1.0F, 64.0F);
        const float regionalMip = clamp(log2(max(footprint / regionalPeriod, 1.0F)), 0.0F, 6.0F);
        const float weather = SampleVolumeCloudNoise(planetary ? pattern / regionalPeriod :
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
        const float3 macroNoise = SampleVolumeCloudNoise(pattern / variationPeriod, macroMip).gba;
        warpedPattern += float3(VolumeCloudWarpOffset(macroNoise.x),
            VolumeCloudWarpOffset(macroNoise.y), VolumeCloudWarpOffset(macroNoise.z)) * (variationStrength / 0.75F);
    }
    const float4 noise = SampleVolumeCloudNoise(warpedPattern, mip);
    const float coarseErosion = 1.0F - dot(noise.gba, float3(0.625F, 0.25F, 0.125F));
    float erosion = coarseErosion;
    float shapeDetail = 0.5F;
    const float detailWeight = VolumeCloudBandWeight(warpedSpacing, parameters.PatternSize / detailFrequency);
    if ((parameters.DetailAmount > 0.0F || parameters.ErosionAmount > 0.0F) && detailWeight > 0.0F)
    {
        // Erode the body at a distinct scale. Increase LOD with frequency so the extra sample does not alias.
        const float detailMip = clamp(log2(max(warpedFootprint * detailFrequency, 1.0F)), 0.0F, 6.0F);
        const float4 detailNoise = SampleVolumeCloudNoise(warpedPattern * detailFrequency, detailMip);
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
    if (parameters.PlanetaryCloud.Controls.x != 0U)
    {
        const float altitude = PlanetaryCloudAltitude(position, parameters.PlanetaryCloud);
        const float3 radial = normalize(PlanetaryCloudRadialVector(position, parameters.PlanetaryCloud));
        const PlanetaryCloudRoots ground = PlanetaryCloudSphereRoots(parameters.PlanetaryCloud.RadiusGeometry.x,
            altitude, dot(radial, sunDirection), 0.0F);
        if (ground.Hit && ground.Near >= 0.0F && ground.Far > 0.0F)
        {
            return 80.0F;
        }
        const float reach = 2.0F * (parameters.PlanetaryCloud.RadiusGeometry.x +
            parameters.BaseHeight + parameters.Thickness + heightVariation);
        const PlanetaryCloudIntervals intervals = LimitPlanetaryCloudTravel(PlanetaryCloudRayIntervals(
            position, sunDirection, parameters.PlanetaryCloud, parameters.BaseHeight - heightVariation,
            parameters.Thickness + 2.0F * heightVariation, reach), parameters.MaximumDistance);
        const uint count = intervals.SecondExit > intervals.SecondEntry ? max(shadowSteps, 2U) : shadowSteps;
        float depth = 0.0F;
        [loop]
        for (uint index = 0U; index < count; ++index)
        {
            const PlanetaryCloudSampleInterval interval = PlanetaryCloudSample(intervals, count, index);
            const float spacing = interval.Exit - interval.Entry;
            depth += VolumeCloudDensity(position + sunDirection * (0.5F * (interval.Entry + interval.Exit)),
                sunDirection, spacing, parameters) * parameters.Extinction * spacing;
        }
        return depth;
    }
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
        float3 direction = float3(cos(angle) * 0.86602540378F, 0.5F, sin(angle) * 0.86602540378F);
        if (parameters.PlanetaryCloud.Controls.x != 0U)
        {
            const float3 up = normalize(PlanetaryCloudRadialVector(position, parameters.PlanetaryCloud));
            const float3 reference = abs(up.y) < 0.9F ? float3(0.0F, 1.0F, 0.0F) : float3(1.0F, 0.0F, 0.0F);
            const float3 tangent = normalize(cross(reference, up));
            direction = tangent * direction.x + up * direction.y + cross(tangent, up) * direction.z;
        }
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
    const float3 cachePosition = parameters.PlanetaryCloud.Controls.x != 0U ?
        PlanetaryCloudToPlanet(position, parameters.PlanetaryCloud) : position;
    const float4 cached = SampleCloudLightCache(CloudLightTransmissionCache, cachePosition, CloudCacheOrigin.xyz,
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

// Call with a finite, nonnegative interval clipped to scene geometry; cloud and fog bounds are intersected here.
// Directions are normalized. Returns premultiplied cloud-correction radiance and opacity, before exposure or bloom.
// The atmosphere is already present in a sky background. For a surface, add (1-opacity)*clearAir.Radiance
// and multiply the surface radiance by (1-opacity)*clearAir.Transmission at that surface's distance.
float4 IntegrateCloudVolumeWithDepth(float3 origin, float3 direction, float entryDistance, float exitDistance,
                                         float3 sunDirection, float3 sunRadiance, float3 ambientRadiance,
                                         CloudVolumeParameters parameters, AtmosphereMedium medium,
                                         bool includeRadiance, FogViewDensity fog, out float meanCloudDistance)
{
    // Zero denotes a ray without measurable cloud opacity. Distances are metres along the normalized view ray.
    meanCloudDistance = 0.0F;
    // Empty fog must preserve cloud-only sample placement and quality, not merely produce zero extinction.
    const bool fogEnabled = fog.Controls.x != 0U && (fog.HeightDensity.z > 0.0F || fog.Controls.z > 0U);
    const bool cloudEnabled = parameters.Thickness > 0.0F && parameters.PatternSize > 0.0F &&
        parameters.Extinction > 0.0F && parameters.Coverage > 0.0F;
    [branch]
    if (exitDistance <= entryDistance || (!cloudEnabled && !fogEnabled))
    {
        return 0.0F;
    }

    const float3 airOrigin = medium.ObserverRadius.xyz + origin * 0.001F;
    // Local fog remains valid below the synthetic atmosphere surface (for example in a basement).
    const bool atmosphereEnabled = medium.Controls.x > 0.5F &&
        !(fogEnabled && length(airOrigin) < medium.ObserverRadius.w);
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
    const bool planetary = parameters.PlanetaryCloud.Controls.x != 0U;
    CloudVolumeInterval slab = IntersectCloudVolumeSlab(
        origin.y, direction.y, parameters.BaseHeight - heightVariation,
        parameters.Thickness + 2.0F * heightVariation,
        fogEnabled ? min(exitDistance, parameters.MaximumDistance) : exitDistance);
    PlanetaryCloudIntervals shell = (PlanetaryCloudIntervals)0;
    CloudVolumeInterval secondSlab = (CloudVolumeInterval)0;
    if (planetary)
    {
        shell = LimitPlanetaryCloudTravel(PlanetaryCloudRayIntervals(origin, direction, parameters.PlanetaryCloud,
            parameters.BaseHeight - heightVariation, parameters.Thickness + 2.0F * heightVariation,
            exitDistance), parameters.MaximumDistance);
        shell.FirstEntry = max(shell.FirstEntry, entryDistance);
        shell.FirstExit = max(shell.FirstEntry, shell.FirstExit);
        shell.SecondEntry = max(shell.SecondEntry, entryDistance);
        shell.SecondExit = max(shell.SecondEntry, shell.SecondExit);
        if (shell.FirstExit <= shell.FirstEntry)
        {
            shell.FirstEntry = shell.SecondEntry;
            shell.FirstExit = shell.SecondExit;
            shell.SecondEntry = 0.0F;
            shell.SecondExit = 0.0F;
        }
        slab.Entry = shell.FirstEntry;
        slab.Exit = shell.FirstExit;
        secondSlab.Entry = shell.SecondEntry;
        secondSlab.Exit = shell.SecondExit;
    }
    if (!fogEnabled)
    {
        entryDistance = max(entryDistance, slab.Entry);
        exitDistance = min(exitDistance, max(slab.Exit, secondSlab.Exit));
    }
    else
    {
        // Fog starts at the camera, including rays which never enter a cloud slab.
        const float cloudExit = cloudEnabled ? max(slab.Exit, secondSlab.Exit) : 0.0F;
        exitDistance = min(exitDistance, max(cloudExit, fog.HeightDensity.w));
    }
    [branch]
    if (exitDistance <= entryDistance)
    {
        return 0.0F;
    }

    const uint steps = clamp(max(cloudEnabled ? parameters.ViewSteps : 1U,
        fogEnabled ? fog.Controls.y : 1U), secondSlab.Exit > secondSlab.Entry ? 2U : 1U, 128U);
    const uint shadowSteps = clamp(parameters.ShadowSteps, 1U, 16U);
    const float stepLength = (exitDistance - entryDistance) / float(steps);
    const float samplePhase = clamp(parameters.RaySamplePhase, 0.0F, 1.0F);
    const bool shiftedSamples = !fogEnabled && !planetary && samplePhase > 0.0F;
    // Each of 16 boxes adds at most two edges; cloud slab and fog cutoff add three more.
    const uint exclusionCount = fogEnabled ? min(fog.Controls.w, 16U) : 0U;
    const uint intervalCount = steps + (shiftedSamples ? 1U : 0U) +
        (fogEnabled ? 35U + exclusionCount * 4U + (planetary ? 2U : 0U) : 0U);
    CloudVolumeInterval exclusionIntervals[32];
    for (uint index = 0U; index < exclusionCount; ++index)
    {
        FogLocalDensity box;
        box.CenterExtinction = fog.Exclusions[index].CenterStrength;
        box.HalfExtentEdge = fog.Exclusions[index].HalfExtentEdge;
        exclusionIntervals[index * 2U] = IntersectFogBox(origin, direction, box,
            min(exitDistance, fog.HeightDensity.w));
        box.HalfExtentEdge.xyz *= max(1.0F - box.HalfExtentEdge.w, 1.0e-6F);
        exclusionIntervals[index * 2U + 1U] = IntersectFogBox(origin, direction, box,
            min(exitDistance, fog.HeightDensity.w));
    }
    CloudVolumeInterval fogIntervals[16];
    const uint localCount = fogEnabled ? min(fog.Controls.z, 16U) : 0U;
    for (uint localIndex = 0U; localIndex < localCount; ++localIndex)
    {
        fogIntervals[localIndex] = IntersectFogBox(origin, direction, fog.Local[localIndex],
            min(exitDistance, fog.HeightDensity.w));
    }
    float cursor = entryDistance;
    uint gridIndex = 1U;
    const float scatteringCosine = dot(direction, sunDirection);
    const bool sunlightEnabled = any(sunRadiance > 0.0F);
    // Both blend passes must march identical density samples even when the compiler removes lighting work.
    precise float transmission = 1.0F;
    float depthWeight = 0.0F;
    float weightedDistance = 0.0F;
    // Keep accumulation ordered; relaxed vector reassociation also fails SPIR-V validation in fog-only variants.
    precise float3 radiance = 0.0F;
    AtmosphereTransport airPrefix;
    airPrefix.Radiance = 0.0F;
    airPrefix.Transmission = 1.0F;
    float previousSampleDistance = fogEnabled ? 0.0F :
        entryDistance + 0.5F * stepLength * (shiftedSamples ? samplePhase : 1.0F);
    if (planetary && !fogEnabled)
    {
        const PlanetaryCloudSampleInterval firstSample = PlanetaryCloudSample(shell, steps, 0U);
        previousSampleDistance = 0.5F * (firstSample.Entry + firstSample.Exit);
    }
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
        if (planetary && !fogEnabled)
        {
            const PlanetaryCloudSampleInterval interval = PlanetaryCloudSample(shell, steps, step);
            intervalLength = interval.Exit - interval.Entry;
            sampleDistance = 0.5F * (interval.Entry + interval.Exit);
        }
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
        if (fogEnabled)
        {
            if (cursor >= exitDistance)
            {
                break;
            }
            float boundary = min(exitDistance, entryDistance + float(gridIndex) * stepLength);
            boundary = NextVolumeIntervalBoundary(cursor, boundary, slab);
            if (planetary)
            {
                boundary = NextVolumeIntervalBoundary(cursor, boundary, secondSlab);
            }
            if (fog.HeightDensity.w > cursor)
            {
                boundary = min(boundary, fog.HeightDensity.w);
            }
            for (uint localIndex = 0U; localIndex < localCount; ++localIndex)
            {
                boundary = NextVolumeIntervalBoundary(cursor, boundary, fogIntervals[localIndex]);
            }
            for (uint index = 0U; index < exclusionCount * 2U; ++index)
            {
                boundary = NextVolumeIntervalBoundary(cursor, boundary, exclusionIntervals[index]);
            }
            intervalLength = boundary - cursor;
            sampleDistance = 0.5F * (cursor + boundary);
            cursor = boundary;
            if (boundary >= entryDistance + float(gridIndex) * stepLength)
            {
                ++gridIndex;
            }
            if (intervalLength <= 0.0F)
            {
                continue;
            }
        }
        precise float3 position = origin + direction * sampleDistance;
        const float3 airPosition = medium.ObserverRadius.xyz + position * 0.001F;
        if (atmosphereEnabled && includeRadiance && (step > 0U || fogEnabled))
        {
            // Advance once through each interval, including intervals without clouds. Reintegrating from the
            // camera at every occupied sample is quadratic work and changes the foreground-air quadrature.
            AtmosphereMedium segmentMedium = medium;
            segmentMedium.Controls.y = 1.0F;
            const AtmosphereTransport segment = IntegrateAtmosphereMedium(
                previousAirPosition, direction,
                ((shiftedSamples || fogEnabled || planetary) ? sampleDistance - previousSampleDistance : stepLength) * 0.001F, segmentMedium);
            airPrefix.Radiance += airPrefix.Transmission * segment.Radiance;
            airPrefix.Transmission *= segment.Transmission;
        }
        previousAirPosition = airPosition;
        previousSampleDistance = sampleDistance;
        const bool insideCloud = (sampleDistance >= slab.Entry && sampleDistance <= slab.Exit) ||
            (planetary && sampleDistance >= secondSlab.Entry && sampleDistance <= secondSlab.Exit);
        const float cloudExtinction = cloudEnabled && insideCloud ?
            VolumeCloudDensity(position, direction, intervalLength, parameters) * parameters.Extinction : 0.0F;
        const float fogExtinction = fogEnabled && sampleDistance < fog.HeightDensity.w ?
            SampleFogSegmentDensity(position, direction * intervalLength, fog, medium.ObserverRadius.xyz) : 0.0F;
        precise float extinction = cloudExtinction + fogExtinction;
        if (extinction <= 0.0F)
        {
            continue;
        }

        // The RGB multiplication pass needs cloud optical depth, but no shadow march or radiance evaluation.
        // Bounding optical depth also avoids Inf * zero in segment integration.
        precise float opticalDepth = min(extinction * intervalLength, 80.0F);
        const VolumeIntegratedMedium cloudSegment = IntegrateVolumeMedium(opticalDepth, opticalDepth, 1.0F);
        const float eventWeight = transmission * cloudSegment.Radiance;
        const float eventFraction = VolumeCloudMeanEventFraction(extinction * intervalLength);
        const float eventDistance = clamp(sampleDistance + (eventFraction - 0.5F) * intervalLength,
            entryDistance, exitDistance);
        weightedDistance += eventWeight * eventDistance;
        depthWeight += eventWeight;
        if (!includeRadiance)
        {
            transmission *= cloudSegment.Transmittance;
            continue;
        }

        float3 solarTransmission = 1.0F;
        if (atmosphereEnabled && sunlightEnabled)
        {
            solarTransmission = AtmosphereMediumSunTransmission(airPosition, medium);
        }
        else if (planetary && sunlightEnabled)
        {
            // The opaque planet blocks every scattering order even when atmospheric scattering is disabled.
            const PlanetaryCloudRoots sunGround = PlanetaryCloudSphereRoots(
                parameters.PlanetaryCloud.RadiusGeometry.x, PlanetaryCloudAltitude(position, parameters.PlanetaryCloud),
                dot(normalize(PlanetaryCloudRadialVector(position, parameters.PlanetaryCloud)), sunDirection), 0.0F);
            if (sunGround.Hit && sunGround.Near >= 0.0F && sunGround.Far > 0.0F)
            {
                solarTransmission = 0.0F;
            }
        }
        float3 cloudSource = 0.0F;
        if (cloudExtinction > 0.0F)
        {
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
            const float ambientTransmission = parameters.AmbientOcclusionStrength > 0.0F ?
                VolumeCloudAmbientTransmission(position, parameters) : 1.0F;
            const float lightOpticalDepth = sunlightEnabled ? VolumeCloudLightOpticalDepth(
                position, sunDirection, parameters, shadowSteps) : 0.0F;

            const float incident = sunlightEnabled ?
                VolumeCloudIncidentLight(scatteringCosine, parameters.Anisotropy, lightOpticalDepth,
                    parameters.MultipleScatteringContribution, parameters.MultipleScatteringOcclusion) : 0.0F;
#endif
            const float ambientWeight = lerp(1.0F, ambientTransmission, saturate(parameters.AmbientOcclusionStrength));
            cloudSource = (ambientRadiance * ambientWeight + sunRadiance * incident * solarTransmission) *
                saturate(parameters.ScatteringAlbedo) * cloudExtinction;
        }
        const float fogPhase = VolumeCloudPhase(scatteringCosine, fog.Scattering.w);
        const float fogVisibility = fogExtinction > 0.0F && sunlightEnabled ?
            KEIRE_FOG_TERRAIN_VISIBILITY(position) : 1.0F;
        const float3 fogSource = (ambientRadiance + sunRadiance * fogPhase * solarTransmission * fogVisibility) *
            saturate(fog.Scattering.xyz) * fogExtinction;
        const float3 illumination = (cloudSource + fogSource) / extinction;
        // Replacing background sky at this opacity event must preserve the air in front of the cloud.
        // The caller adds the remaining clear-air integral for an opaque/transparent surface background.
        radiance += transmission * (airPrefix.Radiance + airPrefix.Transmission * illumination) *
            cloudSegment.Radiance;
        transmission *= cloudSegment.Transmittance;
    }
    if (depthWeight > 0.0F && transmission < 1.0F)
    {
        meanCloudDistance = clamp(weightedDistance / depthWeight, entryDistance, exitDistance);
    }
    return float4(radiance, 1.0F - transmission);
}

// Preserve the existing cloud-only call contract and its sampling cost.
float4 IntegrateCloudVolumeWithDepth(float3 origin, float3 direction, float entryDistance, float exitDistance,
    float3 sunDirection, float3 sunRadiance, float3 ambientRadiance,
    CloudVolumeParameters parameters, AtmosphereMedium medium, bool includeRadiance, out float meanCloudDistance)
{
    const FogViewDensity disabledFog = (FogViewDensity)0;
    return IntegrateCloudVolumeWithDepth(origin, direction, entryDistance, exitDistance,
        sunDirection, sunRadiance, ambientRadiance, parameters, medium, includeRadiance, disabledFog, meanCloudDistance);
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
