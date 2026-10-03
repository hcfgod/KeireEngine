#pragma once

#include "Keire/Core.h"

namespace KeireEditor
{
    enum class LimbRigAssignmentProgress
    {
        Loading,
        Complete,
        Cancelled
    };

    struct LimbRigAssignmentGuard
    {
        Keire::Ref<Keire::Scene> Scene;
        Keire::EntityId Entity;
        Keire::Ref<Keire::AnimatorComponent> Animator;
        Keire::AssetId RequestedRig;
        Keire::AssetId OriginalRig;
        Keire::AssetId Skin;
        Keire::AssetId AuthoredSkeleton;

        [[nodiscard]] bool Matches(const Keire::Ref<Keire::Scene>& scene, Keire::EntityId selection, bool editing) const
        {
            if (!editing || !Scene || !Scene->IsOpen() || scene != Scene || selection != Entity)
                return false;
            const auto entity = scene->FindEntity(selection);
            const auto animator =
                entity ? entity.GetComponent<Keire::AnimatorComponent>() : Keire::Ref<Keire::AnimatorComponent>{};
            return animator && animator == Animator && animator->RigDefinition() == OriginalRig &&
                   animator->SkinnedMesh() == Skin && animator->Skeleton() == AuthoredSkeleton &&
                   animator->PoseSource() == Keire::AnimatorPoseSource::AnimationGraph;
        }
    };

    template <typename RecordUndo>
    [[nodiscard]] LimbRigAssignmentProgress
    CompleteLimbRigAssignment(const LimbRigAssignmentGuard& request, const Keire::Ref<Keire::Scene>& scene,
                              Keire::EntityId selection, bool editing,
                              const Keire::Ref<const Keire::RigDefinitionAsset>& rig,
                              const Keire::Ref<const Keire::SkeletonAsset>& skeleton, const RecordUndo& recordUndo)
    {
        if (!request.Matches(scene, selection, editing))
            return LimbRigAssignmentProgress::Cancelled;
        if (!rig || !skeleton)
            return LimbRigAssignmentProgress::Loading;
        const Keire::BoundLimbRig validated(skeleton, rig->Definition().Limbs);
        (void)validated;
        if (request.Animator->RigDefinition() != request.RequestedRig)
        {
            recordUndo();
            request.Animator->SetRigDefinition(request.RequestedRig);
            scene->MarkDirty();
        }
        return LimbRigAssignmentProgress::Complete;
    }
} // namespace KeireEditor
