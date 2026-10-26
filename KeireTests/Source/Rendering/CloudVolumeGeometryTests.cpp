#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    using std::isfinite;
    using std::max;
    using std::min;

#include "../../../KeireCore/Shaders/CloudVolumeGeometry.hlsli"
} // namespace

TEST_CASE("Cloud volume slab clips below inside and above views to scene depth")
{
    const auto below = IntersectCloudVolumeSlab(0.0F, 1.0F, 1000.0F, 500.0F, 10000.0F);
    CHECK(below.Entry == 1000.0F);
    CHECK(below.Exit == 1500.0F);

    const auto above = IntersectCloudVolumeSlab(2000.0F, -1.0F, 1000.0F, 500.0F, 10000.0F);
    CHECK(above.Entry == 500.0F);
    CHECK(above.Exit == 1000.0F);

    const auto inside = IntersectCloudVolumeSlab(1250.0F, 1.0F, 1000.0F, 500.0F, 100.0F);
    CHECK(inside.Entry == 0.0F);
    CHECK(inside.Exit == 100.0F);

    const auto occluded = IntersectCloudVolumeSlab(0.0F, 1.0F, 1000.0F, 500.0F, 900.0F);
    CHECK(occluded.Entry == occluded.Exit);
    const auto lookingAway = IntersectCloudVolumeSlab(0.0F, -1.0F, 1000.0F, 500.0F, 10000.0F);
    CHECK(lookingAway.Entry == lookingAway.Exit);
}

TEST_CASE("Cloud volume tangent rays retain bounded inside travel without inventing boundary density")
{
    CHECK(IntersectCloudVolumeSlab(1250.0F, 0.0F, 1000.0F, 500.0F, 20000.0F).Exit == 20000.0F);
    CHECK(IntersectCloudVolumeSlab(1250.0F, 0.00000001F, 1000.0F, 500.0F, 20000.0F).Exit == 20000.0F);
    CHECK(IntersectCloudVolumeSlab(1000.0F, 0.0F, 1000.0F, 500.0F, 20000.0F).Exit == 0.0F);
    CHECK(IntersectCloudVolumeSlab(1500.0F, 0.0F, 1000.0F, 500.0F, 20000.0F).Exit == 0.0F);
}

TEST_CASE("Cloud volume slab rejects invalid or unrepresentable geometry")
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(IntersectCloudVolumeSlab(nan, 1.0F, 1000.0F, 500.0F, 20000.0F).Exit == 0.0F);
    CHECK(IntersectCloudVolumeSlab(0.0F, infinity, 1000.0F, 500.0F, 20000.0F).Exit == 0.0F);
    CHECK(IntersectCloudVolumeSlab(0.0F, 1.0F, 1000.0F, -1.0F, 20000.0F).Exit == 0.0F);
    CHECK(IntersectCloudVolumeSlab(0.0F, 1.0F, 1000.0F, 500.0F, infinity).Exit == 0.0F);
    CHECK(IntersectCloudVolumeSlab(0.0F, 1.0F, 1.0e30F, 1.0F, 20000.0F).Exit == 0.0F);
}
