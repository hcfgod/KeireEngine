#pragma once

#include "Keire/Scenes/Scene.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/PhysicsMaterialAsset.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Log.h"
#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Vfx/VfxSystem.h"
#include "Keire/Vfx/VfxVolumeAsset.h"
#include "KeireInternal/Animation/ProceduralPoseMath.h"
#include "KeireInternal/Animation/RiggingMath.h"
#include "KeireInternal/Scenes/AnimationIkPasses.h"
#include "KeireInternal/Scenes/CharacterGrounding.h"
#include "KeireInternal/Scenes/FootGroundingSpace.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    [[nodiscard]] inline bool HasCanonicalVfxRangeEndpoints(const VfxParameterValue& value) noexcept
    {
        return std::visit(
            [](const auto& item) noexcept
            {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, VfxScalarRange> || std::is_same_v<T, VfxIntegerRange> ||
                              std::is_same_v<T, VfxUnsignedIntegerRange>)
                {
                    return item.Minimum <= item.Maximum;
                }
                else if constexpr (std::is_same_v<T, VfxVector2Range>)
                {
                    return item.Minimum.X <= item.Maximum.X && item.Minimum.Y <= item.Maximum.Y;
                }
                else if constexpr (std::is_same_v<T, VfxVector3Range>)
                {
                    return item.Minimum.X <= item.Maximum.X && item.Minimum.Y <= item.Maximum.Y &&
                           item.Minimum.Z <= item.Maximum.Z;
                }
                else if constexpr (std::is_same_v<T, VfxVector4Range>)
                {
                    return item.Minimum.X <= item.Maximum.X && item.Minimum.Y <= item.Maximum.Y &&
                           item.Minimum.Z <= item.Maximum.Z && item.Minimum.W <= item.Maximum.W;
                }
                else if constexpr (std::is_same_v<T, VfxColorRange>)
                {
                    return item.Minimum.Red <= item.Maximum.Red && item.Minimum.Green <= item.Maximum.Green &&
                           item.Minimum.Blue <= item.Maximum.Blue && item.Minimum.Alpha <= item.Maximum.Alpha;
                }
                else
                {
                    return true;
                }
            },
            value);
    }

    [[nodiscard]] inline bool VfxOverrideMatches(const VfxValueType type, const VfxParameterValue& value) noexcept
    {
        return VfxValueMatchesType(type, value) && IsFiniteVfxValue(value) && HasCanonicalVfxRangeEndpoints(value);
    }

    [[nodiscard]] inline std::vector<VfxParameterOverride>
    CompatibleVfxOverrides(const VfxEffectDefinition& definition, const std::span<const VfxParameterOverride> authored)
    {
        std::vector<VfxParameterOverride> result;
        result.reserve(authored.size());
        for (const auto& overrideValue : authored)
        {
            const auto parameter =
                std::ranges::find(definition.Blackboard, overrideValue.Parameter, &VfxBlackboardParameter::Id);
            if (parameter != definition.Blackboard.end() && parameter->Exposed &&
                VfxOverrideMatches(parameter->Type, overrideValue.Value))
                result.push_back(overrideValue);
        }
        return result;
    }
} // namespace Keire::Detail

namespace Keire
{
    using Detail::CompatibleVfxOverrides;
    using Detail::VfxOverrideMatches;

    class SceneRuntimeSession::Impl final
    {
      public:
        struct AnimationRuntimeState final
        {
            struct FootPlantRuntimeState final
            {
                Detail::AutomaticFootPlantState Plant;
                std::optional<EntityId> Support;
                Detail::FootPlantSupportAnchor SupportAnchor;
                Detail::FootPlantSupportAnchor SupportSurfaceAnchor;
                Vector3 SurfacePosition;
                Vector3 SurfaceNormal{0.0F, 1.0F, 0.0F};
                Vector3 ReleasePosition;
                Vector3 ReleaseNormal{0.0F, 1.0F, 0.0F};
            };

            struct RetargetedClip final
            {
                AssetId SourceSkeleton;
                AssetHandle<SkeletonAsset> SourceSkeletonHandle;
                Ref<AnimationClipAsset> Clip;
                std::uint64_t ClipRevision = 0;
                std::uint64_t SourceSkeletonRevision = 0;
                std::uint64_t TargetSkeletonRevision = 0;
            };

            AssetId Graph;
            AssetId Skeleton;
            AssetId Skin;
            AnimatorPoseSource PoseSource = AnimatorPoseSource::AnimationGraph;
            AssetId ProceduralProfile;
            AssetId RigDefinition;
            AssetHandle<AnimationGraphAsset> GraphHandle;
            AssetHandle<SkeletonAsset> SkeletonHandle;
            AssetHandle<ProceduralMotionProfileAsset> ProceduralProfileHandle;
            AssetHandle<RigDefinitionAsset> RigDefinitionHandle;
            std::map<AssetId, AssetHandle<AnimationClipAsset>> Clips;
            std::map<AssetId, RetargetedClip> RetargetedClips;
            std::map<AssetId, AssetHandle<AvatarMaskAsset>> Masks;
            std::map<std::string, std::uint32_t, std::less<>> BoneIndices;
            std::map<RigBoneSemantic, std::uint32_t> SemanticBoneIndices;
            std::unique_ptr<AnimatorInstance> Instance;
            std::uint64_t GraphRevision = 0;
            std::uint64_t DependencyGraphRevision = 0;
            std::uint64_t SkeletonRevision = 0;
            std::uint64_t ProceduralProfileRevision = 0;
            std::uint64_t RigDefinitionRevision = 0;
            std::string DependencyDiagnostic;
            Detail::AutomaticLimbIkState LeftArmIkState;
            Detail::AutomaticLimbIkState RightArmIkState;
            Detail::AutomaticLimbIkState LeftFootIkState;
            Detail::AutomaticLimbIkState RightFootIkState;
            Detail::AutomaticFootGroundingSmoothingState LeftFootGroundingSmoothingState;
            Detail::AutomaticFootGroundingSmoothingState RightFootGroundingSmoothingState;
            FootPlantRuntimeState LeftFootPlantState;
            FootPlantRuntimeState RightFootPlantState;
            AssetId FootClearanceMesh;
            std::uint64_t FootClearanceSkinRevision = 0;
            std::uint64_t FootClearanceMeshRevision = 0;
            std::map<std::uint32_t, std::optional<float>> FootMeshClearances;
            std::map<std::uint32_t, std::optional<std::uint32_t>> FootToeBones;
            std::vector<BoneTransform> PreviousProceduralPose;
            std::vector<BoneTransform> CurrentProceduralPose;
            ProceduralLocomotionState ProceduralState;
            ProceduralLocomotionIntent ProceduralIntent;
            ProceduralMotionState PreviousProceduralState = ProceduralMotionState::Idle;
            float GaitPhase = 0.0F;
            float PreviousGaitPhase = 0.0F;
            float ProceduralTime = 0.0F;
            float LandingElapsed = 0.0F;
            float StopSettleRemaining = 0.0F;
            float PreviousVerticalSpeed = 0.0F;
            Vector3 PreviousRootForward;
            Vector3 PreviousHorizontalVelocity;
            float RootAngularVelocityDegrees = 0.0F;
            float HorizontalAcceleration = 0.0F;
            bool PreviousGrounded = true;
            bool HasPreviousRootForward = false;
            bool ProceduralInitialized = false;
            bool ApexSent = false;
            std::uint64_t ProceduralTick = 0;
        };

        struct PhysicsRuntimeState final
        {
            PhysicsBodyId Body;
            PhysicsBodyDefinition Definition;
            bool HasDefinition = false;
            AssetId Material;
            AssetHandle<PhysicsMaterialAsset> MaterialHandle;
            std::uint64_t MaterialRevision = 0;
            AssetId Mesh;
            AssetHandle<MeshAsset> MeshHandle;
            std::uint64_t MeshRevision = 0;
            Vector3 CookedScale;
            std::shared_ptr<const CookedCollisionMesh> CookedCollision;
            Vector3 ColliderCenter;
            Vector3 WorldScale{1.0F, 1.0F, 1.0F};
            Vector3 CharacterVelocity;
            float CharacterRequestedVerticalDisplacement = 0.0F;
            std::uint32_t CharacterMissedWalkableFrames = 0;
            std::uint32_t Generation = 0;
            Matrix4 PreviousPresentationWorld;
            Matrix4 CurrentPresentationWorld;
            std::uint64_t PresentationResetRevision = 0;
            bool HasPresentationSamples = false;
        };

        struct VfxRuntimeState final
        {
            AssetId Effect;
            AssetHandle<VfxEffectAsset> EffectHandle;
            VfxHandle Handle;
            std::uint64_t Revision = 0;
            std::vector<VfxParameterOverride> Overrides;
            std::uint64_t RejectedRevision = 0;
            std::vector<VfxParameterOverride> RejectedOverrides;
            std::string Diagnostic;
        };

        struct VfxMeshShapeState final
        {
            struct Triangle final
            {
                Vector3 A;
                Vector3 B;
                Vector3 C;
                float CumulativeArea = 0.0F;
            };

            AssetHandle<MeshAsset> Handle;
            std::vector<Triangle> Triangles;
            std::uint64_t Revision = 0;
            float TotalArea = 0.0F;
        };

        Impl(Ref<Scene> scene, Ref<AssetSystem> assets, Ref<AudioSystem> audio, Ref<PhysicsSystem> physics)
            : Edit(std::move(scene)), Assets(std::move(assets)), PhysicsService(std::move(physics)),
              OwnerThread(std::this_thread::get_id())
        {
            if (!Edit || !Edit->IsOpen())
                throw std::invalid_argument("SceneRuntimeSession requires an open edit scene.");
            if (Assets)
                Presentation = CreateRef<ScenePresentationRuntime>(Assets, std::move(audio));
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("SceneRuntimeSession::") + operation +
                                       " must run on the owner thread.");
        }

        template <typename Callback> void Invoke(const char* callback, Callback&& operation)
        {
            try
            {
                std::forward<Callback>(operation)();
            }
            catch (const std::exception& exception)
            {
                PlayState = ScenePlayState::Faulted;
                Failure = {callback, exception.what()};
            }
            catch (...)
            {
                PlayState = ScenePlayState::Faulted;
                Failure = {callback, "Component callback threw a non-standard exception."};
            }
        }

        [[nodiscard]] Ref<const AnimationClipAsset> ResolveClip(AnimationRuntimeState& state, const AssetId id)
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
                retargeted.SourceSkeletonRevision != sourceRevision ||
                retargeted.TargetSkeletonRevision != targetRevision)
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

        [[nodiscard]] Ref<const AvatarMaskAsset> ResolveMask(AnimationRuntimeState& state, const AssetId id)
        {
            if (!id)
                return {};
            auto [iterator, inserted] = state.Masks.try_emplace(id);
            if (inserted)
                iterator->second = Assets->Load<AvatarMaskAsset>(id, AssetPriority::High);
            return iterator->second.TryGetLoaded();
        }

        [[nodiscard]] bool DependenciesReady(AnimationRuntimeState& state, const AnimationGraphAsset& graph)
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

        [[nodiscard]] static RigDefinition BestRuntimeRig(const SkeletonAsset& skeleton)
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

        static void ApplyCommands(AnimatorInstance& instance, std::span<const AnimatorCommand> commands)
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

        [[nodiscard]] static std::vector<Matrix4> SkinPalette(const SkeletonAsset& skeleton,
                                                              std::span<const BoneTransform> localPose)
        {
            if (localPose.size() != skeleton.Bones().size())
                throw std::runtime_error("Animator pose does not match its skeleton.");
            std::vector<Matrix4> world(localPose.size());
            std::vector<Matrix4> palette(localPose.size());
            for (std::size_t index = 0; index < localPose.size(); ++index)
            {
                const auto& transform = localPose[index];
                const auto local = Math::ComposeTransform(transform.Translation, transform.Rotation, transform.Scale);
                const auto parent = skeleton.Bones()[index].Parent;
                world[index] = parent < 0 ? local : Math::Multiply(world[static_cast<std::size_t>(parent)], local);
                palette[index] = Math::Multiply(world[index], skeleton.Bones()[index].InverseBindPose);
            }
            return palette;
        }

        [[nodiscard]] static std::vector<Matrix4> ModelBoneMatrices(const SkeletonAsset& skeleton,
                                                                    std::span<const BoneTransform> localPose)
        {
            std::vector<Matrix4> world(localPose.size());
            for (std::size_t index = 0; index < localPose.size(); ++index)
            {
                const auto& transform = localPose[index];
                const auto local = Math::ComposeTransform(transform.Translation, transform.Rotation, transform.Scale);
                const auto parent = skeleton.Bones()[index].Parent;
                world[index] = parent < 0 ? local : Math::Multiply(world[static_cast<std::size_t>(parent)], local);
            }
            return world;
        }

        [[nodiscard]] static std::shared_ptr<const AnimatorDebugSnapshot>
        FinalPoseDebugSnapshot(const SkeletonAsset& skeleton, const std::span<const BoneTransform> localPose,
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

        [[nodiscard]] static std::optional<std::uint32_t>
        ResolveIkBone(const std::map<std::string, std::uint32_t, std::less<>>& names,
                      const std::map<RigBoneSemantic, std::uint32_t>& semantics, const bool automatic,
                      const std::string_view fallback, const RigBoneSemantic semantic)
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

        [[nodiscard]] std::string ApplyAuthoredArmIk(const Entity& entity, const SkeletonAsset& skeleton,
                                                     const AnimatorComponent& animator,
                                                     const std::span<BoneTransform> localPose,
                                                     const std::map<std::string, std::uint32_t, std::less<>>& names,
                                                     const std::map<RigBoneSemantic, std::uint32_t>& semantics,
                                                     AnimationRuntimeState& runtimeState)
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
                const auto middle =
                    ResolveIkBone(names, semantics, settings.AutomaticBoneMapping, settings.Middle,
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
                    const auto modelBones = ModelBoneMatrices(skeleton, localPose);
                    const auto rootPosition = Math::TransformPoint(modelBones[*root], {});
                    const auto middlePosition = Math::TransformPoint(modelBones[*middle], {});
                    const auto endPosition = Math::TransformPoint(modelBones[*end], {});
                    pole =
                        Detail::StableAutomaticLimbPole(rootPosition, middlePosition, endPosition, target, stability);
                }

                TwoBoneIkRequest request{*root, *middle, *end, target, pole, settings.PositionWeight};
                request.EndRotation = targetRotation;
                request.EndRotationWeight = settings.RotationWeight;
                if (!SolveTwoBoneIk(skeleton, localPose, request))
                    return std::string(side) + " arm IK could not solve the resolved skeleton chain.";
                return {};
            };

            return Detail::EvaluateIndependentAnimationIkPasses(
                [&] { return solve(animator.LeftArmIk(), true, runtimeState.LeftArmIkState); },
                [&] { return solve(animator.RightArmIk(), false, runtimeState.RightArmIkState); });
        }

        [[nodiscard]] static std::string ApplyIkGoals(const Entity& entity, const SkeletonAsset& skeleton,
                                                      const AnimatorComponent& animator,
                                                      std::span<BoneTransform> localPose,
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
                    solved =
                        SolveTwoBoneIk(skeleton, localPose, {chain[0], chain[1], chain[2], target, pole, goal.Weight});
                }
                else if (goal.Solver == AnimatorIkSolver::Fabrik)
                {
                    solved =
                        SolveFabrikIk(skeleton, localPose,
                                      {std::move(chain), target, goal.MaximumIterations, goal.Tolerance, goal.Weight});
                }
                if (!solved)
                    return "IK goal '" + goal.Name + "' does not describe a valid contiguous skeleton chain.";
            }
            return {};
        }

        [[nodiscard]] std::string ApplyFootGrounding(
            const Entity& entity, const SkeletonAsset& skeleton, const AnimatorFootGroundingSettings& settings,
            const float runtimeWeight, const AssetId skinnedMesh, const float deltaSeconds,
            std::span<BoneTransform> localPose, const std::map<std::string, std::uint32_t, std::less<>>& indices,
            const std::map<RigBoneSemantic, std::uint32_t>& semantics, AnimationRuntimeState& runtimeState,
            const std::optional<float> horizontalPelvisRatio = std::nullopt,
            const std::optional<float> maximumFootRotationDegrees = std::nullopt,
            const std::optional<std::array<float, 2>> proceduralFootWeights = std::nullopt,
            const std::optional<float> unsupportedFootDropRatio = std::nullopt)
        {
            if (!settings.Enabled || runtimeWeight <= std::numeric_limits<float>::epsilon())
            {
                runtimeState.LeftFootIkState = {};
                runtimeState.RightFootIkState = {};
                runtimeState.LeftFootGroundingSmoothingState = {};
                runtimeState.RightFootGroundingSmoothingState = {};
                runtimeState.LeftFootPlantState = {};
                runtimeState.RightFootPlantState = {};
                return {};
            }
            if (!PhysicsWorldService)
                return "Foot grounding requires an active physics world.";
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!transform)
                return "Foot grounding requires an Animator world transform.";

            const auto bone = [&](const std::string_view name, const RigBoneSemantic semantic)
            { return ResolveIkBone(indices, semantics, settings.AutomaticBoneMapping, name, semantic); };
            const auto pelvis = bone(settings.Pelvis, RigBoneSemantic::Pelvis);
            const std::array chains{std::array{bone(settings.LeftUpperLeg, RigBoneSemantic::LeftUpperLeg),
                                               bone(settings.LeftLowerLeg, RigBoneSemantic::LeftLowerLeg),
                                               bone(settings.LeftFoot, RigBoneSemantic::LeftFoot)},
                                    std::array{bone(settings.RightUpperLeg, RigBoneSemantic::RightUpperLeg),
                                               bone(settings.RightLowerLeg, RigBoneSemantic::RightLowerLeg),
                                               bone(settings.RightFoot, RigBoneSemantic::RightFoot)}};
            if (!pelvis ||
                std::ranges::any_of(
                    chains, [](const auto& chain)
                    { return std::ranges::any_of(chain, [](const auto value) { return !value.has_value(); }); }))
                return "Foot grounding references one or more unavailable skeleton bones.";

            Matrix4 worldToModel;
            try
            {
                worldToModel = Math::Inverse(transform->WorldMatrix());
            }
            catch (const std::exception&)
            {
                return "Foot grounding could not invert the Animator world transform.";
            }
            const auto modelToWorld = transform->WorldMatrix();
            const auto modelBones = ModelBoneMatrices(skeleton, localPose);
            const auto leftHipPosition = Math::TransformPoint(modelBones[*chains[0][0]], {});
            const auto rightHipPosition = Math::TransformPoint(modelBones[*chains[1][0]], {});
            const auto gravityUpModel = Math::TransformDirection(worldToModel, {0.0F, 1.0F, 0.0F});
            const auto leftKneePosition = Math::TransformPoint(modelBones[*chains[0][1]], {});
            const auto leftFootPosition = Math::TransformPoint(modelBones[*chains[0][2]], {});
            const auto rightKneePosition = Math::TransformPoint(modelBones[*chains[1][1]], {});
            const auto rightFootPosition = Math::TransformPoint(modelBones[*chains[1][2]], {});
            const auto kneeReference = Detail::OrientBipedKneeReference(
                Detail::AutomaticBipedKneeReference(leftHipPosition, rightHipPosition, gravityUpModel), leftHipPosition,
                leftKneePosition, leftFootPosition, rightHipPosition, rightKneePosition, rightFootPosition);
            auto characterPhysicsRoot = Detail::FindCharacterControllerRoot(entity);
            if (!characterPhysicsRoot)
            {
                for (auto current = entity; current; current = current.Parent())
                {
                    if (PhysicsBodies.contains(current.Id()))
                    {
                        characterPhysicsRoot = current;
                        break;
                    }
                }
            }
            if (!characterPhysicsRoot)
                characterPhysicsRoot = entity;

            std::set<PhysicsBodyId> characterBodies;
            auto queryLayer = 1U;
            bool hasQueryLayer = false;
            for (const auto& [bodyEntityId, physics] : PhysicsBodies)
            {
                const auto bodyEntity = Runtime->FindEntity(bodyEntityId);
                if (!Detail::IsSameOrDescendantOf(bodyEntity, characterPhysicsRoot))
                    continue;
                characterBodies.insert(physics.Body);
                if (!hasQueryLayer && bodyEntity == characterPhysicsRoot && physics.HasDefinition)
                {
                    queryLayer = physics.Definition.Layer;
                    hasQueryLayer = true;
                }
            }

            FootGroundingRequest request;
            request.Pelvis = *pelvis;
            request.FootHeight = 0.0F;
            request.PelvisWeight = settings.Weight * runtimeWeight;
            request.MaximumPelvisAdjustment =
                Detail::WorldVerticalDistanceToModel(worldToModel, settings.MaximumPelvisAdjustment);
            request.PositionTolerance = Detail::WorldVerticalDistanceToModel(worldToModel, 0.01F);
            float totalLegLength = 0.0F;
            std::size_t legCount = 0;
            float maximumGroundingBlend = 0.0F;
            float minimumGroundingBlend = 1.0F;
            std::array<bool, 2> unsupportedFeet{};
            Ref<const SkinnedMeshAsset> skin;
            Ref<const MeshAsset> skinMesh;
            if (Assets && skinnedMesh)
            {
                const auto skinHandle = Assets->Load<SkinnedMeshAsset>(skinnedMesh, AssetPriority::High);
                skin = skinHandle.TryGetLoaded();
                if (skin)
                {
                    const auto meshHandle = Assets->Load<MeshAsset>(skin->Mesh(), AssetPriority::High);
                    skinMesh = meshHandle.TryGetLoaded();
                    if (skinMesh && (runtimeState.FootClearanceMesh != skin->Mesh() ||
                                     runtimeState.FootClearanceSkinRevision != skinHandle.Revision() ||
                                     runtimeState.FootClearanceMeshRevision != meshHandle.Revision()))
                    {
                        runtimeState.FootClearanceMesh = skin->Mesh();
                        runtimeState.FootClearanceSkinRevision = skinHandle.Revision();
                        runtimeState.FootClearanceMeshRevision = meshHandle.Revision();
                        runtimeState.FootMeshClearances.clear();
                        runtimeState.FootToeBones.clear();
                    }
                }
            }
            const auto distance = [](const Vector3 left, const Vector3 right)
            {
                const auto x = right.X - left.X;
                const auto y = right.Y - left.Y;
                const auto z = right.Z - left.Z;
                return std::sqrt(x * x + y * y + z * z);
            };
            for (std::size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex)
            {
                const auto& chain = chains[chainIndex];
                const auto footPosition = Math::TransformPoint(modelBones[*chain[2]], {});
                const auto footWorld = Math::TransformPoint(modelToWorld, footPosition);
                const auto upperWorld =
                    Math::TransformPoint(modelToWorld, Math::TransformPoint(modelBones[*chain[0]], {}));
                const auto lowerWorld =
                    Math::TransformPoint(modelToWorld, Math::TransformPoint(modelBones[*chain[1]], {}));
                const auto legLength = distance(upperWorld, lowerWorld) + distance(lowerWorld, footWorld);
                const auto upperModel = Math::TransformPoint(modelBones[*chain[0]], {});
                const auto lowerModel = Math::TransformPoint(modelBones[*chain[1]], {});
                totalLegLength += distance(upperModel, lowerModel) + distance(lowerModel, footPosition);
                ++legCount;
                auto& plantRuntime =
                    chainIndex == 0 ? runtimeState.LeftFootPlantState : runtimeState.RightFootPlantState;
                auto& plantState = plantRuntime.Plant;
                auto& stability = chainIndex == 0 ? runtimeState.LeftFootIkState : runtimeState.RightFootIkState;
                auto& smoothing = chainIndex == 0 ? runtimeState.LeftFootGroundingSmoothingState
                                                  : runtimeState.RightFootGroundingSmoothingState;
                const auto chainRuntimeWeight =
                    proceduralFootWeights ? std::clamp((*proceduralFootWeights)[chainIndex], 0.0F, 1.0F) : 1.0F;
                if (chainRuntimeWeight <= std::numeric_limits<float>::epsilon())
                {
                    plantRuntime = {};
                    stability = {};
                    smoothing = {};
                    continue;
                }
                bool forcePlantCandidate = false;
                bool resolvedLockedSupport = settings.LockPlantedFeet && plantState.Locked;
                if (resolvedLockedSupport && plantRuntime.Support)
                {
                    const auto supportEntity = Runtime->FindEntity(*plantRuntime.Support);
                    const auto supportTransform =
                        supportEntity ? supportEntity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
                    const auto supportContact =
                        supportTransform ? Detail::ResolveFootPlantSupportAnchor(supportTransform->WorldMatrix(),
                                                                                 plantRuntime.SupportAnchor)
                                         : std::nullopt;
                    const auto supportSurface =
                        supportTransform ? Detail::ResolveFootPlantSupportAnchor(supportTransform->WorldMatrix(),
                                                                                 plantRuntime.SupportSurfaceAnchor)
                                         : std::nullopt;
                    if (supportContact && supportSurface)
                    {
                        plantState.Position = supportContact->Position;
                        plantState.Normal = supportContact->Normal;
                        plantRuntime.SurfacePosition = supportSurface->Position;
                        plantRuntime.SurfaceNormal = supportSurface->Normal;
                    }
                    else
                    {
                        plantRuntime = {};
                        resolvedLockedSupport = false;
                        forcePlantCandidate = true;
                    }
                }
                const Vector3 origin{footWorld.X, footWorld.Y + settings.RaycastHeight, footWorld.Z};
                auto raycastDistance = settings.RaycastDistance;
                if (settings.AutomaticRaycastDistance)
                {
                    raycastDistance = std::max(raycastDistance, legLength * 1.1F);
                }
                const auto hits =
                    PhysicsWorldService->RayCast({.Origin = origin,
                                                  .Direction = {0.0F, -1.0F, 0.0F},
                                                  .MaximumDistance = settings.RaycastHeight + raycastDistance,
                                                  .Mask = settings.CollisionMask,
                                                  .IncludeTriggers = false,
                                                  .Layer = queryLayer});
                const auto hit = std::ranges::find_if(
                    hits,
                    [&](const auto& candidate)
                    {
                        if (characterBodies.contains(candidate.Body))
                            return false;
                        const auto normalLength = std::sqrt(candidate.Normal.X * candidate.Normal.X +
                                                            candidate.Normal.Y * candidate.Normal.Y +
                                                            candidate.Normal.Z * candidate.Normal.Z);
                        if (normalLength <= 0.000001F)
                            return false;
                        const auto normalY = std::clamp(candidate.Normal.Y / normalLength, -1.0F, 1.0F);
                        const auto slope = std::acos(normalY) * 57.2957795131F;
                        return slope <= settings.MaximumSlopeDegrees;
                    });
                std::optional<Detail::ModelFootGroundContact> contact;
                std::optional<Vector3> footTarget;
                std::optional<Vector3> targetNormalWorld;
                if (resolvedLockedSupport && hit != hits.end() &&
                    Detail::ShouldReplaceAutomaticFootSupport(plantRuntime.SurfacePosition, plantRuntime.SurfaceNormal,
                                                              hit->Position))
                {
                    plantRuntime = {};
                    resolvedLockedSupport = false;
                    forcePlantCandidate = true;
                }
                if (resolvedLockedSupport)
                {
                    const auto animationReleased = Detail::ShouldReleaseAutomaticFootPlant(
                        footWorld, plantRuntime.ReleasePosition, plantRuntime.ReleaseNormal, legLength,
                        settings.ReleaseDistance);
                    const auto supportNeedsReanchor =
                        plantRuntime.Support && Detail::ShouldReanchorMovingFootSupport(
                                                    plantState.Position, plantRuntime.ReleasePosition,
                                                    plantRuntime.ReleaseNormal, legLength, settings.ReleaseDistance);
                    const auto outsideReach =
                        distance(upperWorld, plantState.Position) > legLength + settings.MaximumPelvisAdjustment;
                    if (!animationReleased && !supportNeedsReanchor && !outsideReach)
                    {
                        contact = Detail::ToModelFootGroundContact(worldToModel, plantState.Position, plantState.Normal,
                                                                   0.0F);
                        footTarget = Math::TransformPoint(worldToModel, plantState.Position);
                        targetNormalWorld = plantState.Normal;
                        if (contact)
                            contact->Normal = Math::TransformDirection(worldToModel, plantState.Normal);
                    }
                    else
                    {
                        forcePlantCandidate = !animationReleased && (supportNeedsReanchor || outsideReach);
                        plantRuntime = {};
                        resolvedLockedSupport = false;
                    }
                }

                if (!footTarget)
                {
                    if (hit == hits.end())
                    {
                        plantRuntime = {};
                        unsupportedFeet[chainIndex] = unsupportedFootDropRatio.has_value();
                    }
                    else
                    {
                        contact = Detail::ToModelFootGroundContact(worldToModel, hit->Position, hit->Normal, 0.0F);
                        if (!contact)
                            continue;
                        const auto minimumClearance =
                            Detail::WorldSurfaceDistanceToModel(worldToModel, hit->Normal, settings.FootOffset);
                        if (!minimumClearance)
                            continue;
                        auto soleClearance = Detail::FootBoneBindSurfaceClearance(skeleton, *chain[2]);
                        if (!soleClearance)
                            continue;
                        auto cachedMeshClearance = runtimeState.FootMeshClearances.find(*chain[2]);
                        if (cachedMeshClearance == runtimeState.FootMeshClearances.end() && skin && skinMesh)
                        {
                            cachedMeshClearance = runtimeState.FootMeshClearances
                                                      .emplace(*chain[2], Detail::FootMeshBindSurfaceClearance(
                                                                              skeleton, *skin, *skinMesh, *chain[2]))
                                                      .first;
                        }
                        if (cachedMeshClearance != runtimeState.FootMeshClearances.end() && cachedMeshClearance->second)
                            *soleClearance = std::max(*soleClearance, *cachedMeshClearance->second);
                        footTarget = Detail::FootTargetAboveSurface(*contact, *soleClearance, *minimumClearance);
                        if (!footTarget)
                            continue;
                        const auto candidateWorld = Math::TransformPoint(modelToWorld, *footTarget);
                        const auto candidateNormal = Detail::IkNormalize(hit->Normal);
                        targetNormalWorld = candidateNormal;
                        if (settings.LockPlantedFeet)
                        {
                            const auto wasLocked = plantState.Locked;
                            const auto separation =
                                Detail::IkDot(Detail::IkSubtract(footWorld, candidateWorld), candidateNormal);
                            if (forcePlantCandidate || separation < -settings.ReleaseDistance)
                            {
                                if (!Detail::ForceAutomaticFootPlant(candidateWorld, candidateNormal, plantState))
                                    continue;
                            }
                            else
                            {
                                (void)Detail::UpdateAutomaticFootPlant(footWorld, candidateWorld, candidateNormal,
                                                                       legLength, settings.PlantDistance,
                                                                       settings.ReleaseDistance, plantState);
                            }
                            *footTarget = Math::TransformPoint(worldToModel, plantState.Locked ? plantState.Position
                                                                                               : candidateWorld);
                            if (plantState.Locked)
                            {
                                targetNormalWorld = plantState.Normal;
                                contact->Normal = Math::TransformDirection(worldToModel, plantState.Normal);
                                if (!wasLocked || forcePlantCandidate)
                                {
                                    plantRuntime.ReleasePosition = plantState.Position;
                                    plantRuntime.ReleaseNormal = plantState.Normal;
                                }
                                plantRuntime.SurfacePosition = hit->Position;
                                plantRuntime.SurfaceNormal = candidateNormal;
                                if (const auto support = EntityForBody(hit->Body))
                                {
                                    const auto supportEntity = Runtime->FindEntity(*support);
                                    const auto supportTransform = supportEntity
                                                                      ? supportEntity.GetComponent<TransformComponent>()
                                                                      : Ref<TransformComponent>{};
                                    const auto supportAnchor = supportTransform
                                                                   ? Detail::CaptureFootPlantSupportAnchor(
                                                                         supportTransform->WorldMatrix(),
                                                                         plantState.Position, plantState.Normal)
                                                                   : std::nullopt;
                                    const auto supportSurfaceAnchor =
                                        supportTransform
                                            ? Detail::CaptureFootPlantSupportAnchor(supportTransform->WorldMatrix(),
                                                                                    hit->Position, candidateNormal)
                                            : std::nullopt;
                                    if (supportAnchor && supportSurfaceAnchor)
                                    {
                                        plantRuntime.Support = *support;
                                        plantRuntime.SupportAnchor = *supportAnchor;
                                        plantRuntime.SupportSurfaceAnchor = *supportSurfaceAnchor;
                                    }
                                    else
                                    {
                                        plantRuntime.Support.reset();
                                    }
                                }
                                else
                                {
                                    plantRuntime.Support.reset();
                                }
                            }
                            else
                            {
                                footTarget.reset();
                                contact.reset();
                                targetNormalWorld.reset();
                            }
                        }
                        else
                        {
                            plantRuntime = {};
                        }
                    }
                }

                std::optional<Vector3> desiredWorldTarget;
                if (footTarget && contact && targetNormalWorld)
                    desiredWorldTarget = Math::TransformPoint(modelToWorld, *footTarget);
                const auto smoothed = Detail::UpdateAutomaticFootGroundingSmoothing(
                    desiredWorldTarget, desiredWorldTarget ? targetNormalWorld : std::nullopt, footWorld, deltaSeconds,
                    settings.ResponseTime, smoothing);
                if (!smoothed)
                {
                    if (!unsupportedFeet[chainIndex])
                        stability = {};
                    continue;
                }
                unsupportedFeet[chainIndex] = false;
                auto limitedNormal = smoothed->Normal;
                if (maximumFootRotationDegrees)
                {
                    limitedNormal = Detail::IkNormalize(limitedNormal);
                    const auto slope = std::acos(std::clamp(limitedNormal.Y, -1.0F, 1.0F)) * 57.2957795131F;
                    if (slope > *maximumFootRotationDegrees)
                    {
                        const auto horizontalLength =
                            std::sqrt(limitedNormal.X * limitedNormal.X + limitedNormal.Z * limitedNormal.Z);
                        if (horizontalLength > 0.000001F)
                        {
                            const auto radians = *maximumFootRotationDegrees * 0.0174532925199F;
                            const auto sine = std::sin(radians);
                            limitedNormal = {limitedNormal.X / horizontalLength * sine, std::cos(radians),
                                             limitedNormal.Z / horizontalLength * sine};
                        }
                    }
                }
                contact = Detail::ToModelFootGroundContact(worldToModel, smoothed->Position, limitedNormal, 0.0F);
                if (!contact)
                {
                    smoothing = {};
                    stability = {};
                    continue;
                }
                footTarget = Math::TransformPoint(worldToModel, smoothed->Position);
                const auto upperLeg = Math::TransformPoint(modelBones[*chain[0]], {});
                const auto knee = Math::TransformPoint(modelBones[*chain[1]], {});
                const auto pole = Detail::StableAutomaticLimbPole(upperLeg, knee, footPosition, *footTarget,
                                                                  kneeReference, deltaSeconds, settings.ResponseTime,
                                                                  settings.KneeStability, stability);
                FootGroundContact grounded{*chain[0],
                                           *chain[1],
                                           *chain[2],
                                           *footTarget,
                                           contact->Normal,
                                           pole,
                                           settings.Weight * runtimeWeight * chainRuntimeWeight * smoothed->Blend,
                                           settings.RotationWeight * runtimeWeight * chainRuntimeWeight *
                                               smoothed->Blend};
                auto toe = runtimeState.FootToeBones.find(*chain[2]);
                if (toe == runtimeState.FootToeBones.end())
                {
                    toe = runtimeState.FootToeBones
                              .emplace(*chain[2], Detail::AutomaticFootToeBone(skeleton, skin.Get(), *chain[2]))
                              .first;
                }
                grounded.Toe = toe->second;
                const auto effectiveBlend = chainRuntimeWeight * smoothed->Blend;
                maximumGroundingBlend = std::max(maximumGroundingBlend, effectiveBlend);
                minimumGroundingBlend = std::min(minimumGroundingBlend, effectiveBlend);
                request.Contacts.push_back(std::move(grounded));
            }
            if (!request.Contacts.empty())
            {
                request.PelvisWeight *= maximumGroundingBlend;
                if (legCount != 0 && settings.LockPlantedFeet)
                {
                    const auto averageLegLength = totalLegLength / static_cast<float>(legCount);
                    request.MaximumHorizontalPelvisAdjustment =
                        averageLegLength * horizontalPelvisRatio.value_or(0.25F) * minimumGroundingBlend;
                    request.PelvisSupportRadius = averageLegLength * 0.025F;
                    const auto chest = semantics.find(RigBoneSemantic::Chest);
                    const auto spine = semantics.find(RigBoneSemantic::Spine);
                    if (chest != semantics.end() && Detail::IsBoneInSubtree(skeleton, chest->second, *pelvis) &&
                        chest->second != *pelvis)
                        request.Torso = chest->second;
                    else if (spine != semantics.end() && Detail::IsBoneInSubtree(skeleton, spine->second, *pelvis) &&
                             spine->second != *pelvis)
                        request.Torso = spine->second;
                    if (request.Torso)
                    {
                        request.PelvisRotationWeight =
                            settings.Weight * settings.LeanCorrectionWeight * minimumGroundingBlend;
                        request.MaximumPelvisRotationDegrees = settings.MaximumLeanCorrectionDegrees;
                    }
                }
                const auto solved = SolveFootGrounding(skeleton, localPose, request);
                if (!solved)
                    return "Foot grounding could not solve the configured leg chains.";
                if (solved->UnreachableFeet != 0)
                    return "Foot grounding reached the configured pelvis/leg limit for " +
                           std::to_string(solved->UnreachableFeet) + " foot target(s).";
            }
            if (unsupportedFootDropRatio)
            {
                for (std::size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex)
                {
                    if (!unsupportedFeet[chainIndex])
                        continue;
                    const auto& chain = chains[chainIndex];
                    const auto matrices = ModelBoneMatrices(skeleton, localPose);
                    const auto upperLeg = Math::TransformPoint(matrices[*chain[0]], {});
                    const auto knee = Math::TransformPoint(matrices[*chain[1]], {});
                    const auto foot = Math::TransformPoint(matrices[*chain[2]], {});
                    const auto target = Detail::ProceduralUnsupportedFootTarget(
                        foot, gravityUpModel, distance(upperLeg, knee) + distance(knee, foot),
                        *unsupportedFootDropRatio);
                    auto& stability = chainIndex == 0 ? runtimeState.LeftFootIkState : runtimeState.RightFootIkState;
                    const auto pole =
                        Detail::StableAutomaticLimbPole(upperLeg, knee, foot, target, kneeReference, deltaSeconds,
                                                        settings.ResponseTime, settings.KneeStability, stability);
                    const auto footWeights = proceduralFootWeights.value_or(std::array{1.0F, 1.0F});
                    const auto weight =
                        settings.Weight * runtimeWeight * std::clamp(footWeights[chainIndex], 0.0F, 1.0F);
                    (void)SolveTwoBoneIk(skeleton, localPose, {*chain[0], *chain[1], *chain[2], target, pole, weight});
                }
            }
            return {};
        }

        static void ApplyRootMotion(const Entity& entity, const AnimatorSample& sample, AnimatorComponent& animator)
        {
            if (!animator.ApplyRootMotion())
                return;
            const auto body = entity.GetComponent<RigidBodyComponent>();
            if (body && body->Motion() == PhysicsMotionType::Dynamic)
            {
                animator.SetRuntimeDiagnostic(
                    "Root motion is disabled because the Animator shares an entity with a dynamic rigid body.");
                return;
            }
            bool routedToCharacter = false;
            if (const auto character = entity.GetComponent<CharacterControllerComponent>();
                character && character->Enabled())
            {
                (void)character->QueueDesiredMovement(sample.RootMotion);
                routedToCharacter = true;
            }
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!transform)
                return;
            const auto position = transform->LocalPosition();
            if (!routedToCharacter)
            {
                transform->SetLocalPosition({position.X + sample.RootMotion.X, position.Y + sample.RootMotion.Y,
                                             position.Z + sample.RootMotion.Z});
            }
            if (sample.RootRotation != Quaternion{})
            {
                const auto current = Math::ComposeTransform({}, transform->LocalRotation(), {1.0F, 1.0F, 1.0F});
                const auto delta = Math::ComposeTransform({}, sample.RootRotation, {1.0F, 1.0F, 1.0F});
                Vector3 ignoredPosition;
                Vector3 ignoredScale;
                Quaternion rotation;
                if (Math::DecomposeTransform(Math::Multiply(current, delta), ignoredPosition, rotation, ignoredScale))
                    transform->SetLocalRotation(rotation);
            }
        }

        [[nodiscard]] bool PrepareProceduralAnimator(const Entity& entity, AnimatorComponent& animator,
                                                     AnimationRuntimeState& state, Ref<const SkeletonAsset>& skeleton,
                                                     Ref<const ProceduralMotionProfileAsset>& profile)
        {
            auto targetSkeleton = animator.Skeleton();
            if (animator.SkinnedMesh())
            {
                const auto skin =
                    Assets->Load<SkinnedMeshAsset>(animator.SkinnedMesh(), AssetPriority::High).TryGetLoaded();
                if (!skin)
                {
                    animator.SetRuntimeDiagnostic("Procedural Animator is waiting for its skinned mesh to load.");
                    return false;
                }
                targetSkeleton = skin->Skeleton();
                if (!targetSkeleton)
                {
                    animator.SetRuntimeDiagnostic("The assigned skinned mesh does not reference a skeleton.");
                    return false;
                }
                if (animator.Skeleton() != targetSkeleton)
                    animator.SetSkeleton(targetSkeleton);
            }

            const bool changed = state.PoseSource != animator.PoseSource() || state.Skeleton != targetSkeleton ||
                                 state.Skin != animator.SkinnedMesh() ||
                                 state.ProceduralProfile != animator.ProceduralProfile() ||
                                 state.RigDefinition != animator.RigDefinition();
            if (changed)
            {
                state = {};
                state.PoseSource = AnimatorPoseSource::ProceduralHumanoid;
                state.Skeleton = targetSkeleton;
                state.Skin = animator.SkinnedMesh();
                state.ProceduralProfile = animator.ProceduralProfile();
                state.RigDefinition = animator.RigDefinition();
                if (state.Skeleton)
                    state.SkeletonHandle = Assets->Load<SkeletonAsset>(state.Skeleton, AssetPriority::High);
                if (state.ProceduralProfile)
                {
                    state.ProceduralProfileHandle =
                        Assets->Load<ProceduralMotionProfileAsset>(state.ProceduralProfile, AssetPriority::High);
                }
                if (state.RigDefinition)
                    state.RigDefinitionHandle =
                        Assets->Load<RigDefinitionAsset>(state.RigDefinition, AssetPriority::High);
                animator.SetRuntimeDiagnostic({});
            }
            if (!state.Skeleton || !state.ProceduralProfile || !state.RigDefinition)
            {
                animator.SetRuntimeDiagnostic(
                    "Procedural Humanoid requires skeleton, motion profile, and Rig Definition assets.");
                return false;
            }
            skeleton = state.SkeletonHandle.TryGetLoaded();
            profile = state.ProceduralProfileHandle.TryGetLoaded();
            const auto rig = state.RigDefinitionHandle.TryGetLoaded();
            if (!skeleton || !profile || !rig)
            {
                animator.SetRuntimeDiagnostic("Procedural Animator is waiting for its profile and rig dependencies.");
                return false;
            }

            const auto skeletonRevision = state.SkeletonHandle.Revision();
            const auto profileRevision = state.ProceduralProfileHandle.Revision();
            const auto rigRevision = state.RigDefinitionHandle.Revision();
            if (!state.ProceduralInitialized || state.SkeletonRevision != skeletonRevision ||
                state.ProceduralProfileRevision != profileRevision || state.RigDefinitionRevision != rigRevision)
            {
                state.BoneIndices.clear();
                state.SemanticBoneIndices.clear();
                for (std::uint32_t index = 0; index < skeleton->Bones().size(); ++index)
                    state.BoneIndices.emplace(skeleton->Bones()[index].Name, index);
                for (const auto& bone : rig->Definition().Bones)
                {
                    const auto found = state.BoneIndices.find(bone.Name);
                    if (bone.Semantic != RigBoneSemantic::None && found != state.BoneIndices.end())
                        state.SemanticBoneIndices.emplace(bone.Semantic, found->second);
                }
                constexpr std::array required{
                    RigBoneSemantic::Pelvis,        RigBoneSemantic::Spine,     RigBoneSemantic::LeftUpperArm,
                    RigBoneSemantic::LeftLowerArm,  RigBoneSemantic::LeftHand,  RigBoneSemantic::RightUpperArm,
                    RigBoneSemantic::RightLowerArm, RigBoneSemantic::RightHand, RigBoneSemantic::LeftUpperLeg,
                    RigBoneSemantic::LeftLowerLeg,  RigBoneSemantic::LeftFoot,  RigBoneSemantic::RightUpperLeg,
                    RigBoneSemantic::RightLowerLeg, RigBoneSemantic::RightFoot};
                const auto missing = std::ranges::find_if(required, [&](const auto semantic)
                                                          { return !state.SemanticBoneIndices.contains(semantic); });
                if (missing != required.end())
                {
                    animator.SetRuntimeDiagnostic("Procedural Humanoid rig is missing semantic bone '" +
                                                  std::string(RigBoneSemanticName(*missing)) + "'.");
                    return false;
                }
                state.PreviousProceduralPose.clear();
                state.CurrentProceduralPose.clear();
                state.PreviousProceduralPose.reserve(skeleton->Bones().size());
                for (const auto& bone : skeleton->Bones())
                    state.PreviousProceduralPose.push_back(bone.BindPose);
                state.CurrentProceduralPose = state.PreviousProceduralPose;
                state.SkeletonRevision = skeletonRevision;
                state.ProceduralProfileRevision = profileRevision;
                state.RigDefinitionRevision = rigRevision;
                state.ProceduralInitialized = true;
                state.PreviousGrounded = true;
                state.PreviousProceduralState = ProceduralMotionState::Idle;
                state.LeftFootPlantState = {};
                state.RightFootPlantState = {};
                if (skeletonRevision > 1 || profileRevision > 1 || rigRevision > 1)
                {
                    animator.SetRuntimeDiagnostic(
                        "Procedural profile or rig reload reset planted contacts and pose interpolation safely.");
                }
            }
            (void)entity;
            return true;
        }

        [[nodiscard]] static float VectorLength(const Vector3 value) noexcept
        {
            return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        }

        [[nodiscard]] static Vector3 NormalizeHorizontal(const Vector3 value, const Vector3 fallback = {}) noexcept
        {
            const auto length = std::sqrt(value.X * value.X + value.Z * value.Z);
            return length > 0.000001F ? Vector3{value.X / length, 0.0F, value.Z / length} : fallback;
        }

        [[nodiscard]] static bool CrossedPhase(const float previous, const float current, const float target) noexcept
        {
            return current >= previous ? previous < target && current >= target
                                       : previous < target || current >= target;
        }

        [[nodiscard]] static float SignedHorizontalAngleDegrees(const Vector3 from, const Vector3 to) noexcept
        {
            const auto first = NormalizeHorizontal(from);
            const auto second = NormalizeHorizontal(to);
            if (VectorLength(first) <= 0.000001F || VectorLength(second) <= 0.000001F)
                return 0.0F;
            const auto cosine = std::clamp(first.X * second.X + first.Z * second.Z, -1.0F, 1.0F);
            const auto angle = std::acos(cosine) * 57.2957795131F;
            return first.X * second.Z - first.Z * second.X < 0.0F ? -angle : angle;
        }

        void DispatchProceduralFootEvents(const Entity& entity, AnimationRuntimeState& state,
                                          const ProceduralLocomotionState& procedural)
        {
            if (!Runtime || (procedural.State != ProceduralMotionState::Locomotion &&
                             procedural.State != ProceduralMotionState::TurnInPlace))
                return;
            const auto emit = [&](const bool left, const bool plant)
            {
                const auto name = plant ? "Procedural.FootPlant" : "Procedural.FootLift";
                const auto& footState = left ? state.LeftFootPlantState : state.RightFootPlantState;
                AssetId physicsMaterial;
                if (footState.Support)
                {
                    const auto supportBody = PhysicsBodies.find(*footState.Support);
                    if (supportBody != PhysicsBodies.end())
                        physicsMaterial = supportBody->second.Material;
                }
                Runtime->DispatchProceduralMotionEvent(
                    entity.Id(),
                    {plant ? ProceduralMotionEventType::FootPlant : ProceduralMotionEventType::FootLift,
                     left ? ProceduralFootSide::Left : ProceduralFootSide::Right, procedural.State,
                     procedural.GaitPhase, std::clamp(procedural.Speed / 6.0F, 0.0F, 1.0F), footState.Plant.Position,
                     footState.Plant.Normal, footState.Support.value_or(EntityId{}), physicsMaterial});
                Runtime->DispatchAnimationEvent(entity.Id(),
                                                {name, procedural.GaitPhase, left ? 0 : 1, procedural.Speed, {}});
                if (plant)
                {
                    Runtime->DispatchAnimationEvent(entity.Id(), {"Footstep",
                                                                  procedural.GaitPhase,
                                                                  left ? 0 : 1,
                                                                  std::clamp(procedural.Speed / 6.0F, 0.0F, 1.0F),
                                                                  {}});
                }
            };
            constexpr float liftPhase = 0.02F;
            constexpr float plantPhase = 0.50F;
            for (std::size_t foot = 0; foot < 2; ++foot)
            {
                const auto offset = foot == 0 ? 0.0F : 0.5F;
                auto previous = state.PreviousGaitPhase + offset;
                auto current = state.GaitPhase + offset;
                previous -= std::floor(previous);
                current -= std::floor(current);
                if (CrossedPhase(previous, current, liftPhase))
                    emit(foot == 0, false);
                if (CrossedPhase(previous, current, plantPhase))
                    emit(foot == 0, true);
            }
        }

        void AdvanceProceduralAnimation(const float deltaSeconds)
        {
            if (!Assets || !Runtime || deltaSeconds <= 0.0F)
                return;
            for (const auto& entity : Runtime->Query<AnimatorComponent>())
            {
                const auto animator = entity.GetComponent<AnimatorComponent>();
                if (!animator || animator->PoseSource() != AnimatorPoseSource::ProceduralHumanoid ||
                    !entity.ActiveInHierarchy() || !animator->Enabled())
                {
                    continue;
                }
                auto& statePointer = Animators[entity.Id()];
                if (!statePointer)
                    statePointer = std::make_unique<AnimationRuntimeState>();
                auto& state = *statePointer;
                Ref<const SkeletonAsset> skeleton;
                Ref<const ProceduralMotionProfileAsset> profileAsset;
                if (!PrepareProceduralAnimator(entity, *animator, state, skeleton, profileAsset))
                    continue;
                const auto& profile = profileAsset->Profile();
                animator->SetRuntimeDiagnostic({});
                state.PreviousProceduralPose = state.CurrentProceduralPose;
                state.PreviousGaitPhase = state.GaitPhase;
                state.ProceduralIntent = animator->ConsumeProceduralLocomotionIntent();
                state.ProceduralTime += deltaSeconds;
                ++state.ProceduralTick;

                const auto characterRoot = Detail::FindCharacterControllerRoot(entity);
                const auto character = characterRoot ? characterRoot.GetComponent<CharacterControllerComponent>()
                                                     : entity.GetComponent<CharacterControllerComponent>();
                const auto characterState = character ? character->RuntimeState() : CharacterControllerRuntimeState{};
                const auto grounded = character ? characterState.Grounded : false;
                const auto velocity = character ? characterState.Velocity : state.ProceduralIntent.DesiredWorldVelocity;
                const Vector3 horizontalVelocity{velocity.X, 0.0F, velocity.Z};
                const auto speed = VectorLength(horizontalVelocity);
                state.HorizontalAcceleration =
                    VectorLength(RiggingDetail::Subtract(horizontalVelocity, state.PreviousHorizontalVelocity)) /
                    deltaSeconds;
                Vector3 rootForward{0.0F, 0.0F, 1.0F};
                const auto rootTransform = characterRoot ? characterRoot.GetComponent<TransformComponent>()
                                                         : entity.GetComponent<TransformComponent>();
                if (rootTransform)
                    rootForward = NormalizeHorizontal(
                        Math::TransformDirection(rootTransform->WorldMatrix(), rootForward), rootForward);
                state.RootAngularVelocityDegrees =
                    state.HasPreviousRootForward
                        ? SignedHorizontalAngleDegrees(state.PreviousRootForward, rootForward) / deltaSeconds
                        : 0.0F;
                const auto turningInPlace =
                    grounded && speed <= profile.MinimumMovementSpeed &&
                    std::abs(state.RootAngularVelocityDegrees) >= profile.TurnInPlaceThresholdDegrees;
                const auto justLanded = grounded && !state.PreviousGrounded;
                const auto confirmedTakeoff =
                    !grounded && state.PreviousGrounded && (state.ProceduralIntent.JumpRequested || velocity.Y > 0.1F);
                auto motionState = state.ProceduralState.State;
                if (justLanded)
                {
                    motionState = ProceduralMotionState::Landing;
                    state.LandingElapsed = 0.0F;
                    state.ProceduralState.LandingIntensity =
                        std::clamp(-state.PreviousVerticalSpeed / profile.MaximumLandingSpeed, 0.0F, 1.0F);
                    Runtime->DispatchProceduralMotionEvent(entity.Id(),
                                                           {ProceduralMotionEventType::Land, ProceduralFootSide::None,
                                                            motionState, 0.0F, state.ProceduralState.LandingIntensity});
                    Runtime->DispatchAnimationEvent(
                        entity.Id(), {"Procedural.Land", 0.0F, 0, state.ProceduralState.LandingIntensity, {}});
                }
                else if (!grounded)
                {
                    if (confirmedTakeoff)
                    {
                        motionState = ProceduralMotionState::Takeoff;
                        state.ApexSent = false;
                        Runtime->DispatchProceduralMotionEvent(
                            entity.Id(),
                            {ProceduralMotionEventType::Takeoff, ProceduralFootSide::None, motionState, 0.0F, 1.0F});
                        Runtime->DispatchAnimationEvent(entity.Id(), {"Procedural.Takeoff", 0.0F, 0, 1.0F, {}});
                    }
                    else if (velocity.Y > 0.08F)
                    {
                        motionState = ProceduralMotionState::Rising;
                    }
                    else
                    {
                        motionState = ProceduralMotionState::Falling;
                        if (!state.ApexSent && state.PreviousVerticalSpeed > 0.0F)
                        {
                            state.ApexSent = true;
                            Runtime->DispatchProceduralMotionEvent(
                                entity.Id(),
                                {ProceduralMotionEventType::Apex, ProceduralFootSide::None, motionState, 0.0F, 1.0F});
                            Runtime->DispatchAnimationEvent(entity.Id(), {"Procedural.Apex", 0.0F, 0, 1.0F, {}});
                        }
                    }
                }
                else if (motionState == ProceduralMotionState::Landing &&
                         state.LandingElapsed < profile.LandingRecoveryTime)
                {
                    state.LandingElapsed += deltaSeconds;
                }
                else if (speed > profile.MinimumMovementSpeed)
                {
                    motionState = ProceduralMotionState::Locomotion;
                    state.ProceduralState.LandingIntensity = 0.0F;
                }
                else if (turningInPlace)
                {
                    motionState = ProceduralMotionState::TurnInPlace;
                    state.ProceduralState.LandingIntensity = 0.0F;
                }
                else
                {
                    motionState = ProceduralMotionState::Idle;
                    state.ProceduralState.LandingIntensity = 0.0F;
                    if (state.PreviousProceduralState == ProceduralMotionState::Locomotion)
                        state.StopSettleRemaining = profile.StopSettleTime;
                }
                if (motionState == ProceduralMotionState::Takeoff && !confirmedTakeoff)
                    motionState = velocity.Y > 0.08F ? ProceduralMotionState::Rising : ProceduralMotionState::Falling;
                if (motionState != state.ProceduralState.State)
                {
                    Runtime->DispatchProceduralMotionEvent(entity.Id(), {ProceduralMotionEventType::StateChanged,
                                                                         ProceduralFootSide::None, motionState,
                                                                         state.GaitPhase, 0.0F});
                    Runtime->DispatchAnimationEvent(entity.Id(), {"Procedural.StateChanged", 0.0F,
                                                                  static_cast<std::int32_t>(motionState), 0.0F,
                                                                  std::string(ProceduralMotionStateName(motionState))});
                }

                auto quality = animator->ProceduralQuality();
                if (quality == ProceduralMotionQuality::Auto)
                {
                    quality = ProceduralMotionQuality::High;
                    const auto animatorTransform = entity.GetComponent<TransformComponent>();
                    if (animatorTransform)
                    {
                        auto closestSquared = std::numeric_limits<float>::max();
                        for (const auto& cameraEntity : Runtime->Query<CameraComponent>())
                        {
                            const auto camera = cameraEntity.GetComponent<CameraComponent>();
                            const auto cameraTransform = cameraEntity.GetComponent<TransformComponent>();
                            if (!camera || !camera->Primary() || !cameraTransform || !cameraEntity.ActiveInHierarchy())
                            {
                                continue;
                            }
                            const auto delta = RiggingDetail::Subtract(cameraTransform->WorldPosition(),
                                                                       animatorTransform->WorldPosition());
                            closestSquared = std::min(closestSquared, RiggingDetail::Dot(delta, delta));
                        }
                        if (closestSquared > 50.0F * 50.0F)
                            quality = ProceduralMotionQuality::Low;
                        else if (closestSquared > 20.0F * 20.0F)
                            quality = ProceduralMotionQuality::Medium;
                    }
                }
                state.ProceduralState.State = motionState;
                state.ProceduralState.Quality = quality;
                state.ProceduralState.ActualWorldVelocity = velocity;
                state.ProceduralState.GroundNormal = characterState.GroundNormal;
                state.ProceduralState.Speed = speed;
                state.ProceduralState.VerticalSpeed = velocity.Y;
                state.ProceduralState.Grounded = grounded;

                auto phaseRate = 0.0F;
                if (motionState == ProceduralMotionState::Locomotion)
                {
                    phaseRate = Detail::ProceduralGaitPhaseRate(speed, state.ProceduralIntent.RunBlend,
                                                                profile.WalkSpeed, profile.SprintSpeed,
                                                                profile.WalkCadence, profile.SprintCadence);
                    state.GaitPhase += deltaSeconds * phaseRate;
                    state.GaitPhase -= std::floor(state.GaitPhase);
                }
                else if (motionState == ProceduralMotionState::TurnInPlace)
                {
                    state.GaitPhase += deltaSeconds * std::abs(state.RootAngularVelocityDegrees) /
                                       std::max(profile.TurnStepDegrees, 1.0F);
                    state.GaitPhase -= std::floor(state.GaitPhase);
                }
                else if (motionState == ProceduralMotionState::Idle && state.GaitPhase != 0.0F)
                {
                    state.StopSettleRemaining = std::max(0.0F, state.StopSettleRemaining - deltaSeconds);
                    const auto settle = profile.StopSettleTime <= 0.0F ? 1.0F : deltaSeconds / profile.StopSettleTime;
                    const auto nearest = state.GaitPhase < 0.5F ? 0.0F : 1.0F;
                    state.GaitPhase += (nearest - state.GaitPhase) * std::clamp(settle, 0.0F, 1.0F);
                    if (state.GaitPhase >= 0.999F || state.GaitPhase <= 0.001F)
                        state.GaitPhase = 0.0F;
                }
                state.ProceduralState.GaitPhase = state.GaitPhase;

                const auto solveInterval = quality == ProceduralMotionQuality::Low      ? std::uint64_t{4}
                                           : quality == ProceduralMotionQuality::Medium ? std::uint64_t{2}
                                                                                        : std::uint64_t{1};
                if ((state.ProceduralTick - 1U) % solveInterval != 0U)
                {
                    animator->SetRuntimeProceduralState(state.ProceduralState);
                    DispatchProceduralFootEvents(entity, state, state.ProceduralState);
                    state.PreviousGrounded = grounded;
                    state.PreviousVerticalSpeed = velocity.Y;
                    state.PreviousProceduralState = motionState;
                    state.PreviousRootForward = rootForward;
                    state.PreviousHorizontalVelocity = horizontalVelocity;
                    state.HasPreviousRootForward = true;
                    continue;
                }

                auto pose = std::vector<BoneTransform>{};
                pose.reserve(skeleton->Bones().size());
                for (const auto& bone : skeleton->Bones())
                    pose.push_back(bone.BindPose);
                const auto semantic = [&](const RigBoneSemantic value) { return state.SemanticBoneIndices.at(value); };
                const auto pelvis = semantic(RigBoneSemantic::Pelvis);
                const auto leftUpper = semantic(RigBoneSemantic::LeftUpperLeg);
                const auto leftLower = semantic(RigBoneSemantic::LeftLowerLeg);
                const auto leftFoot = semantic(RigBoneSemantic::LeftFoot);
                const auto rightUpper = semantic(RigBoneSemantic::RightUpperLeg);
                const auto rightLower = semantic(RigBoneSemantic::RightLowerLeg);
                const auto rightFoot = semantic(RigBoneSemantic::RightFoot);

                const auto bindMatrices = ModelBoneMatrices(*skeleton, pose);
                const auto position = [&](const std::uint32_t bone)
                { return Math::TransformPoint(bindMatrices[bone], {}); };
                const auto distance = [&](const std::uint32_t first, const std::uint32_t second)
                { return VectorLength(RiggingDetail::Subtract(position(first), position(second))); };
                const auto leftLegLength = distance(leftUpper, leftLower) + distance(leftLower, leftFoot);
                const auto rightLegLength = distance(rightUpper, rightLower) + distance(rightLower, rightFoot);
                const auto legLength = std::max((leftLegLength + rightLegLength) * 0.5F, 0.001F);
                const auto locomotionWeight =
                    motionState == ProceduralMotionState::Locomotion
                        ? Detail::ProceduralLocomotionPoseWeight(speed, profile.MinimumMovementSpeed, profile.WalkSpeed)
                    : motionState == ProceduralMotionState::TurnInPlace
                        ? std::clamp(std::abs(state.RootAngularVelocityDegrees) / 180.0F, 0.25F, 1.0F)
                    : motionState == ProceduralMotionState::Idle && profile.StopSettleTime > 0.0F
                        ? std::clamp(state.StopSettleRemaining / profile.StopSettleTime, 0.0F, 1.0F)
                        : 0.0F;
                const auto bob = profile.PelvisMotion.Evaluate(state.GaitPhase) * profile.PelvisBobRatio * legLength *
                                 locomotionWeight;
                const auto sway =
                    std::sin(state.GaitPhase * 6.28318530718F) * profile.PelvisSwayRatio * legLength * locomotionWeight;
                pose[pelvis].Translation.X += sway;
                pose[pelvis].Translation.Y +=
                    bob - profile.CrouchDepthRatio * legLength * state.ProceduralIntent.CrouchAmount;
                if (motionState == ProceduralMotionState::Landing)
                {
                    const auto recovery = std::clamp(state.LandingElapsed / profile.LandingRecoveryTime, 0.0F, 1.0F);
                    pose[pelvis].Translation.Y -= profile.LandingCompression.Evaluate(recovery) *
                                                  profile.LandingCompressionRatio * legLength *
                                                  state.ProceduralState.LandingIntensity;
                }
                else if (motionState == ProceduralMotionState::Takeoff)
                {
                    pose[pelvis].Translation.Y -= profile.TakeoffCompressionRatio * legLength;
                }
                else if (!grounded)
                {
                    const auto airborneAmount =
                        velocity.Y > 0.0F ? std::clamp(1.0F - velocity.Y / 8.0F, 0.0F, 1.0F) : 1.0F;
                    pose[pelvis].Translation.Y -=
                        profile.AirborneTuck.Evaluate(airborneAmount) * profile.AirborneTuckRatio * legLength * 0.25F;
                }

                const auto animatorTransform = entity.GetComponent<TransformComponent>();
                Matrix4 worldToModel;
                try
                {
                    worldToModel = Math::Inverse(animatorTransform->WorldMatrix());
                }
                catch (const std::exception&)
                {
                    animator->SetRuntimeDiagnostic("Procedural Animator world transform is not invertible.");
                    continue;
                }
                auto localMotion = Math::TransformDirection(worldToModel, horizontalVelocity);
                localMotion.Y = 0.0F;
                const auto modelSpeed = VectorLength(localMotion);
                localMotion = motionState == ProceduralMotionState::TurnInPlace
                                  ? Vector3{state.RootAngularVelocityDegrees < 0.0F ? -1.0F : 1.0F, 0.0F, 0.0F}
                                  : NormalizeHorizontal(localMotion, {0.0F, 0.0F, 1.0F});
                const auto directionalRatio = Detail::ProceduralDirectionalStrideRatio(
                    localMotion, profile.LateralStrideRatio, profile.BackwardStrideRatio);
                const auto stride = motionState == ProceduralMotionState::Locomotion
                                        ? Detail::ProceduralStrideLength(
                                              modelSpeed, phaseRate, legLength, profile.StrideLengthRatio,
                                              directionalRatio, state.ProceduralIntent.RunBlend, profile.WalkSpeed,
                                              profile.SprintSpeed, profile.WalkCadence, profile.SprintCadence)
                                        : legLength * profile.StrideLengthRatio * directionalRatio * locomotionWeight;

                const auto modelRight = RiggingDetail::Normalize(
                    RiggingDetail::Subtract(position(rightUpper), position(leftUpper)), {1.0F, 0.0F, 0.0F});
                const auto solveLeg = [&](const std::uint32_t upper, const std::uint32_t lower,
                                          const std::uint32_t foot, const float offset, const float side)
                {
                    auto phase = state.GaitPhase + offset;
                    phase -= std::floor(phase);
                    auto matrices = ModelBoneMatrices(*skeleton, pose);
                    const auto currentPelvis = Math::TransformPoint(matrices[pelvis], {});
                    const auto currentFoot = Math::TransformPoint(matrices[foot], {});
                    auto target = currentFoot;
                    if (grounded)
                    {
                        const auto travel = profile.StrideTravel.Evaluate(phase) * stride * 0.5F;
                        target.X += localMotion.X * travel;
                        target.Z += localMotion.Z * travel;
                        target.Y += profile.FootLift.Evaluate(phase) * profile.StepClearanceRatio * legLength *
                                    locomotionWeight;
                    }
                    else
                    {
                        const auto verticalPhase =
                            velocity.Y > 0.0F ? std::clamp(1.0F - velocity.Y / 8.0F, 0.0F, 1.0F) : 1.0F;
                        const auto tuck = profile.AirborneTuck.Evaluate(verticalPhase) * profile.AirborneTuckRatio -
                                          (velocity.Y < 0.0F ? profile.FallingExtensionRatio : 0.0F);
                        target.Y += tuck * legLength;
                        target.Z += profile.AirborneTuckRatio * legLength * 0.22F;
                    }
                    const auto currentLateralOffset =
                        RiggingDetail::Dot(RiggingDetail::Subtract(currentFoot, currentPelvis), modelRight);
                    target = RiggingDetail::Add(
                        target, RiggingDetail::Multiply(
                                    modelRight, Detail::ProceduralFootLateralCorrection(
                                                    currentLateralOffset, legLength, profile.FootSpacingRatio, side)));
                    const auto hip = Math::TransformPoint(matrices[upper], {});
                    const auto knee = Math::TransformPoint(matrices[lower], {});
                    const auto upperLength = VectorLength(RiggingDetail::Subtract(knee, hip));
                    const auto lowerLength = VectorLength(RiggingDetail::Subtract(currentFoot, knee));
                    auto hipToTarget = RiggingDetail::Subtract(target, hip);
                    const auto targetDistance = VectorLength(hipToTarget);
                    const auto distanceForBend = [&](const float degrees)
                    {
                        const auto radians = degrees * 0.0174532925199F;
                        return std::sqrt(std::max(0.0F, upperLength * upperLength + lowerLength * lowerLength +
                                                            2.0F * upperLength * lowerLength * std::cos(radians)));
                    };
                    const auto minimumReach = distanceForBend(profile.MaximumKneeBendDegrees);
                    const auto maximumReach = distanceForBend(profile.MinimumKneeBendDegrees);
                    if (targetDistance > 0.000001F)
                    {
                        const auto constrainedDistance = std::clamp(targetDistance, minimumReach, maximumReach);
                        hipToTarget = RiggingDetail::Multiply(hipToTarget, constrainedDistance / targetDistance);
                        target = RiggingDetail::Add(hip, hipToTarget);
                    }
                    const auto forward =
                        Vector3{hip.X + localMotion.X * legLength, knee.Y, hip.Z + localMotion.Z * legLength};
                    (void)SolveTwoBoneIk(*skeleton, pose, {upper, lower, foot, target, forward, 1.0F});
                };
                solveLeg(leftUpper, leftLower, leftFoot, 0.0F, -1.0F);
                solveLeg(rightUpper, rightLower, rightFoot, 0.5F, 1.0F);

                const auto rotateBone = [&](const RigBoneSemantic bone, const Vector3 degrees)
                {
                    const auto found = state.SemanticBoneIndices.find(bone);
                    if (found == state.SemanticBoneIndices.end())
                        return;
                    pose[found->second].Rotation = RiggingDetail::Normalize(
                        RiggingDetail::Multiply(pose[found->second].Rotation, Math::EulerDegreesToQuaternion(degrees)));
                };
                const auto arm =
                    profile.ArmSwing.Evaluate(state.GaitPhase) * profile.ArmSwingDegrees * locomotionWeight;
                const auto solveArm = [&](const RigBoneSemantic upperSemantic, const RigBoneSemantic lowerSemantic,
                                          const RigBoneSemantic handSemantic, const float side, const float swing)
                {
                    const auto upperFound = state.SemanticBoneIndices.find(upperSemantic);
                    const auto lowerFound = state.SemanticBoneIndices.find(lowerSemantic);
                    const auto handFound = state.SemanticBoneIndices.find(handSemantic);
                    if (upperFound == state.SemanticBoneIndices.end() ||
                        lowerFound == state.SemanticBoneIndices.end() || handFound == state.SemanticBoneIndices.end())
                    {
                        return;
                    }

                    const auto matrices = ModelBoneMatrices(*skeleton, pose);
                    const auto shoulder = Math::TransformPoint(matrices[upperFound->second], {});
                    const auto elbow = Math::TransformPoint(matrices[lowerFound->second], {});
                    const auto hand = Math::TransformPoint(matrices[handFound->second], {});
                    const auto upperLength = VectorLength(RiggingDetail::Subtract(elbow, shoulder));
                    const auto lowerLength = VectorLength(RiggingDetail::Subtract(hand, elbow));
                    if (upperLength <= 0.000001F || lowerLength <= 0.000001F)
                        return;

                    const auto shoulderCenter =
                        RiggingDetail::Multiply(RiggingDetail::Add(position(semantic(RigBoneSemantic::LeftUpperArm)),
                                                                   position(semantic(RigBoneSemantic::RightUpperArm))),
                                                0.5F);
                    const auto modelUp = RiggingDetail::Normalize(
                        RiggingDetail::Subtract(shoulderCenter, Math::TransformPoint(matrices[pelvis], {})),
                        {0.0F, 1.0F, 0.0F});
                    const auto geometry =
                        Detail::BuildProceduralArmGeometry(modelRight, modelUp, side, profile.ArmRestDropDegrees, swing,
                                                           profile.ElbowBendDegrees, upperLength, lowerLength);
                    const auto target =
                        RiggingDetail::Add(shoulder, RiggingDetail::Multiply(geometry.TargetDirection, geometry.Reach));
                    const auto pole =
                        RiggingDetail::Add(shoulder, RiggingDetail::Multiply(geometry.PoleDirection, upperLength));
                    (void)SolveTwoBoneIk(
                        *skeleton, pose,
                        {upperFound->second, lowerFound->second, handFound->second, target, pole, 1.0F});
                };
                if (state.SemanticBoneIndices.contains(RigBoneSemantic::LeftUpperArm) &&
                    state.SemanticBoneIndices.contains(RigBoneSemantic::RightUpperArm))
                {
                    solveArm(RigBoneSemantic::LeftUpperArm, RigBoneSemantic::LeftLowerArm, RigBoneSemantic::LeftHand,
                             -1.0F, arm);
                    solveArm(RigBoneSemantic::RightUpperArm, RigBoneSemantic::RightLowerArm, RigBoneSemantic::RightHand,
                             1.0F, -arm);
                }
                const auto breathing = std::sin(state.ProceduralTime * profile.BreathingFrequency * 6.28318530718F) *
                                       profile.BreathingAmplitudeDegrees;
                const auto accelerationLean = std::clamp(state.HorizontalAcceleration / 30.0F, 0.0F, 1.0F) *
                                              profile.MaximumAccelerationLeanDegrees;
                const auto turnLean =
                    std::clamp(state.RootAngularVelocityDegrees / 180.0F, -1.0F, 1.0F) * profile.MaximumTurnLeanDegrees;
                rotateBone(RigBoneSemantic::Spine, {breathing + accelerationLean, -arm * 0.12F, -turnLean});
                rotateBone(RigBoneSemantic::Chest,
                           {breathing * 0.5F,
                            arm * profile.SpineCounterRotationDegrees / std::max(profile.ArmSwingDegrees, 0.001F),
                            0.0F});
                auto lookDirection = NormalizeHorizontal(state.ProceduralIntent.LookWorldDirection, rootForward);
                const auto lookYaw =
                    std::clamp(SignedHorizontalAngleDegrees(rootForward, lookDirection), -55.0F, 55.0F);
                rotateBone(RigBoneSemantic::Neck, {0.0F, lookYaw * 0.35F, 0.0F});
                rotateBone(RigBoneSemantic::Head, {0.0F, lookYaw * 0.65F, 0.0F});

                if (grounded && motionState != ProceduralMotionState::Takeoff)
                {
                    AnimatorFootGroundingSettings grounding;
                    grounding.Enabled = true;
                    grounding.AutomaticBoneMapping = true;
                    grounding.AutomaticRaycastDistance = false;
                    grounding.LockPlantedFeet = true;
                    grounding.Weight = 1.0F;
                    grounding.RotationWeight = 1.0F;
                    const auto characterHeight = character ? character->Height() : legLength * 2.0F;
                    grounding.RaycastHeight = characterHeight * profile.ProbeHeightRatio;
                    grounding.RaycastDistance = characterHeight * profile.ProbeDistanceRatio;
                    grounding.FootOffset = characterHeight * profile.SoleOffsetRatio;
                    grounding.MaximumPelvisAdjustment = characterHeight * profile.MaximumPelvisAdjustmentRatio;
                    grounding.PlantDistance = characterHeight * profile.PlantDistanceRatio;
                    grounding.ReleaseDistance = characterHeight * profile.ReleaseDistanceRatio;
                    grounding.ResponseTime = profile.GroundingResponseTime;
                    grounding.MaximumSlopeDegrees = profile.MaximumSlopeDegrees;
                    grounding.CollisionMask = profile.CollisionMask;
                    auto footWeights = std::array{1.0F, 1.0F};
                    if (motionState == ProceduralMotionState::Locomotion ||
                        motionState == ProceduralMotionState::TurnInPlace)
                    {
                        footWeights = {Detail::ProceduralFootGroundingWeight(state.GaitPhase),
                                       Detail::ProceduralFootGroundingWeight(state.GaitPhase + 0.5F)};
                    }
                    const auto diagnostic =
                        ApplyFootGrounding(entity, *skeleton, grounding, 1.0F, animator->SkinnedMesh(), deltaSeconds,
                                           pose, state.BoneIndices, state.SemanticBoneIndices, state,
                                           profile.MaximumHorizontalPelvisAdjustmentRatio,
                                           profile.MaximumAnkleSlopeDegrees, footWeights, 0.10F);
                    if (!diagnostic.empty())
                        animator->SetRuntimeDiagnostic(diagnostic);
                }

                if (grounded && locomotionWeight > 0.0F)
                {
                    const auto rollFoot = [&](const std::uint32_t foot, const float phaseOffset)
                    {
                        const auto toe = state.FootToeBones.find(foot);
                        if (toe == state.FootToeBones.end() || !toe->second)
                            return;
                        auto phase = state.GaitPhase + phaseOffset;
                        phase -= std::floor(phase);
                        const auto roll = profile.FootRoll.Evaluate(phase) * 18.0F * locomotionWeight;
                        pose[*toe->second].Rotation = RiggingDetail::Normalize(RiggingDetail::Multiply(
                            pose[*toe->second].Rotation, Math::EulerDegreesToQuaternion({roll, 0.0F, 0.0F})));
                    };
                    rollFoot(leftFoot, 0.0F);
                    rollFoot(rightFoot, 0.5F);
                }
                else if (Detail::ShouldResetProceduralFootContacts(grounded, motionState))
                {
                    state.LeftFootIkState = {};
                    state.RightFootIkState = {};
                    state.LeftFootGroundingSmoothingState = {};
                    state.RightFootGroundingSmoothingState = {};
                    state.LeftFootPlantState = {};
                    state.RightFootPlantState = {};
                }

                Runtime->DispatchAnimatorIk(entity.Id(), {.LayerWeight = 1.0F});
                const auto overrideDiagnostic = Detail::EvaluateIndependentAnimationIkPasses(
                    [&] { return ApplyIkGoals(entity, *skeleton, *animator, pose, state.BoneIndices); },
                    [&]
                    {
                        return ApplyAuthoredArmIk(entity, *skeleton, *animator, pose, state.BoneIndices,
                                                  state.SemanticBoneIndices, state);
                    });
                if (!overrideDiagnostic.empty())
                    animator->SetRuntimeDiagnostic(overrideDiagnostic);
                state.CurrentProceduralPose = std::move(pose);
                state.ProceduralState.LeftFootPlanted = state.LeftFootPlantState.Plant.Locked;
                state.ProceduralState.RightFootPlanted = state.RightFootPlantState.Plant.Locked;
                animator->SetRuntimeProceduralState(state.ProceduralState);
                DispatchProceduralFootEvents(entity, state, state.ProceduralState);
                state.PreviousGrounded = grounded;
                state.PreviousVerticalSpeed = velocity.Y;
                state.PreviousProceduralState = motionState;
                state.PreviousRootForward = rootForward;
                state.PreviousHorizontalVelocity = horizontalVelocity;
                state.HasPreviousRootForward = true;
            }
        }

        void PublishProceduralAnimation(const Entity& entity, AnimatorComponent& animator, AnimationRuntimeState& state)
        {
            const auto skeleton = state.SkeletonHandle.TryGetLoaded();
            if (!skeleton || state.CurrentProceduralPose.size() != skeleton->Bones().size())
                return;
            const auto alpha = std::clamp(PresentationInterpolationAlpha, 0.0F, 1.0F);
            auto pose = state.CurrentProceduralPose;
            if (state.PreviousProceduralPose.size() == pose.size())
            {
                for (std::size_t index = 0; index < pose.size(); ++index)
                {
                    const auto& previous = state.PreviousProceduralPose[index];
                    const auto& current = state.CurrentProceduralPose[index];
                    pose[index].Translation = {
                        previous.Translation.X + (current.Translation.X - previous.Translation.X) * alpha,
                        previous.Translation.Y + (current.Translation.Y - previous.Translation.Y) * alpha,
                        previous.Translation.Z + (current.Translation.Z - previous.Translation.Z) * alpha};
                    pose[index].Scale = {previous.Scale.X + (current.Scale.X - previous.Scale.X) * alpha,
                                         previous.Scale.Y + (current.Scale.Y - previous.Scale.Y) * alpha,
                                         previous.Scale.Z + (current.Scale.Z - previous.Scale.Z) * alpha};
                    pose[index].Rotation = RiggingDetail::Nlerp(previous.Rotation, current.Rotation, alpha);
                }
            }
            const auto palette = SkinPalette(*skeleton, pose);
            animator.SetRuntimePose(std::string(ProceduralMotionStateName(state.ProceduralState.State)),
                                    state.GaitPhase, true, palette);
            auto snapshot = std::make_shared<AnimatorDebugSnapshot>();
            snapshot =
                std::const_pointer_cast<AnimatorDebugSnapshot>(FinalPoseDebugSnapshot(*skeleton, pose, snapshot));
            animator.SetRuntimeDebugSnapshot(std::move(snapshot));
            (void)entity;
        }

        void SynchronizeAnimation(const float deltaSeconds)
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
                        skeleton, graph,
                        [this, runtimeState](const AssetId id) { return ResolveClip(*runtimeState, id); },
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

        void ClearAnimation() noexcept { Animators.clear(); }

        [[nodiscard]] std::optional<VfxCollisionHit> QueryVfxCollision(const Vector3 start, const Vector3 end) const
        {
            if (!PhysicsWorldService)
                return std::nullopt;
            const Vector3 delta{end.X - start.X, end.Y - start.Y, end.Z - start.Z};
            const auto distance = std::sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
            if (distance <= 0.000001F)
                return std::nullopt;
            const Vector3 direction{delta.X / distance, delta.Y / distance, delta.Z / distance};
            const auto hits = PhysicsWorldService->RayCast({start, direction, distance, ~0U, true, 1});
            if (hits.empty())
                return std::nullopt;
            return VfxCollisionHit{hits.front().Position, hits.front().Normal};
        }

        [[nodiscard]] static std::uint32_t HashVfxSample(std::uint32_t value) noexcept
        {
            value ^= value >> 16U;
            value *= 0x7feb352dU;
            value ^= value >> 15U;
            value *= 0x846ca68bU;
            value ^= value >> 16U;
            return value;
        }

        [[nodiscard]] static float VfxSampleUnit(const std::uint32_t value) noexcept
        {
            return static_cast<float>(value >> 8U) * (1.0F / 16'777'216.0F);
        }

        [[nodiscard]] std::optional<Vector3> SampleVfxMesh(const AssetId asset, const std::uint32_t randomValue)
        {
            auto& state = VfxMeshShapes[asset];
            Ref<const MeshAsset> mesh;
            std::uint64_t revision = 1;
            if (auto builtin = MeshAsset::ResolveBuiltin(asset))
            {
                mesh = std::move(builtin);
            }
            else
            {
                if (!Assets)
                    return std::nullopt;
                if (!state.Handle)
                    state.Handle = Assets->Load<MeshAsset>(asset, AssetPriority::High);
                mesh = state.Handle.TryGetLoaded();
                revision = state.Handle.Revision();
                if (!mesh)
                    return std::nullopt;
            }
            if (state.Revision != revision)
            {
                std::vector<VfxMeshShapeState::Triangle> triangles;
                triangles.reserve(mesh->Indices().size() / 3U);
                double cumulativeArea = 0.0;
                for (std::size_t index = 0; index + 2U < mesh->Indices().size(); index += 3U)
                {
                    const auto& a = mesh->Vertices()[mesh->Indices()[index]].Position;
                    const auto& b = mesh->Vertices()[mesh->Indices()[index + 1U]].Position;
                    const auto& c = mesh->Vertices()[mesh->Indices()[index + 2U]].Position;
                    const auto edge0 = Vector3{b.X - a.X, b.Y - a.Y, b.Z - a.Z};
                    const auto edge1 = Vector3{c.X - a.X, c.Y - a.Y, c.Z - a.Z};
                    const auto cross =
                        Vector3{edge0.Y * edge1.Z - edge0.Z * edge1.Y, edge0.Z * edge1.X - edge0.X * edge1.Z,
                                edge0.X * edge1.Y - edge0.Y * edge1.X};
                    const auto area = 0.5 * std::sqrt(static_cast<double>(cross.X) * cross.X +
                                                      static_cast<double>(cross.Y) * cross.Y +
                                                      static_cast<double>(cross.Z) * cross.Z);
                    if (!std::isfinite(area) || area <= 0.0)
                        continue;
                    cumulativeArea += area;
                    if (!std::isfinite(cumulativeArea) || cumulativeArea > std::numeric_limits<float>::max())
                        return std::nullopt;
                    triangles.push_back({a, b, c, static_cast<float>(cumulativeArea)});
                }
                if (triangles.empty())
                    return std::nullopt;
                state.Triangles = std::move(triangles);
                state.TotalArea = static_cast<float>(cumulativeArea);
                state.Revision = revision;
            }
            if (state.Triangles.empty() || state.TotalArea <= 0.0F)
                return std::nullopt;
            const auto selected = VfxSampleUnit(HashVfxSample(randomValue ^ 0x3c6ef372U)) * state.TotalArea;
            const auto found = std::lower_bound(state.Triangles.begin(), state.Triangles.end(), selected,
                                                [](const VfxMeshShapeState::Triangle& triangle, const float value)
                                                { return triangle.CumulativeArea < value; });
            const auto& triangle = found == state.Triangles.end() ? state.Triangles.back() : *found;
            const auto root = std::sqrt(VfxSampleUnit(HashVfxSample(randomValue ^ 0xa54ff53aU)));
            const auto barycentricA = 1.0F - root;
            const auto barycentricB = root * (1.0F - VfxSampleUnit(HashVfxSample(randomValue ^ 0x510e527fU)));
            const auto barycentricC = 1.0F - barycentricA - barycentricB;
            return Vector3{triangle.A.X * barycentricA + triangle.B.X * barycentricB + triangle.C.X * barycentricC,
                           triangle.A.Y * barycentricA + triangle.B.Y * barycentricB + triangle.C.Y * barycentricC,
                           triangle.A.Z * barycentricA + triangle.B.Z * barycentricB + triangle.C.Z * barycentricC};
        }

        [[nodiscard]] std::optional<Vector3> SampleVfxShape(const AssetId asset, const std::uint32_t randomValue)
        {
            if (!asset || (!Assets && !MeshAsset::IsBuiltin(asset)))
                return std::nullopt;
            const auto type =
                MeshAsset::IsBuiltin(asset) ? std::optional{MeshAsset::StaticType()} : Assets->TryGetType(asset);
            if (type == MeshAsset::StaticType())
                return SampleVfxMesh(asset, randomValue);
            if (type != VfxVolumeAsset::StaticType())
                return std::nullopt;
            auto& handle = VfxVolumes[asset];
            if (!handle)
                handle = Assets->Load<VfxVolumeAsset>(asset, AssetPriority::High);
            const auto volume = handle.TryGetLoaded();
            return volume ? std::optional{volume->Sample(randomValue)} : std::nullopt;
        }

        void InitializeVfx(VfxBackend backend);
        void InitializeVfx();
        void SynchronizeVfx(float deltaSeconds);
        void ClearVfx() noexcept;

        [[nodiscard]] static bool SameCollision(const std::shared_ptr<const CookedCollisionMesh>& first,
                                                const std::shared_ptr<const CookedCollisionMesh>& second) noexcept;
        [[nodiscard]] static bool SamePhysicsDefinition(const PhysicsBodyDefinition& first,
                                                        const PhysicsBodyDefinition& second) noexcept;
        [[nodiscard]] std::optional<PhysicsBodyDefinition> BuildPhysicsDefinition(const Entity& entity,
                                                                                  PhysicsRuntimeState& state);
        void InitializePhysics();
        void SynchronizePhysicsBodies();
        static void MoveTransformInWorld(const Entity& entity, TransformComponent& transform, Vector3 displacement);
        void ApplyCharacterMovement(float deltaSeconds);
        void UpdateCharacterGrounding();
        [[nodiscard]] std::optional<EntityId> EntityForBody(PhysicsBodyId body) const noexcept;
        void PullDynamicBodies();
        void DispatchPhysicsContacts();
        void StepPhysics(float deltaSeconds);
        void CapturePhysicsPresentationSamples();
        void ApplyPhysicsPresentationInterpolation(float alpha);
        void ClearPhysics() noexcept;

        Ref<Scene> Edit;
        Ref<Scene> Runtime;
        Ref<AssetSystem> Assets;
        Ref<PhysicsSystem> PhysicsService;
        Ref<PhysicsWorld> PhysicsWorldService;
        Ref<VfxWorld> VfxWorldService;
        VfxBackend VfxBackendMode = VfxBackend::Cpu;
        bool DeterministicSimulation = false;
        std::thread::id OwnerThread;
        ScenePlayState PlayState = ScenePlayState::Stopped;
        SceneRuntimeDiagnostic Failure;
        Ref<ScenePresentationRuntime> Presentation;
        std::map<EntityId, std::unique_ptr<AnimationRuntimeState>> Animators;
        std::map<EntityId, PhysicsRuntimeState> PhysicsBodies;
        std::map<EntityId, VfxRuntimeState> VfxEmitters;
        std::map<AssetId, VfxMeshShapeState> VfxMeshShapes;
        std::map<AssetId, AssetHandle<VfxVolumeAsset>> VfxVolumes;
        float PresentationWidth = 1920.0F;
        float PresentationHeight = 1080.0F;
        float PresentationInterpolationAlpha = 1.0F;
        RuntimeUiInsets SafeArea;
    };
} // namespace Keire
