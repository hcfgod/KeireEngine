#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

#include <algorithm>
#include <cmath>

namespace Keire
{
    [[nodiscard]] Ref<const AnimationClipAsset> SceneRuntimeSession::Impl::ResolveClip(AnimationRuntimeState& state,
                                                                                       const AssetId id)
    {
        if (!id)
            return {};
        auto [iterator, inserted] = state.Clips.try_emplace(id);
        if (inserted)
            iterator->second = Assets->Load<AnimationClipAsset>(id, AssetPriority::High);
        auto clip = iterator->second.TryGetLoaded();
        if (!clip || clip->Skeleton() == state.Skeleton)
            return clip;

        auto& retargeted = state.RetargetedClips[id];
        if (retargeted.SourceSkeleton != clip->Skeleton())
        {
            retargeted = {};
            retargeted.SourceSkeleton = clip->Skeleton();
            retargeted.SourceSkeletonHandle =
                Assets->Load<SkeletonAsset>(retargeted.SourceSkeleton, AssetPriority::High);
        }
        const auto sourceSkeleton = retargeted.SourceSkeletonHandle.TryGetLoaded();
        const auto targetSkeleton = state.SkeletonHandle.TryGetLoaded();
        if (!sourceSkeleton || !targetSkeleton)
            return {};

        const auto clipRevision = iterator->second.Revision();
        const auto sourceRevision = retargeted.SourceSkeletonHandle.Revision();
        const auto targetRevision = state.SkeletonHandle.Revision();
        if (!retargeted.Clip || retargeted.ClipRevision != clipRevision ||
            retargeted.SourceSkeletonRevision != sourceRevision || retargeted.TargetSkeletonRevision != targetRevision)
        {
            try
            {
                const auto sourceRig = BestRuntimeRig(*sourceSkeleton);
                const auto targetRig = BestRuntimeRig(*targetSkeleton);
                retargeted.Clip = RetargetAnimationClip(*sourceSkeleton, sourceRig, *clip, state.Skeleton,
                                                        *targetSkeleton, targetRig);
                retargeted.ClipRevision = clipRevision;
                retargeted.SourceSkeletonRevision = sourceRevision;
                retargeted.TargetSkeletonRevision = targetRevision;
            }
            catch (const std::exception& error)
            {
                state.DependencyDiagnostic =
                    "Animation clip is incompatible with the Animator skeleton: " + std::string(error.what());
                retargeted.Clip = {};
                return {};
            }
        }
        return retargeted.Clip;
    }

    [[nodiscard]] Ref<const AvatarMaskAsset> SceneRuntimeSession::Impl::ResolveMask(AnimationRuntimeState& state,
                                                                                    const AssetId id)
    {
        if (!id)
            return {};
        auto [iterator, inserted] = state.Masks.try_emplace(id);
        if (inserted)
            iterator->second = Assets->Load<AvatarMaskAsset>(id, AssetPriority::High);
        return iterator->second.TryGetLoaded();
    }

    [[nodiscard]] bool SceneRuntimeSession::Impl::DependenciesReady(AnimationRuntimeState& state,
                                                                    const AnimationGraphAsset& graph)
    {
        state.DependencyDiagnostic.clear();
        bool ready = true;
        for (const auto& layer : graph.Definition().Layers)
        {
            if (layer.AvatarMask && !ResolveMask(state, layer.AvatarMask))
                ready = false;
            for (const auto& animationState : layer.States)
            {
                const auto clip = animationState.Motion.Clip ? animationState.Motion.Clip : animationState.Clip;
                if (clip && !ResolveClip(state, clip))
                    ready = false;
                for (const auto& child : animationState.Motion.Children)
                    if (child.Clip && !ResolveClip(state, child.Clip))
                        ready = false;
            }
        }
        return ready;
    }

    [[nodiscard]] RigDefinition SceneRuntimeSession::Impl::BestRuntimeRig(const SkeletonAsset& skeleton)
    {
        auto humanoid = InferRigDefinition(skeleton, RigProfileType::Humanoid);
        auto quadruped = InferRigDefinition(skeleton, RigProfileType::Quadruped);
        const auto semanticCount = [](const RigDefinition& rig)
        {
            return std::ranges::count_if(rig.Bones,
                                         [](const auto& bone) { return bone.Semantic != RigBoneSemantic::None; });
        };
        return semanticCount(quadruped) > semanticCount(humanoid) ? std::move(quadruped) : std::move(humanoid);
    }

    void SceneRuntimeSession::Impl::ApplyCommands(AnimatorInstance& instance, std::span<const AnimatorCommand> commands)
    {
        for (const auto& command : commands)
        {
            switch (command.Type)
            {
            case AnimatorCommandType::SetFloat:
                instance.SetFloat(command.Name, command.FloatValue);
                break;
            case AnimatorCommandType::SetInteger:
                instance.SetInteger(command.Name, command.IntegerValue);
                break;
            case AnimatorCommandType::SetBoolean:
                instance.SetBool(command.Name, command.BooleanValue);
                break;
            case AnimatorCommandType::SetTrigger:
                instance.SetTrigger(command.Name);
                break;
            case AnimatorCommandType::ResetTrigger:
                instance.ResetTrigger(command.Name);
                break;
            case AnimatorCommandType::SetLayerWeight:
                instance.SetLayerWeight(command.Name, command.FloatValue);
                break;
            case AnimatorCommandType::Play:
                instance.Play(command.Name, command.Layer, command.FloatValue);
                break;
            case AnimatorCommandType::CrossFade:
                instance.CrossFade(command.Name, command.SecondaryFloatValue, command.Layer, command.FloatValue);
                break;
            case AnimatorCommandType::Stop:
                instance.Stop();
                break;
            }
        }
    }

    void SceneRuntimeSession::Impl::SkinPalette(const SkeletonAsset& skeleton,
                                                const std::span<const BoneTransform> localPose,
                                                std::vector<Matrix4>& world, std::vector<Matrix4>& palette)
    {
        if (localPose.size() != skeleton.Bones().size())
            throw std::runtime_error("Animator pose does not match its skeleton.");
        world.resize(localPose.size());
        palette.resize(localPose.size());
        for (std::size_t index = 0; index < localPose.size(); ++index)
        {
            const auto& transform = localPose[index];
            const auto local = Math::ComposeTransform(transform.Translation, transform.Rotation, transform.Scale);
            const auto parent = skeleton.Bones()[index].Parent;
            world[index] = parent < 0 ? local : Math::Multiply(world[static_cast<std::size_t>(parent)], local);
            palette[index] = Math::Multiply(world[index], skeleton.Bones()[index].InverseBindPose);
        }
    }

    [[nodiscard]] std::vector<Matrix4>
    SceneRuntimeSession::Impl::SkinPalette(const SkeletonAsset& skeleton,
                                           const std::span<const BoneTransform> localPose)
    {
        std::vector<Matrix4> world;
        std::vector<Matrix4> palette;
        SkinPalette(skeleton, localPose, world, palette);
        return palette;
    }

    void SceneRuntimeSession::Impl::ModelBoneMatrices(const SkeletonAsset& skeleton,
                                                      const std::span<const BoneTransform> localPose,
                                                      std::vector<Matrix4>& world)
    {
        world.resize(localPose.size());
        for (std::size_t index = 0; index < localPose.size(); ++index)
        {
            const auto& transform = localPose[index];
            const auto local = Math::ComposeTransform(transform.Translation, transform.Rotation, transform.Scale);
            const auto parent = skeleton.Bones()[index].Parent;
            world[index] = parent < 0 ? local : Math::Multiply(world[static_cast<std::size_t>(parent)], local);
        }
    }

    [[nodiscard]] std::vector<Matrix4>
    SceneRuntimeSession::Impl::ModelBoneMatrices(const SkeletonAsset& skeleton,
                                                 const std::span<const BoneTransform> localPose)
    {
        std::vector<Matrix4> world;
        ModelBoneMatrices(skeleton, localPose, world);
        return world;
    }

    [[nodiscard]] bool SceneRuntimeSession::Impl::SetBoneModelRotationCached(
        const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose, const std::uint32_t bone,
        const Quaternion modelRotation, const float weight, std::vector<Matrix4>& world)
    {
        const auto parent = skeleton.Bones()[bone].Parent;
        Quaternion parentRotation;
        if (parent >= 0)
        {
            ModelBoneMatrices(skeleton, localPose, world);
            if (!RiggingDetail::MatrixRotation(world[static_cast<std::size_t>(parent)], parentRotation))
                return false;
        }
        const auto desiredLocal =
            parent >= 0 ? RiggingDetail::Multiply(RiggingDetail::Conjugate(RiggingDetail::Normalize(parentRotation)),
                                                  RiggingDetail::Normalize(modelRotation))
                        : RiggingDetail::Normalize(modelRotation);
        localPose[bone].Rotation = RiggingDetail::Nlerp(localPose[bone].Rotation, desiredLocal, weight);
        return true;
    }

    [[nodiscard]] bool SceneRuntimeSession::Impl::ApplyBoneModelRotationDeltaCached(
        const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose, const std::uint32_t bone,
        const Quaternion delta, const float weight, std::vector<Matrix4>& world)
    {
        ModelBoneMatrices(skeleton, localPose, world);
        Quaternion currentRotation;
        if (!RiggingDetail::MatrixRotation(world[bone], currentRotation))
            return false;
        return SetBoneModelRotationCached(
            skeleton, localPose, bone,
            RiggingDetail::Multiply(RiggingDetail::Normalize(delta), RiggingDetail::Normalize(currentRotation)), weight,
            world);
    }

    [[nodiscard]] bool SceneRuntimeSession::Impl::SolveTwoBoneIkCached(const SkeletonAsset& skeleton,
                                                                       const std::span<BoneTransform> localPose,
                                                                       const TwoBoneIkRequest& request,
                                                                       std::vector<Matrix4>& world)
    {
        if (localPose.size() != skeleton.Bones().size() || request.Root >= localPose.size() ||
            request.Middle >= localPose.size() || request.End >= localPose.size() ||
            !RiggingDetail::IsDescendantOf(skeleton, request.Middle, request.Root) ||
            !RiggingDetail::IsDescendantOf(skeleton, request.End, request.Middle) || !Math::IsFinite(request.Target) ||
            !Math::IsFinite(request.Pole) || !std::isfinite(request.Weight) ||
            (request.EndRotation &&
             (!Math::IsFinite(*request.EndRotation) || Math::Length(*request.EndRotation) <= RiggingDetail::Epsilon)) ||
            !std::isfinite(request.EndRotationWeight))
        {
            return false;
        }

        const auto weight = std::clamp(request.Weight, 0.0F, 1.0F);
        const auto endRotationWeight = std::clamp(request.EndRotationWeight, 0.0F, 1.0F);
        if (weight <= 0.0F)
        {
            return !request.EndRotation || endRotationWeight <= 0.0F ||
                   SetBoneModelRotationCached(skeleton, localPose, request.End, *request.EndRotation, endRotationWeight,
                                              world);
        }

        ModelBoneMatrices(skeleton, localPose, world);
        const auto rootPosition = Math::TransformPoint(world[request.Root], {});
        auto middlePosition = Math::TransformPoint(world[request.Middle], {});
        auto endPosition = Math::TransformPoint(world[request.End], {});
        const auto upperLength = VectorLength(RiggingDetail::Subtract(middlePosition, rootPosition));
        const auto lowerLength = VectorLength(RiggingDetail::Subtract(endPosition, middlePosition));
        if (upperLength <= RiggingDetail::Epsilon || lowerLength <= RiggingDetail::Epsilon)
            return false;

        const auto requestedDelta = RiggingDetail::Subtract(request.Target, rootPosition);
        const auto requestedDistance = VectorLength(requestedDelta);
        auto targetDelta = requestedDelta;
        if (requestedDistance <= RiggingDetail::Epsilon)
        {
            targetDelta = RiggingDetail::Subtract(endPosition, rootPosition);
            if (VectorLength(targetDelta) <= RiggingDetail::Epsilon)
                targetDelta = RiggingDetail::Subtract(middlePosition, rootPosition);
        }
        const auto singularityMargin = std::min(std::max((upperLength + lowerLength) * 0.0025F, RiggingDetail::Epsilon),
                                                std::min(upperLength, lowerLength) * 0.25F);
        const auto targetDistance =
            std::clamp(requestedDistance, std::abs(upperLength - lowerLength) + singularityMargin,
                       upperLength + lowerLength - singularityMargin);
        const auto forward = RiggingDetail::Normalize(targetDelta);
        auto bendVector = RiggingDetail::ProjectOntoPlane(RiggingDetail::Subtract(request.Pole, rootPosition), forward);
        if (VectorLength(bendVector) <= RiggingDetail::Epsilon)
        {
            bendVector =
                RiggingDetail::ProjectOntoPlane(RiggingDetail::Subtract(middlePosition, rootPosition), forward);
        }
        if (VectorLength(bendVector) <= RiggingDetail::Epsilon)
        {
            const auto fallback = std::abs(forward.Y) < 0.95F ? Vector3{0.0F, 1.0F, 0.0F} : Vector3{0.0F, 0.0F, 1.0F};
            bendVector = RiggingDetail::ProjectOntoPlane(fallback, forward);
        }
        const auto bend = RiggingDetail::Normalize(bendVector);
        const auto projected =
            (upperLength * upperLength + targetDistance * targetDistance - lowerLength * lowerLength) /
            (2.0F * targetDistance);
        const auto height = std::sqrt(std::max(0.0F, upperLength * upperLength - projected * projected));
        const auto desiredMiddle =
            RiggingDetail::Add(rootPosition, RiggingDetail::Add(RiggingDetail::Multiply(forward, projected),
                                                                RiggingDetail::Multiply(bend, height)));
        const auto rootDelta = RiggingDetail::FromTo(RiggingDetail::Subtract(middlePosition, rootPosition),
                                                     RiggingDetail::Subtract(desiredMiddle, rootPosition));
        if (!ApplyBoneModelRotationDeltaCached(skeleton, localPose, request.Root, rootDelta, weight, world))
            return false;

        ModelBoneMatrices(skeleton, localPose, world);
        middlePosition = Math::TransformPoint(world[request.Middle], {});
        endPosition = Math::TransformPoint(world[request.End], {});
        const auto reachableTarget = RiggingDetail::Add(rootPosition, RiggingDetail::Multiply(forward, targetDistance));
        const auto middleDelta = RiggingDetail::FromTo(RiggingDetail::Subtract(endPosition, middlePosition),
                                                       RiggingDetail::Subtract(reachableTarget, middlePosition));
        if (!ApplyBoneModelRotationDeltaCached(skeleton, localPose, request.Middle, middleDelta, weight, world))
            return false;
        return !request.EndRotation || endRotationWeight <= 0.0F ||
               SetBoneModelRotationCached(skeleton, localPose, request.End, *request.EndRotation, endRotationWeight,
                                          world);
    }

    [[nodiscard]] std::shared_ptr<AnimatorDebugSnapshot>
    SceneRuntimeSession::Impl::ProceduralDebugSnapshot(AnimationRuntimeState& state, const SkeletonAsset& skeleton,
                                                       const std::span<const BoneTransform> localPose,
                                                       const std::span<const Matrix4> modelBones)
    {
        auto slot = std::ranges::find_if(state.ProceduralDebugSnapshots,
                                         [](const auto& snapshot) { return snapshot && snapshot.use_count() == 1; });
        if (slot == state.ProceduralDebugSnapshots.end())
        {
            slot = std::ranges::find(state.ProceduralDebugSnapshots, nullptr);
            if (slot == state.ProceduralDebugSnapshots.end())
                slot = state.ProceduralDebugSnapshots.begin();
            *slot = std::make_shared<AnimatorDebugSnapshot>();
        }

        auto& result = **slot;
        result.Revision = ++state.ProceduralDebugRevision;
        result.RootMotion = {};
        result.RootRotation = {};
        result.Profile.UpdateCount = state.ProceduralTick;
        result.Pose.resize(localPose.size());
        for (std::size_t index = 0; index < localPose.size(); ++index)
        {
            auto& debugBone = result.Pose[index];
            const auto& skeletonBone = skeleton.Bones()[index];
            if (debugBone.Name != skeletonBone.Name)
                debugBone.Name = skeletonBone.Name;
            debugBone.Parent = skeletonBone.Parent;
            debugBone.LocalTransform = localPose[index];
            debugBone.WorldPosition = Math::TransformPoint(modelBones[index], {});
        }
        return *slot;
    }

    [[nodiscard]] std::shared_ptr<const AnimatorDebugSnapshot>
    SceneRuntimeSession::Impl::FinalPoseDebugSnapshot(const SkeletonAsset& skeleton,
                                                      const std::span<const BoneTransform> localPose,
                                                      const std::shared_ptr<const AnimatorDebugSnapshot>& source)
    {
        if (!source || localPose.size() != skeleton.Bones().size())
            return source;
        auto result = std::make_shared<AnimatorDebugSnapshot>(*source);
        const auto modelBones = ModelBoneMatrices(skeleton, localPose);
        result->Pose.clear();
        result->Pose.reserve(localPose.size());
        for (std::size_t index = 0; index < localPose.size(); ++index)
        {
            const auto& bone = skeleton.Bones()[index];
            result->Pose.push_back(
                {bone.Name, bone.Parent, localPose[index], Math::TransformPoint(modelBones[index], {})});
        }
        return result;
    }

    [[nodiscard]] std::optional<std::uint32_t>
    SceneRuntimeSession::Impl::ResolveIkBone(const std::map<std::string, std::uint32_t, std::less<>>& names,
                                             const std::map<RigBoneSemantic, std::uint32_t>& semantics,
                                             const bool automatic, const std::string_view fallback,
                                             const RigBoneSemantic semantic)
    {
        if (automatic)
        {
            const auto inferred = semantics.find(semantic);
            if (inferred != semantics.end())
                return inferred->second;
        }
        const auto named = names.find(fallback);
        return named == names.end() ? std::nullopt : std::optional(named->second);
    }

    [[nodiscard]] std::string SceneRuntimeSession::Impl::ApplyAuthoredArmIk(
        const Entity& entity, const SkeletonAsset& skeleton, const AnimatorComponent& animator,
        const std::span<BoneTransform> localPose, const std::map<std::string, std::uint32_t, std::less<>>& names,
        const std::map<RigBoneSemantic, std::uint32_t>& semantics, AnimationRuntimeState& runtimeState)
    {
        const auto animatorTransform = entity.GetComponent<TransformComponent>();
        if (!animatorTransform)
            return "Authored arm IK requires an Animator world transform.";
        Matrix4 worldToModel;
        try
        {
            worldToModel = Math::Inverse(animatorTransform->WorldMatrix());
        }
        catch (const std::exception&)
        {
            return "Authored arm IK could not invert the Animator world transform.";
        }

        const auto solve = [&](const AnimatorLimbIkSettings& settings, const bool left,
                               Detail::AutomaticLimbIkState& stability) -> std::string
        {
            if (!settings.Enabled)
            {
                stability = {};
                return {};
            }
            const auto side = left ? std::string_view("Left") : std::string_view("Right");
            if (!settings.Target)
                return std::string(side) + " arm IK is enabled but has no target entity.";
            const auto targetEntity = Runtime->FindEntity(settings.Target);
            const auto targetTransform =
                targetEntity ? targetEntity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            if (!targetTransform)
                return std::string(side) + " arm IK target is unavailable in the runtime scene.";

            const auto root = ResolveIkBone(names, semantics, settings.AutomaticBoneMapping, settings.Root,
                                            left ? RigBoneSemantic::LeftUpperArm : RigBoneSemantic::RightUpperArm);
            const auto middle = ResolveIkBone(names, semantics, settings.AutomaticBoneMapping, settings.Middle,
                                              left ? RigBoneSemantic::LeftLowerArm : RigBoneSemantic::RightLowerArm);
            const auto end = ResolveIkBone(names, semantics, settings.AutomaticBoneMapping, settings.End,
                                           left ? RigBoneSemantic::LeftHand : RigBoneSemantic::RightHand);
            if (!root || !middle || !end)
                return std::string(side) +
                       " arm IK could not resolve a contiguous upper-arm, lower-arm, and hand chain.";

            const auto targetModelMatrix = Math::Multiply(worldToModel, targetTransform->WorldMatrix());
            const auto target = Math::TransformPoint(targetModelMatrix, settings.TargetOffset);
            Vector3 ignoredPosition;
            Vector3 ignoredScale;
            Quaternion targetRotation;
            if (!Math::DecomposeTransform(targetModelMatrix, ignoredPosition, targetRotation, ignoredScale))
                return std::string(side) + " arm IK target has a non-decomposable transform.";

            Vector3 pole;
            if (settings.Pole)
            {
                stability = {};
                const auto poleEntity = Runtime->FindEntity(settings.Pole);
                const auto poleTransform =
                    poleEntity ? poleEntity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
                if (!poleTransform)
                    return std::string(side) + " arm IK pole override is unavailable in the runtime scene.";
                pole = Math::TransformPoint(worldToModel, poleTransform->WorldPosition());
            }
            else
            {
                ModelBoneMatrices(skeleton, localPose, runtimeState.ModelMatrixScratch);
                const auto& modelBones = runtimeState.ModelMatrixScratch;
                const auto rootPosition = Math::TransformPoint(modelBones[*root], {});
                const auto middlePosition = Math::TransformPoint(modelBones[*middle], {});
                const auto endPosition = Math::TransformPoint(modelBones[*end], {});
                pole = Detail::StableAutomaticLimbPole(rootPosition, middlePosition, endPosition, target, stability);
            }

            TwoBoneIkRequest request{*root, *middle, *end, target, pole, settings.PositionWeight};
            request.EndRotation = targetRotation;
            request.EndRotationWeight = settings.RotationWeight;
            if (!SolveTwoBoneIkCached(skeleton, localPose, request, runtimeState.ModelMatrixScratch))
                return std::string(side) + " arm IK could not solve the resolved skeleton chain.";
            return {};
        };

        return Detail::EvaluateIndependentAnimationIkPasses(
            [&] { return solve(animator.LeftArmIk(), true, runtimeState.LeftArmIkState); },
            [&] { return solve(animator.RightArmIk(), false, runtimeState.RightArmIkState); });
    }

    [[nodiscard]] std::string
    SceneRuntimeSession::Impl::ApplyIkGoals(const Entity& entity, const SkeletonAsset& skeleton,
                                            const AnimatorComponent& animator, std::span<BoneTransform> localPose,
                                            const std::map<std::string, std::uint32_t, std::less<>>& indices)
    {
        if (animator.IkGoals().empty())
            return {};

        Matrix4 worldToModel;
        bool hasWorldToModel = false;
        if (const auto transform = entity.GetComponent<TransformComponent>())
        {
            try
            {
                worldToModel = Math::Inverse(transform->WorldMatrix());
                hasWorldToModel = true;
            }
            catch (const std::exception&)
            {
            }
        }

        for (const auto& goal : animator.IkGoals())
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
                if (!hasWorldToModel)
                    return "IK goal '" + goal.Name + "' could not resolve the Animator world transform.";
                target = Math::TransformPoint(worldToModel, target);
                pole = Math::TransformPoint(worldToModel, pole);
            }

            bool solved = false;
            if (goal.Solver == AnimatorIkSolver::TwoBone && chain.size() == 3)
            {
                solved = SolveTwoBoneIk(skeleton, localPose, {chain[0], chain[1], chain[2], target, pole, goal.Weight});
            }
            else if (goal.Solver == AnimatorIkSolver::Fabrik)
            {
                solved = SolveFabrikIk(skeleton, localPose,
                                       {std::move(chain), target, goal.MaximumIterations, goal.Tolerance, goal.Weight});
            }
            if (!solved)
                return "IK goal '" + goal.Name + "' does not describe a valid contiguous skeleton chain.";
        }
        return {};
    }

    void SceneRuntimeSession::Impl::SynchronizeAnimation(const float deltaSeconds)
    {
        if (!Assets || !Runtime)
            return;
        std::set<EntityId> seen;
        for (const auto& entity : Runtime->Query<AnimatorComponent>())
        {
            const auto animator = entity.GetComponent<AnimatorComponent>();
            if (!animator)
                continue;
            seen.emplace(entity.Id());
            auto& state = Animators[entity.Id()];
            if (!state)
                state = std::make_unique<AnimationRuntimeState>();
            if (animator->PoseSource() == AnimatorPoseSource::ProceduralHumanoid)
            {
                if (entity.ActiveInHierarchy() && animator->Enabled())
                {
                    PublishProceduralAnimation(entity, *animator, *state);
                    if (animator->FootGrounding().Enabled && animator->RuntimeDiagnostic().empty())
                    {
                        animator->SetRuntimeDiagnostic(
                            "Legacy automatic foot grounding is ignored in Procedural Humanoid mode.");
                    }
                }
                continue;
            }

            const auto skinId = animator->SkinnedMesh();
            auto targetSkeleton = animator->Skeleton();
            if (skinId)
            {
                const auto skin = Assets->Load<SkinnedMeshAsset>(skinId, AssetPriority::High).TryGetLoaded();
                if (!skin)
                {
                    animator->SetRuntimeDiagnostic("Animator is waiting for the assigned skinned mesh to load.");
                    continue;
                }
                targetSkeleton = skin->Skeleton();
                if (!targetSkeleton)
                {
                    animator->SetRuntimeDiagnostic("The assigned skinned mesh does not reference a skeleton.");
                    continue;
                }
                if (animator->Skeleton() != targetSkeleton)
                    animator->SetSkeleton(targetSkeleton);
            }

            if (state->PoseSource != AnimatorPoseSource::AnimationGraph || state->Graph != animator->Graph() ||
                state->Skeleton != targetSkeleton || state->Skin != skinId)
            {
                *state = {};
                state->PoseSource = AnimatorPoseSource::AnimationGraph;
                state->Graph = animator->Graph();
                state->Skeleton = targetSkeleton;
                state->Skin = skinId;
                if (state->Graph)
                    state->GraphHandle = Assets->Load<AnimationGraphAsset>(state->Graph, AssetPriority::High);
                if (state->Skeleton)
                    state->SkeletonHandle = Assets->Load<SkeletonAsset>(state->Skeleton, AssetPriority::High);
                animator->SetRuntimeDiagnostic({});
            }
            if (!entity.ActiveInHierarchy() || !animator->Enabled())
                continue;
            if (!state->Graph || !state->Skeleton)
            {
                animator->SetRuntimeDiagnostic("Animator requires both controller and skeleton assets.");
                continue;
            }
            const auto graph = state->GraphHandle.TryGetLoaded();
            const auto skeleton = state->SkeletonHandle.TryGetLoaded();
            if (!graph || !skeleton)
            {
                animator->SetRuntimeDiagnostic("Animator is waiting for controller dependencies to load.");
                continue;
            }

            const auto graphRevision = state->GraphHandle.Revision();
            if (state->DependencyGraphRevision != graphRevision)
            {
                state->Clips.clear();
                state->RetargetedClips.clear();
                state->Masks.clear();
                state->DependencyDiagnostic.clear();
                state->DependencyGraphRevision = graphRevision;
            }
            if (!DependenciesReady(*state, *graph))
            {
                animator->SetRuntimeDiagnostic(state->DependencyDiagnostic.empty()
                                                   ? "Animator is waiting for controller dependencies to load."
                                                   : state->DependencyDiagnostic);
                continue;
            }

            const auto skeletonRevision = state->SkeletonHandle.Revision();
            if (!state->Instance || state->SkeletonRevision != skeletonRevision)
            {
                auto* runtimeState = state.get();
                state->Instance = std::make_unique<AnimatorInstance>(
                    skeleton, graph, [this, runtimeState](const AssetId id) { return ResolveClip(*runtimeState, id); },
                    [this, runtimeState](const AssetId id) { return ResolveMask(*runtimeState, id); });
                state->GraphRevision = graphRevision;
                state->SkeletonRevision = skeletonRevision;
                state->BoneIndices.clear();
                state->SemanticBoneIndices.clear();
                for (std::uint32_t index = 0; index < skeleton->Bones().size(); ++index)
                    state->BoneIndices.emplace(skeleton->Bones()[index].Name, index);
                try
                {
                    const auto inferred = InferRigDefinition(*skeleton);
                    for (std::uint32_t index = 0; index < inferred.Bones.size(); ++index)
                    {
                        if (inferred.Bones[index].Semantic != RigBoneSemantic::None)
                            state->SemanticBoneIndices.emplace(inferred.Bones[index].Semantic, index);
                    }
                }
                catch (const std::exception&)
                {
                    state->SemanticBoneIndices.clear();
                }
                if (skeletonRevision > 1)
                    animator->SetRuntimeDiagnostic("Skeleton reload restarted Animator state safely.");
            }
            else if (state->GraphRevision != graphRevision)
            {
                const bool preserved = state->Instance->Reload(graph);
                state->GraphRevision = graphRevision;
                animator->SetRuntimeDiagnostic(
                    preserved ? std::string{} : "Controller topology changed; Animator state restarted safely.");
            }

            ApplyCommands(*state->Instance, animator->ConsumeRuntimeCommands());
            const float speed = std::max(animator->Speed(), 0.0F);
            if (animator->Speed() < 0.0F)
                animator->SetRuntimeDiagnostic("Negative Animator speed is not supported and is treated as zero.");
            auto sample = state->Instance->Update(animator->Paused() ? 0.0F : deltaSeconds * speed);
            Runtime->DispatchAnimatorIk(entity.Id(), {.LayerWeight = 1.0F});
            const auto ikDiagnostics = Detail::EvaluateIndependentAnimationIkPasses(
                [&] { return ApplyIkGoals(entity, *skeleton, *animator, sample.LocalPose, state->BoneIndices); },
                [&]
                {
                    return ApplyAuthoredArmIk(entity, *skeleton, *animator, sample.LocalPose, state->BoneIndices,
                                              state->SemanticBoneIndices, *state);
                },
                [&]
                {
                    return ApplyFootGrounding(entity, *skeleton, animator->FootGrounding(),
                                              animator->RuntimeFootGroundingWeight(), animator->SkinnedMesh(),
                                              deltaSeconds, sample.LocalPose, state->BoneIndices,
                                              state->SemanticBoneIndices, *state);
                });
            if (!ikDiagnostics.empty())
                animator->SetRuntimeDiagnostic(ikDiagnostics);
            const auto palette = SkinPalette(*skeleton, sample.LocalPose);
            animator->SetRuntimePose(sample.State, sample.NormalizedTime, state->Instance->Playing(), palette);
            auto debugSnapshot = state->Instance->DebugSnapshot();
            if (!animator->IkGoals().empty() || animator->LeftArmIk().Enabled || animator->RightArmIk().Enabled ||
                animator->FootGrounding().Enabled)
                debugSnapshot = FinalPoseDebugSnapshot(*skeleton, sample.LocalPose, debugSnapshot);
            animator->SetRuntimeDebugSnapshot(std::move(debugSnapshot));
            ApplyRootMotion(entity, sample, *animator);
            for (const auto& event : sample.Events)
                Runtime->DispatchAnimationEvent(entity.Id(),
                                                {event.Name, sample.NormalizedTime, 0, 0.0F, event.Payload});
        }
        for (auto iterator = Animators.begin(); iterator != Animators.end();)
        {
            if (!seen.contains(iterator->first))
                iterator = Animators.erase(iterator);
            else
                ++iterator;
        }
    }
} // namespace Keire
