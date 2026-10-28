#include "KeireInternal/Environment/AtmosphereGeometry.h"
#include "KeireInternal/Spatial/FrameSnapshot.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>

TEST_CASE("Atmosphere entry remains accurate on interplanetary approach rays")
{
    using namespace Keire::Detail;
    constexpr double radius = 6371000.0;
    constexpr double outerRadius = radius + 100000.0;
    for (const double distance : {1.0e10, 1.0e12, 1.0e15})
    {
        const auto interval = IntersectAtmosphere({distance, 0.0, 0.0}, {-1.0, 0.0, 0.0}, radius, outerRadius);
        REQUIRE(interval.has_value());
        CHECK(interval->GroundOccluded);
        CHECK(std::abs(interval->Begin - (distance - outerRadius)) <= 0.25);
        CHECK(std::abs(interval->End - (distance - radius)) <= 0.25);
        CHECK_FALSE(IntersectAtmosphere({distance, outerRadius + 10.0, 0.0}, {-1.0, 0.0, 0.0}, radius, outerRadius)
                        .has_value());
    }
}

TEST_CASE("Planetary ascent and return preserve local motion at distant sector origins")
{
    using namespace Keire::Detail;
    constexpr double radius = 6371000.0;
    constexpr double atmosphereHeight = 100000.0;
    constexpr double spin = 7.292115e-5;
    constexpr double orbitalSpeed = 29780.0;
    const SectorPosition planetOrigin({1, 1}, {1000000000000LL, -1000000000000LL, 0}, {0.125, 0.25, 0.5});
    const SectorPosition localOrigin({1, 2}, {}, {});
    const std::array heights{0.125, 2000.0, 100000.0, 400000.0, 10000000.0, 400000.0, 2000.0, 0.125};

    for (std::size_t step = 0; step < heights.size(); ++step)
    {
        const double time = static_cast<double>(step) * 600.0;
        const double angle = spin * time;
        const auto rotation = FrameRotation::Normalize(0.0, 0.0, std::sin(angle / 2.0), std::cos(angle / 2.0));
        const FrameSnapshot frame({1, 2}, planetOrigin, rotation, {orbitalSpeed, 0.0, 0.0}, {0.0, 0.0, spin}, time, 1);
        const double radialSpeed = step < heights.size() / 2 ? 1000.0 : -1000.0;
        const SpatialMotion local{
            localOrigin.Translated({radius + heights[step], 0.0, 0.0}), {radialSpeed, 0.0, 0.0}, {0.0, 0.0, 0.0}};
        const auto inertial = frame.ToParent(local);
        const auto restored = frame.ToChild(inertial);
        const auto error = restored.Position.RelativeTo(local.Position);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            CHECK(std::abs(error[axis]) < 1.0e-7);
            CHECK(restored.Velocity[axis] == doctest::Approx(local.Velocity[axis]).epsilon(1.0e-10));
            CHECK(std::abs(restored.Acceleration[axis]) < 1.0e-8);
        }
        // Independent rotating-frame velocity: orbital translation plus radial flight and omega cross radius.
        const double distance = radius + heights[step];
        CHECK(inertial.Velocity[0] ==
              doctest::Approx(orbitalSpeed + radialSpeed * std::cos(angle) - spin * distance * std::sin(angle)));
        CHECK(inertial.Velocity[1] ==
              doctest::Approx(radialSpeed * std::sin(angle) + spin * distance * std::cos(angle)));

        const std::array position{distance, 0.0, 0.0};
        const auto inward = IntersectAtmosphere(position, {-1.0, 0.0, 0.0}, radius, radius + atmosphereHeight);
        REQUIRE(inward.has_value());
        CHECK(inward->GroundOccluded);
        CHECK(inward->End == doctest::Approx(heights[step]));
        const auto outward = IntersectAtmosphere(position, {1.0, 0.0, 0.0}, radius, radius + atmosphereHeight);
        if (heights[step] >= atmosphereHeight)
        {
            CHECK_FALSE(outward.has_value());
        }
        else
        {
            REQUIRE(outward.has_value());
            CHECK_FALSE(outward->GroundOccluded);
            CHECK(outward->End == doctest::Approx(atmosphereHeight - heights[step]));
        }
    }
}
