#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

#include "KeireInternal/Scenes/AnimationBoundLimbGoals.h"

#include <exception>
#include <optional>
#include <utility>

namespace Keire
{
    std::string SceneRuntimeSession::Impl::ApplyLimbGoals(const Entity& entity, Ref<const SkeletonAsset> skeleton,
                                                          AnimatorComponent& animator,
                                                          const std::span<BoneTransform> pose,
                                                          AnimationRuntimeState& state,
                                                          const bool presentationEvaluation)
    {
        animator.SetRuntimeLimbResults({});
        if (animator.LimbIkTargets().empty())
            return {};
        try
        {
            if (state.LimbRigAsset != animator.RigDefinition())
            {
                state.LimbBinding.Reset();
                state.LimbRigAsset = animator.RigDefinition();
                state.LimbRigHandle = {};
                if (state.LimbRigAsset)
                    state.LimbRigHandle = Assets->Load<RigDefinitionAsset>(state.LimbRigAsset, AssetPriority::High);
            }
            if (!state.LimbRigAsset)
                return "Limb IK needs a Rig Definition asset with named limb definitions assigned to this Animator.";
            if (state.LimbRigHandle.State() == AssetState::Failed ||
                state.LimbRigHandle.State() == AssetState::Cancelled)
                (void)state.LimbRigHandle.Require();
            const auto rig = state.LimbRigHandle.TryGetLoaded();
            if (!rig)
            {
                state.LimbBinding.Reset();
                return "Limb IK is waiting for its Rig Definition asset to load.";
            }
            const auto rigRevision = state.LimbRigHandle.Revision();
            const auto skeletonRevision = state.SkeletonHandle.Revision();
            const auto bindingDiagnostic = state.LimbBinding.Bind(skeleton, rig, rigRevision, skeletonRevision);
            if (!bindingDiagnostic.empty())
                return std::string(bindingDiagnostic);
            std::optional<Matrix4> worldToModel;
            std::optional<Matrix4> presentationWorldToModel;
            if (const auto transform = entity.GetComponent<TransformComponent>())
            {
                try
                {
                    worldToModel = Math::Inverse(transform->WorldMatrix());
                    presentationWorldToModel =
                        presentationEvaluation ? Math::Inverse(transform->PresentationWorldMatrix()) : worldToModel;
                }
                catch (const std::exception&)
                {
                }
            }
            std::vector<LimbSolveResult> results;
            const auto diagnostic =
                Detail::ApplyBoundAnimationLimbGoals(*state.LimbBinding.Binding(), animator.LimbIkTargets(), pose,
                                                     worldToModel, presentationWorldToModel, results);
            animator.SetRuntimeLimbResults(std::move(results));
            return diagnostic;
        }
        catch (const std::exception& error)
        {
            state.LimbBinding.Reset();
            if (!state.LimbRigHandle)
                state.LimbRigAsset = {};
            return std::string("Limb IK could not bind or evaluate the assigned rig: ") + error.what();
        }
    }
} // namespace Keire
