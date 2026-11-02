#include "KeireInternal/Rendering/PlanetWeatherCapture.h"
#include "KeireInternal/Rendering/PlanetWeatherUniforms.h"
#include "KeireInternal/Rendering/RenderSceneRecordingTypesInternal.h"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("Planet weather capture preserves disabled overrides and stable identity without atmosphere")
{
    Keire::PlanetEnvironmentView first;
    first.Planet = Keire::AssetId(2, 1);
    first.World = 1;
    first.PlanetFrame = 2;
    first.ObserverFrame = 3;
    first.TopologyRevision = 1;
    first.RadiusMetres = 1000.0;
    first.ObserverFromCenterPlanetMetres = {0.0, 1001.0, 0.0};
    auto second = first;
    second.Planet = Keire::AssetId(1, 1);
    second.PlanetFrame = 4;
    second.CloudOverride.emplace();
    second.FogOverride.emplace();
    Keire::AtmosphereSettings atmosphere;
    atmosphere.Enabled = false;
    Keire::CloudSettings clouds;
    clouds.Layers[0].Enabled = true;
    clouds.Layers[0].Seed = 42;
    Keire::FogSettings fog;
    fog.Enabled = true;
    std::array candidates{first, second};
    const auto captured = Keire::Detail::CapturePlanetWeather(candidates, first, atmosphere, clouds, fog, 8);
    REQUIRE(captured.size() == 2);
    CHECK(captured[0].Frame.Planet == second.Planet);
    CHECK_FALSE(captured[0].Clouds.Layers[0].Enabled);
    CHECK_FALSE(captured[0].Fog.Enabled);
    CHECK(captured[1].Clouds == clouds);
    CHECK(captured[1].Fog == fog);
    const auto uniforms = Keire::RenderBackend::MakePlanetWeatherUniforms(captured);
    CHECK(uniforms.Controls[0] == 2);
    CHECK(uniforms.Bodies[0].Clouds.Layers[0].Geometry[3] == 0.0F);
    CHECK(uniforms.Bodies[1].Clouds.Layers[0].Geometry[3] > 0.0F);
    CHECK(uniforms.Bodies[1].Clouds.Layers[0].Noise[1] == 42);
    Keire::RenderBackend::CameraMotionHistoryEntry history;
    history.PlanetWeather = Keire::RenderBackend::MakePlanetWeatherHistory(captured);
    auto nextHistory = history;
    CHECK(history.HasContinuousEnvironment(nextHistory));
    nextHistory.PlanetWeather[0].Clouds.Layers[0].Seed += 1;
    CHECK_FALSE(history.HasContinuousEnvironment(nextHistory));
    nextHistory = history;
    nextHistory.PlanetWeather[0].Fog.ExtinctionPerMeter *= 2.0;
    CHECK_FALSE(history.HasContinuousEnvironment(nextHistory));
    nextHistory = history;
    nextHistory.PlanetWeather[0].Geometry.DiscontinuityRevision += 1;
    CHECK_FALSE(history.HasContinuousEnvironment(nextHistory));

    SUBCASE("weather observations must share an epoch")
    {
        candidates[1].SnapshotTime = 1.0;
        CHECK_THROWS_AS(Keire::Detail::CapturePlanetWeather(candidates, first, atmosphere, clouds, fog, 8),
                        std::invalid_argument);
    }
    SUBCASE("weather candidate overflow is explicit")
    {
        CHECK_THROWS_AS(Keire::Detail::CapturePlanetWeather(candidates, first, atmosphere, clouds, fog, 1),
                        std::length_error);
    }
    SUBCASE("selected weather cannot disagree with the same candidate")
    {
        first.CloudOverride.emplace();
        CHECK_THROWS_AS(Keire::Detail::CapturePlanetWeather(candidates, first, atmosphere, clouds, fog, 8),
                        std::invalid_argument);
    }
}
