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
            std::vector<BoneTransform> BindProceduralPose;
            std::vector<BoneTransform> PreviousProceduralPose;
            std::vector<BoneTransform> CurrentProceduralPose;
            std::vector<BoneTransform> TargetProceduralPose;
            std::vector<BoneTransform> PublishedProceduralPose;
            std::vector<Matrix4> BindModelMatrices;
            std::vector<Matrix4> ModelMatrixScratch;
            std::vector<Matrix4> PublishedModelMatrices;
            std::vector<Matrix4> SkinPaletteCache;
            std::vector<PhysicsBodyId> CharacterBodyScratch;
            FootGroundingRequest FootGroundingRequestCache;
            std::array<std::shared_ptr<AnimatorDebugSnapshot>, 2> ProceduralDebugSnapshots;
            std::uint64_t ProceduralDebugRevision = 0;
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
            float RootAngularVelocityDegrees = 0.0F;
            float HorizontalAcceleration = 0.0F;
            float PreLandingAmount = 0.0F;
            Vector3 FilteredHorizontalVelocity;
            Vector3 FilteredFacingWorldDirection{0.0F, 0.0F, 1.0F};
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

        static void SkinPalette(const SkeletonAsset& skeleton, const std::span<const BoneTransform> localPose,
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

        [[nodiscard]] static std::vector<Matrix4> SkinPalette(const SkeletonAsset& skeleton,
                                                              const std::span<const BoneTransform> localPose)
        {
            std::vector<Matrix4> world;
            std::vector<Matrix4> palette;
            SkinPalette(skeleton, localPose, world, palette);
            return palette;
        }

        static void ModelBoneMatrices(const SkeletonAsset& skeleton, const std::span<const BoneTransform> localPose,
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

        [[nodiscard]] static std::vector<Matrix4> ModelBoneMatrices(const SkeletonAsset& skeleton,
                                                                    const std::span<const BoneTransform> localPose)
        {
            std::vector<Matrix4> world;
            ModelBoneMatrices(skeleton, localPose, world);
            return world;
        }

        [[nodiscard]] static bool SetBoneModelRotationCached(const SkeletonAsset& skeleton,
                                                             const std::span<BoneTransform> localPose,
                                                             const std::uint32_t bone, const Quaternion modelRotation,
                                                             const float weight, std::vector<Matrix4>& world)
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
                parent >= 0
                    ? RiggingDetail::Multiply(RiggingDetail::Conjugate(RiggingDetail::Normalize(parentRotation)),
                                              RiggingDetail::Normalize(modelRotation))
                    : RiggingDetail::Normalize(modelRotation);
            localPose[bone].Rotation = RiggingDetail::Nlerp(localPose[bone].Rotation, desiredLocal, weight);
            return true;
        }

        [[nodiscard]] static bool ApplyBoneModelRotationDeltaCached(const SkeletonAsset& skeleton,
                                                                    const std::span<BoneTransform> localPose,
                                                                    const std::uint32_t bone, const Quaternion delta,
                                                                    const float weight, std::vector<Matrix4>& world)
        {
            ModelBoneMatrices(skeleton, localPose, world);
            Quaternion currentRotation;
            if (!RiggingDetail::MatrixRotation(world[bone], currentRotation))
                return false;
            return SetBoneModelRotationCached(
                skeleton, localPose, bone,
                RiggingDetail::Multiply(RiggingDetail::Normalize(delta), RiggingDetail::Normalize(currentRotation)),
                weight, world);
        }

        [[nodiscard]] static bool SolveTwoBoneIkCached(const SkeletonAsset& skeleton,
                                                       const std::span<BoneTransform> localPose,
                                                       const TwoBoneIkRequest& request, std::vector<Matrix4>& world)
        {
            if (localPose.size() != skeleton.Bones().size() || request.Root >= localPose.size() ||
                request.Middle >= localPose.size() || request.End >= localPose.size() ||
                !RiggingDetail::IsDescendantOf(skeleton, request.Middle, request.Root) ||
                !RiggingDetail::IsDescendantOf(skeleton, request.End, request.Middle) ||
                !Math::IsFinite(request.Target) || !Math::IsFinite(request.Pole) || !std::isfinite(request.Weight) ||
                (request.EndRotation && (!Math::IsFinite(*request.EndRotation) ||
                                         Math::Length(*request.EndRotation) <= RiggingDetail::Epsilon)) ||
                !std::isfinite(request.EndRotationWeight))
            {
                return false;
            }

            const auto weight = std::clamp(request.Weight, 0.0F, 1.0F);
            const auto endRotationWeight = std::clamp(request.EndRotationWeight, 0.0F, 1.0F);
            if (weight <= 0.0F)
            {
                return !request.EndRotation || endRotationWeight <= 0.0F ||
                       SetBoneModelRotationCached(skeleton, localPose, request.End, *request.EndRotation,
                                                  endRotationWeight, world);
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
            const auto singularityMargin =
                std::min(std::max((upperLength + lowerLength) * 0.0025F, RiggingDetail::Epsilon),
                         std::min(upperLength, lowerLength) * 0.25F);
            const auto targetDistance =
                std::clamp(requestedDistance, std::abs(upperLength - lowerLength) + singularityMargin,
                           upperLength + lowerLength - singularityMargin);
            const auto forward = RiggingDetail::Normalize(targetDelta);
            auto bendVector =
                RiggingDetail::ProjectOntoPlane(RiggingDetail::Subtract(request.Pole, rootPosition), forward);
            if (VectorLength(bendVector) <= RiggingDetail::Epsilon)
            {
                bendVector =
                    RiggingDetail::ProjectOntoPlane(RiggingDetail::Subtract(middlePosition, rootPosition), forward);
            }
            if (VectorLength(bendVector) <= RiggingDetail::Epsilon)
            {
                const auto fallback =
                    std::abs(forward.Y) < 0.95F ? Vector3{0.0F, 1.0F, 0.0F} : Vector3{0.0F, 0.0F, 1.0F};
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
            const auto reachableTarget =
                RiggingDetail::Add(rootPosition, RiggingDetail::Multiply(forward, targetDistance));
            const auto middleDelta = RiggingDetail::FromTo(RiggingDetail::Subtract(endPosition, middlePosition),
                                                           RiggingDetail::Subtract(reachableTarget, middlePosition));
            if (!ApplyBoneModelRotationDeltaCached(skeleton, localPose, request.Middle, middleDelta, weight, world))
                return false;
            return !request.EndRotation || endRotationWeight <= 0.0F ||
                   SetBoneModelRotationCached(skeleton, localPose, request.End, *request.EndRotation, endRotationWeight,
                                              world);
        }

        [[nodiscard]] static std::shared_ptr<AnimatorDebugSnapshot>
        ProceduralDebugSnapshot(AnimationRuntimeState& state, const SkeletonAsset& skeleton,
                                const std::span<const BoneTransform> localPose,
                                const std::span<const Matrix4> modelBones)
        {
            auto slot = std::ranges::find_if(state.ProceduralDebugSnapshots, [](const auto& snapshot)
                                             { return snapshot && snapshot.use_count() == 1; });
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
                    ModelBoneMatrices(skeleton, localPose, runtimeState.ModelMatrixScratch);
                    const auto& modelBones = runtimeState.ModelMatrixScratch;
                    const auto rootPosition = Math::TransformPoint(modelBones[*root], {});
                    const auto middlePosition = Math::TransformPoint(modelBones[*middle], {});
                    const auto endPosition = Math::TransformPoint(modelBones[*end], {});
                    pole =
                        Detail::StableAutomaticLimbPole(rootPosition, middlePosition, endPosition, target, stability);
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
            ModelBoneMatrices(skeleton, localPose, runtimeState.ModelMatrixScratch);
            const auto& modelBones = runtimeState.ModelMatrixScratch;
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

            runtimeState.CharacterBodyScratch.clear();
            auto queryLayer = 1U;
            bool hasQueryLayer = false;
            for (const auto& [bodyEntityId, physics] : PhysicsBodies)
            {
                const auto bodyEntity = Runtime->FindEntity(bodyEntityId);
                if (!Detail::IsSameOrDescendantOf(bodyEntity, characterPhysicsRoot))
                    continue;
                runtimeState.CharacterBodyScratch.push_back(physics.Body);
                if (!hasQueryLayer && bodyEntity == characterPhysicsRoot && physics.HasDefinition)
                {
                    queryLayer = physics.Definition.Layer;
                    hasQueryLayer = true;
                }
            }

            auto& request = runtimeState.FootGroundingRequestCache;
            request.Pelvis = *pelvis;
            request.Torso.reset();
            request.Contacts.clear();
            request.FootHeight = 0.0F;
            request.PelvisWeight = settings.Weight * runtimeWeight;
            request.MaximumPelvisAdjustment =
                Detail::WorldVerticalDistanceToModel(worldToModel, settings.MaximumPelvisAdjustment);
            request.MaximumHorizontalPelvisAdjustment = 0.0F;
            request.PelvisSupportRadius = 0.0F;
            request.PelvisRotationWeight = 0.0F;
            request.MaximumPelvisRotationDegrees = 0.0F;
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
                        if (std::ranges::find(runtimeState.CharacterBodyScratch, candidate.Body) !=
                            runtimeState.CharacterBodyScratch.end())
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
                    ModelBoneMatrices(skeleton, localPose, runtimeState.ModelMatrixScratch);
                    const auto& matrices = runtimeState.ModelMatrixScratch;
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
                    (void)SolveTwoBoneIkCached(skeleton, localPose,
                                               {*chain[0], *chain[1], *chain[2], target, pole, weight},
                                               runtimeState.ModelMatrixScratch);
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
                state.BindProceduralPose.clear();
                state.BindProceduralPose.reserve(skeleton->Bones().size());
                for (const auto& bone : skeleton->Bones())
                    state.BindProceduralPose.push_back(bone.BindPose);
                state.PreviousProceduralPose = state.BindProceduralPose;
                state.CurrentProceduralPose = state.BindProceduralPose;
                state.TargetProceduralPose = state.BindProceduralPose;
                state.PublishedProceduralPose = state.BindProceduralPose;
                ModelBoneMatrices(*skeleton, state.BindProceduralPose, state.BindModelMatrices);
                state.ModelMatrixScratch.resize(skeleton->Bones().size());
                state.PublishedModelMatrices.resize(skeleton->Bones().size());
                state.SkinPaletteCache.resize(skeleton->Bones().size());
                state.CharacterBodyScratch.reserve(4);
                state.FootGroundingRequestCache.Contacts.reserve(2);
                state.ProceduralDebugSnapshots = {std::make_shared<AnimatorDebugSnapshot>(),
                                                  std::make_shared<AnimatorDebugSnapshot>()};
                for (auto& snapshot : state.ProceduralDebugSnapshots)
                {
                    snapshot->Pose.resize(skeleton->Bones().size());
                    for (std::size_t index = 0; index < skeleton->Bones().size(); ++index)
                    {
                        snapshot->Pose[index].Name = skeleton->Bones()[index].Name;
                        snapshot->Pose[index].Parent = skeleton->Bones()[index].Parent;
                    }
                }
                state.ProceduralDebugRevision = 0;
                state.SkeletonRevision = skeletonRevision;
                state.ProceduralProfileRevision = profileRevision;
                state.RigDefinitionRevision = rigRevision;
                state.ProceduralInitialized = true;
                state.PreviousGrounded = true;
                state.PreviousProceduralState = ProceduralMotionState::Idle;
                state.FilteredHorizontalVelocity = {};
                state.FilteredFacingWorldDirection = {0.0F, 0.0F, 1.0F};
                state.PreviousRootForward = {};
                state.HasPreviousRootForward = false;
                state.PreLandingAmount = 0.0F;
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

        [[nodiscard]] static Vector3 RespondHorizontalDirection(const Vector3 current, const Vector3 target,
                                                                const float blend) noexcept
        {
            const auto from = NormalizeHorizontal(current, {0.0F, 0.0F, 1.0F});
            const auto to = NormalizeHorizontal(target, from);
            const auto radians =
                SignedHorizontalAngleDegrees(from, to) * std::clamp(blend, 0.0F, 1.0F) * 0.0174532925199F;
            const auto cosine = std::cos(radians);
            const auto sine = std::sin(radians);
            return NormalizeHorizontal({from.X * cosine - from.Z * sine, 0.0F, from.X * sine + from.Z * cosine}, to);
        }

        [[nodiscard]] float PreLandingAmount(const Entity& entity, const Entity& characterRoot,
                                             const Ref<CharacterControllerComponent>& character, const Vector3 velocity,
                                             const ProceduralMotionProfile& profile, AnimationRuntimeState& state) const
        {
            if (!PhysicsWorldService || velocity.Y >= 0.0F || profile.PreLandingProbeTime <= 0.0F)
                return 0.0F;
            const auto transform = characterRoot ? characterRoot.GetComponent<TransformComponent>()
                                                 : entity.GetComponent<TransformComponent>();
            if (!transform)
                return 0.0F;

            const auto downwardSpeed = -velocity.Y;
            const auto maximumClearance = downwardSpeed * profile.PreLandingProbeTime +
                                          0.5F * 9.81F * profile.PreLandingProbeTime * profile.PreLandingProbeTime;
            if (maximumClearance <= 0.000001F)
                return 0.0F;
            const auto bodyClearance = character ? character->Height() * 0.5F : 0.0F;

            state.CharacterBodyScratch.clear();
            const auto physicsRoot = characterRoot ? characterRoot : entity;
            for (const auto& [bodyEntityId, physics] : PhysicsBodies)
            {
                if (Detail::IsSameOrDescendantOf(Runtime->FindEntity(bodyEntityId), physicsRoot))
                    state.CharacterBodyScratch.push_back(physics.Body);
            }

            const auto hits = PhysicsWorldService->RayCast({.Origin = transform->WorldPosition(),
                                                            .Direction = {0.0F, -1.0F, 0.0F},
                                                            .MaximumDistance = bodyClearance + maximumClearance,
                                                            .Mask = profile.CollisionMask,
                                                            .IncludeTriggers = false,
                                                            .Layer = character ? character->Layer() : 1U});
            const auto landing = std::ranges::find_if(
                hits,
                [&](const PhysicsQueryHit& hit)
                {
                    if (std::ranges::find(state.CharacterBodyScratch, hit.Body) != state.CharacterBodyScratch.end())
                        return false;
                    const auto normalLength = VectorLength(hit.Normal);
                    if (normalLength <= 0.000001F)
                        return false;
                    const auto slope = std::acos(std::clamp(hit.Normal.Y / normalLength, -1.0F, 1.0F)) * 57.2957795131F;
                    return slope <= profile.MaximumSlopeDegrees;
                });
            if (landing == hits.end())
                return 0.0F;
            return Detail::ProceduralPreLandingAmount(std::max(0.0F, landing->Distance - bodyClearance), downwardSpeed,
                                                      profile.PreLandingProbeTime);
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

        void AdvanceProceduralAnimation(float deltaSeconds);
        void PublishProceduralAnimation(const Entity& entity, AnimatorComponent& animator,
                                        AnimationRuntimeState& state);

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
