#pragma once

#ifndef KEIRE_PLANET_VOLUME_WEATHER_UNIFORMS_HLSLI
#define KEIRE_PLANET_VOLUME_WEATHER_UNIFORMS_HLSLI

#include "CloudVolume.hlsli"
#include "PlanetAtmosphereSet.hlsli"

struct PlanetVolumeWeatherBodyUniforms
{
    float4 Geometry;
    float4 Density;
    float4 PatternOrigin;
    float4 Albedo;
    uint4 Quality;
    float4 ShapeControls;
    float4 RegionalControls;
    float4 HeightProfile;
    float4 WeatherControls;
    float4 LightingControls;
    PlanetaryCloudGeometry PlanetaryCloud;
    FogViewDensity Fog;
};

struct PlanetVolumeWeatherUniforms
{
    uint4 Controls;
    PlanetVolumeWeatherBodyUniforms Bodies[KEIRE_MAX_PLANET_ATMOSPHERES];
};

bool PlanetVolumeCloudEnabled(PlanetVolumeWeatherBodyUniforms body)
{
    return body.Quality.x != 0U && body.Density.x > 0.0F && body.Density.y > 0.0F &&
        body.Geometry.y > 0.0F && body.Geometry.z > 0.0F;
}

uint PlanetVolumeActiveCloudMask(PlanetVolumeWeatherUniforms weather)
{
    uint mask = 0U;
    [loop]
    for (uint index = 0U; index < weather.Controls.x; ++index)
    {
        if (PlanetVolumeCloudEnabled(weather.Bodies[index]))
        {
            mask |= 1U << index;
        }
    }
    return mask;
}

CloudVolumeParameters MakePlanetVolumeParameters(PlanetVolumeWeatherBodyUniforms body, float samplePhase)
{
    CloudVolumeParameters parameters;
    parameters.BaseHeight = body.Geometry.x;
    parameters.Thickness = body.Geometry.y;
    parameters.PatternSize = body.Geometry.z;
    parameters.MaximumDistance = body.Geometry.w;
    parameters.Coverage = body.Density.x;
    parameters.Extinction = body.Quality.x != 0U ? body.Density.y : 0.0F;
    parameters.DetailAmount = body.Density.z;
    parameters.ErosionAmount = VolumeCloudErosionAmount(body.RegionalControls.w, body.Density.z);
    parameters.Anisotropy = body.Density.w;
    parameters.Seed = body.Quality.y;
    parameters.WindOffset = body.PatternOrigin.xyz * body.Geometry.z;
    parameters.ViewSteps = body.Quality.z;
    parameters.ScatteringAlbedo = body.Albedo.xyz;
    parameters.ShadowSteps = body.Quality.w;
    parameters.MultipleScatteringContribution = body.Albedo.w;
    parameters.MultipleScatteringOcclusion = body.PatternOrigin.w;
    parameters.AmbientOcclusionStrength = body.LightingControls.x;
    parameters.ShapeControls = body.ShapeControls;
    parameters.RegionalControls = body.RegionalControls;
    parameters.HeightProfile = body.HeightProfile;
    parameters.WeatherControls = body.WeatherControls;
    parameters.RaySamplePhase = samplePhase;
    parameters.PlanetaryCloud = body.PlanetaryCloud;
    return parameters;
}

#endif // KEIRE_PLANET_VOLUME_WEATHER_UNIFORMS_HLSLI
