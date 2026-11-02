#pragma once

// The shader include resolver can alias slash and backslash paths; pragma once alone is insufficient.
#ifndef KEIRE_PLANET_WEATHER_UNIFORMS_HLSLI
#define KEIRE_PLANET_WEATHER_UNIFORMS_HLSLI

#include "PlanetaryCloudGeometry.hlsli"
#include "PlanetAtmosphereSet.hlsli"

struct PlanetWeatherLayerUniforms
{
    float4 Geometry;
    float4 Pattern;
    float4 Color;
    uint4 Noise;
};

struct PlanetWeatherBodyUniforms
{
    PlanetWeatherLayerUniforms Clouds[2];
    PlanetaryCloudGeometry Geometry;
    float4 Phase[2];
};

struct PlanetWeatherUniforms
{
    uint4 Controls;
    PlanetWeatherBodyUniforms Bodies[KEIRE_MAX_PLANET_ATMOSPHERES];
};

#endif // KEIRE_PLANET_WEATHER_UNIFORMS_HLSLI
