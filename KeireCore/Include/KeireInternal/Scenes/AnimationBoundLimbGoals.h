#pragma once

#include "Keire/Animation/LimbRig.h"
#include "Keire/Animation/RiggingSystem.h"
#include "Keire/ECS/Components/AnimatorComponent.h"

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    class AnimationLimbBindingCache final
    {
      public:
        // Retaining the immutable sources prevents pointer reuse from making a failed key appear current.
        [[nodiscard]] std::string_view Bind(Ref<const SkeletonAsset> skeleton, Ref<const RigDefinitionAsset> rig,
                                            const std::uint64_t rigRevision, const std::uint64_t skeletonRevision)
        {
            if ((m_Binding || !m_Diagnostic.empty()) && m_Skeleton == skeleton && m_Rig == rig &&
                m_RigRevision == rigRevision && m_SkeletonRevision == skeletonRevision)
                return m_Diagnostic;
            m_Binding.reset();
            m_Diagnostic.clear();
            m_Skeleton = std::move(skeleton);
            m_Rig = std::move(rig);
            m_RigRevision = rigRevision;
            m_SkeletonRevision = skeletonRevision;
            ++m_BindingAttempts;
            try
            {
                if (!m_Rig)
                    throw std::invalid_argument("The assigned limb rig is unavailable.");
                m_Binding = std::make_unique<BoundLimbRig>(m_Skeleton, m_Rig->Definition().Limbs);
            }
            catch (const std::invalid_argument& error)
            {
                m_Diagnostic = std::string("Limb IK could not bind the assigned rig: ") + error.what();
            }
            return m_Diagnostic;
        }

        void Reset() noexcept
        {
            m_Binding.reset();
            m_Skeleton = {};
            m_Rig = {};
            m_Diagnostic.clear();
            m_RigRevision = 0;
            m_SkeletonRevision = 0;
        }

        [[nodiscard]] BoundLimbRig* Binding() const noexcept { return m_Binding.get(); }
        [[nodiscard]] std::uint64_t BindingAttempts() const noexcept { return m_BindingAttempts; }

      private:
        Ref<const SkeletonAsset> m_Skeleton;
        Ref<const RigDefinitionAsset> m_Rig;
        std::unique_ptr<BoundLimbRig> m_Binding;
        std::uint64_t m_RigRevision = 0;
        std::uint64_t m_SkeletonRevision = 0;
        std::uint64_t m_BindingAttempts = 0;
        std::string m_Diagnostic;
    };

    [[nodiscard]] inline std::string
    ApplyBoundAnimationLimbGoals(BoundLimbRig& rig, const std::span<const AnimatorLimbTarget> goals,
                                 const std::span<BoneTransform> pose, const std::optional<Matrix4>& worldToModel,
                                 const std::optional<Matrix4>& presentationWorldToModel,
                                 std::vector<LimbSolveResult>& results)
    {
        results.clear();
        std::vector<LimbTarget> targets;
        targets.reserve(goals.size());
        for (const auto& goal : goals)
        {
            auto target = goal.Target;
            if (goal.Space != AnimatorIkSpace::Model)
            {
                if (goal.Space != AnimatorIkSpace::World && goal.Space != AnimatorIkSpace::PresentationWorld)
                    return "Limb IK has an invalid coordinate space.";
                const auto& inverse = goal.Space == AnimatorIkSpace::World ? worldToModel : presentationWorldToModel;
                if (!inverse)
                    return "Limb IK could not resolve the Animator world transform; pose was preserved.";
                target.Position = Math::TransformPoint(*inverse, target.Position);
                target.Pole = Math::TransformPoint(*inverse, target.Pole);
            }
            targets.push_back(target);
        }
        if (!rig.Solve(pose, targets, results))
            return "Limb IK batch rejected: check stable IDs, transforms, joint lengths, and bend limits. Pose was "
                   "preserved.";
        return {};
    }
} // namespace Keire::Detail
