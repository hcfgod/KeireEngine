#include "Keire/Math/Vector3d.h"

#include <doctest/doctest.h>

#include <limits>
#include <type_traits>

static_assert(!std::is_convertible_v<Keire::Vector3d, Keire::Vector3>);
static_assert(!std::is_convertible_v<Keire::Vector3, Keire::Vector3d>);

TEST_CASE("Precise positions subtract their origin before float conversion")
{
    const Keire::Vector3d origin{1.0e15, -1.0e15, 6371000.0};
    const Keire::Vector3d position{origin.X + 0.125, origin.Y - 0.25, origin.Z + 0.001};
    const auto relative = Keire::Math::RelativeFloat(position, origin, 1.0e-9);
    CHECK(relative.X == 0.125F);
    CHECK(relative.Y == -0.25F);
    CHECK(relative.Z == doctest::Approx(0.001).epsilon(1.0e-6));
    CHECK(Keire::Math::Widen({1.0F, 2.0F, 3.0F}) == Keire::Vector3d{1.0, 2.0, 3.0});
}

TEST_CASE("Relative float conversion rejects range precision and nonfinite failures")
{
    using Keire::Math::RelativeFloat;
    CHECK_THROWS_AS((void)RelativeFloat({16777217.0, 0.0, 0.0}, {}, 0.5), std::out_of_range);
    CHECK(RelativeFloat({16777217.0, 0.0, 0.0}, {}, 1.0).X == 16777216.0F);
    const double maximum = std::numeric_limits<double>::max();
    CHECK_THROWS_AS((void)RelativeFloat({maximum, 0, 0}, {-maximum, 0, 0}, 1.0), std::out_of_range);
    CHECK_THROWS_AS((void)RelativeFloat({maximum, 0, 0}, {}, 1.0), std::out_of_range);
    CHECK_THROWS_AS((void)RelativeFloat({}, {}, -1.0), std::invalid_argument);
    CHECK_THROWS_AS((void)RelativeFloat({}, {}, std::numeric_limits<double>::infinity()), std::invalid_argument);
    CHECK_THROWS_AS((void)RelativeFloat({std::numeric_limits<double>::quiet_NaN(), 0, 0}, {}, 0.0),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)RelativeFloat({}, {0, std::numeric_limits<double>::infinity(), 0}, 0.0),
                    std::invalid_argument);
}
