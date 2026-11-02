#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace
{
    using uint = std::uint32_t;
    using std::floor;
    using std::isfinite;
    using std::max;
    using std::min;

#include "../../../KeireCore/Shaders/PlanetVolumeIntervalGrid.hlsli"

    struct HomogeneousVolume
    {
        CloudVolumeInterval Interval;
        double Extinction;
        double Source;
        uint Samples;
    };

    struct ScalarTransport
    {
        double Radiance = 0.0;
        double Transmission = 1.0;
    };

    void AddSegment(ScalarTransport& transport, const double length, const double extinction, const double source)
    {
        const double opacity = -std::expm1(-length * extinction);
        transport.Radiance += transport.Transmission * opacity * source;
        transport.Transmission *= 1.0 - opacity;
    }
} // namespace

TEST_CASE("Merged planetary volume grids integrate overlapping media independently of candidate order")
{
    std::array volumes{HomogeneousVolume{{1.0F, 3.0F}, 0.2, 2.0, 7}, HomogeneousVolume{{2.0F, 4.0F}, 0.3, 5.0, 11}};
    SUBCASE("stable forward order") {}
    SUBCASE("reversed candidate order") { std::reverse(volumes.begin(), volumes.end()); }
    for (const float phase : {0.0F, 0.125F, 0.5F, 0.9999F})
    {
        for (const float surface : {3.25F, 4.0F, 100.0F})
        {
            CAPTURE(phase);
            CAPTURE(surface);
            ScalarTransport actual;
            float cursor = 0.0F;
            uint iterations = 0;
            while (cursor < surface && iterations < 64)
            {
                float boundary = surface;
                for (const auto& volume : volumes)
                {
                    boundary = NextPlanetVolumeGridBoundary(cursor, boundary, volume.Interval, volume.Samples, phase);
                }
                REQUIRE(boundary > cursor);
                const double midpoint = double(cursor) + (double(boundary) - cursor) * 0.5;
                double extinction = 0.0;
                double weightedSource = 0.0;
                for (const auto& volume : volumes)
                {
                    if (midpoint > volume.Interval.Entry && midpoint < volume.Interval.Exit)
                    {
                        extinction += volume.Extinction;
                        weightedSource += volume.Extinction * volume.Source;
                    }
                }
                if (extinction > 0.0)
                {
                    AddSegment(actual, double(boundary) - cursor, extinction, weightedSource / extinction);
                }
                cursor = boundary;
                ++iterations;
            }
            REQUIRE(cursor == surface);
            ScalarTransport expected;
            AddSegment(expected, 1.0, 0.2, 2.0);
            AddSegment(expected, 1.0, 0.5, (0.2 * 2.0 + 0.3 * 5.0) / 0.5);
            AddSegment(expected, std::min(double(surface), 4.0) - 3.0, 0.3, 5.0);
            CHECK(actual.Radiance == doctest::Approx(expected.Radiance).epsilon(1.0e-10));
            CHECK(actual.Transmission == doctest::Approx(expected.Transmission).epsilon(1.0e-10));
        }
    }
}

TEST_CASE("Distant thin planetary intervals retain strict progress when grid boundaries round together")
{
    const CloudVolumeInterval interval{1000000.0F, 1000000.125F};
    float cursor = 0.0F;
    double occupiedLength = 0.0;
    uint iterations = 0;
    while (cursor < interval.Exit && iterations < 132)
    {
        const float boundary = NextPlanetVolumeGridBoundary(cursor, interval.Exit, interval, 128, 0.125F);
        REQUIRE(boundary > cursor);
        if (cursor >= interval.Entry)
        {
            occupiedLength += double(boundary) - cursor;
        }
        cursor = boundary;
        ++iterations;
    }
    CHECK(cursor == interval.Exit);
    CHECK(occupiedLength == 0.125);
    CHECK(iterations <= 3);
}
