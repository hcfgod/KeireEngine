#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace
{
    using std::abs;
    using std::isfinite;
    using std::max;

#include "../../../KeireCore/Shaders/CloudTemporalRejection.hlsli"
} // namespace

TEST_CASE("Cloud history accepts nearby depth and rejects invalid or unrelated layers")
{
    CHECK(CloudTemporalDepthMatches(1000.0F, 1010.0F, 10.0F, 0.0F));
    CHECK_FALSE(CloudTemporalDepthMatches(1000.0F, 1011.0F, 10.0F, 0.0F));
    CHECK(CloudTemporalDepthMatches(100000.0F, 100100.0F, 10.0F, 0.001F));
    CHECK_FALSE(CloudTemporalDepthMatches(100000.0F, 100101.0F, 10.0F, 0.001F));
    CHECK_FALSE(CloudTemporalDepthMatches(1000.0F, 500.0F, 10.0F, 0.001F));
    CHECK(CloudTemporalDepthMatches(1000.0F, 1000.0F, -10.0F, -1.0F));

    for (const float invalid :
         {0.0F, -1.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        CHECK_FALSE(CloudTemporalDepthMatches(invalid, 1000.0F, 10.0F, 0.001F));
        CHECK_FALSE(CloudTemporalDepthMatches(1000.0F, invalid, 10.0F, 0.001F));
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(CloudTemporalDepthMatches(1000.0F, 1000.0F, nan, 0.001F));
    CHECK_FALSE(CloudTemporalDepthMatches(1000.0F, 1000.0F, 1.0F, nan));
}

TEST_CASE("Cloud history rejects sky and foreground disocclusion without confusing camera motion")
{
    CHECK(CloudTemporalSceneMatches(-1.0F, -1.0F, -1.0F, 1.0F, 0.001F));
    CHECK_FALSE(CloudTemporalSceneMatches(-1.0F, 100.0F, -1.0F, 1.0F, 0.001F));
    CHECK_FALSE(CloudTemporalSceneMatches(100.0F, -1.0F, 100.0F, 1.0F, 0.001F));
    // The camera moved 20 m towards the same surface; compare its reprojected endpoint, not current depth.
    CHECK(CloudTemporalSceneMatches(80.0F, 100.0F, 100.0F, 1.0F, 0.001F));
    CHECK_FALSE(CloudTemporalSceneMatches(80.0F, 50.0F, 100.0F, 1.0F, 0.001F));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(CloudTemporalSceneMatches(nan, -1.0F, -1.0F, 1.0F, 0.001F));
    CHECK_FALSE(CloudTemporalSceneMatches(-1.0F, nan, -1.0F, 1.0F, 0.001F));
    CHECK_FALSE(CloudTemporalSceneMatches(80.0F, 100.0F, nan, 1.0F, 0.001F));
}
