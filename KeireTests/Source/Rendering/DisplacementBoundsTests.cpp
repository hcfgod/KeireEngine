#include "KeireInternal/Rendering/DisplacementBoundsInternal.h"

#include <doctest/doctest.h>

#include <limits>
#include <optional>

TEST_CASE("current-pose and mesh VFX bounds inflate by an unscaled world displacement radius")
{
    using namespace Keire;
    using namespace Keire::RenderBackend::DisplacementBounds;

    const MeshBounds local{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    const auto world = Math::ComposeTransform({10.0F, -2.0F, 5.0F}, {}, {2.0F, 3.0F, 4.0F});
    const auto inflated = WorldBounds(local, world, 1.0F);
    REQUIRE(inflated);
    CHECK(inflated->Minimum.X == doctest::Approx(7.0F));
    CHECK(inflated->Minimum.Y == doctest::Approx(-6.0F));
    CHECK(inflated->Minimum.Z == doctest::Approx(0.0F));
    CHECK(inflated->Maximum.X == doctest::Approx(13.0F));
    CHECK(inflated->Maximum.Y == doctest::Approx(2.0F));
    CHECK(inflated->Maximum.Z == doctest::Approx(10.0F));

    CHECK_FALSE(WorldBounds(local, world, std::nullopt));
    CHECK_FALSE(WorldBounds(local, world, std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE("spatial whole-containment reserves the transformed world displacement margin")
{
    using namespace Keire;
    using namespace Keire::RenderBackend::DisplacementBounds;

    const MeshBounds centered{{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};
    const Matrix4 identity;
    CHECK(WhollyContained(centered, identity, identity, {1.0F, 1.0F, 1.0F}, 0.49F));
    CHECK_FALSE(WhollyContained(centered, identity, identity, {1.0F, 1.0F, 1.0F}, 0.51F));
    CHECK_FALSE(WhollyContained(centered, identity, identity, {1.0F, 1.0F, 1.0F}, std::nullopt));

    const MeshBounds point{{}, {}};
    const auto volumeToWorld = Math::ComposeTransform({}, {}, {2.0F, 4.0F, 8.0F});
    const auto worldToVolume = Math::Inverse(volumeToWorld);
    CHECK(WhollyContained(point, identity, worldToVolume, {1.0F, 1.0F, 1.0F}, 1.9F));
    CHECK_FALSE(WhollyContained(point, identity, worldToVolume, {1.0F, 1.0F, 1.0F}, 2.1F));
}
