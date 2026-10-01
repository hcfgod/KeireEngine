#include "Keire/ECS/Components/AnimatorComponent.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

TEST_CASE("Animator rejects unknown IK coordinate spaces without changing goals")
{
    for (const bool originalTwoBone : {false, true})
    {
        for (const bool requestedTwoBone : {false, true})
        {
            for (const bool replace : {false, true})
            {
                for (const auto invalid : {3, 255})
                {
                    Keire::AnimatorComponent animator;
                    if (originalTwoBone)
                        animator.SetTwoBoneIk("arm", "upper", "lower", "hand", {1, 2, 3}, {4, 5, 6}, 0.75F);
                    else
                        animator.SetFabrikIk("arm", {"root", "tip"}, {1, 2, 3}, 0.75F, 24, 0.002F,
                                             Keire::AnimatorIkSpace::Model);
                    const auto previous = animator.IkGoals().front();
                    const std::string name = replace ? "arm" : "new-goal";
                    const auto space = static_cast<Keire::AnimatorIkSpace>(invalid);
                    if (requestedTwoBone)
                        CHECK_THROWS_AS(animator.SetTwoBoneIk(name, "a", "b", "c", {7, 8, 9}, {}, 0.5F, space),
                                        std::invalid_argument);
                    else
                        CHECK_THROWS_AS(animator.SetFabrikIk(name, {"a", "b"}, {7, 8, 9}, 0.5F, 6, 0.01F, space),
                                        std::invalid_argument);
                    REQUIRE(animator.IkGoals().size() == 1);
                    const auto& current = animator.IkGoals().front();
                    CHECK(current.Name == previous.Name);
                    CHECK(current.Solver == previous.Solver);
                    CHECK(current.Space == previous.Space);
                    CHECK(current.Bones == previous.Bones);
                    CHECK(current.Target == previous.Target);
                    CHECK(current.Pole == previous.Pole);
                    CHECK(current.Weight == previous.Weight);
                    CHECK(current.MaximumIterations == previous.MaximumIterations);
                    CHECK(current.Tolerance == previous.Tolerance);
                }
            }
        }
    }
}

TEST_CASE("Animator accepts every supported IK coordinate space for both solvers")
{
    for (const auto space :
         {Keire::AnimatorIkSpace::Model, Keire::AnimatorIkSpace::World, Keire::AnimatorIkSpace::PresentationWorld})
    {
        Keire::AnimatorComponent animator;
        animator.SetFabrikIk("leg", {"root", "tip"}, {1, 2, 3}, 1.0F, 24, 0.002F, space);
        REQUIRE(animator.IkGoals().size() == 1);
        CHECK(animator.IkGoals().front().Space == space);
        CHECK(animator.IkGoals().front().Solver == Keire::AnimatorIkSolver::Fabrik);
        animator.SetTwoBoneIk("leg", "upper", "lower", "foot", {4, 5, 6}, {7, 8, 9}, 0.75F, space);
        REQUIRE(animator.IkGoals().size() == 1);
        CHECK(animator.IkGoals().front().Space == space);
        CHECK(animator.IkGoals().front().Solver == Keire::AnimatorIkSolver::TwoBone);
        CHECK(animator.IkGoals().front().Target == Keire::Vector3(4, 5, 6));
        CHECK(animator.IkGoals().front().Pole == Keire::Vector3(7, 8, 9));
    }
}
