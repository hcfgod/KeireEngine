#include "KeireInternal/Rendering/PlanetWeatherFogComposition.h"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("Multi-planet fog counts inherited environment density once and radial height per body")
{
    using namespace Keire;
    using namespace Keire::Detail;
    FogSettings inherited;
    inherited.Enabled = true;
    inherited.ExtinctionPerMeter = 0.01;
    inherited.LocalVolumes.emplace_back();
    std::array<CapturedPlanetWeather, 2> weather{};
    SUBCASE("flat inherited fog") { inherited.PlanetaryHeight = false; }
    SUBCASE("radial inherited fog") { inherited.PlanetaryHeight = true; }
    for (auto& planet : weather)
    {
        planet.Fog = inherited;
        planet.InheritsFog = true;
    }
    const auto result = ComposePlanetWeatherFog(inherited, weather);
    REQUIRE(result.Planets.size() == weather.size());
    CHECK(result.Common.Enabled);
    CHECK(result.Common.LocalVolumes == inherited.LocalVolumes);
    CHECK(result.Common.ExtinctionPerMeter == (inherited.PlanetaryHeight ? 0.0 : inherited.ExtinctionPerMeter));
    for (const auto& planet : result.Planets)
    {
        CHECK(planet.Enabled == inherited.PlanetaryHeight);
        CHECK(planet.LocalVolumes.empty());
        CHECK(planet.ExtinctionPerMeter == inherited.ExtinctionPerMeter);
    }
}

TEST_CASE("Equal authored fog overrides remain distinct from shared inherited fog")
{
    using namespace Keire;
    using namespace Keire::Detail;
    FogSettings inherited;
    inherited.Enabled = true;
    inherited.PlanetaryHeight = false;
    inherited.LocalVolumes.emplace_back();
    std::array<CapturedPlanetWeather, 2> weather{};
    for (auto& planet : weather)
    {
        planet.Fog = inherited;
    }
    const auto result = ComposePlanetWeatherFog(inherited, weather);
    CHECK_FALSE(result.Common.Enabled);
    REQUIRE(result.Planets.size() == 2);
    CHECK(result.Planets[0] == inherited);
    CHECK(result.Planets[1] == inherited);
    weather[0].InheritsFog = true;
    const auto mixed = ComposePlanetWeatherFog(inherited, weather);
    CHECK(mixed.Common == inherited);
    CHECK_FALSE(mixed.Planets[0].Enabled);
    CHECK(mixed.Planets[1] == inherited);
    CHECK(ComposePlanetWeatherFog(inherited, {}).Common == inherited);
}

TEST_CASE("Single planet fog retains one complete profile for cached volume rendering")
{
    using namespace Keire;
    using namespace Keire::Detail;
    FogSettings inherited;
    inherited.Enabled = true;
    inherited.PlanetaryHeight = true;
    inherited.LocalVolumes.emplace_back();
    std::array<CapturedPlanetWeather, 1> weather{};
    weather[0].Fog = inherited;
    weather[0].InheritsFog = true;
    const auto result = ComposePlanetWeatherFog(inherited, weather);
    CHECK_FALSE(result.Common.Enabled);
    REQUIRE(result.Planets.size() == 1);
    CHECK(result.Planets.front() == inherited);
}
