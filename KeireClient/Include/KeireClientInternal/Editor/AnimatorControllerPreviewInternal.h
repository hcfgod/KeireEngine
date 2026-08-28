#pragma once

#include "KeireClientInternal/Editor/AnimatorControllerPanelModelInternal.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace KeireEditor
{
    struct AnimatorControllerPanel::PreviewState final
    {
        struct RetargetedClip final
        {
            Keire::AssetId SourceSkeleton;
            Keire::AssetHandle<Keire::SkeletonAsset> SourceSkeletonHandle;
            Keire::Ref<const Keire::AnimationClipAsset> Clip;
            std::uint64_t ClipRevision = 0;
            std::uint64_t SourceSkeletonRevision = 0;
            std::uint64_t TargetSkeletonRevision = 0;
        };

        bool Active = false;
        bool Playing = false;
        bool RestartRequested = true;
        std::optional<float> SeekRequested;
        float NormalizedTime = 0.0F;
        std::chrono::steady_clock::time_point LastTick;
        Keire::Ref<Keire::Scene> Scene;
        Keire::EntityId Entity;
        Keire::AssetId Graph;
        Keire::AssetId Skeleton;
        Keire::AssetId Skin;
        Keire::AssetHandle<Keire::SkeletonAsset> SkeletonHandle;
        Keire::AssetHandle<Keire::SkinnedMeshAsset> SkinHandle;
        std::map<Keire::AssetId, Keire::AssetHandle<Keire::AnimationClipAsset>> Clips;
        std::map<Keire::AssetId, RetargetedClip> RetargetedClips;
        std::map<Keire::AssetId, Keire::AssetHandle<Keire::AvatarMaskAsset>> Masks;
        Keire::Ref<const Keire::AnimationGraphAsset> GraphAsset;
        std::unique_ptr<Keire::AnimatorInstance> Instance;
        std::uint64_t SkeletonRevision = 0;
        std::string Diagnostic;

        static Keire::RigDefinition BestRig(const Keire::SkeletonAsset& skeleton)
        {
            auto humanoid = Keire::InferRigDefinition(skeleton, Keire::RigProfileType::Humanoid);
            auto quadruped = Keire::InferRigDefinition(skeleton, Keire::RigProfileType::Quadruped);
            const auto semanticCount = [](const Keire::RigDefinition& rig)
            {
                return std::ranges::count_if(rig.Bones, [](const auto& bone)
                                             { return bone.Semantic != Keire::RigBoneSemantic::None; });
            };
            return semanticCount(quadruped) > semanticCount(humanoid) ? std::move(quadruped) : std::move(humanoid);
        }

        static std::vector<Keire::Matrix4> BuildPalette(const Keire::SkeletonAsset& skeleton,
                                                        const std::span<const Keire::BoneTransform> localPose)
        {
            if (localPose.size() != skeleton.Bones().size())
                throw std::runtime_error("Animator preview pose does not match its target skeleton.");
            std::vector<Keire::Matrix4> world(localPose.size());
            std::vector<Keire::Matrix4> palette(localPose.size());
            for (std::size_t index = 0; index < localPose.size(); ++index)
            {
                const auto& transform = localPose[index];
                const auto local =
                    Keire::Math::ComposeTransform(transform.Translation, transform.Rotation, transform.Scale);
                const auto parent = skeleton.Bones()[index].Parent;
                world[index] =
                    parent < 0 ? local : Keire::Math::Multiply(world[static_cast<std::size_t>(parent)], local);
                palette[index] = Keire::Math::Multiply(world[index], skeleton.Bones()[index].InverseBindPose);
            }
            return palette;
        }

        void ClearPose() noexcept
        {
            if (Scene && Scene->IsOpen() && Entity)
            {
                if (const auto entity = Scene->FindEntity(Entity); entity)
                    if (const auto animator = entity.GetComponent<Keire::AnimatorComponent>(); animator)
                        animator->ClearRuntimePose();
            }
        }

        void Invalidate() noexcept
        {
            Instance.reset();
            GraphAsset = {};
            Clips.clear();
            RetargetedClips.clear();
            Masks.clear();
            SkeletonRevision = 0;
            RestartRequested = true;
            NormalizedTime = 0.0F;
        }

        void Stop() noexcept
        {
            ClearPose();
            Active = false;
            Playing = false;
            SeekRequested.reset();
            Diagnostic.clear();
            Scene = {};
            Entity = {};
            Graph = {};
            Skeleton = {};
            Skin = {};
            SkeletonHandle = {};
            SkinHandle = {};
            Invalidate();
        }

        void Restart() noexcept
        {
            Active = true;
            Playing = true;
            RestartRequested = true;
            SeekRequested.reset();
            LastTick = std::chrono::steady_clock::now();
        }

        void Seek(const float normalizedTime) noexcept
        {
            Active = true;
            Playing = false;
            SeekRequested = std::clamp(normalizedTime, 0.0F, 1.0F);
        }

        [[nodiscard]] Keire::Ref<const Keire::AnimationClipAsset>
        ResolveClip(const Keire::AssetId id, const Keire::Ref<Keire::AssetSystem>& assets)
        {
            if (!id)
                return {};
            if (assets->TryGetType(id) != Keire::AnimationClipAsset::StaticType())
            {
                Clips.erase(id);
                RetargetedClips.erase(id);
                Diagnostic =
                    "Preview cannot load an animation clip because the graph references a missing or incompatible "
                    "asset. Reassign the state's Animation Clip.";
                return {};
            }
            auto [iterator, inserted] = Clips.try_emplace(id);
            if (inserted)
                iterator->second = assets->Load<Keire::AnimationClipAsset>(id, Keire::AssetPriority::High);
            auto clip = iterator->second.TryGetLoaded();
            if (!clip || clip->Skeleton() == Skeleton)
                return clip;

            auto& retargeted = RetargetedClips[id];
            if (retargeted.SourceSkeleton != clip->Skeleton())
            {
                retargeted = {};
                retargeted.SourceSkeleton = clip->Skeleton();
                if (assets->TryGetType(retargeted.SourceSkeleton) != Keire::SkeletonAsset::StaticType())
                {
                    Diagnostic =
                        "Preview cannot load the clip's source skeleton. Reimport the animation source or reassign "
                        "the state's Animation Clip.";
                    return {};
                }
                retargeted.SourceSkeletonHandle =
                    assets->Load<Keire::SkeletonAsset>(retargeted.SourceSkeleton, Keire::AssetPriority::High);
            }
            const auto sourceSkeleton = retargeted.SourceSkeletonHandle.TryGetLoaded();
            const auto targetSkeleton = SkeletonHandle.TryGetLoaded();
            if (!sourceSkeleton || !targetSkeleton)
                return {};

            const auto clipRevision = iterator->second.Revision();
            const auto sourceRevision = retargeted.SourceSkeletonHandle.Revision();
            const auto targetRevision = SkeletonHandle.Revision();
            if (!retargeted.Clip || retargeted.ClipRevision != clipRevision ||
                retargeted.SourceSkeletonRevision != sourceRevision ||
                retargeted.TargetSkeletonRevision != targetRevision)
            {
                try
                {
                    const auto sourceRig = BestRig(*sourceSkeleton);
                    const auto targetRig = BestRig(*targetSkeleton);
                    retargeted.Clip = Keire::RetargetAnimationClip(*sourceSkeleton, sourceRig, *clip, Skeleton,
                                                                   *targetSkeleton, targetRig);
                    retargeted.ClipRevision = clipRevision;
                    retargeted.SourceSkeletonRevision = sourceRevision;
                    retargeted.TargetSkeletonRevision = targetRevision;
                }
                catch (const std::exception& error)
                {
                    Diagnostic = "Preview clip is incompatible with the target skeleton: " + std::string(error.what());
                    retargeted.Clip = {};
                    return {};
                }
            }
            return retargeted.Clip;
        }

        [[nodiscard]] Keire::Ref<const Keire::AvatarMaskAsset> ResolveMask(const Keire::AssetId id,
                                                                           const Keire::Ref<Keire::AssetSystem>& assets)
        {
            if (!id)
                return {};
            if (assets->TryGetType(id) != Keire::AvatarMaskAsset::StaticType())
            {
                Masks.erase(id);
                Diagnostic =
                    "Preview cannot load an avatar mask because the graph references a missing or incompatible asset.";
                return {};
            }
            auto [iterator, inserted] = Masks.try_emplace(id);
            if (inserted)
                iterator->second = assets->Load<Keire::AvatarMaskAsset>(id, Keire::AssetPriority::High);
            return iterator->second.TryGetLoaded();
        }

        [[nodiscard]] bool DependenciesReady(const Keire::AnimationGraphAsset& graph,
                                             const Keire::Ref<Keire::AssetSystem>& assets)
        {
            Diagnostic.clear();
            bool ready = true;
            for (const auto& layer : graph.Definition().Layers)
            {
                if (layer.AvatarMask && !ResolveMask(layer.AvatarMask, assets))
                    ready = false;
                for (const auto& state : layer.States)
                {
                    const auto clip = state.Motion.Clip ? state.Motion.Clip : state.Clip;
                    if (clip && !ResolveClip(clip, assets))
                        ready = false;
                    for (const auto& child : state.Motion.Children)
                        if (child.Clip && !ResolveClip(child.Clip, assets))
                            ready = false;
                }
            }
            if (!ready && Diagnostic.empty())
                Diagnostic = "Preview is waiting for animation dependencies to load.";
            return ready;
        }

        [[nodiscard]] float CurrentClipDuration(const Keire::AnimationGraphDefinition& graph,
                                                const Keire::Ref<Keire::AssetSystem>& assets)
        {
            if (graph.Layers.empty())
                return 1.0F;
            const auto& layer = graph.Layers.front();
            std::string_view stateId = layer.EntryStateId;
            if (Instance)
            {
                if (const auto snapshot = Instance->DebugSnapshot(); snapshot && !snapshot->Layers.empty())
                    stateId = snapshot->Layers.front().StateId;
            }
            auto state = std::ranges::find(layer.States, stateId, &Keire::AnimationStateDefinition::Id);
            if (state == layer.States.end() && !layer.States.empty())
                state = layer.States.begin();
            if (state == layer.States.end())
                return 1.0F;
            auto clipId = state->Motion.Clip ? state->Motion.Clip : state->Clip;
            if (!clipId && !state->Motion.Children.empty())
                clipId = state->Motion.Children.front().Clip;
            if (const auto clip = ResolveClip(clipId, assets); clip)
                return std::max(clip->Duration(), 0.001F);
            return 1.0F;
        }

        void Synchronize(SceneDocument& sceneDocument, const AnimatorControllerDocument& controller,
                         const Keire::Ref<Keire::AssetSystem>& assets)
        {
            const auto now = std::chrono::steady_clock::now();
            const float deltaSeconds =
                LastTick.time_since_epoch().count() == 0
                    ? 0.0F
                    : std::clamp(std::chrono::duration<float>(now - LastTick).count(), 0.0F, 0.1F);
            LastTick = now;

            const auto scene = sceneDocument.EditingScene();
            const auto selection = sceneDocument.Selection();
            if (!scene || !selection)
            {
                ClearPose();
                Diagnostic = "Select a scene entity with an Animator to preview this controller.";
                return;
            }
            const Keire::EntityId entityId(selection);
            if (Scene != scene || Entity != entityId)
            {
                ClearPose();
                Scene = scene;
                Entity = entityId;
                Invalidate();
            }
            const auto entity = scene->FindEntity(entityId);
            const auto animator =
                entity ? entity.GetComponent<Keire::AnimatorComponent>() : Keire::Ref<Keire::AnimatorComponent>{};
            if (!animator)
            {
                Diagnostic = "The selected entity does not have an Animator component.";
                return;
            }
            if (animator->Graph() != controller.Asset())
            {
                Diagnostic = "Assign this controller to the selected Animator before previewing it.";
                return;
            }
            if (!animator->SkinnedMesh())
            {
                Diagnostic = "Assign a skinned mesh to the selected Animator before previewing it.";
                return;
            }
            if (!assets)
            {
                Diagnostic = "The asset system is unavailable.";
                return;
            }

            if (Skin != animator->SkinnedMesh())
            {
                Invalidate();
                Skin = animator->SkinnedMesh();
                SkinHandle = assets->Load<Keire::SkinnedMeshAsset>(Skin, Keire::AssetPriority::High);
            }
            const auto skin = SkinHandle.TryGetLoaded();
            if (!skin)
            {
                Diagnostic = "Preview is waiting for the skinned mesh to load.";
                return;
            }

            const auto targetSkeleton = skin->Skeleton();
            if (!targetSkeleton)
            {
                Diagnostic = "The assigned skinned mesh does not reference a skeleton.";
                return;
            }
            if (animator->Skeleton() != targetSkeleton)
                animator->SetSkeleton(targetSkeleton);
            if (Graph != controller.Asset() || Skeleton != targetSkeleton)
            {
                Invalidate();
                Graph = controller.Asset();
                Skeleton = targetSkeleton;
                SkeletonHandle = assets->Load<Keire::SkeletonAsset>(Skeleton, Keire::AssetPriority::High);
            }

            const auto skeleton = SkeletonHandle.TryGetLoaded();
            if (!skeleton)
            {
                Diagnostic = "Preview is waiting for the target skeleton to load.";
                return;
            }
            if (Instance && SkeletonRevision != SkeletonHandle.Revision())
                Invalidate();
            if (!GraphAsset)
                GraphAsset = Keire::CreateRef<Keire::AnimationGraphAsset>(controller.Definition());
            if (!DependenciesReady(*GraphAsset, assets))
                return;
            if (!Instance)
            {
                Instance = std::make_unique<Keire::AnimatorInstance>(
                    skeleton, GraphAsset, [this, assets](const Keire::AssetId id) { return ResolveClip(id, assets); },
                    [this, assets](const Keire::AssetId id) { return ResolveMask(id, assets); });
                SkeletonRevision = SkeletonHandle.Revision();
                RestartRequested = true;
            }

            Keire::AnimatorSample sample;
            bool sampled = false;
            if (RestartRequested)
            {
                Instance->Reset();
                sample = Instance->Update(0.0F);
                RestartRequested = false;
                NormalizedTime = 0.0F;
                sampled = true;
            }
            if (SeekRequested)
            {
                Instance->Reset();
                const float duration = CurrentClipDuration(controller.Definition(), assets);
                sample = Instance->Update(duration * std::min(*SeekRequested, 0.999999F));
                SeekRequested.reset();
                sampled = true;
            }
            else if (Playing)
            {
                sample = Instance->Update(deltaSeconds * std::max(animator->Speed(), 0.0F));
                sampled = true;
            }
            if (!sampled)
                return;

            const auto palette = BuildPalette(*skeleton, sample.LocalPose);
            animator->SetRuntimePose(sample.State, sample.NormalizedTime, Instance->Playing(), palette);
            animator->SetRuntimeDebugSnapshot(Instance->DebugSnapshot());
            animator->SetRuntimeDiagnostic({});
            NormalizedTime = AnimatorControllerPanelInternal::TimelineFraction(sample.NormalizedTime);
            Diagnostic.clear();
        }
    };
} // namespace KeireEditor
