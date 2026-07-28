#include "Keire/Core.h"

#include <doctest/doctest.h>

TEST_CASE("Animator IK goals are named persistent runtime state")
{
    Keire::AnimatorComponent animator;
    animator.SetTwoBoneIk("left-hand", "shoulder", "elbow", "hand", {1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 1.0F});

    REQUIRE(animator.IkGoals().size() == 1);
    CHECK(animator.IkGoals().front().Name == "left-hand");
    CHECK(animator.IkGoals().front().Solver == Keire::AnimatorIkSolver::TwoBone);
    CHECK(animator.IkGoals().front().Space == Keire::AnimatorIkSpace::World);
    CHECK(animator.IkGoals().front().Bones == std::vector<std::string>{"shoulder", "elbow", "hand"});

    animator.SetTwoBoneIk("left-hand", "upper", "lower", "tip", {4.0F, 5.0F, 6.0F}, {}, 0.5F,
                          Keire::AnimatorIkSpace::Model);
    REQUIRE(animator.IkGoals().size() == 1);
    CHECK(animator.IkGoals().front().Bones == std::vector<std::string>{"upper", "lower", "tip"});
    CHECK(animator.IkGoals().front().Weight == doctest::Approx(0.5F));
    CHECK(animator.ClearIk("left-hand"));
    CHECK(animator.IkGoals().empty());
    CHECK_FALSE(animator.ClearIk("left-hand"));
}

TEST_CASE("Animator rejects invalid IK state without replacing a last-good goal")
{
    Keire::AnimatorComponent animator;
    animator.SetTwoBoneIk("arm", "upper", "lower", "hand", {}, {});

    CHECK_THROWS_AS(animator.SetTwoBoneIk("arm", "upper", "lower", "hand", {}, {}, 2.0F), std::invalid_argument);
    REQUIRE(animator.IkGoals().size() == 1);
    CHECK(animator.IkGoals().front().Weight == doctest::Approx(1.0F));

    CHECK_THROWS_AS(animator.SetFabrikIk("spine", {"root"}, {}, 1.0F), std::invalid_argument);
    CHECK_THROWS_AS(animator.SetFabrikIk("spine", {"root", "chest"}, {}, 1.0F, 0), std::invalid_argument);
    CHECK(animator.IkGoals().size() == 1);
}
