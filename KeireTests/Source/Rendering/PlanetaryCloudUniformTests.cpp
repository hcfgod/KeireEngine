#include "KeireInternal/Rendering/CloudVolumePacket.h"
#include "KeireInternal/Rendering/PlanetaryCloudUniforms.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

TEST_CASE("Planetary cloud capture preserves local altitude and all three weather axes")
{
    using namespace Keire;
    using namespace Keire::RenderBackend;
    Detail::ResolvedPlanetFrame frame;
    frame.Planet = {1, 2};
    frame.RadiusMetres = 6360000.0;
    frame.ObserverFromCenterPlanetMetres = {0.0, 6360000.125, 0.0};
    const auto uniforms = MakePlanetaryCloudUniforms(frame, 1000.0);
    CHECK(uniforms.ObserverRadialAltitude.W == 0.125F);
    CHECK(uniforms.ObserverRadialAltitude.Y == 1.0F);
    CHECK(uniforms.Controls[0] == 1U);
    frame.PlanetToObserver = Detail::FrameRotation::Normalize(0.0, 0.0, std::sqrt(0.5), std::sqrt(0.5));
    const auto rotated = MakePlanetaryCloudUniforms(frame, 1000.0);
    CHECK(rotated.ObserverRadialAltitude.X == doctest::Approx(-1.0F));
    CHECK(rotated.ObserverRadialAltitude.W == 0.125F);
    CHECK(rotated.ObserverToPlanetX.Y == doctest::Approx(1.0F));

    VolumetricCloudSettings settings;
    settings.Enabled = true;
    const auto captured = CaptureCloudVolume({}, settings, std::nullopt, false, {1e12, -1e12, 1e12}, {}, &frame);
    CHECK(captured.Quality[0] == 1U);
    CHECK(captured.Geometry[0] == static_cast<float>(settings.BaseHeightMeters));
    const auto phase = MakePlanetaryCloudPhase(frame, settings.PatternSizeMeters, settings.WindMetersPerSecond);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        CHECK(captured.PatternOrigin[axis] == phase[axis]);
    }
}

TEST_CASE("Planetary weather phase is periodic and independent of observer-frame rotation")
{
    using namespace Keire;
    using namespace Keire::RenderBackend;
    Detail::ResolvedPlanetFrame frame;
    frame.ObserverFromCenterPlanetMetres = {1234.5, -6343210.25, 9876.5};
    const auto first = MakePlanetaryCloudPhase(frame, 1000.0, {5.0, 1.0, -2.0});
    frame.PlanetToObserver = Detail::FrameRotation::Normalize(0.0, 1.0, 0.0, 0.0);
    CHECK(MakePlanetaryCloudPhase(frame, 1000.0, {5.0, 1.0, -2.0}) == first);
    for (auto& coordinate : frame.ObserverFromCenterPlanetMetres)
    {
        coordinate += 256000.0;
    }
    CHECK(MakePlanetaryCloudPhase(frame, 1000.0, {5.0, 1.0, -2.0}) == first);
    CHECK_THROWS_AS(MakePlanetaryCloudPhase(frame, 0.0, {}), std::invalid_argument);
    frame.RadiusMetres = 0.0;
    CHECK_THROWS_AS(MakePlanetaryCloudUniforms(frame, 1000.0), std::invalid_argument);
}
