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
#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Vfx/VfxSystem.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Keire
{
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
            const auto clip = iterator->second.TryGetLoaded();
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
            for (const auto entity : Runtime->Query<AnimatorComponent>())
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
                auto sample = state->Instance->Update(deltaSeconds * speed);
                if (const auto ikDiagnostic =
                        ApplyIkGoals(entity, *skeleton, *animator, sample.LocalPose, state->BoneIndices);
                    !ikDiagnostic.empty())
                {
                    animator->SetRuntimeDiagnostic(ikDiagnostic);
                }
                const auto palette = SkinPalette(*skeleton, sample.LocalPose);
                animator->SetRuntimePose(sample.State, palette);
                animator->SetRuntimeDebugSnapshot(state->Instance->DebugSnapshot());
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

        void InitializeVfx()
        {
            ClearVfx();
            VfxWorldSpecification specification;
            specification.Backend = Assets ? VfxBackend::Gpu : VfxBackend::Cpu;
            specification.MaximumParticles = Assets ? 1'000'000U : VfxRenderSnapshot::MaximumParticles;
            specification.CollisionQuery = [this](const Vector3 start, const Vector3 end)
            { return QueryVfxCollision(start, end); };
            VfxWorldService = CreateRef<VfxWorld>(std::move(specification));
        }

        void SynchronizeVfx(const float deltaSeconds)
        {
            if (!VfxWorldService || !Runtime)
                return;

            std::set<EntityId> seen;
            for (const auto entity : Runtime->Query<VfxEmitterComponent>())
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
                Vector3 position;
                Quaternion rotation;
                Vector3 scale;
                if (!Math::DecomposeTransform(transform->WorldMatrix(), position, rotation, scale))
                    throw std::runtime_error("VFX Emitter Transform cannot be decomposed.");
                const auto revision = state.EffectHandle.Revision();
                if (!state.Handle || !VfxWorldService->IsAlive(state.Handle))
                {
                    state.Handle =
                        VfxWorldService->Activate({effect, revision, position, rotation, emitter->SeedOffset()});
                    state.Revision = revision;
                }
                else
                {
                    if (revision != state.Revision)
                    {
                        (void)VfxWorldService->Reload(state.Handle, effect, revision);
                        state.Revision = revision;
                    }
                    VfxWorldService->SetTransform(state.Handle, position, rotation);
                }
                if (state.Handle)
                    VfxWorldService->SetSimulationSpeed(state.Handle, emitter->SimulationSpeed());
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
            definition.Layer = useCharacter ? character->Layer() : collider->Layer();
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
            for (const auto entity : Runtime->Query<ColliderComponent>())
                candidates.emplace(entity.Id());
            for (const auto entity : Runtime->Query<CharacterControllerComponent>())
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
            for (const auto entity : Runtime->Query<CharacterControllerComponent>())
            {
                const auto character = entity.GetComponent<CharacterControllerComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                if (!character || !character->Enabled() || !entity.ActiveInHierarchy() || !transform)
                    continue;
                const auto displacement = character->ConsumeDesiredMovement();
                auto& state = PhysicsBodies[entity.Id()];
                state.CharacterVelocity = deltaSeconds > 0.0F
                                              ? Vector3{displacement.X / deltaSeconds, displacement.Y / deltaSeconds,
                                                        displacement.Z / deltaSeconds}
                                              : Vector3{};
                if (displacement != Vector3{})
                    MoveTransformInWorld(entity, *transform, displacement);
            }
        }

        void UpdateCharacterGrounding()
        {
            constexpr float Pi = 3.14159265358979323846F;
            for (const auto entity : Runtime->Query<CharacterControllerComponent>())
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
                const auto distance = character->Height() * std::abs(worldScale.Y) * 0.5F + character->StepHeight() +
                                      character->SkinWidth();
                const PhysicsRayQuery query{worldPosition, {0.0F, -1.0F, 0.0F}, distance, character->Mask(), false};
                bool grounded = false;
                Vector3 normal{0.0F, 1.0F, 0.0F};
                const auto minimumNormal = std::cos(character->MaximumSlopeDegrees() * Pi / 180.0F);
                for (const auto& hit : PhysicsWorldService->RayCast(query))
                {
                    if (hit.Body == state->second.Body)
                        continue;
                    if (hit.Normal.Y >= minimumNormal)
                    {
                        grounded = true;
                        normal = hit.Normal;
                    }
                    break;
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
        std::thread::id OwnerThread;
        ScenePlayState PlayState = ScenePlayState::Stopped;
        SceneRuntimeDiagnostic Failure;
        Ref<ScenePresentationRuntime> Presentation;
        std::map<EntityId, std::unique_ptr<AnimationRuntimeState>> Animators;
        std::map<EntityId, PhysicsRuntimeState> PhysicsBodies;
        std::map<EntityId, VfxRuntimeState> VfxEmitters;
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
        m_Impl->Failure = {};
        m_Impl->ClearAnimation();
        m_Impl->Runtime = CreateRef<Scene>(m_Impl->Edit->Asset(), m_Impl->Edit->Snapshot(), m_Impl->Edit->Components());
        m_Impl->Runtime->MarkSaved();
        m_Impl->PlayState = ScenePlayState::Playing;
        m_Impl->Invoke("Physics initialization", [&] { m_Impl->InitializePhysics(); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("Awake/OnEnable", [&] { m_Impl->Runtime->BeginPlay(); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("VFX initialization", [&] { m_Impl->InitializeVfx(); });
        if (m_Impl->Presentation)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth, m_Impl->PresentationHeight,
                                              true, m_Impl->SafeArea);
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
