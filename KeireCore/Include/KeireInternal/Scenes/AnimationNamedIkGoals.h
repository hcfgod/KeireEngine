#pragma once

#include "Keire/Animation/RiggingSystem.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/Math/Math.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    [[nodiscard]] inline std::string
    ApplyNamedAnimationIkGoals(const SkeletonAsset& skeleton, const std::span<const AnimatorIkGoal> goals,
                               const std::span<BoneTransform> localPose,
                               const std::map<std::string, std::uint32_t, std::less<>>& indices,
                               const std::optional<Matrix4>& worldToModel)
    {
        std::string diagnostics;
        for (const auto& goal : goals)
        {
            const auto apply = [&]() -> std::string
            {
                std::vector<std::uint32_t> chain;
                chain.reserve(goal.Bones.size());
                for (const auto& name : goal.Bones)
                {
                    const auto found = indices.find(name);
                    if (found == indices.end())
                        return "IK goal '" + goal.Name + "' references missing bone '" + name + "'.";
                    chain.push_back(found->second);
                }
                auto target = goal.Target;
                auto pole = goal.Pole;
                if (goal.Space == AnimatorIkSpace::World)
                {
                    if (!worldToModel)
                        return "IK goal '" + goal.Name + "' could not resolve the Animator world transform.";
                    target = Math::TransformPoint(*worldToModel, target);
                    pole = Math::TransformPoint(*worldToModel, pole);
                }
                bool solved = false;
                if (goal.Solver == AnimatorIkSolver::TwoBone && chain.size() == 3)
                    solved =
                        SolveTwoBoneIk(skeleton, localPose, {chain[0], chain[1], chain[2], target, pole, goal.Weight});
                else if (goal.Solver == AnimatorIkSolver::Fabrik)
                    solved =
                        SolveFabrikIk(skeleton, localPose,
                                      {std::move(chain), target, goal.MaximumIterations, goal.Tolerance, goal.Weight});
                if (!solved)
                    return "IK goal '" + goal.Name +
                           "' could not solve its chain. Check bone order, joint lengths, "
                           "transforms, and target values.";
                return {};
            };
            const auto diagnostic = apply();
            if (!diagnostic.empty())
            {
                if (!diagnostics.empty())
                    diagnostics += '\n';
                diagnostics += diagnostic;
            }
        }
        return diagnostics;
    }
} // namespace Keire::Detail
