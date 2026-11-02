#include "KeireInternal/Rendering/CloudLightCacheDemand.h"
#include "KeireInternal/Rendering/CloudVolumeViewUniforms.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <doctest/doctest.h>

TEST_CASE("Volume view retains nonselected weather and original inherited fog after selected override")
{
    using namespace Keire;
    using namespace Keire::Detail;
    using namespace Keire::RenderBackend;
    SceneRenderPacket packet;
    packet.Environment.Atmosphere.Enabled = false;
    packet.Environment.Fog.Enabled = false;
    packet.Environment.Clouds.Volume.Enabled = false;
    packet.Lighting.Direction = {0.0F, -1.0F, 0.0F, 0.0F};
    CapturedPlanetWeather selected;
    selected.Frame.Planet = AssetId(1, 1);
    selected.Frame.RadiusMetres = 1000.0;
    selected.Frame.ObserverFromCenterPlanetMetres = {0.0, 1001.0, 0.0};
    selected.Clouds.Volume.Enabled = false;
    selected.Fog.Enabled = false;
    selected.InheritsFog = false;
    auto other = selected;
    other.Frame.Planet = AssetId(2, 1);
    other.Clouds.Volume.Enabled = true;
    other.Clouds.Volume.Seed = 73;
    FogSettings inherited;
    inherited.Enabled = true;
    inherited.PlanetaryHeight = false;
    inherited.ExtinctionPerMeter = 0.03;
    inherited.LocalVolumes.emplace_back();
    other.Fog = inherited;
    other.InheritsFog = true;
    packet.PlanetWeather = {selected, other};
    packet.PlanetVolumeFog = ComposePlanetWeatherFog(inherited, packet.PlanetWeather);
    packet.PlanetFrame = selected.Frame;
    packet.PlanetAtmospheres = {selected.Frame, other.Frame};
    packet.PlanetAtmosphereSettings = {packet.Environment.Atmosphere, packet.Environment.Atmosphere};
    const auto view = MakeCloudVolumeViewUniforms(packet, 800, 600);
    CHECK(view.PlanetWeather.Controls[0] == 2);
    CHECK(view.PlanetAtmospheres.Controls[0] == 2);
    CHECK(view.PlanetWeather.Bodies[0].Volume.Quality[0] == 0);
    CHECK(view.PlanetWeather.Bodies[1].Volume.Quality[0] == 1);
    CHECK(view.PlanetWeather.Bodies[1].Volume.Quality[1] == 73);
    CHECK(view.Fog.Controls[0] == 1);
    CHECK(view.Fog.Controls[2] == 1);
    CHECK(view.Fog.HeightDensity[2] == doctest::Approx(0.03F));
    CHECK(view.PlanetWeather.Bodies[0].Fog.Controls[0] == 0);
    CHECK(view.PlanetWeather.Bodies[1].Fog.Controls[0] == 0);
    CHECK(MakeCloudVolumeViewUniforms(packet, 0, 600).PlanetWeather.Controls[0] == 0);
}

TEST_CASE("Combined planetary transport bypasses the selected-profile cache while single planet retains it")
{
    using namespace Keire::RenderBackend;
    CloudVolumeViewUniforms view;
    view.Volume.Quality[0] = 1;
    view.Volume.Density = {0.5F, 0.01F, 0.0F, 0.0F};
    view.SunRadiance = {1.0F, 1.0F, 1.0F, 0.0F};
    CHECK(CloudLightCacheHasDemand(view));
    view.PlanetWeather.Controls[0] = 1;
    CHECK(CloudLightCacheHasDemand(view));
    view.PlanetWeather.Controls[0] = 2;
    CHECK_FALSE(CloudLightCacheHasDemand(view));
}
