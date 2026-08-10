#include "Keire/Scenes/Scene.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/PhysicsMaterialAsset.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Log.h"
#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Vfx/VfxSystem.h"
#include "Keire/Vfx/VfxVolumeAsset.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool HasCanonicalVfxRangeEndpoints(const VfxParameterValue& value) noexcept
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

        [[nodiscard]] bool VfxOverrideMatches(const VfxValueType type, const VfxParameterValue& value) noexcept
        {
            return VfxValueMatchesType(type, value) && IsFiniteVfxValue(value) && HasCanonicalVfxRangeEndpoints(value);
        }

        [[nodiscard]] std::vector<VfxParameterOverride>
        CompatibleVfxOverrides(const VfxEffectDefinition& definition,
                               const std::span<const VfxParameterOverride> authored)
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
    } // namespace

    class SceneRuntimeSession::Impl final
    {
      public:
        struct AnimationRuntimeState final
        {
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
            AssetHandle<AnimationGraphAsset> GraphHandle;
            AssetHandle<SkeletonAsset> SkeletonHandle;
            std::map<AssetId, AssetHandle<AnimationClipAsset>> Clips;
            std::map<AssetId, RetargetedClip> RetargetedClips;
            std::map<AssetId, AssetHandle<AvatarMaskAsset>> Masks;
            std::map<std::string, std::uint32_t, std::less<>> BoneIndices;
            std::unique_ptr<AnimatorInstance> Instance;
            std::uint64_t GraphRevision = 0;
            std::uint64_t DependencyGraphRevision = 0;
            std::uint64_t SkeletonRevision = 0;
            std::string DependencyDiagnostic;
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
            std::uint32_t Generation = 0;
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

        [[nodiscard]] std::string ApplyFootGrounding(const Entity& entity, const SkeletonAsset& skeleton,
                                                     const AnimatorComponent& animator,
                                                     std::span<BoneTransform> localPose,
                                                     const std::map<std::string, std::uint32_t, std::less<>>& indices)
        {
            const auto& settings = animator.FootGrounding();
            if (!settings.Enabled)
                return {};
            if (!PhysicsWorldService)
                return "Foot grounding requires an active physics world.";
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!transform)
                return "Foot grounding requires an Animator world transform.";

            const auto bone = [&](const std::string_view name) -> std::optional<std::uint32_t>
            {
                const auto found = indices.find(name);
                return found == indices.end() ? std::nullopt : std::optional(found->second);
            };
            const auto pelvis = bone(settings.Pelvis);
            const std::array chains{
                std::array{bone(settings.LeftUpperLeg), bone(settings.LeftLowerLeg), bone(settings.LeftFoot)},
                std::array{bone(settings.RightUpperLeg), bone(settings.RightLowerLeg), bone(settings.RightFoot)}};
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
            const auto ownPhysics = PhysicsBodies.find(entity.Id());
            const auto ownBody = ownPhysics == PhysicsBodies.end() ? PhysicsBodyId{} : ownPhysics->second.Body;
            const auto queryLayer = ownPhysics == PhysicsBodies.end() || !ownPhysics->second.HasDefinition
                                        ? 1U
                                        : ownPhysics->second.Definition.Layer;

            FootGroundingRequest request;
            request.Pelvis = *pelvis;
            request.FootHeight = settings.FootOffset;
            request.PelvisWeight = settings.Weight;
            request.MaximumPelvisAdjustment = settings.MaximumPelvisAdjustment;
            for (const auto& chain : chains)
            {
                const auto footPosition = Math::TransformPoint(modelBones[*chain[2]], {});
                const auto footWorld = Math::TransformPoint(modelToWorld, footPosition);
                const Vector3 origin{footWorld.X, footWorld.Y + settings.RaycastHeight, footWorld.Z};
                const auto hits =
                    PhysicsWorldService->RayCast({.Origin = origin,
                                                  .Direction = {0.0F, -1.0F, 0.0F},
                                                  .MaximumDistance = settings.RaycastHeight + settings.RaycastDistance,
                                                  .Mask = settings.CollisionMask,
                                                  .IncludeTriggers = false,
                                                  .Layer = queryLayer});
                const auto hit = std::ranges::find_if(hits, [&](const auto& candidate)
                                                      { return !ownBody || candidate.Body != ownBody; });
                if (hit == hits.end())
                    continue;
                const auto position = Math::TransformPoint(worldToModel, hit->Position);
                const auto normalEnd = Math::TransformPoint(worldToModel, {hit->Position.X + hit->Normal.X,
                                                                           hit->Position.Y + hit->Normal.Y,
                                                                           hit->Position.Z + hit->Normal.Z});
                const Vector3 normal{normalEnd.X - position.X, normalEnd.Y - position.Y, normalEnd.Z - position.Z};
                const auto knee = Math::TransformPoint(modelBones[*chain[1]], {});
                request.Contacts.push_back({*chain[0],
                                            *chain[1],
                                            *chain[2],
                                            position,
                                            normal,
                                            {knee.X, knee.Y, knee.Z + 1.0F},
                                            settings.Weight,
                                            settings.RotationWeight});
            }
            if (request.Contacts.empty())
                return {};
            if (!SolveFootGrounding(skeleton, localPose, request))
                return "Foot grounding could not solve the configured leg chains.";
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

                if (state->Graph != animator->Graph() || state->Skeleton != targetSkeleton || state->Skin != skinId)
                {
                    *state = {};
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
                    for (std::uint32_t index = 0; index < skeleton->Bones().size(); ++index)
                        state->BoneIndices.emplace(skeleton->Bones()[index].Name, index);
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
                if (const auto ikDiagnostic =
                        ApplyIkGoals(entity, *skeleton, *animator, sample.LocalPose, state->BoneIndices);
                    !ikDiagnostic.empty())
                {
                    animator->SetRuntimeDiagnostic(ikDiagnostic);
                }
                else if (const auto footDiagnostic =
                             ApplyFootGrounding(entity, *skeleton, *animator, sample.LocalPose, state->BoneIndices);
                         !footDiagnostic.empty())
                {
                    animator->SetRuntimeDiagnostic(footDiagnostic);
                }
                const auto palette = SkinPalette(*skeleton, sample.LocalPose);
                animator->SetRuntimePose(sample.State, sample.NormalizedTime, state->Instance->Playing(), palette);
                auto debugSnapshot = state->Instance->DebugSnapshot();
                if (!animator->IkGoals().empty() || animator->FootGrounding().Enabled)
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

        void InitializeVfx(const VfxBackend backend)
        {
            ClearVfx();
            VfxWorldSpecification specification;
            specification.Backend = backend;
            specification.MaximumParticles =
                backend == VfxBackend::Gpu ? 1'000'000U : VfxRenderSnapshot::MaximumParticles;
            specification.CollisionQuery = [this](const Vector3 start, const Vector3 end)
            { return QueryVfxCollision(start, end); };
            specification.ShapeSample = [this](const AssetId asset, const std::uint32_t randomValue)
            { return SampleVfxShape(asset, randomValue); };
            VfxWorldService = CreateRef<VfxWorld>(std::move(specification));
            VfxBackendMode = backend;
        }

        void InitializeVfx() { InitializeVfx(DeterministicSimulation || !Assets ? VfxBackend::Cpu : VfxBackend::Gpu); }

        void SynchronizeVfx(const float deltaSeconds)
        {
            if (!VfxWorldService || !Runtime)
                return;

            std::set<EntityId> seen;
            bool fallbackToCpu = false;
            std::string fallbackDiagnostic;
            for (const auto& entity : Runtime->Query<VfxEmitterComponent>())
            {
                const auto emitter = entity.GetComponent<VfxEmitterComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                if (!emitter || !transform)
                    continue;
                seen.emplace(entity.Id());
                auto& state = VfxEmitters[entity.Id()];
                if (state.Effect != emitter->Effect())
                {
                    if (state.Handle)
                        VfxWorldService->Stop(state.Handle);
                    state = {};
                    state.Effect = emitter->Effect();
                    if (state.Effect && Assets)
                        state.EffectHandle = Assets->Load<VfxEffectAsset>(state.Effect, AssetPriority::High);
                }
                if (!entity.ActiveInHierarchy() || !emitter->Enabled() || !emitter->PlayOnAwake() || !state.Effect)
                {
                    if (state.Handle)
                    {
                        VfxWorldService->Stop(state.Handle);
                        state.Handle = {};
                    }
                    continue;
                }

                const auto effect = state.EffectHandle.TryGetLoaded();
                if (!effect)
                    continue;
                const auto overrides = CompatibleVfxOverrides(effect->Definition(), emitter->ParameterOverrides());
                Vector3 position;
                Quaternion rotation;
                Vector3 scale;
                if (!Math::DecomposeTransform(transform->WorldMatrix(), position, rotation, scale))
                {
                    constexpr std::string_view diagnostic = "VFX Emitter Transform cannot be decomposed.";
                    if (state.Diagnostic != diagnostic)
                    {
                        KEIRE_CORE_ERROR("VFX emitter '{}' (entity={}) is disabled: {}", entity.Name(),
                                         entity.Id().Value().ToString(), diagnostic);
                        state.Diagnostic = diagnostic;
                    }
                    continue;
                }
                const auto revision = state.EffectHandle.Revision();
                if (state.RejectedRevision == revision && state.RejectedOverrides == overrides)
                    continue;
                try
                {
                    if (!state.Handle || !VfxWorldService->IsAlive(state.Handle))
                    {
                        state.Handle = VfxWorldService->Activate(
                            {effect, revision, position, rotation, emitter->SeedOffset(), overrides});
                        state.Revision = revision;
                        state.Overrides = overrides;
                    }
                    else
                    {
                        if (revision != state.Revision)
                        {
                            const auto reloadOverrides = CompatibleVfxOverrides(effect->Definition(), state.Overrides);
                            (void)VfxWorldService->Reload(state.Handle, effect, revision);
                            state.Revision = revision;
                            if (overrides == reloadOverrides)
                                state.Overrides = overrides;
                        }
                        if (state.Overrides != overrides)
                        {
                            VfxWorldService->SetParameterOverrides(state.Handle, overrides);
                            state.Overrides = overrides;
                        }
                        VfxWorldService->SetTransform(state.Handle, position, rotation);
                    }
                    if (state.Handle)
                        VfxWorldService->SetSimulationSpeed(state.Handle, emitter->SimulationSpeed());
                    state.RejectedRevision = 0;
                    state.RejectedOverrides.clear();
                    state.Diagnostic.clear();
                }
                catch (const std::exception& exception)
                {
                    if (VfxBackendMode == VfxBackend::Gpu)
                    {
                        const auto gpuPrograms = CompileVfxEffectSystems(effect->Definition(), VfxBackend::Gpu);
                        const auto cpuPrograms = CompileVfxEffectSystems(effect->Definition(), VfxBackend::Cpu);
                        const auto allValid = [](const std::vector<VfxCompiledProgram>& programs)
                        { return !programs.empty() && std::ranges::all_of(programs, &VfxCompiledProgram::Valid); };
                        if (!allValid(gpuPrograms) && allValid(cpuPrograms))
                        {
                            fallbackToCpu = true;
                            fallbackDiagnostic = exception.what();
                            break;
                        }
                    }
                    if (state.Handle)
                    {
                        VfxWorldService->Stop(state.Handle);
                        state.Handle = {};
                    }
                    state.RejectedRevision = revision;
                    state.RejectedOverrides = overrides;
                    if (state.Diagnostic != exception.what())
                    {
                        KEIRE_CORE_ERROR("VFX emitter '{}' (entity={}, effect={}) is disabled: {}", entity.Name(),
                                         entity.Id().Value().ToString(), state.Effect.ToString(), exception.what());
                        state.Diagnostic = exception.what();
                    }
                }
                catch (...)
                {
                    if (state.Handle)
                    {
                        VfxWorldService->Stop(state.Handle);
                        state.Handle = {};
                    }
                    state.RejectedRevision = revision;
                    state.RejectedOverrides = overrides;
                    constexpr std::string_view diagnostic = "VFX activation failed with a non-standard exception.";
                    if (state.Diagnostic != diagnostic)
                    {
                        KEIRE_CORE_ERROR("VFX emitter '{}' (entity={}, effect={}) is disabled: {}", entity.Name(),
                                         entity.Id().Value().ToString(), state.Effect.ToString(), diagnostic);
                        state.Diagnostic = diagnostic;
                    }
                }
            }

            if (fallbackToCpu)
            {
                KEIRE_CORE_WARN("Scene VFX is falling back to the CPU backend because a GPU effect is unsupported: {}",
                                fallbackDiagnostic);
                InitializeVfx(VfxBackend::Cpu);
                SynchronizeVfx(deltaSeconds);
                return;
            }

            for (auto iterator = VfxEmitters.begin(); iterator != VfxEmitters.end();)
            {
                if (!seen.contains(iterator->first))
                {
                    if (iterator->second.Handle)
                        VfxWorldService->Stop(iterator->second.Handle);
                    iterator = VfxEmitters.erase(iterator);
                }
                else
                    ++iterator;
            }

            std::set<EntityId> autoDestroy;
            for (const auto& [entityId, state] : VfxEmitters)
            {
                const auto entity = Runtime->FindEntity(entityId);
                const auto emitter = entity ? entity.GetComponent<VfxEmitterComponent>() : Ref<VfxEmitterComponent>{};
                if (emitter && emitter->AutoDestroy() && state.Handle && VfxWorldService->IsAlive(state.Handle))
                    autoDestroy.emplace(entityId);
            }
            VfxWorldService->Update(deltaSeconds);
            for (const auto entityId : autoDestroy)
            {
                const auto found = VfxEmitters.find(entityId);
                if (found != VfxEmitters.end() && !VfxWorldService->IsAlive(found->second.Handle))
                {
                    (void)Runtime->DestroyEntity(entityId);
                    VfxEmitters.erase(found);
                }
            }
        }

        void ClearVfx() noexcept
        {
            VfxEmitters.clear();
            VfxMeshShapes.clear();
            VfxVolumes.clear();
            if (VfxWorldService)
            {
                VfxWorldService->Clear();
                VfxWorldService.Reset();
            }
        }

        [[nodiscard]] static bool SameCollision(const std::shared_ptr<const CookedCollisionMesh>& first,
                                                const std::shared_ptr<const CookedCollisionMesh>& second) noexcept
        {
            if (!first || !second)
                return !first && !second;
            return first->ContentHash == second->ContentHash && first->Kind == second->Kind;
        }

        [[nodiscard]] static bool SamePhysicsDefinition(const PhysicsBodyDefinition& first,
                                                        const PhysicsBodyDefinition& second) noexcept
        {
            const bool transformMatches = first.Motion != PhysicsMotionType::Static ||
                                          (first.Position == second.Position && first.Rotation == second.Rotation);
            return transformMatches && first.Motion == second.Motion && first.Shape == second.Shape &&
                   first.LinearVelocity == second.LinearVelocity && first.HalfExtent == second.HalfExtent &&
                   first.Radius == second.Radius && first.Height == second.Height && first.Mass == second.Mass &&
                   first.Layer == second.Layer && first.Mask == second.Mask && first.Trigger == second.Trigger &&
                   first.Continuous == second.Continuous && first.UseGravity == second.UseGravity &&
                   first.Friction == second.Friction && first.Restitution == second.Restitution &&
                   first.FrictionCombine == second.FrictionCombine &&
                   first.RestitutionCombine == second.RestitutionCombine &&
                   SameCollision(first.Collision, second.Collision);
        }

        [[nodiscard]] std::optional<PhysicsBodyDefinition> BuildPhysicsDefinition(const Entity& entity,
                                                                                  PhysicsRuntimeState& state)
        {
            const auto collider = entity.GetComponent<ColliderComponent>();
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            const bool useCharacter = character && character->Enabled();
            if ((!collider && !useCharacter) || !transform || (!useCharacter && !collider->Enabled()) ||
                !entity.ActiveInHierarchy())
                return std::nullopt;
            const auto rigidBody = entity.GetComponent<RigidBodyComponent>();

            Vector3 worldPosition;
            Quaternion worldRotation;
            Vector3 worldScale;
            if (!Math::DecomposeTransform(transform->WorldMatrix(), worldPosition, worldRotation, worldScale))
                throw std::runtime_error("Physics body Transform cannot be decomposed.");
            const Vector3 absoluteScale{std::abs(worldScale.X), std::abs(worldScale.Y), std::abs(worldScale.Z)};

            PhysicsBodyDefinition definition;
            definition.Motion = useCharacter ? PhysicsMotionType::Kinematic
                                             : (rigidBody ? rigidBody->Motion() : PhysicsMotionType::Static);
            definition.Shape = useCharacter ? ColliderShape::Capsule : collider->Shape();
            definition.Position =
                useCharacter ? worldPosition : Math::TransformPoint(transform->WorldMatrix(), collider->Center());
            definition.Rotation = worldRotation;
            definition.LinearVelocity = rigidBody ? rigidBody->LinearVelocity() : Vector3{};
            definition.HalfExtent =
                useCharacter
                    ? Vector3{character->Radius() * absoluteScale.X, character->Height() * absoluteScale.Y * 0.5F,
                              character->Radius() * absoluteScale.Z}
                    : Vector3{collider->HalfExtent().X * absoluteScale.X, collider->HalfExtent().Y * absoluteScale.Y,
                              collider->HalfExtent().Z * absoluteScale.Z};
            definition.Radius =
                useCharacter ? character->Radius() * std::max(absoluteScale.X, absoluteScale.Z)
                             : collider->Radius() * std::max({absoluteScale.X, absoluteScale.Y, absoluteScale.Z});
            definition.Height = (useCharacter ? character->Height() : collider->Height()) * absoluteScale.Y;
            definition.Mass = rigidBody ? rigidBody->Mass() : 1.0F;
            definition.Layer = EntityLayerBit(entity.Layer());
            definition.Mask = useCharacter ? character->Mask() : collider->Mask();
            definition.Trigger = !useCharacter && collider->Trigger();
            definition.Continuous = !useCharacter && rigidBody && rigidBody->Continuous();
            definition.UseGravity = !useCharacter && rigidBody && rigidBody->UseGravity();

            if (useCharacter)
            {
                state.Material = {};
                state.MaterialHandle = {};
                state.MaterialRevision = 0;
                state.Mesh = {};
                state.MeshHandle = {};
                state.MeshRevision = 0;
                state.CookedCollision.reset();
                state.ColliderCenter = {};
                state.WorldScale = absoluteScale;
                return definition;
            }

            if (state.Material != collider->PhysicsMaterial())
            {
                state.Material = collider->PhysicsMaterial();
                state.MaterialHandle = {};
                state.MaterialRevision = 0;
                if (state.Material && Assets)
                    state.MaterialHandle = Assets->Load<PhysicsMaterialAsset>(state.Material, AssetPriority::High);
            }
            if (state.Material)
            {
                const auto material = state.MaterialHandle.TryGetLoaded();
                if (!material)
                    return std::nullopt;
                const auto& value = material->Definition();
                definition.Friction = value.Friction;
                definition.Restitution = value.Restitution;
                definition.FrictionCombine = value.FrictionCombine;
                definition.RestitutionCombine = value.RestitutionCombine;
                state.MaterialRevision = state.MaterialHandle.Revision();
            }

            const bool meshShape =
                definition.Shape == ColliderShape::ConvexMesh || definition.Shape == ColliderShape::TriangleMesh;
            if (state.Mesh != collider->CollisionMesh())
            {
                state.Mesh = collider->CollisionMesh();
                state.MeshHandle = {};
                state.MeshRevision = 0;
                state.CookedCollision.reset();
                if (state.Mesh && Assets)
                    state.MeshHandle = Assets->Load<MeshAsset>(state.Mesh, AssetPriority::High);
            }
            if (meshShape)
            {
                if (!state.Mesh)
                    throw std::runtime_error("Mesh collider requires a collision Mesh asset.");
                const auto mesh = state.MeshHandle.TryGetLoaded();
                if (!mesh)
                    return std::nullopt;
                const auto revision = state.MeshHandle.Revision();
                if (!state.CookedCollision || state.MeshRevision != revision || state.CookedScale != absoluteScale)
                {
                    CollisionCookInput input;
                    input.Kind = definition.Shape == ColliderShape::ConvexMesh ? CollisionMeshKind::Convex
                                                                               : CollisionMeshKind::Triangle;
                    input.Vertices.reserve(mesh->Vertices().size());
                    for (const auto& vertex : mesh->Vertices())
                        input.Vertices.push_back({vertex.Position.X * absoluteScale.X,
                                                  vertex.Position.Y * absoluteScale.Y,
                                                  vertex.Position.Z * absoluteScale.Z});
                    input.Indices.assign(mesh->Indices().begin(), mesh->Indices().end());
                    state.CookedCollision = CookCollisionMesh(std::move(input));
                    state.MeshRevision = revision;
                    state.CookedScale = absoluteScale;
                }
                definition.Collision = state.CookedCollision;
            }
            state.ColliderCenter = collider->Center();
            state.WorldScale = absoluteScale;
            return definition;
        }

        void InitializePhysics()
        {
            ClearPhysics();
            if (!PhysicsService || !Runtime)
                return;
            PhysicsWorldService = PhysicsService->CreateWorld();
            SynchronizePhysicsBodies();
        }

        void SynchronizePhysicsBodies()
        {
            if (!PhysicsWorldService || !Runtime)
                return;
            std::set<EntityId> candidates;
            for (const auto& entity : Runtime->Query<ColliderComponent>())
                candidates.emplace(entity.Id());
            for (const auto& entity : Runtime->Query<CharacterControllerComponent>())
                candidates.emplace(entity.Id());
            std::set<EntityId> seen;
            for (const auto entityId : candidates)
            {
                const auto entity = Runtime->FindEntity(entityId);
                seen.emplace(entityId);
                auto& state = PhysicsBodies[entityId];
                const auto definition = BuildPhysicsDefinition(entity, state);
                if (!definition)
                {
                    if (state.Body)
                    {
                        PhysicsWorldService->DestroyBody(state.Body);
                        state.Body = {};
                    }
                    state.HasDefinition = false;
                    continue;
                }
                if (!state.Body || !state.HasDefinition || !SamePhysicsDefinition(state.Definition, *definition))
                {
                    if (state.Body)
                        PhysicsWorldService->DestroyBody(state.Body);
                    state.Body = PhysicsWorldService->CreateBody(*definition);
                    ++state.Generation;
                    if (state.Generation == 0)
                        state.Generation = 1;
                }
                else if (definition->Motion == PhysicsMotionType::Kinematic)
                {
                    PhysicsWorldService->SetKinematicTarget(state.Body, definition->Position, definition->Rotation);
                    if (definition->UseGravity != state.Definition.UseGravity)
                        PhysicsWorldService->SetGravityEnabled(state.Body, definition->UseGravity);
                }
                state.Definition = *definition;
                state.HasDefinition = true;
            }
            for (auto iterator = PhysicsBodies.begin(); iterator != PhysicsBodies.end();)
            {
                if (!seen.contains(iterator->first))
                {
                    if (iterator->second.Body)
                        PhysicsWorldService->DestroyBody(iterator->second.Body);
                    iterator = PhysicsBodies.erase(iterator);
                }
                else
                    ++iterator;
            }
        }

        static void MoveTransformInWorld(const Entity& entity, TransformComponent& transform,
                                         const Vector3 displacement)
        {
            Vector3 worldPosition;
            Quaternion worldRotation;
            Vector3 worldScale;
            if (!Math::DecomposeTransform(transform.WorldMatrix(), worldPosition, worldRotation, worldScale))
                throw std::runtime_error("Character Controller Transform cannot be decomposed.");
            worldPosition = {worldPosition.X + displacement.X, worldPosition.Y + displacement.Y,
                             worldPosition.Z + displacement.Z};
            auto local = Math::ComposeTransform(worldPosition, worldRotation, worldScale);
            if (const auto parent = entity.Parent())
            {
                if (const auto parentTransform = parent.GetComponent<TransformComponent>())
                    local = Math::Multiply(Math::Inverse(parentTransform->WorldMatrix()), local);
            }
            Vector3 localPosition;
            Quaternion localRotation;
            Vector3 localScale;
            if (!Math::DecomposeTransform(local, localPosition, localRotation, localScale))
                throw std::runtime_error("Character Controller produced a non-decomposable local Transform.");
            transform.SetLocalPosition(localPosition);
        }

        void ApplyCharacterMovement(const float deltaSeconds)
        {
            for (const auto& entity : Runtime->Query<CharacterControllerComponent>())
            {
                const auto character = entity.GetComponent<CharacterControllerComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                if (!character || !character->Enabled() || !entity.ActiveInHierarchy() || !transform)
                    continue;
                const auto displacement = character->ConsumeDesiredMovement();
                auto runtimeState = PhysicsBodies.find(entity.Id());
                if (runtimeState == PhysicsBodies.end() || !runtimeState->second.Body ||
                    !runtimeState->second.HasDefinition)
                {
                    continue;
                }
                auto& state = runtimeState->second;
                if (displacement == Vector3{})
                {
                    state.CharacterVelocity = {};
                    continue;
                }

                Vector3 start;
                Quaternion rotation;
                Vector3 scale;
                if (!Math::DecomposeTransform(transform->WorldMatrix(), start, rotation, scale))
                    throw std::runtime_error("Character Controller Transform cannot be decomposed.");

                const auto add = [](const Vector3 left, const Vector3 right) noexcept
                { return Vector3{left.X + right.X, left.Y + right.Y, left.Z + right.Z}; };
                const auto subtract = [](const Vector3 left, const Vector3 right) noexcept
                { return Vector3{left.X - right.X, left.Y - right.Y, left.Z - right.Z}; };
                const auto multiply = [](const Vector3 value, const float scalar) noexcept
                { return Vector3{value.X * scalar, value.Y * scalar, value.Z * scalar}; };
                const auto dot = [](const Vector3 left, const Vector3 right) noexcept
                { return left.X * right.X + left.Y * right.Y + left.Z * right.Z; };
                const auto length = [&](const Vector3 value) noexcept { return std::sqrt(dot(value, value)); };
                const auto hasResolvableDisplacement = [&](const Vector3 value) noexcept
                { return dot(value, value) > std::numeric_limits<float>::epsilon(); };

                const auto padding = std::min(character->SkinWidth(), state.Definition.Radius * 0.5F);
                const auto castRadius = state.Definition.Radius - padding;
                const auto castHeight = state.Definition.Height - padding * 2.0F;
                const auto slopeNormal = std::cos(character->MaximumSlopeDegrees() * 3.14159265358979323846F / 180.0F);
                Vector3 current = start;
                const auto cast = [&](const Vector3 origin, const Vector3 movement) -> std::optional<PhysicsQueryHit>
                {
                    if (!hasResolvableDisplacement(movement))
                        return std::nullopt;
                    return PhysicsWorldService->CastCapsule({.Origin = origin,
                                                             .Rotation = rotation,
                                                             .Radius = castRadius,
                                                             .Height = castHeight,
                                                             .Displacement = movement,
                                                             .Mask = character->Mask(),
                                                             .IncludeTriggers = false,
                                                             .Layer = character->Layer(),
                                                             .IgnoreBody = state.Body});
                };
                const auto moveAndSlide = [&](Vector3 movement)
                {
                    for (std::size_t iteration = 0; iteration < 4; ++iteration)
                    {
                        if (!hasResolvableDisplacement(movement))
                            break;
                        const auto movementLength = length(movement);
                        const auto hit = cast(current, movement);
                        if (!hit)
                        {
                            current = add(current, movement);
                            break;
                        }
                        const auto safeDistance = std::max(0.0F, hit->Distance - padding);
                        const auto safeFraction = std::clamp(safeDistance / movementLength, 0.0F, 1.0F);
                        current = add(current, multiply(movement, safeFraction));
                        movement = multiply(movement, 1.0F - safeFraction);
                        const auto intoSurface = dot(movement, hit->Normal);
                        if (intoSurface < 0.0F)
                            movement = subtract(movement, multiply(hit->Normal, intoSurface));
                    }
                };

                const Vector3 horizontal{displacement.X, 0.0F, displacement.Z};
                bool stepped = false;
                if (character->Grounded() && character->StepHeight() > 0.0F && hasResolvableDisplacement(horizontal))
                {
                    const auto obstruction = cast(current, horizontal);
                    if (obstruction && obstruction->Normal.Y < slopeNormal)
                    {
                        const auto upwardDistance = character->StepHeight() + padding;
                        const Vector3 upward{0.0F, upwardDistance, 0.0F};
                        if (!cast(current, upward))
                        {
                            const auto elevated = add(current, upward);
                            if (!cast(elevated, horizontal))
                            {
                                const auto forward = add(elevated, horizontal);
                                const Vector3 downward{0.0F, -(upwardDistance + padding + 0.05F), 0.0F};
                                const auto landing = cast(forward, downward);
                                if (landing && landing->Normal.Y >= slopeNormal)
                                {
                                    const auto downDistance = std::max(0.0F, landing->Distance - padding);
                                    current = add(forward, {0.0F, -downDistance, 0.0F});
                                    stepped = true;
                                }
                            }
                        }
                    }
                }
                if (!stepped)
                    moveAndSlide(horizontal);
                moveAndSlide({0.0F, displacement.Y, 0.0F});

                const auto applied = subtract(current, start);
                state.CharacterVelocity = deltaSeconds > 0.0F ? multiply(applied, 1.0F / deltaSeconds) : Vector3{};
                if (applied != Vector3{})
                {
                    MoveTransformInWorld(entity, *transform, applied);
                    PhysicsWorldService->SetKinematicTarget(state.Body, current, rotation);
                    state.Definition.Position = current;
                    state.Definition.Rotation = rotation;
                }
            }
        }

        void UpdateCharacterGrounding()
        {
            constexpr float Pi = 3.14159265358979323846F;
            for (const auto& entity : Runtime->Query<CharacterControllerComponent>())
            {
                const auto character = entity.GetComponent<CharacterControllerComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                const auto state = PhysicsBodies.find(entity.Id());
                if (!character || !character->Enabled() || !transform || state == PhysicsBodies.end() ||
                    !state->second.Body || state->second.Generation == 0)
                {
                    continue;
                }

                Vector3 worldPosition;
                Quaternion worldRotation;
                Vector3 worldScale;
                if (!Math::DecomposeTransform(transform->WorldMatrix(), worldPosition, worldRotation, worldScale))
                    continue;
                bool grounded = false;
                Vector3 normal{0.0F, 1.0F, 0.0F};
                const auto minimumNormal = std::cos(character->MaximumSlopeDegrees() * Pi / 180.0F);
                const auto& definition = state->second.Definition;
                const auto padding = std::min(character->SkinWidth(), definition.Radius * 0.5F);
                const auto hit = PhysicsWorldService->CastCapsule(
                    {.Origin = worldPosition,
                     .Rotation = worldRotation,
                     .Radius = definition.Radius - padding,
                     .Height = definition.Height - padding * 2.0F,
                     .Displacement = {0.0F, -(character->StepHeight() + padding + 0.05F), 0.0F},
                     .Mask = character->Mask(),
                     .IncludeTriggers = false,
                     .Layer = character->Layer(),
                     .IgnoreBody = state->second.Body});
                if (hit && hit->Normal.Y >= minimumNormal)
                {
                    grounded = true;
                    normal = hit->Normal;
                }
                character->ApplyRuntimeState(state->second.Generation, grounded, normal,
                                             state->second.CharacterVelocity);
            }
        }

        [[nodiscard]] std::optional<EntityId> EntityForBody(const PhysicsBodyId body) const noexcept
        {
            const auto found =
                std::ranges::find_if(PhysicsBodies, [body](const auto& item) { return item.second.Body == body; });
            return found == PhysicsBodies.end() ? std::nullopt : std::optional(found->first);
        }

        void PullDynamicBodies()
        {
            for (auto& [entityId, runtime] : PhysicsBodies)
            {
                if (!runtime.Body || runtime.Definition.Motion != PhysicsMotionType::Dynamic)
                    continue;
                const auto body = PhysicsWorldService->TryGetBody(runtime.Body);
                const auto entity = body ? Runtime->FindEntity(entityId) : Entity{};
                const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
                if (!body || !transform)
                    continue;
                const auto centerTransform = Math::ComposeTransform({}, body->Rotation, runtime.WorldScale);
                const auto centerOffset = Math::TransformDirection(centerTransform, runtime.ColliderCenter);
                const Vector3 origin{body->Position.X - centerOffset.X, body->Position.Y - centerOffset.Y,
                                     body->Position.Z - centerOffset.Z};
                const auto world = Math::ComposeTransform(origin, body->Rotation, runtime.WorldScale);
                Matrix4 local = world;
                if (const auto parent = entity.Parent())
                {
                    if (const auto parentTransform = parent.GetComponent<TransformComponent>())
                        local = Math::Multiply(Math::Inverse(parentTransform->WorldMatrix()), world);
                }
                Vector3 localPosition;
                Quaternion localRotation;
                Vector3 localScale;
                if (!Math::DecomposeTransform(local, localPosition, localRotation, localScale))
                    throw std::runtime_error("Dynamic physics body produced a non-decomposable Transform.");
                transform->SetLocalPosition(localPosition);
                transform->SetLocalRotation(localRotation);
                runtime.Definition.Position = body->Position;
                runtime.Definition.Rotation = body->Rotation;
            }
        }

        void DispatchPhysicsContacts()
        {
            for (const auto& event : PhysicsWorldService->DrainContactEvents())
            {
                const auto first = EntityForBody(event.First);
                const auto second = EntityForBody(event.Second);
                if (!first || !second)
                    continue;
                const auto phase = event.Phase == ContactPhase::Enter  ? PhysicsContactPhase::Enter
                                   : event.Phase == ContactPhase::Stay ? PhysicsContactPhase::Stay
                                                                       : PhysicsContactPhase::Exit;
                Runtime->DispatchPhysicsContact(*first, phase,
                                                {*second, event.Point, event.Normal, event.Impulse, event.Trigger});
                Runtime->DispatchPhysicsContact(*second, phase,
                                                {*first,
                                                 event.Point,
                                                 {-event.Normal.X, -event.Normal.Y, -event.Normal.Z},
                                                 event.Impulse,
                                                 event.Trigger});
            }
        }

        void StepPhysics(const float deltaSeconds)
        {
            if (!PhysicsWorldService)
                return;
            ApplyCharacterMovement(deltaSeconds);
            SynchronizePhysicsBodies();
            PhysicsWorldService->Step(deltaSeconds);
            PullDynamicBodies();
            UpdateCharacterGrounding();
            DispatchPhysicsContacts();
        }

        void ClearPhysics() noexcept
        {
            PhysicsBodies.clear();
            if (PhysicsWorldService)
            {
                try
                {
                    PhysicsWorldService->Close();
                }
                catch (...)
                {
                }
                PhysicsWorldService.Reset();
            }
        }

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
        RuntimeUiInsets SafeArea;
    };

    SceneRuntimeSession::SceneRuntimeSession(Ref<Scene> editScene, Ref<AssetSystem> assets, Ref<AudioSystem> audio,
                                             Ref<PhysicsSystem> physics)
        : m_Impl(std::make_unique<Impl>(std::move(editScene), std::move(assets), std::move(audio), std::move(physics)))
    {
    }

    SceneRuntimeSession::~SceneRuntimeSession() { Stop(); }

    ScenePlayState SceneRuntimeSession::State() const noexcept { return m_Impl->PlayState; }
    Ref<Scene> SceneRuntimeSession::EditScene() const noexcept { return m_Impl->Edit; }
    Ref<Scene> SceneRuntimeSession::RuntimeScene() const noexcept { return m_Impl->Runtime; }
    SceneRuntimeDiagnostic SceneRuntimeSession::Diagnostic() const { return m_Impl->Failure; }
    Ref<ScenePresentationRuntime> SceneRuntimeSession::Presentation() const noexcept { return m_Impl->Presentation; }
    Ref<PhysicsWorld> SceneRuntimeSession::Physics() const noexcept { return m_Impl->PhysicsWorldService; }
    Ref<VfxWorld> SceneRuntimeSession::Vfx() const noexcept { return m_Impl->VfxWorldService; }

    std::vector<ScenePhysicsCheckpointBody> SceneRuntimeSession::CapturePhysicsCheckpoint() const
    {
        m_Impl->RequireOwner("CapturePhysicsCheckpoint");
        std::vector<ScenePhysicsCheckpointBody> result;
        if (!m_Impl->PhysicsWorldService)
            return result;
        result.reserve(m_Impl->PhysicsBodies.size());
        for (const auto& [entity, runtime] : m_Impl->PhysicsBodies)
        {
            if (!runtime.Body)
                continue;
            const auto state = m_Impl->PhysicsWorldService->TryGetBody(runtime.Body);
            if (!state)
                throw std::runtime_error("A scene physics checkpoint could not resolve a runtime body.");
            result.push_back({entity, state->Position, state->Rotation, state->LinearVelocity, state->AngularVelocity,
                              state->Sleeping});
        }
        return result;
    }

    void SceneRuntimeSession::RestorePhysicsCheckpoint(const std::span<const ScenePhysicsCheckpointBody> bodies)
    {
        m_Impl->RequireOwner("RestorePhysicsCheckpoint");
        if (!m_Impl->PhysicsWorldService)
        {
            if (!bodies.empty())
                throw std::runtime_error("A physics checkpoint cannot be restored without an active physics world.");
            return;
        }

        std::set<EntityId> identities;
        for (const auto& body : bodies)
        {
            const auto found = m_Impl->PhysicsBodies.find(body.Entity);
            if (!body.Entity || !identities.insert(body.Entity).second || found == m_Impl->PhysicsBodies.end() ||
                !found->second.Body || !Math::IsFinite(body.Position) || !Math::IsFinite(body.Rotation) ||
                !Math::IsFinite(body.LinearVelocity) || !Math::IsFinite(body.AngularVelocity))
            {
                throw std::runtime_error("A scene physics checkpoint is incompatible with the runtime scene.");
            }
        }
        const auto liveBodyCount = static_cast<std::size_t>(std::ranges::count_if(
            m_Impl->PhysicsBodies, [](const auto& entry) { return static_cast<bool>(entry.second.Body); }));
        if (identities.size() != liveBodyCount)
            throw std::runtime_error("A scene physics checkpoint does not contain every runtime body.");

        for (const auto& body : bodies)
        {
            const auto runtimeBody = m_Impl->PhysicsBodies.at(body.Entity).Body;
            m_Impl->PhysicsWorldService->SetBodyState(
                runtimeBody,
                {runtimeBody, body.Position, body.Rotation, body.LinearVelocity, body.AngularVelocity, body.Sleeping});
        }
    }

    std::vector<SceneAnimatorCheckpoint> SceneRuntimeSession::CaptureAnimatorCheckpoint() const
    {
        m_Impl->RequireOwner("CaptureAnimatorCheckpoint");
        std::vector<SceneAnimatorCheckpoint> result;
        result.reserve(m_Impl->Animators.size());
        for (const auto& [entity, runtime] : m_Impl->Animators)
        {
            if (runtime && runtime->Instance)
                result.push_back({entity, runtime->Instance->CaptureCheckpoint()});
        }
        return result;
    }

    void SceneRuntimeSession::RestoreAnimatorCheckpoint(const std::span<const SceneAnimatorCheckpoint> animators)
    {
        m_Impl->RequireOwner("RestoreAnimatorCheckpoint");
        std::map<EntityId, AnimatorInstance*> live;
        for (const auto& [entity, runtime] : m_Impl->Animators)
            if (runtime && runtime->Instance)
                live.emplace(entity, runtime->Instance.get());
        std::set<EntityId> identities;
        for (const auto& animator : animators)
            if (!animator.Entity || !identities.insert(animator.Entity).second || !live.contains(animator.Entity))
                throw std::runtime_error("An animator checkpoint is incompatible with the runtime scene.");
        if (identities.size() != live.size())
            throw std::runtime_error("An animator checkpoint does not contain every runtime animator.");

        std::vector<SceneAnimatorCheckpoint> rollback;
        rollback.reserve(animators.size());
        try
        {
            for (const auto& animator : animators)
            {
                auto* instance = live.at(animator.Entity);
                rollback.push_back({animator.Entity, instance->CaptureCheckpoint()});
                instance->RestoreCheckpoint(animator.State);
            }
        }
        catch (...)
        {
            const auto original = std::current_exception();
            for (auto iterator = rollback.rbegin(); iterator != rollback.rend(); ++iterator)
            {
                try
                {
                    live.at(iterator->Entity)->RestoreCheckpoint(iterator->State);
                }
                catch (...)
                {
                }
            }
            std::rethrow_exception(original);
        }
    }

    void SceneRuntimeSession::SetDeterministicSimulation(const bool enabled)
    {
        m_Impl->RequireOwner("SetDeterministicSimulation");
        if (m_Impl->DeterministicSimulation == enabled)
            return;
        m_Impl->DeterministicSimulation = enabled;
        if (m_Impl->PlayState != ScenePlayState::Stopped && m_Impl->Runtime)
        {
            m_Impl->InitializeVfx();
            m_Impl->SynchronizeVfx(0.0F);
        }
    }

    std::vector<std::byte> SceneRuntimeSession::CaptureVfxCheckpoint() const
    {
        m_Impl->RequireOwner("CaptureVfxCheckpoint");
        if (!m_Impl->VfxWorldService)
            throw std::logic_error("Scene VFX checkpoint state is unavailable.");
        return m_Impl->VfxWorldService->CaptureCheckpoint();
    }

    void SceneRuntimeSession::RestoreVfxCheckpoint(const std::span<const std::byte> checkpoint)
    {
        m_Impl->RequireOwner("RestoreVfxCheckpoint");
        if (!m_Impl->VfxWorldService)
            throw std::logic_error("Scene VFX checkpoint state is unavailable.");
        m_Impl->VfxWorldService->RestoreCheckpoint(checkpoint);
    }

    bool SceneRuntimeSession::PlayVfx(const EntityId entityId, const AssetId effect, const bool restart)
    {
        if (!m_Impl->Runtime || !m_Impl->VfxWorldService || !effect)
            return false;
        auto entity = m_Impl->Runtime->FindEntity(entityId);
        if (!entity)
            return false;
        auto emitter = entity.GetComponent<VfxEmitterComponent>();
        if (!emitter)
            emitter = entity.AddComponent<VfxEmitterComponent>();
        if (!emitter)
            return false;
        if (restart)
        {
            const auto state = m_Impl->VfxEmitters.find(entityId);
            if (state != m_Impl->VfxEmitters.end())
            {
                if (state->second.Handle)
                    m_Impl->VfxWorldService->Stop(state->second.Handle);
                m_Impl->VfxEmitters.erase(state);
            }
        }
        emitter->SetEffect(effect);
        emitter->SetPlayOnAwake(true);
        emitter->SetSimulationSpeed(1.0F);
        emitter->SetEnabled(true);
        return true;
    }

    bool SceneRuntimeSession::StopVfx(const EntityId entityId)
    {
        if (!m_Impl->Runtime || !m_Impl->VfxWorldService)
            return false;
        const auto entity = m_Impl->Runtime->FindEntity(entityId);
        const auto emitter = entity ? entity.GetComponent<VfxEmitterComponent>() : Ref<VfxEmitterComponent>{};
        if (!emitter)
            return false;
        emitter->SetPlayOnAwake(false);
        if (const auto state = m_Impl->VfxEmitters.find(entityId); state != m_Impl->VfxEmitters.end())
        {
            if (state->second.Handle)
                m_Impl->VfxWorldService->Stop(state->second.Handle);
            m_Impl->VfxEmitters.erase(state);
        }
        return true;
    }

    bool SceneRuntimeSession::PauseVfx(const EntityId entityId, const bool paused)
    {
        if (!m_Impl->Runtime)
            return false;
        const auto entity = m_Impl->Runtime->FindEntity(entityId);
        const auto emitter = entity ? entity.GetComponent<VfxEmitterComponent>() : Ref<VfxEmitterComponent>{};
        if (!emitter)
            return false;
        emitter->SetSimulationSpeed(paused ? 0.0F : 1.0F);
        return true;
    }

    bool SceneRuntimeSession::IsVfxAlive(const EntityId entityId) const noexcept
    {
        if (!m_Impl->VfxWorldService)
            return false;
        const auto state = m_Impl->VfxEmitters.find(entityId);
        return state != m_Impl->VfxEmitters.end() && state->second.Handle &&
               m_Impl->VfxWorldService->IsAlive(state->second.Handle);
    }

    bool SceneRuntimeSession::SendVfxEvent(const EntityId entityId, const std::string_view eventName,
                                           const std::uint32_t spawnCount)
    {
        m_Impl->RequireOwner("SendVfxEvent");
        if (!m_Impl->VfxWorldService)
            return false;
        const auto state = m_Impl->VfxEmitters.find(entityId);
        return state != m_Impl->VfxEmitters.end() && state->second.Handle &&
               m_Impl->VfxWorldService->SendEvent(state->second.Handle, eventName, spawnCount);
    }

    bool SceneRuntimeSession::SetVfxParameter(const EntityId entityId, const VfxParameterOverride& value)
    {
        m_Impl->RequireOwner("SetVfxParameter");
        if (!m_Impl->Runtime || !m_Impl->VfxWorldService || !value.Parameter)
            return false;

        const auto entity = m_Impl->Runtime->FindEntity(entityId);
        const auto emitter = entity ? entity.GetComponent<VfxEmitterComponent>() : Ref<VfxEmitterComponent>{};
        const auto state = m_Impl->VfxEmitters.find(entityId);
        if (!emitter || state == m_Impl->VfxEmitters.end() || state->second.Effect != emitter->Effect() ||
            !state->second.Handle || !m_Impl->VfxWorldService->IsAlive(state->second.Handle))
        {
            return false;
        }

        const auto effect = state->second.EffectHandle.TryGetLoaded();
        if (!effect)
            return false;
        const auto parameter =
            std::ranges::find(effect->Definition().Blackboard, value.Parameter, &VfxBlackboardParameter::Id);
        if (parameter == effect->Definition().Blackboard.end() || !parameter->Exposed ||
            !VfxOverrideMatches(parameter->Type, value.Value))
        {
            return false;
        }

        const auto authored = emitter->ParameterOverrides();
        std::vector<VfxParameterOverride> componentCandidate(authored.begin(), authored.end());
        const auto existing =
            std::ranges::lower_bound(componentCandidate, value.Parameter, {}, &VfxParameterOverride::Parameter);
        if (existing == componentCandidate.end() || existing->Parameter != value.Parameter)
        {
            if (componentCandidate.size() >= 1024)
                return false;
            componentCandidate.insert(existing, value);
        }
        else
        {
            *existing = value;
        }

        auto liveCandidate = CompatibleVfxOverrides(effect->Definition(), componentCandidate);
        if (std::ranges::find(liveCandidate, value.Parameter, &VfxParameterOverride::Parameter) == liveCandidate.end())
            return false;
        auto trackedCandidate = liveCandidate;
        try
        {
            m_Impl->VfxWorldService->SetParameterOverrides(state->second.Handle, liveCandidate);
        }
        catch (...)
        {
            return false;
        }

        emitter->CommitRuntimeParameterOverrides(std::move(componentCandidate));
        state->second.Overrides.swap(trackedCandidate);
        return true;
    }

    std::vector<ScenePhysicsQueryHit> SceneRuntimeSession::RayCast(const PhysicsRayQuery& query,
                                                                   const EntityId ignoredEntity) const
    {
        m_Impl->RequireOwner("RayCast");
        std::vector<ScenePhysicsQueryHit> result;
        if (!m_Impl->PhysicsWorldService)
            return result;
        for (const auto& hit : m_Impl->PhysicsWorldService->RayCast(query))
        {
            const auto entity = m_Impl->EntityForBody(hit.Body);
            if (entity && *entity != ignoredEntity)
                result.push_back({*entity, hit});
        }
        return result;
    }

    void SceneRuntimeSession::SetPresentationViewport(const float width, const float height,
                                                      const RuntimeUiInsets safeArea)
    {
        m_Impl->RequireOwner("SetPresentationViewport");
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0F || height <= 0.0F)
            throw std::invalid_argument("Scene presentation viewport dimensions must be finite and positive.");
        if (m_Impl->PresentationWidth == width && m_Impl->PresentationHeight == height &&
            m_Impl->SafeArea.Left == safeArea.Left && m_Impl->SafeArea.Top == safeArea.Top &&
            m_Impl->SafeArea.Right == safeArea.Right && m_Impl->SafeArea.Bottom == safeArea.Bottom)
        {
            return;
        }
        m_Impl->PresentationWidth = width;
        m_Impl->PresentationHeight = height;
        m_Impl->SafeArea = safeArea;
        if (m_Impl->Presentation && m_Impl->Runtime)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, width, height, true, safeArea);
    }

    void SceneRuntimeSession::Play()
    {
        m_Impl->RequireOwner("Play");
        if (m_Impl->PlayState != ScenePlayState::Stopped)
            return;
        const auto startupBegan = std::chrono::steady_clock::now();
        const auto elapsedMilliseconds = [](const auto began)
        { return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - began).count(); };
        m_Impl->Failure = {};
        m_Impl->ClearAnimation();
        const auto cloneBegan = std::chrono::steady_clock::now();
        m_Impl->Runtime = CreateRef<Scene>(m_Impl->Edit->Asset(), m_Impl->Edit->Snapshot(), m_Impl->Edit->Components());
        m_Impl->Runtime->MarkSaved();
        const float cloneMilliseconds = elapsedMilliseconds(cloneBegan);
        m_Impl->PlayState = ScenePlayState::Playing;
        const auto physicsBegan = std::chrono::steady_clock::now();
        m_Impl->Invoke("Physics initialization", [&] { m_Impl->InitializePhysics(); });
        const float physicsMilliseconds = elapsedMilliseconds(physicsBegan);
        const auto scriptsBegan = std::chrono::steady_clock::now();
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("Awake/OnEnable", [&] { m_Impl->Runtime->BeginPlay(); });
        const float scriptsMilliseconds = elapsedMilliseconds(scriptsBegan);
        const auto vfxBegan = std::chrono::steady_clock::now();
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("VFX initialization", [&] { m_Impl->InitializeVfx(); });
        const float vfxMilliseconds = elapsedMilliseconds(vfxBegan);
        const auto presentationBegan = std::chrono::steady_clock::now();
        if (m_Impl->Presentation)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth, m_Impl->PresentationHeight,
                                              true, m_Impl->SafeArea);
        const float presentationMilliseconds = elapsedMilliseconds(presentationBegan);
        const float totalMilliseconds = elapsedMilliseconds(startupBegan);
        if (totalMilliseconds >= 100.0F)
        {
            KEIRE_CORE_WARN("Play Mode startup {:.2f} ms (scene clone {:.2f}, physics {:.2f}, scripts {:.2f}, VFX "
                            "{:.2f}, presentation {:.2f}).",
                            totalMilliseconds, cloneMilliseconds, physicsMilliseconds, scriptsMilliseconds,
                            vfxMilliseconds, presentationMilliseconds);
        }
        else
        {
            KEIRE_CORE_INFO("Play Mode startup {:.2f} ms (scene clone {:.2f}, physics {:.2f}, scripts {:.2f}, VFX "
                            "{:.2f}, presentation {:.2f}).",
                            totalMilliseconds, cloneMilliseconds, physicsMilliseconds, scriptsMilliseconds,
                            vfxMilliseconds, presentationMilliseconds);
        }
    }

    void SceneRuntimeSession::Pause(const bool paused)
    {
        m_Impl->RequireOwner("Pause");
        if (m_Impl->PlayState == ScenePlayState::Playing && paused)
            m_Impl->PlayState = ScenePlayState::Paused;
        else if (m_Impl->PlayState == ScenePlayState::Paused && !paused)
            m_Impl->PlayState = ScenePlayState::Playing;
    }

    void SceneRuntimeSession::TogglePause() { Pause(m_Impl->PlayState != ScenePlayState::Paused); }

    bool SceneRuntimeSession::Step(const float fixedDeltaSeconds)
    {
        m_Impl->RequireOwner("Step");
        if (m_Impl->PlayState != ScenePlayState::Paused)
            return false;
        if (fixedDeltaSeconds <= 0.0F)
            throw std::invalid_argument("Scene step delta must be positive.");
        m_Impl->Invoke("FixedUpdate", [&] { m_Impl->Runtime->FixedUpdate(fixedDeltaSeconds); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("Physics", [&] { m_Impl->StepPhysics(fixedDeltaSeconds); });
        return m_Impl->PlayState != ScenePlayState::Faulted;
    }

    void SceneRuntimeSession::FixedUpdate(const float deltaSeconds)
    {
        m_Impl->RequireOwner("FixedUpdate");
        if (m_Impl->PlayState == ScenePlayState::Playing)
        {
            m_Impl->Invoke("FixedUpdate", [&] { m_Impl->Runtime->FixedUpdate(deltaSeconds); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("Physics", [&] { m_Impl->StepPhysics(deltaSeconds); });
        }
    }

    void SceneRuntimeSession::Update(const float deltaSeconds)
    {
        m_Impl->RequireOwner("Update");
        if (m_Impl->PlayState == ScenePlayState::Playing)
        {
            m_Impl->Invoke("Update", [&] { m_Impl->Runtime->Update(deltaSeconds); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("Animation", [&] { m_Impl->SynchronizeAnimation(deltaSeconds); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("LateUpdate", [&] { m_Impl->Runtime->LateUpdate(); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("VFX", [&] { m_Impl->SynchronizeVfx(deltaSeconds); });
            if (m_Impl->Presentation && m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth,
                                                  m_Impl->PresentationHeight, true, m_Impl->SafeArea);
        }
    }

    void SceneRuntimeSession::ReplaceRuntime(SceneDefinition definition)
    {
        m_Impl->RequireOwner("ReplaceRuntime");
        if (m_Impl->PlayState == ScenePlayState::Stopped || !m_Impl->Runtime)
            throw std::logic_error("SceneRuntimeSession::ReplaceRuntime requires an active Play session.");
        auto replacement = CreateRef<Scene>(m_Impl->Edit->Asset(), std::move(definition), m_Impl->Edit->Components());
        replacement->MarkSaved();
        m_Impl->ClearAnimation();
        m_Impl->ClearVfx();
        m_Impl->ClearPhysics();
        m_Impl->Runtime->EndPlay();
        m_Impl->Runtime->Close();
        if (m_Impl->Presentation)
            m_Impl->Presentation->Clear();
        m_Impl->Runtime = std::move(replacement);
        m_Impl->Failure = {};
        m_Impl->Invoke("Physics initialization", [&] { m_Impl->InitializePhysics(); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("Awake/OnEnable", [&] { m_Impl->Runtime->BeginPlay(); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("VFX initialization", [&] { m_Impl->InitializeVfx(); });
        if (m_Impl->Presentation)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth, m_Impl->PresentationHeight,
                                              true, m_Impl->SafeArea);
    }

    void SceneRuntimeSession::Stop() noexcept
    {
        if (!m_Impl || m_Impl->PlayState == ScenePlayState::Stopped)
            return;
        if (m_Impl->Runtime)
        {
            m_Impl->ClearAnimation();
            m_Impl->ClearVfx();
            m_Impl->ClearPhysics();
            if (m_Impl->Presentation)
                m_Impl->Presentation->Clear();
            m_Impl->Runtime->EndPlay();
            m_Impl->Runtime->Close();
            m_Impl->Runtime.Reset();
        }
        m_Impl->PlayState = ScenePlayState::Stopped;
        m_Impl->Failure = {};
    }
} // namespace Keire
