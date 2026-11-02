#include "KeireInternal/Rendering/EnvironmentLightingApplication.h"
#include "KeireInternal/Rendering/PlanetAtmosphereUniforms.h"
#include "KeireInternal/Rendering/PlanetEnvironmentCapture.h"
#include "KeireInternal/Rendering/PlanetFogViewUniforms.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <limits>

namespace
{
    Keire::PlanetEnvironmentView ValidPlanetView()
    {
        Keire::PlanetEnvironmentView view;
        view.Planet = Keire::AssetId::Parse("11111111-2222-4333-8444-555555555555");
        view.World = 1;
        view.PlanetFrame = 2;
        view.ObserverFrame = 3;
        view.TopologyRevision = 4;
        view.DiscontinuityRevision = 5;
        view.RadiusMetres = 6360000.0;
        view.ObserverFromCenterPlanetMetres = {0.125, 6360010.25, -0.5};
        view.Clock = {1234567890123456789LL, 0.375, 2400.0};
        return view;
    }
} // namespace

TEST_CASE("Planet composition capture preserves profile pairing and inherited settings in stable identity order")
{
    auto earth = ValidPlanetView();
    auto mars = earth;
    mars.Planet = Keire::AssetId(1, 2);
    mars.PlanetFrame = 10;
    mars.RadiusMetres = 3'389'500.0;
    mars.ObserverFromCenterPlanetMetres = {25'000'000.0, 0.0, 0.0};
    mars.AtmosphereOverride.emplace();
    mars.AtmosphereOverride->Enabled = false;
    mars.AtmosphereOverride->GroundReflectance = {0.6, 0.2, 0.1};
    Keire::AtmosphereSettings inherited;
    inherited.Enabled = true;
    inherited.MieAnisotropy = 0.7;
    const auto baseline = inherited;
    std::array candidates{earth, mars};
    SUBCASE("request order") {}
    SUBCASE("reversed request order") { std::ranges::reverse(candidates); }

    const auto captured = Keire::Detail::CapturePlanetAtmospheres(candidates, earth, inherited);
    REQUIRE(captured.Frames.size() == 2);
    REQUIRE(captured.Profiles.size() == 2);
    CHECK(captured.Frames[0].Planet == mars.Planet);
    CHECK(captured.Frames[1].Planet == earth.Planet);
    CHECK(captured.Profiles[0] == *mars.AtmosphereOverride);
    CHECK(captured.Profiles[1] == baseline);
    CHECK(inherited == baseline);
}

TEST_CASE("Planet composition capture rejects mismatched active observations")
{
    const auto selected = ValidPlanetView();
    auto candidate = selected;
    candidate.Planet = Keire::AssetId(1, 2);
    candidate.PlanetFrame = 10;
    SUBCASE("observer") { ++candidate.ObserverFrame; }
    SUBCASE("world") { ++candidate.World; }
    SUBCASE("topology") { ++candidate.TopologyRevision; }
    SUBCASE("epoch") { candidate.SnapshotTime += 0.25; }
    SUBCASE("clock day") { ++candidate.Clock.Day; }
    SUBCASE("clock fraction") { candidate.Clock.DayFraction += 0.125; }
    SUBCASE("clock rate") { candidate.Clock.SecondsPerDay += 1.0; }
    SUBCASE("observer axes") { candidate.RootToObserverRotation = {0.0, 1.0, 0.0, 0.0}; }
    SUBCASE("invalid radius") { candidate.RadiusMetres = 0.0; }
    const std::array candidates{candidate};
    Keire::AtmosphereSettings inherited;
    inherited.Enabled = true;
    CHECK_THROWS_AS(Keire::Detail::CapturePlanetAtmospheres(candidates, selected, inherited), std::invalid_argument);
    CHECK_THROWS_AS(Keire::Detail::CapturePlanetAtmospheres(std::array{selected, candidate}, std::nullopt, inherited),
                    std::invalid_argument);
}

TEST_CASE("Planet composition capture rejects duplicate and inconsistent selected candidates")
{
    const auto selected = ValidPlanetView();
    auto candidate = selected;
    Keire::AtmosphereSettings inherited;
    inherited.Enabled = true;
    SUBCASE("duplicate planet")
    {
        candidate.PlanetFrame = 10;
        CHECK_THROWS_AS(
            Keire::Detail::CapturePlanetAtmospheres(std::array{selected, candidate}, std::nullopt, inherited),
            std::invalid_argument);
        return;
    }
    SUBCASE("duplicate frame")
    {
        candidate.Planet = Keire::AssetId(1, 2);
        CHECK_THROWS_AS(
            Keire::Detail::CapturePlanetAtmospheres(std::array{selected, candidate}, std::nullopt, inherited),
            std::invalid_argument);
        return;
    }
    SUBCASE("selected radius") { candidate.RadiusMetres += 1.0; }
    SUBCASE("selected discontinuity") { ++candidate.DiscontinuityRevision; }
    SUBCASE("selected position") { candidate.ObserverFromCenterPlanetMetres[0] += 0.125; }
    SUBCASE("selected orientation") { candidate.PlanetToObserverRotation = {0.0, 1.0, 0.0, 0.0}; }
    SUBCASE("selected profile") { candidate.AtmosphereOverride.emplace(); }
    CHECK_THROWS_AS(Keire::Detail::CapturePlanetAtmospheres(std::array{candidate}, selected, inherited),
                    std::invalid_argument);
}

TEST_CASE("Planet composition capture accepts equivalent quaternion signs and explicit disabled profiles")
{
    auto selected = ValidPlanetView();
    selected.AtmosphereOverride.emplace();
    selected.AtmosphereOverride->Enabled = false;
    auto candidate = selected;
    candidate.PlanetToObserverRotation = {0.0, 0.0, 0.0, -1.0};
    candidate.RootToObserverRotation = {0.0, 0.0, 0.0, -1.0};
    const auto captured = Keire::Detail::CapturePlanetAtmospheres(std::array{candidate}, selected, {});
    REQUIRE(captured.Frames.size() == 1);
    REQUIRE(captured.Profiles.size() == 1);
    CHECK_FALSE(captured.Profiles[0].Enabled);
    const auto inactive = Keire::Detail::CapturePlanetAtmospheres(std::array{ValidPlanetView()}, std::nullopt, {});
    CHECK(inactive.Frames.empty());
    CHECK(inactive.Profiles.empty());
}

TEST_CASE("Planet composition capture keeps independent planet discontinuities and skips inactive candidates")
{
    const auto selected = ValidPlanetView();
    auto distant = selected;
    distant.Planet = Keire::AssetId(1, 2);
    distant.PlanetFrame = 10;
    ++distant.DiscontinuityRevision;
    Keire::AtmosphereSettings inherited;
    inherited.Enabled = true;
    const auto captured = Keire::Detail::CapturePlanetAtmospheres(std::array{selected, distant}, selected, inherited);
    REQUIRE(captured.Frames.size() == 2);
    CHECK(captured.Frames[0].DiscontinuityRevision == distant.DiscontinuityRevision);
    CHECK(captured.Frames[1].DiscontinuityRevision == selected.DiscontinuityRevision);
    inherited.Enabled = false;
    const auto inactive = Keire::Detail::CapturePlanetAtmospheres(std::array{selected, selected}, selected, inherited);
    CHECK(inactive.Frames.empty());
    CHECK(inactive.Profiles.empty());
}

TEST_CASE("planet render capture preserves precise observer and split clock")
{
    auto source = ValidPlanetView();
    source.RootToObserverRotation = {0.0, 1.0, 0.0, 0.0};
    const auto resolved = Keire::Detail::CapturePlanetEnvironment(source);
    const auto restored = Keire::Detail::ToPlanetEnvironmentView(resolved);
    CHECK(restored.ObserverFromCenterPlanetMetres == source.ObserverFromCenterPlanetMetres);
    CHECK(restored.Clock == source.Clock);
    CHECK(restored.RootToObserverRotation == source.RootToObserverRotation);
    CHECK(restored.PlanetToObserverRotation == source.PlanetToObserverRotation);
    CHECK(restored.DiscontinuityRevision == source.DiscontinuityRevision);
    CHECK(restored.ObserverFrame == source.ObserverFrame);
    CHECK(restored.PlanetFrame == source.PlanetFrame);
}

TEST_CASE("planet render capture rejects invalid frames and nonfinite view inputs")
{
    auto view = ValidPlanetView();
    SUBCASE("missing frame") { view.ObserverFrame = 0; }
    SUBCASE("invalid radius") { view.RadiusMetres = 0.0; }
    SUBCASE("nonfinite observer") { view.ObserverFromCenterPlanetMetres[0] = std::numeric_limits<double>::infinity(); }
    SUBCASE("invalid root rotation") { view.RootToObserverRotation[3] = 2.0; }
    SUBCASE("invalid clock") { view.Clock.DayFraction = 1.0; }
    SUBCASE("unnormalized rotation") { view.PlanetToObserverRotation[3] = 2.0; }
    CHECK_THROWS_AS(Keire::Detail::CapturePlanetEnvironment(view), std::invalid_argument);
}

TEST_CASE("planet atmosphere and fog use selected frame without moving local fog boxes")
{
    auto view = ValidPlanetView();
    view.ObserverFromCenterPlanetMetres = {0.0, view.RadiusMetres + 10.25, 0.0};
    view.PlanetToObserverRotation = {0.0, 0.0, 1.0, 0.0};
    const auto planet = Keire::Detail::CapturePlanetEnvironment(view);
    Keire::AtmosphereSettings atmosphere;
    atmosphere.PlanetCenter = {1.0e12, -1.0e12, 0.0};
    const auto air = Keire::RenderBackend::MakePlanetAtmosphereUniforms(atmosphere, {0.0F, -1.0F, 0.0F}, planet);
    CHECK(air.ObserverRadius.Y == doctest::Approx(-(view.RadiusMetres + 10.25) / 1000.0));
    CHECK(air.ObserverRadius.W == doctest::Approx(view.RadiusMetres / 1000.0));

    Keire::FogSettings fog;
    fog.Enabled = true;
    fog.PlanetaryHeight = true;
    fog.LocalVolumes.emplace_back();
    fog.LocalVolumes.front().Center = {1.0e12 + 2.0, 0.0, 0.0};
    fog.ExclusionRegions.emplace_back();
    fog.ExclusionRegions.front().Center = {1.0e12 + 3.0, 0.0, 0.0};
    const auto packed = Keire::RenderBackend::MakePlanetFogViewUniforms(fog, {1.0e12, 0.0, 0.0}, planet);
    CHECK(packed.Controls[0] == 2U);
    CHECK(packed.HeightDensity[0] == doctest::Approx(-10.25));
    REQUIRE(packed.Controls[2] == 1U);
    REQUIRE(packed.Controls[3] == 1U);
    CHECK(packed.Local[0].CenterExtinction[0] == doctest::Approx(2.0));
    CHECK(packed.Exclusions[0].CenterExtinction[0] == doctest::Approx(3.0));
}

TEST_CASE("Planet atmosphere packing rejects coordinates that overflow shader intersections")
{
    using namespace Keire;
    using namespace Keire::Detail;
    using namespace Keire::RenderBackend;
    auto frame = CapturePlanetEnvironment(ValidPlanetView());
    const AtmosphereSettings settings;
    SUBCASE("finite packed observer with overflowing squared distance")
    {
        frame.ObserverFromCenterPlanetMetres = {1.0e30, 0.0, 0.0};
    }
    SUBCASE("observer outside float range") { frame.ObserverFromCenterPlanetMetres = {1.0e100, 0.0, 0.0}; }
    SUBCASE("nonfinite observer")
    {
        frame.ObserverFromCenterPlanetMetres = {std::numeric_limits<double>::infinity(), 0.0, 0.0};
        CHECK_THROWS_AS(MakePlanetAtmosphereUniforms(settings, {}, frame), std::invalid_argument);
        return;
    }
    SUBCASE("squared radius overflow") { frame.RadiusMetres = 1.0e30; }
    SUBCASE("radius underflows float") { frame.RadiusMetres = 1.0e-100; }
    CHECK_THROWS_AS(MakePlanetAtmosphereUniforms(settings, {}, frame), std::out_of_range);
}

TEST_CASE("Planet atmosphere packing keeps supported interplanetary distances finite")
{
    auto frame = Keire::Detail::CapturePlanetEnvironment(ValidPlanetView());
    const Keire::AtmosphereSettings settings;
    for (const double distance : {0.0, 1.0e12, -1.0e12})
    {
        frame.ObserverFromCenterPlanetMetres = {distance, frame.RadiusMetres + 10.25, -0.5};
        const auto packed = Keire::RenderBackend::MakePlanetAtmosphereUniforms(settings, {}, frame);
        CHECK(Keire::Math::IsFinite(packed.ObserverRadius));
        CHECK(packed.ObserverRadius.X == doctest::Approx(distance / 1000.0));
        CHECK(packed.ObserverRadius.Z == doctest::Approx(-0.0005));
    }
}

TEST_CASE("Planet view samples attach only the atmosphere authored for their stable identity")
{
    using namespace Keire::Detail;
    ResolvedPlanetFrame earth;
    earth.Planet = Keire::AssetId(50, 1);
    earth.PlanetFrame = {1, 2};
    earth.ObserverFrame = {1, 1};
    earth.TopologyRevision = 1;
    earth.RadiusMetres = 6'360'000.0;
    earth.ObserverFromCenterPlanetMetres = {0.0, 6'360'001.0, 0.0};
    auto mars = earth;
    mars.Planet = Keire::AssetId(50, 2);
    mars.PlanetFrame = {1, 3};
    PlanetProjectSettings settings;
    Keire::AtmosphereSettings thinAir;
    thinAir.Enabled = true;
    thinAir.MieAnisotropy = 0.4;
    settings.AtmosphereOverrides.push_back({mars.Planet, thinAir});
    const auto earthView = ToPlanetEnvironmentView(earth, settings);
    const auto marsView = ToPlanetEnvironmentView(mars, settings);
    CHECK_FALSE(earthView.AtmosphereOverride.has_value());
    REQUIRE(marsView.AtmosphereOverride.has_value());
    CHECK(*marsView.AtmosphereOverride == thinAir);
    CHECK(CapturePlanetEnvironment(marsView).Planet == mars.Planet);
    CHECK(ToPlanetEnvironmentView(earth, settings).AtmosphereOverride == earthView.AtmosphereOverride);
}

TEST_CASE("Selected planet cloud and fog profiles preserve inheritance and explicit disabled settings")
{
    using namespace Keire::Detail;
    const auto earth = CapturePlanetEnvironment(ValidPlanetView());
    auto mars = earth;
    mars.Planet = Keire::AssetId(50, 2);
    mars.PlanetFrame.Frame = 8;
    PlanetProjectSettings settings;
    Keire::CloudSettings clouds;
    clouds.Volume.Enabled = false;
    clouds.Volume.BaseHeightMeters = 3500.0;
    Keire::FogSettings fog;
    fog.Enabled = false;
    fog.PlanetaryHeight = true;
    fog.LocalVolumes.emplace_back();
    fog.LocalVolumes.front().Center = {1.0e12 + 2.0, 3.0, -5.0};
    fog.ExclusionRegions.emplace_back();
    fog.ExclusionRegions.front().Center = {1.0e12 + 4.0, 3.0, -5.0};
    settings.CloudOverrides.push_back({mars.Planet, clouds});
    settings.FogOverrides.push_back({mars.Planet, fog});

    const auto earthView = ToPlanetEnvironmentView(earth, settings);
    const auto marsView = ToPlanetEnvironmentView(mars, settings);
    CHECK_FALSE(earthView.CloudOverride);
    CHECK_FALSE(earthView.FogOverride);
    REQUIRE(marsView.CloudOverride);
    REQUIRE(marsView.FogOverride);
    CHECK(*marsView.CloudOverride == clouds);
    CHECK(*marsView.FogOverride == fog);

    Keire::AtmosphereSettings inheritedAir;
    Keire::CloudSettings inheritedClouds;
    inheritedClouds.Volume.Enabled = true;
    Keire::FogSettings inheritedFog;
    inheritedFog.Enabled = true;
    auto resolvedAir = inheritedAir;
    auto resolvedClouds = inheritedClouds;
    auto resolvedFog = inheritedFog;
    ApplyPlanetEnvironmentOverrides(earthView, resolvedAir, resolvedClouds, resolvedFog);
    CHECK(resolvedClouds == inheritedClouds);
    CHECK(resolvedFog == inheritedFog);
    ApplyPlanetEnvironmentOverrides(marsView, resolvedAir, resolvedClouds, resolvedFog);
    CHECK(resolvedAir == inheritedAir);
    CHECK(resolvedClouds == clouds);
    CHECK(resolvedFog == fog);
    CHECK_FALSE(resolvedClouds.Volume.Enabled);
    CHECK_FALSE(resolvedFog.Enabled);
    CHECK(resolvedFog.LocalVolumes.front().Center == fog.LocalVolumes.front().Center);
    CHECK(resolvedFog.ExclusionRegions.front().Center == fog.ExclusionRegions.front().Center);
    CHECK(inheritedClouds.Volume.Enabled);
    CHECK(inheritedFog.Enabled);
}

TEST_CASE("Planet render capture rejects malformed cloud and fog profiles even when disabled")
{
    auto view = ValidPlanetView();
    SUBCASE("cloud density")
    {
        view.CloudOverride.emplace();
        view.CloudOverride->Volume.Coverage = std::numeric_limits<double>::quiet_NaN();
    }
    SUBCASE("fog density")
    {
        view.FogOverride.emplace();
        view.FogOverride->ExtinctionPerMeter = -1.0;
    }
    SUBCASE("fog local volume")
    {
        view.FogOverride.emplace();
        view.FogOverride->LocalVolumes.emplace_back();
        view.FogOverride->LocalVolumes.front().HalfExtent[0] = 0.0;
    }
    CHECK_THROWS_AS(Keire::Detail::CapturePlanetEnvironment(view), std::invalid_argument);
}

TEST_CASE("Planet weather selection round trips independently of profile order and evaluated timeline")
{
    using namespace Keire::Detail;
    const auto earth = CapturePlanetEnvironment(ValidPlanetView());
    auto mars = earth;
    mars.Planet = Keire::AssetId(50, 2);
    mars.PlanetFrame.Frame = 8;
    Keire::CloudSettings earthClouds;
    earthClouds.Volume.Enabled = true;
    earthClouds.Volume.ThicknessMeters = 6000.0;
    earthClouds.Layers[0].Enabled = true;
    earthClouds.Layers[0].WindMetersPerSecond = {12.0, -2.0};
    auto marsClouds = earthClouds;
    marsClouds.Volume.ThicknessMeters = 300.0;
    marsClouds.Volume.Coverage = 0.2;
    marsClouds.Layers[0].WindMetersPerSecond = {-7.0, 4.0};
    Keire::FogSettings earthFog;
    earthFog.Enabled = true;
    earthFog.PlanetaryHeight = true;
    earthFog.ExtinctionPerMeter = 0.003;
    auto marsFog = earthFog;
    marsFog.ExtinctionPerMeter = 0.0001;
    marsFog.ScatteringAlbedo = {0.8, 0.4, 0.2};
    PlanetProjectSettings settings;
    settings.CloudOverrides = {{earth.Planet, earthClouds}, {mars.Planet, marsClouds}};
    settings.FogOverrides = {{earth.Planet, earthFog}, {mars.Planet, marsFog}};
    SUBCASE("authored profile order") {}
    SUBCASE("reversed profile order")
    {
        std::ranges::reverse(settings.CloudOverrides);
        std::ranges::reverse(settings.FogOverrides);
    }

    const Keire::RenderEnvironmentSettings inherited;
    Keire::EnvironmentLightingSnapshot timeline;
    timeline.Time = Keire::EnvironmentTimeSample{1234567890123456789LL, 0.375, 2400.0};
    timeline.Ambient = Keire::EnvironmentAmbientLight{{0.1F, 0.2F, 0.3F}, 0.25F};
    const auto capture = [&](const ResolvedPlanetFrame& planet)
    {
        // Production starts each immutable request from project settings, never the preceding planet packet.
        auto environment = inherited;
        const auto view = ToPlanetEnvironmentView(planet, settings);
        ApplyPlanetEnvironmentOverrides(view, environment.Atmosphere, environment.Clouds, environment.Fog);
        const auto selectedClouds = environment.Clouds;
        const auto selectedFog = environment.Fog;
        std::array<std::array<double, 2>, Keire::MaximumCloudLayers> phase{};
        double stars = 0.0;
        ApplyEnvironment(environment, phase, stars, timeline);
        CHECK(environment.Clouds == selectedClouds);
        CHECK(environment.Fog == selectedFog);
        CHECK(phase == EvaluateEnvironmentCloudPhase(selectedClouds, {timeline.Time->Day, timeline.Time->DayFraction,
                                                                      timeline.Time->SecondsPerDay}));
        CHECK(environment.AmbientIntensity == doctest::Approx(0.25F));
        return environment;
    };
    const auto firstEarth = capture(earth);
    const auto visitedMars = capture(mars);
    const auto returnedEarth = capture(earth);
    CHECK(firstEarth.Clouds == earthClouds);
    CHECK(firstEarth.Fog == earthFog);
    CHECK(visitedMars.Clouds == marsClouds);
    CHECK(visitedMars.Fog == marsFog);
    CHECK(returnedEarth == firstEarth);
    CHECK_FALSE(inherited.Clouds.Volume.Enabled);
    CHECK_FALSE(inherited.Fog.Enabled);
}
