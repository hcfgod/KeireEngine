#include "AtmosphereGround.hlsli"

RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    const float distance = 0.01F + 1000.0F * (float(id.x) + 0.5F) / float(Quality.x);
    const float reflectance = (float(id.y) + 0.5F) / float(Quality.y);
    AtmosphereMedium medium = (AtmosphereMedium)0;
    medium.ObserverRadius = float4(0, 6360.0F + distance, 0, 6360);
    medium.SunHeight = float4(0, 1, 0, 100);
    medium.RayleighScale.w = 8.0F;
    medium.Mie.w = 1.2F;
    medium.SolarWidth = float4(20, 15, 10, 15);
    medium.Controls = float4(1, 4, 2, 0);
    const float3 direction = float3(0, -1, 0);
    const float3 skyIrradiance = float3(0.1F, 0.2F, 0.3F);
    const float3 expected = reflectance * (medium.SolarWidth.xyz + skyIrradiance) / 3.14159265359F;
    const float3 daylight = AtmosphereGroundRadiance(medium.ObserverRadius.xyz, direction,
        distance, medium, reflectance.xxx, skyIrradiance);
    bool valid = all(abs(daylight - expected) < 0.00002F);
    // Artistic brightness scales the boundary without scaling atmospheric scattering.
    const float intensity = 100.0F * reflectance;
    const float3 bright = AtmosphereGroundRadiance(medium.ObserverRadius.xyz, direction,
        distance, medium, reflectance.xxx * intensity, skyIrradiance);
    valid = valid && all(abs(bright - expected * intensity) < 0.002F);
    const float3 black = AtmosphereGroundRadiance(medium.ObserverRadius.xyz, direction,
        distance, medium, 0.0F.xxx, skyIrradiance);
    valid = valid && all(black == 0.0F);
    const float3 surface = AtmosphereGroundRadiance(float3(0, 6360, 0), direction,
        0.0F, medium, reflectance.xxx, skyIrradiance);
    valid = valid && all(abs(surface - expected) < 0.00002F);
    medium.SunHeight.xyz = float3(1, 0, 0);
    const float3 terminator = AtmosphereGroundRadiance(medium.ObserverRadius.xyz, direction,
        distance, medium, reflectance.xxx, skyIrradiance);
    valid = valid && all(abs(terminator - reflectance * skyIrradiance / 3.14159265359F) < 0.00002F);
    medium.SunHeight.xyz = float3(0, -1, 0);
    const float3 night = AtmosphereGroundRadiance(medium.ObserverRadius.xyz, direction,
        distance, medium, reflectance.xxx, 0.0F.xxx);
    valid = valid && all(night == 0.0F);
    medium.SunHeight.xyz = float3(0, 1, 0);
    // With scattering disabled, boundary composition must be transmission times Lambertian
    // radiance, without turning black merely because the ray intersects the reference planet.
    medium.Mie.y = 0.01F;
    const AtmosphereTransport air = IntegrateAtmosphereToSurface(medium.ObserverRadius.xyz,
        direction, distance, medium, true);
    const float3 solarTransmission = AtmosphereMediumSunTransmission(float3(0, 6360.001F, 0), medium);
    const float3 attenuated = AtmosphereGroundRadiance(medium.ObserverRadius.xyz, direction,
        distance, medium, reflectance.xxx, skyIrradiance);
    const float3 boundary = reflectance * (medium.SolarWidth.xyz * solarTransmission + skyIrradiance) /
        3.14159265359F;
    valid = valid && all(abs(attenuated - air.Transmission * boundary) < 0.00002F);
    valid = valid && all(isfinite(attenuated)) && all(attenuated >= 0.0F) && all(attenuated <= daylight);
    Pixels[id.y * Quality.x + id.x] = float4(attenuated, valid ? 0.0F : 1.0F);
}
