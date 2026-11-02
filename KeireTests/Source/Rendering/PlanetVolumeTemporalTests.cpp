#include "KeireInternal/Rendering/CloudTemporalReprojection.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

namespace
{
    Keire::RenderBackend::CloudTemporalView PlanetVolumeView(const std::uint64_t frameId)
    {
        using namespace Keire;
        using namespace Keire::RenderBackend;
        CloudTemporalView result;
        result.FrameId = frameId;
        result.Volume.SunDirection = {0.0F, 1.0F, 0.0F, 0.0F};
        result.Volume.SunRadiance = {1.0F, 1.0F, 1.0F, 0.0F};
        result.Volume.PlanetWeather.Controls[0] = 2;
        for (std::size_t index = 0; index < 2; ++index)
        {
            auto& frame = result.PlanetFrames[index];
            frame.Planet = AssetId(1, index + 1);
            frame.RadiusMetres = 6360000.0;
            frame.ObserverFromCenterPlanetMetres = {0.0, 6360100.0, 0.0};
            auto& body = result.Volume.PlanetWeather.Bodies[index];
            body.Volume.Geometry = {1000.0F, 1000.0F, 3000.0F, 100000.0F};
            body.Volume.Quality = {1U, 1U, 48U, 8U};
            body.Volume.Density = {0.5F, 0.001F, 0.5F, 0.0F};
            body.Geometry = MakePlanetaryCloudUniforms(frame, 1000.0);
        }
        return result;
    }
} // namespace

TEST_CASE("Combined volume history requires compatible motion for every contributing planet")
{
    using namespace Keire::RenderBackend;
    const auto previous = PlanetVolumeView(10);
    auto current = PlanetVolumeView(11);
    bool compatible = true;
    SUBCASE("stationary profiles reuse history") {}
    SUBCASE("shared small advection reuses history")
    {
        for (std::size_t index = 0; index < 2; ++index)
        {
            current.Volume.PlanetWeather.Bodies[index].Volume.PatternOrigin[0] += 0.00001F;
        }
    }
    SUBCASE("different advection rejects shared history")
    {
        current.Volume.PlanetWeather.Bodies[1].Volume.PatternOrigin[0] += 0.00001F;
        compatible = false;
    }
    SUBCASE("nonselected profile change rejects history")
    {
        ++current.Volume.PlanetWeather.Bodies[1].Volume.Quality[1];
        compatible = false;
    }
    SUBCASE("nonselected frame discontinuity rejects history")
    {
        ++current.PlanetFrames[1].DiscontinuityRevision;
        compatible = false;
    }
    CHECK(MakeCloudTemporalReprojection(current, previous, true).has_value() == compatible);
}

TEST_CASE("Common scene fog prevents combined cloud history from following advected density")
{
    using namespace Keire::RenderBackend;
    auto previous = PlanetVolumeView(10);
    auto current = PlanetVolumeView(11);
    previous.Volume.Fog.Controls[0] = 1;
    current.Volume.Fog = previous.Volume.Fog;
    REQUIRE(MakeCloudTemporalReprojection(current, previous, true));
    for (std::size_t index = 0; index < 2; ++index)
    {
        current.Volume.PlanetWeather.Bodies[index].Volume.PatternOrigin[0] += 0.00001F;
    }
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
}
