#include "KeireInternal/Rendering/CloudTemporalReprojection.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <limits>

namespace
{
    using namespace Keire::RenderBackend;

    CloudTemporalView View(const std::uint64_t frame)
    {
        CloudTemporalView view;
        view.FrameId = frame;
        view.Volume.Volume.Geometry = {1000.0F, 1000.0F, 3000.0F, 100000.0F};
        view.Volume.Volume.Quality = {1U, 1U, 48U, 8U};
        view.Volume.SunDirection = {0.0F, 1.0F, 0.0F, 0.0F};
        view.Volume.SunRadiance = {1.0F, 1.0F, 1.0F, 0.0F};
        return view;
    }
} // namespace

TEST_CASE("Cloud reprojection separates horizontal wind from camera motion and crosses phase wraps")
{
    auto previous = View(10);
    auto current = View(11);
    current.Volume.InverseView.Elements[12] = 30.0F;
    current.Volume.Volume.PatternOrigin[0] = 0.007F; // Camera +30 m and wind +9 m.
    auto result = MakeCloudTemporalReprojection(current, previous, true);
    REQUIRE(result);
    CHECK(result->CloudPositionOffset.X == doctest::Approx(21.0F));
    CHECK(result->ScenePositionOffset.X == 30.0F);
    CHECK(result->HistoryControls.X == 0.875F);

    previous.Volume.Volume.PatternOrigin[2] = 255.99F;
    current.Volume.Volume.PatternOrigin[2] = 0.01F;
    result = MakeCloudTemporalReprojection(current, previous, true);
    REQUIRE(result);
    CHECK(result->CloudPositionOffset.Z == doctest::Approx(60.0F).epsilon(0.002));
    CHECK(result->ScenePositionOffset.Z == 0.0F);

    current.Volume.InverseView.Elements[13] = 30.0F;
    current.Volume.Volume.Geometry[0] = 970.0F;
    current.Volume.Volume.PatternOrigin[1] = 0.01F;
    result = MakeCloudTemporalReprojection(current, previous, true);
    REQUIRE(result);
    CHECK(result->CloudPositionOffset.Y == 30.0F);
    CHECK(result->ScenePositionOffset.Y == 30.0F);
    current.Volume.Volume.PatternOrigin[1] = 0.02F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
}

TEST_CASE("Cloud reprojection resets discontinuities while tolerating projection jitter and exposure")
{
    const auto previous = View(10);
    auto current = View(11);
    REQUIRE(MakeCloudTemporalReprojection(current, previous, true));
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, false));
    current.FrameId = 12;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
    current.FrameId = 11;
    current.Volume.InverseProjection.Elements[8] = 0.001F;
    current.Volume.CameraControls.Y = 10.0F;
    CHECK(MakeCloudTemporalReprojection(current, previous, true));
    current.UnjitteredProjection.Elements[0] = 2.0F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
}

TEST_CASE("Large cloud patterns retain history despite quantized wrapped camera height")
{
    auto previous = View(10);
    auto current = View(11);
    previous.Volume.Volume.Geometry[2] = 100000.0F;
    current.Volume.Volume.Geometry[2] = 100000.0F;
    previous.Volume.Volume.PatternOrigin[1] = 255.0F;
    current.Volume.Volume.PatternOrigin[1] = static_cast<float>(255.0 + 1.0 / 100000.0);
    current.Volume.InverseView.Elements[13] = 1.0F;
    current.Volume.Volume.Geometry[0] = 999.0F;
    REQUIRE(MakeCloudTemporalReprojection(current, previous, true));
    current.Volume.Volume.PatternOrigin[1] += 0.01F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
}

TEST_CASE("Cloud reprojection rejects cloud edits, light jumps, cuts and invalid displacement")
{
    const auto previous = View(10);
    auto current = View(11);
    current.Volume.WeatherControls.W = 1000.0F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
    current = View(11);
    current.Volume.Volume.Density[0] = 0.9F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
    current = View(11);
    current.Volume.SunRadiance.X = 1.005F;
    CHECK(MakeCloudTemporalReprojection(current, previous, true));
    current.Volume.SunRadiance.X = 2.0F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
    current = View(11);
    current.Volume.InverseView.Elements[10] = -1.0F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
    current = View(11);
    current.Volume.InverseView.Elements[12] = 1000.0F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
    current = View(11);
    current.Volume.Volume.PatternOrigin[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
}

TEST_CASE("Fog exclusion edits invalidate accumulated cloud and fog history")
{
    auto previous = View(10);
    previous.Volume.Fog.Controls = {1U, 64U, 0U, 16U};
    auto current = previous;
    current.FrameId = 11;
    REQUIRE(MakeCloudTemporalReprojection(current, previous, true));

    for (std::size_t index = 0; index < previous.Volume.Fog.Exclusions.size(); ++index)
    {
        current.Volume.Fog = previous.Volume.Fog;
        current.Volume.Fog.Exclusions[index].CenterExtinction[3] = 0.5F;
        CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));

        current.Volume.Fog = previous.Volume.Fog;
        current.Volume.Fog.Exclusions[index].HalfExtentEdge[3] = 0.25F;
        CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
    }

    current.Volume.Fog = previous.Volume.Fog;
    current.Volume.Fog.Controls[3] = 0U;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
}

TEST_CASE("Combined cloud fog history rejects fog edits and mismatched medium motion")
{
    auto previous = View(10);
    previous.Volume.Fog.Controls = {1U, 64U, 0U, 0U};
    previous.Volume.Fog.HeightDensity = {0.0F, 0.01F, 0.002F, 1000.0F};
    auto current = previous;
    current.FrameId = 11;
    REQUIRE(MakeCloudTemporalReprojection(current, previous, true));
    current.Volume.Fog.HeightDensity[2] = 0.003F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
    current.Volume.Fog = previous.Volume.Fog;
    current.Volume.Volume.PatternOrigin[0] = 0.001F;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
    current.Volume.Volume.PatternOrigin[0] = 0.0F;
    current.Volume.Fog.Controls[0] = 0U;
    CHECK_FALSE(MakeCloudTemporalReprojection(current, previous, true));
}
