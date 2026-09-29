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
                for (const auto invalid : {2, 255})
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
