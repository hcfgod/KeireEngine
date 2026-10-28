#include "AtmosphereMedium.hlsli"

Texture3D<float4> Noise : register(t0, space0);
SamplerState NoiseSampler : register(s0, space0);
RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};

bool ValidTransport(AtmosphereTransport transport)
{
    return all(isfinite(transport.Radiance)) && all(transport.Radiance >= 0.0F) &&
           all(isfinite(transport.Transmission)) && all(transport.Transmission >= 0.0F) &&
           all(transport.Transmission <= 1.0F);
}

bool IdentityTransport(AtmosphereTransport transport)
{
    return all(transport.Radiance == 0.0F) && all(transport.Transmission == 1.0F);
}

bool SegmentChecks(float2 uv, AtmosphereMedium medium)
{
    medium.Controls.yz = float2(4.0F, 2.0F);
    medium.SunHeight.xyz = float3(0.0F, 1.0F, 0.0F);
    const float3 direction = float3(0.0F, 1.0F, 0.0F);
    const float distance = 0.01F + uv.x * 40.0F;
    const float3 origin = medium.ObserverRadius.xyz;
    const AtmosphereTransport original = IntegrateAtmosphereMedium(origin, direction, distance, medium);
    const AtmosphereTransport attenuation = IntegrateAtmosphereMedium(origin, direction, distance, medium, false);
    if (!ValidTransport(original) ||
        !ValidTransport(attenuation) || any(attenuation.Radiance != 0.0F) ||
        any(abs(attenuation.Transmission - original.Transmission) > 0.000001F) ||
        !IdentityTransport(IntegrateAtmosphereMedium(origin, direction, 0.0F, medium)))
    {
        return false;
    }

    medium.SolarWidth.xyz *= 2.0F;
    const AtmosphereTransport doubled = IntegrateAtmosphereMedium(origin, direction, distance, medium);
    if (!ValidTransport(doubled) ||
        any(abs(doubled.Radiance - original.Radiance * 2.0F) > 0.00001F * (1.0F + doubled.Radiance)) ||
        any(doubled.Transmission != original.Transmission))
    {
        return false;
    }

    AtmosphereMedium nightMedium = medium;
    nightMedium.SolarWidth.xyz = 0.0F;
    const AtmosphereTransport night = IntegrateAtmosphereMedium(origin, direction, distance, nightMedium);
    const AtmosphereTransport nightSurface = IntegrateAtmosphereToSurface(
        origin, direction, distance, nightMedium, true);
    if (!ValidTransport(night) || !ValidTransport(nightSurface) ||
        any(night.Radiance != 0.0F) || any(nightSurface.Radiance != 0.0F) ||
        any(abs(night.Transmission - original.Transmission) > 0.000001F) ||
        any(abs(nightSurface.Transmission - original.Transmission) > 0.000001F))
    {
        return false;
    }

    medium.Controls.x = 0.0F;
    if (!IdentityTransport(IntegrateAtmosphereMedium(origin, direction, distance, medium)))
    {
        return false;
    }
    medium.Controls.x = 1.0F;
    const float3 aboveGround = float3(0.0F, medium.ObserverRadius.w + 1.0F, 0.0F);
    const AtmosphereTransport beforeGround = IntegrateAtmosphereMedium(aboveGround, -direction, 0.5F, medium);
    const AtmosphereTransport throughGround = IntegrateAtmosphereMedium(aboveGround, -direction, 2.0F, medium);
    if (!ValidTransport(beforeGround) || !ValidTransport(throughGround) ||
        any(beforeGround.Transmission <= 0.0F) || any(throughGround.Transmission != 0.0F))
    {
        return false;
    }
    const AtmosphereTransport underground = IntegrateAtmosphereMedium(
        float3(0.0F, medium.ObserverRadius.w - 1.0F, 0.0F), direction, distance, medium);
    if (any(underground.Radiance != 0.0F) || any(underground.Transmission != 0.0F))
    {
        return false;
    }
    if (!IdentityTransport(IntegrateAtmosphereMedium(float3(-2000.0F, medium.ObserverRadius.w + 10.0F, 0.0F),
                                                     float3(1.0F, 0.0F, 0.0F), 100.0F, medium)))
    {
        return false;
    }

    // Effectively homogeneous pure absorption has a known Beer-Lambert solution independent of sample count.
    medium.RayleighScale.xyz = 0.0F;
    medium.Mie = float4(0.0F, 0.02F + uv.y * 2.0F, 0.0F, 1.0e9F);
    medium.AbsorptionPeak.xyz = 0.0F;
    const float3 horizontal = float3(1.0F, 0.0F, 0.0F);
    const AtmosphereTransport whole = IntegrateAtmosphereMedium(aboveGround, horizontal, distance, medium);
    const AtmosphereTransport first = IntegrateAtmosphereMedium(aboveGround, horizontal, distance * 0.5F, medium);
    const AtmosphereTransport second = IntegrateAtmosphereMedium(
        aboveGround + horizontal * (distance * 0.5F), horizontal, distance * 0.5F, medium);
    const float expected = exp(-medium.Mie.y * distance);
    if (!ValidTransport(whole) || !ValidTransport(first) || !ValidTransport(second) ||
        any(whole.Radiance != 0.0F) || any(abs(whole.Transmission - expected) > 0.00001F) ||
        any(abs(whole.Transmission - first.Transmission * second.Transmission) > 0.00001F))
    {
        return false;
    }
    // Authored geometry below the reference sea level is not occluded by an invisible planet.
    // Homogeneous absorption isolates the surface policy from quadrature and sunlight differences.
    const float3 belowGround = float3(0.0F, medium.ObserverRadius.w - 0.001F, 0.0F);
    const AtmosphereTransport surfaceCrossing = IntegrateAtmosphereToSurface(
        aboveGround, -direction, 2.0F, medium, false);
    const AtmosphereTransport surfaceUnderground = IntegrateAtmosphereToSurface(
        belowGround, horizontal, distance, medium, false);
    if (!ValidTransport(surfaceCrossing) || !ValidTransport(surfaceUnderground) ||
        any(abs(surfaceCrossing.Transmission - exp(-medium.Mie.y * 2.0F)) > 0.00001F) ||
        any(abs(surfaceUnderground.Transmission - expected) > 0.00001F) ||
        !IdentityTransport(IntegrateAtmosphereToSurface(belowGround, horizontal, 0.0F, medium, true)))
    {
        return false;
    }
    medium.Mie.y = 0.0F;
    return IdentityTransport(IntegrateAtmosphereMedium(aboveGround, horizontal, distance, medium));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5F) / float2(Quality.xy);
    const float noise = Noise.SampleLevel(NoiseSampler, float3(uv, 0.5F), 0.0F).r;
    const float altitude = float(id.y) * 0.5F;
    const float vertical = uv.x * 2.0F - 1.0F;
    AtmosphereMedium medium;
    medium.ObserverRadius = float4(0.0F, 6360.0F + altitude, 0.0F, 6360.0F);
    medium.SunHeight = float4(sqrt(1.0F - vertical * vertical), vertical, 0.0F, 100.0F);
    medium.RayleighScale = float4(0.005802F, 0.013558F, 0.0331F, 8.0F);
    medium.Mie = float4(0.003996F, 0.00444F, 0.8F, 1.2F);
    medium.AbsorptionPeak = float4(0.00065F, 0.001881F, 0.000085F, 25.0F);
    medium.SolarWidth = float4(20.0F, 20.0F, 20.0F, 15.0F);
    medium.Controls = float4(1.0F, 24.0F, 1.0F + floor(noise * 15.0F), 0.004675F);
    const float3 position = medium.ObserverRadius.xyz;
    const float3 density = AtmosphereMediumDensity(position, medium);
    const float3 transmission = AtmosphereMediumSunTransmission(position, medium);
    bool failed = !all(isfinite(density)) || any(density < 0.0F) || any(density > 1.0F) ||
                  !all(isfinite(transmission)) || any(transmission < 0.0F) || any(transmission > 1.0F);
    if (id.y == 0U)
    {
        failed = failed || any(abs(density - float3(1.0F, 1.0F, 0.0F)) > 0.00001F);
        if (vertical < -0.01F)
        {
            failed = failed || any(transmission != 0.0F);
        }
    }
    AtmosphereMedium doubled = medium;
    doubled.RayleighScale.xyz *= 2.0F;
    doubled.Mie.y *= 2.0F;
    doubled.AbsorptionPeak.xyz *= 2.0F;
    const float3 doubledTransmission = AtmosphereMediumSunTransmission(position, doubled);
    failed = failed || any(abs(doubledTransmission - transmission * transmission) > 0.00001F);
    AtmosphereMedium vacuum = medium;
    vacuum.RayleighScale.xyz = 0.0F;
    vacuum.Mie.y = 0.0F;
    vacuum.AbsorptionPeak.xyz = 0.0F;
    vacuum.SunHeight.xyz = float3(0.0F, 1.0F, 0.0F);
    failed = failed || any(AtmosphereMediumSunTransmission(position, vacuum) != 1.0F);
    medium.SunHeight.xyz = float3(0.0F, -1.0F, 0.0F);
    failed = failed || any(AtmosphereMediumSunTransmission(position, medium) != 0.0F);

    // Adding vacuum before the same grazing chord must not change its optical depth or sample allocation.
    medium.SunHeight.xyz = float3(1.0F, 0.0F, 0.0F);
    const float chordHeight = medium.ObserverRadius.w + 1.0F + float(id.y) * 0.2F;
    const float3 nearChord = AtmosphereMediumSunTransmission(float3(-2000.0F, chordHeight, 0.0F), medium);
    const float3 farChord = AtmosphereMediumSunTransmission(
        float3(-4000.0F - float(id.x) * 10.0F, chordHeight, 0.0F), medium);
    failed = failed || !all(isfinite(nearChord)) || !all(isfinite(farChord)) ||
             any(nearChord < 0.0F) || any(nearChord > 1.0F) ||
             any(farChord < 0.0F) || any(farChord > 1.0F) || any(abs(nearChord - farChord) > 0.0002F);
    failed = failed || !SegmentChecks(uv, medium);
    // The generic appearance host reports mean_opacity; here it is the fraction of failing test pixels.
    Pixels[id.y * Quality.x + id.x] = failed ? 1.0F.xxxx : 0.0F.xxxx;
}
