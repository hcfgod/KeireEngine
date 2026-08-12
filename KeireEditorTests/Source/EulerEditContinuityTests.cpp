#include "KeireClient/Editor/EulerEditContinuity.h"

#include <doctest/doctest.h>

TEST_CASE("Continuous Euler editing crosses positive yaw 180 without changing the other axes")
{
    Keire::Vector3 displayed{10.0F, 170.0F, 20.0F};
    for (float yaw = 171.0F; yaw <= 190.0F; yaw += 1.0F)
    {
        displayed =
            KeireEditor::ContinuousEulerAngles(Keire::Math::EulerDegreesToQuaternion({10.0F, yaw, 20.0F}), displayed);
        CHECK(displayed.X == doctest::Approx(10.0F).epsilon(0.001));
        CHECK(displayed.Y == doctest::Approx(yaw).epsilon(0.001));
        CHECK(displayed.Z == doctest::Approx(20.0F).epsilon(0.001));
    }
}

TEST_CASE("Continuous Euler editing crosses negative yaw 180 without changing the other axes")
{
    Keire::Vector3 displayed{-12.0F, -170.0F, 35.0F};
    for (float yaw = -171.0F; yaw >= -190.0F; yaw -= 1.0F)
    {
        displayed =
            KeireEditor::ContinuousEulerAngles(Keire::Math::EulerDegreesToQuaternion({-12.0F, yaw, 35.0F}), displayed);
        CHECK(displayed.X == doctest::Approx(-12.0F).epsilon(0.001));
        CHECK(displayed.Y == doctest::Approx(yaw).epsilon(0.001));
        CHECK(displayed.Z == doctest::Approx(35.0F).epsilon(0.001));
    }
}

TEST_CASE("Quaternion sign does not look like an external rotation change")
{
    const auto rotation = Keire::Math::EulerDegreesToQuaternion({23.0F, 181.0F, -7.0F});
    CHECK(KeireEditor::SameRotation(rotation, {-rotation.X, -rotation.Y, -rotation.Z, -rotation.W}));
}
