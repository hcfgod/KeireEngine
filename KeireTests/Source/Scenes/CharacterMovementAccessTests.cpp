#include "KeireInternal/Scenes/CharacterMovementAccess.h"

#include <doctest/doctest.h>

TEST_CASE("Prepared character movement preserves commands until explicit commit")
{
    Keire::CharacterControllerComponent controller;
    REQUIRE(controller.QueueDesiredMovement({1.0F, 2.0F, 0.0F}));
    REQUIRE(controller.QueueDesiredMovement({-0.5F, 0.0F, 3.0F}));
    using Access = Keire::Detail::CharacterMovementAccess;
    const auto prepared = Access::Capture(controller);
    CHECK(prepared.Count == 2);
    CHECK(prepared.Displacement == Keire::Vector3{0.5F, 2.0F, 3.0F});
    CHECK(Access::Capture(controller).Commands == prepared.Commands);
    CHECK(controller.RuntimeState().DroppedMovementCommands == 0);
    // A rejected geometry plan simply discards its snapshot; no queue restoration occurs.
    CHECK_NOTHROW(Access::Validate(controller, prepared));
    Access::Commit(controller, prepared);
    CHECK(Access::Capture(controller).Count == 0);
    CHECK_THROWS_AS(Access::Commit(controller, prepared), std::logic_error);
}

TEST_CASE("Prepared character movement rejects callback changes without losing live input")
{
    Keire::CharacterControllerComponent controller;
    REQUIRE(controller.QueueDesiredMovement({1.0F, 0.0F, 0.0F}));
    using Access = Keire::Detail::CharacterMovementAccess;
    const auto prepared = Access::Capture(controller);
    SUBCASE("append") { REQUIRE(controller.QueueDesiredMovement({0.0F, 2.0F, 0.0F})); }
    SUBCASE("replace with same count")
    {
        controller.ClearDesiredMovement();
        REQUIRE(controller.QueueDesiredMovement({0.0F, 2.0F, 0.0F}));
    }
    const auto before = Access::Capture(controller);
    CHECK_THROWS_AS(Access::Commit(controller, prepared), std::logic_error);
    const auto after = Access::Capture(controller);
    CHECK(after.Count == before.Count);
    CHECK(after.Commands == before.Commands);
    CHECK(after.Displacement == before.Displacement);
    Keire::CharacterControllerComponent other;
    REQUIRE(other.QueueDesiredMovement({1.0F, 0.0F, 0.0F}));
    CHECK_THROWS_AS(Access::Commit(other, prepared), std::logic_error);
    CHECK(Access::Capture(other).Count == 1);
}
