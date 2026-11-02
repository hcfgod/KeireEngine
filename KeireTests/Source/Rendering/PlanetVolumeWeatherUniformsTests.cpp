#include "KeireInternal/Rendering/PlanetVolumeWeatherUniforms.h"
#include "KeireInternal/Rendering/PlanetWeatherHistory.h"

#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

TEST_CASE("Planet volume packing retains distinct profiles phases and inheritance history")
{
    using namespace Keire;
    using namespace Keire::Detail;
    using namespace Keire::RenderBackend;
    PlanetEnvironmentView first;
    first.Planet = AssetId(2, 1);
    first.World = 1;
    first.PlanetFrame = 2;
    first.ObserverFrame = 3;
    first.TopologyRevision = 1;
    first.RadiusMetres = 1000.0;
    first.ObserverFromCenterPlanetMetres = {0.0, 1001.25, 0.0};
    auto second = first;
    second.Planet = AssetId(1, 1);
    second.PlanetFrame = 4;
    second.ObserverFromCenterPlanetMetres = {1.0e12 + 0.25, 1001.25, 0.0};
    CloudSettings clouds;
    clouds.Volume.Enabled = true;
    clouds.Volume.Seed = 42;
    second.CloudOverride = clouds;
    second.CloudOverride->Volume.Seed = 91;
    second.CloudOverride->Volume.BaseHeightMeters = 200.0;
    FogSettings fog;
    fog.Enabled = true;
    fog.PlanetaryHeight = true;
    fog.LocalVolumes.emplace_back();
    const std::array candidates{first, second};
    const auto weather = CapturePlanetWeather(candidates, first, AtmosphereSettings{}, clouds, fog, 8);
    const auto composition = ComposePlanetWeatherFog(fog, weather);
    const auto packed = MakePlanetVolumeWeatherUniforms(weather, composition.Planets, {});
    REQUIRE(packed.Controls[0] == 2);
    CHECK(packed.Bodies[0].Volume.Quality[1] == 91);
    CHECK(packed.Bodies[1].Volume.Quality[1] == 42);
    CHECK(packed.Bodies[0].Volume.Geometry[0] == 200.0F);
    CHECK(packed.Bodies[0].Geometry.Controls[0] == 1);
    CHECK(packed.Bodies[1].Fog.Controls[0] == 2);
    CHECK(packed.Bodies[1].Fog.Controls[2] == 0);
    CHECK(packed.Bodies[1].Fog.HeightDensity[0] == doctest::Approx(-1.25F));
    for (std::size_t index = 0; index < weather.size(); ++index)
    {
        const auto& volume = weather[index].Clouds.Volume;
        const auto phase =
            MakePlanetaryCloudPhase(weather[index].Frame, volume.PatternSizeMeters, volume.WindMetersPerSecond);
        for (std::size_t axis = 0; axis < phase.size(); ++axis)
        {
            CHECK(packed.Bodies[index].Volume.PatternOrigin[axis] == phase[axis]);
        }
    }
    auto changed = weather;
    changed[0].InheritsFog = false;
    CHECK(MakePlanetWeatherHistory(weather) != MakePlanetWeatherHistory(changed));
    CHECK_THROWS_AS(MakePlanetVolumeWeatherUniforms(weather, {}, {}), std::invalid_argument);
}
