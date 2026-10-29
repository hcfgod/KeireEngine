#include "PlanetaryCloudGeometry.hlsli"
#include "PlanetaryCloudWeather.hlsli"

RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};

// Compatible with the persistent numerical GPU probe harness's air-check mode.
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    const uint scenario = id.x % 8U;
    float actual = 0.0F;
    float expected = 0.0F;
    float tolerance = 0.01F;
    if (scenario == 0U)
    {
        const PlanetaryCloudIntervals shell = IntersectPlanetaryCloudShell(
            6360000.0F, 0.125F, 1.0F, 1000.0F, 500.0F, 100000.0F);
        actual = shell.FirstEntry;
        expected = 999.875F;
    }
    else if (scenario == 1U)
    {
        const PlanetaryCloudIntervals shell = IntersectPlanetaryCloudShell(
            6360000.0F, 1000000.0F, -1.0F, 1000.0F, 500.0F, 2000000.0F);
        actual = shell.FirstEntry;
        expected = 998500.0F;
        tolerance = 0.5F;
    }
    else if (scenario == 2U)
    {
        const float cosine = -sqrt(1.0F - 1050.0F * 1050.0F / (2000.0F * 2000.0F));
        const PlanetaryCloudIntervals shell = IntersectPlanetaryCloudShell(
            1000.0F, 1000.0F, cosine, 100.0F, 100.0F, 5000.0F);
        actual = shell.SecondExit - shell.SecondEntry;
        expected = shell.FirstExit - shell.FirstEntry;
        if (actual <= 0.0F || shell.SecondEntry <= shell.FirstExit)
        {
            actual = -100.0F;
        }
    }
    else if (scenario == 3U)
    {
        const PlanetaryCloudIntervals shell = IntersectPlanetaryCloudShell(
            1000.0F, 500.0F, -1.0F, 100.0F, 100.0F, 5000.0F);
        actual = shell.SecondExit - shell.SecondEntry;
    }
    else if (scenario == 4U)
    {
        const float3 coordinate = float3(3.125F, -2.5F, 177.75F);
        actual = PlanetaryCloudWeather(coordinate, 8.0F, 31U);
        expected = PlanetaryCloudWeather(coordinate + 256.0F.xxx, 8.0F, 31U);
        tolerance = 0.00001F;
    }
    else if (scenario == 5U)
    {
        PlanetaryCloudGeometry planet = (PlanetaryCloudGeometry)0;
        planet.ObserverToPlanetX = float4(0.0F, 1.0F, 0.0F, 0.0F);
        planet.ObserverToPlanetY = float4(-1.0F, 0.0F, 0.0F, 0.0F);
        planet.ObserverToPlanetZ = float4(0.0F, 0.0F, 1.0F, 0.0F);
        const float3 point = float3(1.0F, 2.0F, 3.0F);
        actual = length(PlanetaryCloudToObserver(PlanetaryCloudToPlanet(point, planet), planet) - point);
    }
    else if (scenario == 6U)
    {
        PlanetaryCloudGeometry planet = (PlanetaryCloudGeometry)0;
        planet.ObserverRadialAltitude = float4(0.0F, 1.0F, 0.0F, 0.125F);
        planet.RadiusGeometry.x = 6360000.0F;
        actual = PlanetaryCloudAltitude(float3(0.0F, 0.125F, 0.0F), planet);
        expected = 0.25F;
        tolerance = 0.00001F;
    }
    else
    {
        PlanetaryCloudIntervals shell;
        shell.FirstEntry = 1000000.0F;
        shell.FirstExit = 1001000.0F;
        shell.SecondEntry = 2000000.0F;
        shell.SecondExit = 2002000.0F;
        shell = LimitPlanetaryCloudTravel(shell, 1500.0F);
        [loop]
        for (uint index = 0U; index < 48U; ++index)
        {
            const PlanetaryCloudSampleInterval interval = PlanetaryCloudSample(shell, 48U, index);
            actual += interval.Exit - interval.Entry;
        }
        expected = 1500.0F;
    }
    Pixels[id.y * Quality.x + id.x] = abs(actual - expected) <= tolerance ? 0.0F :
        float4(actual, expected, float(scenario), 1.0F);
}
