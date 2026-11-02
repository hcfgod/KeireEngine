#pragma once

#ifndef KEIRE_PLANET_VOLUME_TRANSPORT_HLSLI
#define KEIRE_PLANET_VOLUME_TRANSPORT_HLSLI

#include "PlanetVolumeRayIntervals.hlsli"

bool PlanetVolumeGroundBlocksLight(float3 position, float3 direction, PlanetVolumeWeatherUniforms weather)
{
    [loop]
    for (uint index = 0U; index < weather.Controls.x; ++index)
    {
        const PlanetaryCloudGeometry geometry = weather.Bodies[index].PlanetaryCloud;
        const float altitude = PlanetaryCloudAltitude(position, geometry);
        if (altitude < 0.0F)
        {
            return true;
        }
        const float3 radial = normalize(PlanetaryCloudRadialVector(position, geometry));
        const PlanetaryCloudRoots roots = PlanetaryCloudSphereRoots(geometry.RadiusGeometry.x,
            altitude, dot(radial, direction), 0.0F);
        if (roots.Hit && roots.Near >= 0.0F && roots.Far > 0.0F)
        {
            return true;
        }
    }
    return false;
}

float PlanetVolumeViewGroundDistance(float3 origin, float3 direction, float maximumDistance,
    PlanetVolumeWeatherUniforms weather)
{
    [loop]
    for (uint index = 0U; index < weather.Controls.x; ++index)
    {
        const PlanetaryCloudGeometry geometry = weather.Bodies[index].PlanetaryCloud;
        const float altitude = PlanetaryCloudAltitude(origin, geometry);
        // Preserve local fog in authored underground interiors; their scene depth owns visibility.
        if (altitude < 0.0F)
        {
            continue;
        }
        const float3 radial = normalize(PlanetaryCloudRadialVector(origin, geometry));
        const PlanetaryCloudRoots roots = PlanetaryCloudSphereRoots(geometry.RadiusGeometry.x,
            altitude, dot(radial, direction), 0.0F);
        if (roots.Hit && roots.Near >= 0.0F && roots.Far > 0.0F)
        {
            maximumDistance = min(maximumDistance, roots.Near);
        }
    }
    return maximumDistance;
}

float PlanetVolumeCloudOpticalDepth(float3 position, float3 direction, CloudVolumeParameters cloud, uint samples)
{
    if (cloud.Extinction <= 0.0F || cloud.Coverage <= 0.0F || cloud.Thickness <= 0.0F || cloud.PatternSize <= 0.0F)
    {
        return 0.0F;
    }
    const float variation = VolumeCloudBaseHeightVariation(cloud);
    const float altitude = PlanetaryCloudAltitude(position, cloud.PlanetaryCloud);
    // A different planet can be farther from this scattering event than its own diameter.
    const float reach = abs(altitude) + 2.0F * (cloud.PlanetaryCloud.RadiusGeometry.x +
        max(cloud.BaseHeight + cloud.Thickness + variation, 0.0F));
    const PlanetaryCloudIntervals shell = LimitPlanetaryCloudTravel(
        PlanetaryCloudRayIntervals(position, direction, cloud.PlanetaryCloud,
            cloud.BaseHeight - variation, cloud.Thickness + 2.0F * variation, reach), cloud.MaximumDistance);
    if (PlanetaryCloudIntervalsEmpty(shell))
    {
        return 0.0F;
    }
    samples = clamp(samples, shell.SecondExit > shell.SecondEntry ? 2U : 1U, 16U);
    float opticalDepth = 0.0F;
    [loop]
    for (uint sampleIndex = 0U; sampleIndex < samples; ++sampleIndex)
    {
        const PlanetaryCloudSampleInterval interval = PlanetaryCloudSample(shell, samples, sampleIndex);
        const float length = interval.Exit - interval.Entry;
        const float distance = interval.Entry + 0.5F * length;
        opticalDepth += VolumeCloudDensity(position + direction * distance, direction, length, cloud) *
            cloud.Extinction * length;
    }
    return min(opticalDepth, 80.0F);
}

float PlanetVolumeAllCloudOpticalDepth(float3 position, float3 direction, PlanetVolumeWeatherUniforms weather,
    uint fixedSamples, uint activeCloudMask, out bool groundBlocked)
{
    groundBlocked = PlanetVolumeGroundBlocksLight(position, direction, weather);
    if (groundBlocked)
    {
        return 80.0F;
    }
    float opticalDepth = 0.0F;
    [loop]
    for (uint index = 0U; index < weather.Controls.x; ++index)
    {
        if ((activeCloudMask & (1U << index)) == 0U)
        {
            continue;
        }
        const CloudVolumeParameters cloud = MakePlanetVolumeParameters(weather.Bodies[index], 0.0F);
        const uint samples = fixedSamples != 0U ? fixedSamples : cloud.ShadowSteps;
        opticalDepth += PlanetVolumeCloudOpticalDepth(position, direction, cloud, samples);
    }
    return min(opticalDepth, 80.0F);
}

float PlanetVolumeAmbientTransmission(float3 position, float3 up,
    PlanetVolumeWeatherUniforms weather, uint activeCloudMask)
{
    const float3 reference = abs(up.y) < 0.9F ? float3(0.0F, 1.0F, 0.0F) : float3(1.0F, 0.0F, 0.0F);
    const float3 tangent = normalize(cross(reference, up));
    float transmission = 0.0F;
    [loop]
    for (uint azimuth = 0U; azimuth < 4U; ++azimuth)
    {
        const float angle = (float(azimuth) + 0.5F) * 1.57079632679F;
        const float3 direction = tangent * (cos(angle) * 0.86602540378F) + up * 0.5F +
            cross(tangent, up) * (sin(angle) * 0.86602540378F);
        bool groundBlocked;
        transmission += exp(-PlanetVolumeAllCloudOpticalDepth(position, direction, weather, 4U,
            activeCloudMask, groundBlocked));
    }
    return transmission * 0.25F;
}

float3 PlanetVolumeFogSource(float extinction, FogViewDensity fog, float cosine, float3 ambient,
    float3 sunlight, float3 solarTransmission, float cloudTransmission, float terrainVisibility)
{
    return extinction * saturate(fog.Scattering.xyz) * (ambient + sunlight *
        VolumeCloudPhase(cosine, fog.Scattering.w) * solarTransmission * cloudTransmission * terrainVisibility);
}

// One union march for all volume media. Opacity is scalar cloud+fog extinction; clear-air RGB transport
// remains an event prefix so this can replace sky events or compose against an arbitrary surface depth.
// A single-profile light cache is deliberately not sampled: lighting here includes every candidate.
float4 IntegratePlanetVolumeWeatherFiltered(float3 origin, float3 direction, float maximumDistance,
    float3 sunDirection, float3 sunRadiance, float3 ambientRadiance, PlanetVolumeWeatherUniforms weather,
    FogViewDensity commonFog, PlanetAtmosphereSet atmospheres, bool includeRadiance, float samplePhase,
    float2 viewFootprint, out float meanDistance)
{
    meanDistance = 0.0F;
    maximumDistance = PlanetVolumeViewGroundDistance(origin, direction, maximumDistance, weather);
    if (maximumDistance <= 0.0F)
    {
        return 0.0F;
    }
    PlanetVolumeRayIntervals intervals[KEIRE_MAX_PLANET_ATMOSPHERES + 1];
    [loop]
    for (uint index = 0U; index < weather.Controls.x; ++index)
    {
        const CloudVolumeParameters cloud = MakePlanetVolumeParameters(weather.Bodies[index], samplePhase);
        intervals[index] = MakePlanetVolumeRayIntervals(origin, direction, maximumDistance,
            cloud, weather.Bodies[index].Fog);
    }
    const CloudVolumeParameters noCloud = (CloudVolumeParameters)0;
    intervals[weather.Controls.x] = MakePlanetVolumeRayIntervals(origin, direction, maximumDistance, noCloud, commonFog);
    precise float transmission = 1.0F;
    precise float3 radiance = 0.0F;
    float weightedDistance = 0.0F;
    float depthWeight = 0.0F;
    float cursor = 0.0F;
    float previousAirDistance = 0.0F;
    AtmosphereTransport airPrefix;
    airPrefix.Radiance = 0.0F;
    airPrefix.Transmission = 1.0F;
    const float cosine = dot(direction, sunDirection);
    const bool sunlightEnabled = any(sunRadiance > 0.0F);
    const bool ambientEnabled = any(ambientRadiance > 0.0F);
    const uint activeCloudMask = PlanetVolumeActiveCloudMask(weather);
    [loop]
    for (uint step = 0U; step < KEIRE_PLANET_VOLUME_MAX_RAY_INTERVALS && cursor < maximumDistance; ++step)
    {
        if (VolumeCloudTransportComplete(transmission))
        {
            transmission = 0.0F;
            break;
        }
        float boundary = maximumDistance;
        [loop]
        for (uint index = 0U; index <= weather.Controls.x; ++index)
        {
            boundary = NextPlanetVolumeRayBoundary(cursor, boundary, intervals[index], samplePhase);
        }
        const float length = boundary - cursor;
        const float distance = cursor + length * 0.5F;
        const float3 position = origin + direction * distance;
        float cloudExtinction[KEIRE_MAX_PLANET_ATMOSPHERES];
        float fogExtinction[KEIRE_MAX_PLANET_ATMOSPHERES];
        const float commonExtinction = cursor < intervals[weather.Controls.x].FogRange.Exit ?
            SampleFogSegmentDensity(position, direction * length, commonFog, 0.0F.xxx) : 0.0F;
        precise float extinction = commonExtinction;
        [loop]
        for (uint index = 0U; index < weather.Controls.x; ++index)
        {
            // Read the immutable uniforms at the point of use. Caching all expanded structures in a
            // dynamically indexed local array increases compiler lowering cost and temporary storage.
            const CloudVolumeParameters cloud = MakePlanetVolumeParameters(weather.Bodies[index], samplePhase);
            cloudExtinction[index] = InsidePlanetVolumeCloud(cursor, boundary, intervals[index]) ?
                VolumeCloudViewDensity(position, direction, length, cloud, viewFootprint) * cloud.Extinction : 0.0F;
            const PlanetaryCloudGeometry geometry = weather.Bodies[index].PlanetaryCloud;
            const float3 observer = geometry.ObserverRadialAltitude.xyz *
                ((geometry.RadiusGeometry.x + geometry.ObserverRadialAltitude.w) * 0.001F);
            fogExtinction[index] = cursor < intervals[index].FogRange.Exit ?
                SampleFogSegmentDensity(position, direction * length, weather.Bodies[index].Fog, observer) : 0.0F;
            extinction += cloudExtinction[index] + fogExtinction[index];
        }
        if (extinction > 0.0F)
        {
            const float opticalDepth = min(extinction * length, 80.0F);
            const VolumeIntegratedMedium segment = IntegrateVolumeMedium(opticalDepth, opticalDepth, 1.0F);
            const float eventDistance = cursor + VolumeCloudMeanEventFraction(opticalDepth) * length;
            const float eventWeight = transmission * segment.Radiance;
            weightedDistance += eventWeight * eventDistance;
            depthWeight += eventWeight;
            if (includeRadiance)
            {
                uint ignoredGround;
                float ignoredEnd;
                const AtmosphereTransport air = IntegratePlanetAtmosphereSet(
                    (origin + direction * previousAirDistance) * 0.001F, direction,
                    max(eventDistance - previousAirDistance, 0.0F) * 0.001F, atmospheres, true, false,
                    ignoredGround, ignoredEnd);
                airPrefix.Radiance += airPrefix.Transmission * air.Radiance;
                airPrefix.Transmission *= air.Transmission;
                previousAirDistance = eventDistance;
                float3 solarTransmission = 1.0F;
                float shadowDepth = 0.0F;
                if (sunlightEnabled)
                {
                    solarTransmission = PlanetAtmosphereSunTransmission(position * 0.001F, atmospheres);
                    bool groundBlocked;
                    shadowDepth = PlanetVolumeAllCloudOpticalDepth(position, sunDirection, weather, 0U,
                        activeCloudMask, groundBlocked);
                    if (groundBlocked)
                    {
                        solarTransmission = 0.0F;
                    }
                }
                const float terrainVisibility = sunlightEnabled ? KEIRE_FOG_TERRAIN_VISIBILITY(position) : 1.0F;
                const float cloudTransmission = exp(-shadowDepth);
                float3 source = PlanetVolumeFogSource(commonExtinction, commonFog, cosine, ambientRadiance,
                    sunRadiance, solarTransmission, cloudTransmission, terrainVisibility);
                // AO quadrature depends on receiver up and all scene media, not on receiver seed or albedo.
                // Reuse only identical directions; differing frames retain their complete four-ray evaluation.
                float3 ambientUp[KEIRE_MAX_PLANET_ATMOSPHERES];
                float ambientValues[KEIRE_MAX_PLANET_ATMOSPHERES];
                uint ambientCount = 0U;
                [loop]
                for (uint index = 0U; index < weather.Controls.x; ++index)
                {
                    const CloudVolumeParameters cloud = MakePlanetVolumeParameters(weather.Bodies[index], samplePhase);
                    if (cloudExtinction[index] > 0.0F)
                    {
                        float ambientTransmission = 1.0F;
                        if (ambientEnabled && cloud.AmbientOcclusionStrength > 0.0F)
                        {
                            const float3 up = normalize(PlanetaryCloudRadialVector(position, cloud.PlanetaryCloud));
                            uint cached = 0U;
                            [loop]
                            while (cached < ambientCount)
                            {
                                if (all(ambientUp[cached] == up))
                                {
                                    break;
                                }
                                ++cached;
                            }
                            if (cached < ambientCount)
                            {
                                ambientTransmission = ambientValues[cached];
                            }
                            else
                            {
                                ambientTransmission = PlanetVolumeAmbientTransmission(position, up, weather, activeCloudMask);
                                ambientUp[ambientCount] = up;
                                ambientValues[ambientCount] = ambientTransmission;
                                ++ambientCount;
                            }
                        }
                        const float ambientWeight = lerp(1.0F, ambientTransmission, saturate(cloud.AmbientOcclusionStrength));
                        const float incident = sunlightEnabled ? VolumeCloudIncidentLight(cosine, cloud.Anisotropy,
                            shadowDepth, cloud.MultipleScatteringContribution, cloud.MultipleScatteringOcclusion) : 0.0F;
                        source += cloudExtinction[index] * saturate(cloud.ScatteringAlbedo) *
                            (ambientRadiance * ambientWeight + sunRadiance * incident * solarTransmission);
                    }
                    source += PlanetVolumeFogSource(fogExtinction[index], weather.Bodies[index].Fog, cosine,
                        ambientRadiance, sunRadiance, solarTransmission, cloudTransmission, terrainVisibility);
                }
                radiance += eventWeight * (airPrefix.Radiance + airPrefix.Transmission * (source / extinction));
            }
            transmission *= segment.Transmittance;
        }
        cursor = boundary;
    }
    if (depthWeight > 0.0F)
    {
        meanDistance = clamp(weightedDistance / depthWeight, 0.0F, maximumDistance);
    }
    return float4(radiance, 1.0F - transmission);
}

float4 IntegratePlanetVolumeWeather(float3 origin, float3 direction, float maximumDistance,
    float3 sunDirection, float3 sunRadiance, float3 ambientRadiance, PlanetVolumeWeatherUniforms weather,
    FogViewDensity commonFog, PlanetAtmosphereSet atmospheres, bool includeRadiance, float samplePhase,
    out float meanDistance)
{
    return IntegratePlanetVolumeWeatherFiltered(origin, direction, maximumDistance,
        sunDirection, sunRadiance, ambientRadiance, weather, commonFog, atmospheres,
        includeRadiance, samplePhase, float2(0.0F, 0.0F), meanDistance);
}

#endif // KEIRE_PLANET_VOLUME_TRANSPORT_HLSLI
