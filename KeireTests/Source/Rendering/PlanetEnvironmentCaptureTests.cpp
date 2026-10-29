#include "KeireInternal/Rendering/PlanetAtmosphereUniforms.h"
#include "KeireInternal/Rendering/PlanetEnvironmentCapture.h"
#include "KeireInternal/Rendering/PlanetFogViewUniforms.h"

#include <doctest/doctest.h>

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
