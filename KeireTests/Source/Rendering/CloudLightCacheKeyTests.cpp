#include "KeireInternal/Rendering/CloudLightCacheKey.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <initializer_list>

namespace
{
    using namespace Keire::RenderBackend;

    constexpr std::array<float, 3> Origin{-6000.0F, 1500.0F, -12000.0F};
    constexpr std::array<float, 3> CellSize{93.75F, 37.5F, 93.75F};
    constexpr std::array<std::uint32_t, 3> Dimensions{129U, 33U, 129U};

    CloudLightCacheKey Key(const CloudVolumeViewUniforms& view)
    {
        return MakeCloudLightCacheKey(view, Origin, CellSize, Dimensions);
    }
} // namespace

TEST_CASE("Cloud light cache keys distinguish planetary geometry identity and discontinuities")
{
    CloudVolumeViewUniforms view{};
    const auto flat = Key(view);
    Keire::Detail::ResolvedPlanetFrame frame;
    frame.Planet = {1, 2};
    frame.RadiusMetres = 6360000.0;
    frame.ObserverFromCenterPlanetMetres = {0.0, 6361000.0, 0.0};
    view.PlanetaryCloud = MakePlanetaryCloudUniforms(frame, 1000.0);
    const auto original = Key(view);
    CHECK(original != flat);
    view.PlanetaryCloud.RadiusGeometry.X += 100.0F;
    CHECK(Key(view) != original);
    auto changedIdentity = original;
    changedIdentity.Planet = {1, 3};
    CHECK(changedIdentity != original);
    auto changedRevision = original;
    ++changedRevision.DiscontinuityRevision;
    CHECK(changedRevision != original);
}

TEST_CASE("Cloud light cache rejects changed density, wind, geometry and shadow budgets")
{
    CloudVolumeViewUniforms baseline{};
    const auto original = Key(baseline);
    for (std::size_t axis = 0; axis < 4; ++axis)
    {
        auto changed = baseline;
        changed.Volume.Geometry[axis] = 1.0F;
        CHECK(Key(changed) != original);
        changed = baseline;
        changed.Volume.PatternOrigin[axis] = 1.0F;
        CHECK(Key(changed) != original);
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        auto changed = baseline;
        changed.Volume.Density[axis] = 1.0F;
        CHECK(Key(changed) != original);
    }
    for (const std::size_t index : {0U, 1U, 3U})
    {
        auto changed = baseline;
        changed.Volume.Quality[index] = 1U;
        CHECK(Key(changed) != original);
    }
}

TEST_CASE("Cloud light cache rejects shape, erosion, sun direction and grid changes")
{
    CloudVolumeViewUniforms baseline{};
    baseline.SunRadiance = {1, 1, 1, 0};
    const auto original = Key(baseline);
    for (const auto controls : {Keire::Vector4{1, 0, 0, 0}, Keire::Vector4{0, 1, 0, 0}, Keire::Vector4{0, 0, 1, 0},
                                Keire::Vector4{0, 0, 0, 1}})
    {
        auto changed = baseline;
        changed.ShapeControls = controls;
        CHECK(Key(changed) != original);
        changed = baseline;
        changed.HeightProfile = controls;
        CHECK(Key(changed) != original);
        changed = baseline;
        changed.WeatherControls = controls;
        CHECK(Key(changed) != original);
    }
    for (const auto controls : {Keire::Vector4{1, 0, 0, 0}, Keire::Vector4{0, 1, 0, 0}, Keire::Vector4{0, 0, 0, 1}})
    {
        auto changed = baseline;
        changed.RegionalControls = controls;
        CHECK(Key(changed) != original);
    }
    for (const auto direction : {Keire::Vector4{1, 0, 0, 0}, Keire::Vector4{0, 1, 0, 0}, Keire::Vector4{0, 0, 1, 0}})
    {
        auto changed = baseline;
        changed.SunDirection = direction;
        CHECK(Key(changed) != original);
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        auto origin = Origin;
        origin[axis] += 1.0F;
        CHECK(MakeCloudLightCacheKey(baseline, origin, CellSize, Dimensions) != original);
        auto cellSize = CellSize;
        cellSize[axis] += 1.0F;
        CHECK(MakeCloudLightCacheKey(baseline, Origin, cellSize, Dimensions) != original);
        auto dimensions = Dimensions;
        ++dimensions[axis];
        CHECK(MakeCloudLightCacheKey(baseline, Origin, CellSize, dimensions) != original);
    }
}

TEST_CASE("Cloud ambient occlusion invalidates only when its cached computation is enabled or disabled")
{
    CloudVolumeViewUniforms view{};
    const auto disabled = Key(view);
    view.AmbientRadiance.W = 0.25F;
    const auto enabled = Key(view);
    CHECK(enabled != disabled);
    view.AmbientRadiance.W = 1.0F;
    CHECK(Key(view) == enabled);
    view.AmbientRadiance.W = 0.0F;
    CHECK(Key(view) == disabled);
}

TEST_CASE("Cloud light cache permits view and lighting changes evaluated after lookup")
{
    CloudVolumeViewUniforms baseline{};
    baseline.SunRadiance = {1, 1, 1, 0};
    auto changed = baseline;
    changed.InverseView.Elements[0] = 2.0F;
    changed.InverseProjection.Elements[0] = 2.0F;
    changed.CameraControls = {1, 2, 3, 4};
    changed.SunRadiance = {1, 2, 3, 4};
    changed.AmbientRadiance = {1, 2, 3, 0};
    changed.Volume.Albedo = {0.5F, 0.6F, 0.7F, 0.8F};
    changed.Volume.Density[3] = 0.5F;
    changed.Volume.Quality[2] = 128U;
    changed.RegionalControls.Z = 2.0F;
    CHECK(Key(changed) == Key(baseline));
}

TEST_CASE("A dark sun does not invalidate cloud lighting until its energy returns")
{
    CloudVolumeViewUniforms night{};
    const auto darkKey = Key(night);
    night.SunDirection = {1, 0, 0, 0};
    CHECK(Key(night) == darkKey);
    night.SunDirection = {0, -1, 0, 0};
    CHECK(Key(night) == darkKey);
    for (const auto light :
         {Keire::Vector4{0.001F, 0, 0, 0}, Keire::Vector4{0, 0.001F, 0, 0}, Keire::Vector4{0, 0, 0.001F, 0}})
    {
        night.SunRadiance = light;
        CHECK(Key(night) != darkKey);
    }
    night.SunRadiance = {};
    CHECK(Key(night) == darkKey);
}
