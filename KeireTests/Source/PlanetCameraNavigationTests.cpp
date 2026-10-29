#include "KeireInternal/EditorCameraController.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

namespace
{
    void CheckDirection(const Keire::Vector3 actual, const Keire::Vector3 expected)
    {
        CHECK(actual.X == doctest::Approx(expected.X).epsilon(1.0e-5));
        CHECK(actual.Y == doctest::Approx(expected.Y).epsilon(1.0e-5));
        CHECK(actual.Z == doctest::Approx(expected.Z).epsilon(1.0e-5));
    }
} // namespace

TEST_CASE("Surface camera navigation transports local up without world-Y pole singularities")
{
    using namespace Keire;
    using namespace Keire::Detail;
    EditorCameraController camera;
    const auto originalForward = camera.Forward();
    camera.FollowSurfaceUp({1, 0, 0});
    CheckDirection(camera.Forward(), originalForward);
    camera.SetSurfaceNavigation(true);
    for (const Vector3 up : {Vector3{1, 0, 0}, Vector3{0, -1, 0}, Vector3{-1, 0, 0}, Vector3{0, 1, 0}})
    {
        camera.FollowSurfaceUp(up);
        CheckDirection(camera.NavigationUp(), up);
        const auto before = camera.State().Focus;
        CHECK(camera.Update({.DeltaSeconds = 0.25F, .MoveUp = 1.0F}));
        const auto after = camera.State().Focus;
        CHECK(after.X - before.X == doctest::Approx(up.X * 2.0));
        CHECK(after.Y - before.Y == doctest::Approx(up.Y * 2.0));
        CHECK(after.Z - before.Z == doctest::Approx(up.Z * 2.0));
        CHECK(std::isfinite(camera.Right().X));
        CHECK(std::isfinite(camera.Up().Y));
    }
    camera.FollowSurfaceUp({0, -1, 0});
    CheckDirection(camera.NavigationUp(), {0, -1, 0});
    const auto orientation = camera.State().NavigationToWorld.Components();
    CHECK_THROWS_AS(camera.FollowSurfaceUp({}), std::invalid_argument);
    CHECK(camera.State().NavigationToWorld.Components() == orientation);
}

TEST_CASE("Camera reference-frame round trips preserve pose and distant local navigation")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto rotation = FrameRotation::Normalize(1, 2, 3, 4);
    for (const double origin : {0.0, 1.0e12, -1.0e12})
    {
        EditorCameraController camera;
        const Vector3d focus{origin, origin, origin};
        camera.SetFocusPrecise(focus);
        const auto original = camera.Forward();
        for (int transfer = 0; transfer < 100; ++transfer)
        {
            camera.ReexpressReferenceFrame({5, 6, 7}, rotation);
            const auto expected = rotation.Apply({original.X, original.Y, original.Z});
            CheckDirection(camera.Forward(), {static_cast<float>(expected[0]), static_cast<float>(expected[1]),
                                              static_cast<float>(expected[2])});
            camera.ReexpressReferenceFrame(focus, rotation.Inverse());
        }
        CheckDirection(camera.Forward(), original);
        CHECK(camera.State().Focus == focus);
        const auto before = camera.State().NavigationToWorld.Components();
        CHECK_THROWS_AS(camera.ReexpressReferenceFrame({std::numeric_limits<double>::infinity(), 0, 0}, rotation),
                        std::invalid_argument);
        CHECK(camera.State().NavigationToWorld.Components() == before);
        CHECK(camera.State().Focus == focus);
    }
}
