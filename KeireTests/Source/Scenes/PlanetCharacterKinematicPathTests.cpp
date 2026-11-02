#include "KeireInternal/Spatial/PlanetCharacterKinematicPath.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

TEST_CASE("Planet character kinematic path follows center of mass with rotating capsule offset")
{
    using namespace Keire;
    PhysicsCapsuleOverlapQuery capsule;
    capsule.Center = {2.0F, 0.0F, 0.0F};
    capsule.Radius = 0.25F;
    capsule.Height = 1.0F;
    const float halfAngle = std::sqrt(0.5F);
    const Quaternion target{0.0F, 0.0F, halfAngle, halfAngle};
    const auto path = Detail::MakePlanetCharacterKinematicPath(capsule, {1.0F, 0.0F, 0.0F}, {3.0F, 1.0F, 0.0F}, target);
    const auto start = path(0.0, 0.0).Midpoint;
    CHECK(start.Center.X == doctest::Approx(2.0));
    CHECK(start.Center.Y == doctest::Approx(0.0));
    const auto middle = path(0.5, 0.5).Midpoint;
    CHECK(middle.Center.X == doctest::Approx(2.0 + std::sqrt(0.5)));
    CHECK(middle.Center.Y == doctest::Approx(0.5 + std::sqrt(0.5)));
    const auto end = path(1.0, 1.0).Midpoint;
    CHECK(end.Center.X == doctest::Approx(3.0));
    CHECK(end.Center.Y == doctest::Approx(2.0));
    // A straight interpolation of object origins would miss this outward arc.
    CHECK(middle.Center.X > (start.Center.X + end.Center.X) * 0.5F);
    const auto same = Detail::MakePlanetCharacterKinematicPath(capsule, {1.0F, 0.0F, 0.0F}, {3.0F, 1.0F, 0.0F},
                                                               {0.0F, 0.0F, -halfAngle, -halfAngle});
    CHECK(same(0.5, 0.5).Midpoint.Center == middle.Center);
    CHECK_THROWS_AS((void)Detail::MakePlanetCharacterKinematicPath(
                        capsule, {}, {std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, target),
                    std::invalid_argument);
}
