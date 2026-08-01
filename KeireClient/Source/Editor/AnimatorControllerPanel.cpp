#include "KeireClient/Editor/AnimatorControllerPanel.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        constexpr std::array<std::string_view, 4> ParameterTypeNames{"Float", "Integer", "Boolean", "Trigger"};
        constexpr std::array<std::string_view, 3> MotionTypeNames{"Clip", "1D Blend Tree", "2D Blend Tree"};
        constexpr std::array<std::string_view, 2> LayerModeNames{"Override", "Additive"};
        constexpr std::array<std::string_view, 4> ComparisonNames{"Greater", "Less", "Equal", "Not Equal"};

        template <typename Range, typename Projection>
        [[nodiscard]] std::string UniqueName(const Range& values, std::string base, Projection projection)
        {
            std::string candidate = base;
            for (std::size_t copy = 2; std::ranges::any_of(values, [&](const auto& value)
                                                           { return std::invoke(projection, value) == candidate; });
                 ++copy)
            {
                candidate = base + " " + std::to_string(copy);
            }
            return candidate;
        }

        [[nodiscard]] Keire::AnimationLayerDefinition* FindLayer(Keire::AnimationGraphDefinition& graph,
                                                                 const std::string_view id)
        {
            const auto found = std::ranges::find(graph.Layers, id, &Keire::AnimationLayerDefinition::Id);
            return found == graph.Layers.end() ? nullptr : &*found;
        }

        [[nodiscard]] Keire::AnimationStateDefinition* FindState(Keire::AnimationLayerDefinition& layer,
                                                                 const std::string_view id)
        {
            const auto found = std::ranges::find(layer.States, id, &Keire::AnimationStateDefinition::Id);
            return found == layer.States.end() ? nullptr : &*found;
        }

        [[nodiscard]] Keire::AnimationParameterDefinition* FindParameter(Keire::AnimationGraphDefinition& graph,
                                                                         const std::string_view id)
        {
            const auto found =
                std::ranges::find(graph.ParameterDefinitions, id, &Keire::AnimationParameterDefinition::Id);
            return found == graph.ParameterDefinitions.end() ? nullptr : &*found;
        }

        [[nodiscard]] Keire::AnimationTransition* FindTransition(Keire::AnimationStateDefinition& state,
                                                                 const std::string_view id)
        {
            const auto found = std::ranges::find(state.Transitions, id, &Keire::AnimationTransition::Id);
            return found == state.Transitions.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool DrawEnumCombo(Keire::UiFrame& ui, const std::string_view label, std::uint8_t& value,
                                         const std::span<const std::string_view> names)
        {
            const auto index = std::min<std::size_t>(value, names.size() - 1);
            bool changed = false;
            if (auto combo = ui.BeginCombo(label, names[index]); combo)
            {
                for (std::size_t candidate = 0; candidate < names.size(); ++candidate)
                {
                    if (ui.Selectable(names[candidate], candidate == index))
                    {
                        value = static_cast<std::uint8_t>(candidate);
                        changed = true;
                    }
                }
            }
            return changed;
        }

        [[nodiscard]] bool EditAssetReference(Keire::UiFrame& ui, const std::string_view label, Keire::AssetId& asset,
                                              const Keire::AssetTypeId expectedType,
                                              const Keire::Ref<Keire::AssetDatabase>& database, std::string& message)
        {
            bool changed = false;
            std::string value = asset ? asset.ToString() : std::string{};
            if (ui.InputText(label, value))
            {
                try
                {
                    const auto replacement = value.empty() ? Keire::AssetId{} : Keire::AssetId::Parse(value);
                    if (replacement && database)
                    {
                        const auto record = database->Find(replacement);
                        if (!record || record->Type != expectedType)
                            throw std::invalid_argument("The dropped or entered asset has the wrong type.");
                    }
                    asset = replacement;
                    changed = true;
                    message.clear();
                }
                catch (const std::exception& error)
                {
                    message = error.what();
                }
            }

            const auto field = ui.LastItemRect();
            if (auto target = ui.BeginDragTarget(field, std::string(label) + "Drop"); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
                {
                    try
                    {
                        const auto assets = AssetBrowserPanel::DecodeDragPayload(payload);
                        const auto found = std::ranges::find_if(assets,
                                                                [&](const Keire::AssetId candidate)
                                                                {
                                                                    const auto record = database
                                                                                            ? database->Find(candidate)
                                                                                            : std::nullopt;
                                                                    return record && record->Type == expectedType;
                                                                });
                        if (found == assets.end())
                            throw std::invalid_argument("Drop an asset of the required type.");
                        asset = *found;
                        changed = true;
                        message.clear();
                    }
                    catch (const std::exception& error)
                    {
                        message = error.what();
                    }
                }
            }
            return changed;
        }

        [[nodiscard]] Keire::Vector2 DisplayPosition(const Keire::AnimationStateDefinition& state,
                                                     const std::size_t index) noexcept
        {
            if (std::abs(state.EditorPosition.X) > 0.001F || std::abs(state.EditorPosition.Y) > 0.001F || index == 0)
                return state.EditorPosition;
            return {static_cast<float>(index % 3) * 190.0F, static_cast<float>(index / 3) * 104.0F};
        }

        void RemoveParameterReferences(Keire::AnimationGraphDefinition& graph, const std::string_view parameter)
        {
            for (auto& layer : graph.Layers)
            {
                for (auto& state : layer.States)
                {
                    for (auto& transition : state.Transitions)
                    {
                        std::erase_if(transition.Conditions,
                                      [&](const auto& condition) { return condition.ParameterId == parameter; });
                    }
                    if (state.Motion.ParameterX == parameter)
                        state.Motion.ParameterX.clear();
                    if (state.Motion.ParameterY == parameter)
                        state.Motion.ParameterY.clear();
                }
            }
        }

        void RemoveStateReferences(Keire::AnimationLayerDefinition& layer, const std::string_view state)
        {
            for (auto& source : layer.States)
                std::erase_if(source.Transitions,
                              [&](const auto& transition) { return transition.DestinationId == state; });
        }

        [[nodiscard]] float TimelineFraction(const float normalizedTime) noexcept
        {
            if (!std::isfinite(normalizedTime) || normalizedTime <= 0.0F)
                return 0.0F;
            return std::fmod(normalizedTime, 1.0F);
        }

        [[nodiscard]] StableNodeId AnimatorCanvasId(const std::string_view value, const std::uint64_t salt) noexcept
        {
            std::uint64_t hash = 1469598103934665603ULL ^ salt;
            for (const auto character : value)
            {
                hash ^= static_cast<std::uint8_t>(character);
                hash *= 1099511628211ULL;
            }
            return hash == 0 ? salt | 1ULL : hash;
        }
    } // namespace

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
            const auto clip = iterator->second.TryGetLoaded();
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
            NormalizedTime = TimelineFraction(sample.NormalizedTime);
            Diagnostic.clear();
        }
    };

    AnimatorControllerPanel::AnimatorControllerPanel(IAnimatorControllerPanelController& controller) noexcept
        : m_Controller(controller), m_Preview(std::make_unique<PreviewState>())
    {
    }

    AnimatorControllerPanel::~AnimatorControllerPanel() { m_Preview->Stop(); }

    void AnimatorControllerPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.animator-controller", "Animator Controller", false});
    }

    void AnimatorControllerPanel::ResetTransientState() noexcept
    {
        m_Preview->Stop();
        m_GraphCanvas.CancelInteractions();
        m_GraphCanvas.Select(std::nullopt);
        m_GraphCanvas.SelectConnection(std::nullopt);
        m_GraphContext.reset();
        m_SelectedTransition.clear();
        m_GraphLayer.clear();
        m_Message.clear();
        m_FocusGraph = true;
    }

    void AnimatorControllerPanel::Draw(Keire::UiFrame& ui)
    {
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
        {
            m_Preview->Stop();
            return;
        }

        auto& document = m_Controller.AnimatorControllerState();
        const auto& theme = m_Controller.AnimatorControllerTheme();
        const auto database = m_Controller.AnimatorControllerDatabase();
        const auto assets = m_Controller.AnimatorControllerAssets();
        auto& sceneDocument = m_Controller.AnimatorControllerSceneDocument();
        if (ui.WindowFocused())
            m_Controller.ActivateAnimatorControllerHistory();
        if (!document.Asset())
        {
            ui.TextColored(theme.Accent, "ANIMATOR CONTROLLER");
            ui.Separator();
            ui.Text("No Animator Controller is open.");
            ui.TextColored(theme.MutedText, "Create or double-click a .keireanimgraph asset in the Project panel.");
            return;
        }

        ui.TextColored(theme.Accent, "ANIMATOR CONTROLLER");
        ui.SameLine();
        ui.Text(document.SourcePath().filename().string() + (document.Dirty() ? " *" : ""));
        ui.Separator();
        if (ui.Button("Save"))
        {
            try
            {
                m_Controller.SaveAnimatorControllerDocument();
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAnimatorControllerError(m_Message);
            }
        }
        ui.SameLine();
        if (ui.Button("Revert"))
        {
            try
            {
                m_Preview->Invalidate();
                m_Controller.ReloadAnimatorControllerDocument(document.Asset());
                return;
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAnimatorControllerError(m_Message);
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.UndoContext() || !document.UndoContext()->CanUndo()); disabled)
        {
            if (ui.Button("Undo"))
            {
                m_Preview->Invalidate();
                m_Controller.UndoAnimatorControllerEdit();
                return;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.UndoContext() || !document.UndoContext()->CanRedo()); disabled)
        {
            if (ui.Button("Redo"))
            {
                m_Preview->Invalidate();
                m_Controller.RedoAnimatorControllerEdit();
                return;
            }
        }
        ui.SameLine();
        if (ui.Button("Validate"))
        {
            try
            {
                Keire::ValidateAnimationGraph(document.Definition());
                m_Message = "Controller validation passed.";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAnimatorControllerError(m_Message);
            }
        }
        if (!m_Message.empty())
            ui.TextColored(theme.MutedText, m_Message);
        ui.Separator();

        const bool playMode =
            sceneDocument.PlaySession() && sceneDocument.PlaySession()->State() != Keire::ScenePlayState::Stopped;
        if (playMode)
            m_Preview->Stop();
        else if (m_Preview->Active)
        {
            try
            {
                m_Preview->Synchronize(sceneDocument, document, assets);
            }
            catch (const std::exception& error)
            {
                m_Preview->Playing = false;
                m_Preview->Diagnostic = error.what();
            }
        }

        const auto playbackScene = sceneDocument.ActiveScene();
        const auto playbackEntity = playbackScene && sceneDocument.Selection()
                                        ? playbackScene->FindEntity(Keire::EntityId(sceneDocument.Selection()))
                                        : Keire::Entity{};
        const auto playbackAnimator = playbackEntity ? playbackEntity.GetComponent<Keire::AnimatorComponent>()
                                                     : Keire::Ref<Keire::AnimatorComponent>{};
        const bool controllerMatches = playbackAnimator && playbackAnimator->Graph() == document.Asset();
        const auto playbackSnapshot = controllerMatches ? playbackAnimator->RuntimeDebugSnapshot()
                                                        : std::shared_ptr<const Keire::AnimatorDebugSnapshot>{};
        std::string playbackDiagnostic;
        if (!playbackAnimator)
            playbackDiagnostic = "Select an entity with an Animator component.";
        else if (!controllerMatches)
            playbackDiagnostic = "The selected Animator uses a different controller.";
        else if (!playbackAnimator->RuntimeDiagnostic().empty())
            playbackDiagnostic = playbackAnimator->RuntimeDiagnostic();
        else if (!playMode)
            playbackDiagnostic = m_Preview->Diagnostic;

        ui.TextColored(theme.Accent, playMode ? "LIVE PLAYBACK" : "EDIT MODE PREVIEW");
        if (!playMode)
        {
            const bool canPreview = controllerMatches && playbackAnimator->SkinnedMesh() && assets;
            if (auto disabled = ui.BeginDisabled(!canPreview); disabled)
            {
                if (ui.Button(m_Preview->Playing ? "Pause" : "Preview"))
                {
                    if (!m_Preview->Active)
                        m_Preview->Restart();
                    else
                        m_Preview->Playing = !m_Preview->Playing;
                    m_Preview->LastTick = std::chrono::steady_clock::now();
                }
                ui.SameLine();
                if (ui.Button("Restart"))
                    m_Preview->Restart();
                ui.SameLine();
                if (ui.Button("Stop"))
                    m_Preview->Stop();
            }
        }

        const Keire::AnimatorLayerDebugState* primaryPlayback = nullptr;
        if (playbackSnapshot && !playbackSnapshot->Layers.empty())
            primaryPlayback = &playbackSnapshot->Layers.front();
        const float progress =
            primaryPlayback ? TimelineFraction(primaryPlayback->NormalizedTime) : m_Preview->NormalizedTime;
        const std::string stateName =
            primaryPlayback && !primaryPlayback->State.empty() ? primaryPlayback->State : "Waiting for first sample";
        const auto progressLabel =
            stateName + "  " + std::to_string(static_cast<int>(std::clamp(progress, 0.0F, 1.0F) * 100.0F)) + "%";
        ui.ProgressBar(progress, {0.0F, 18.0F}, progressLabel);
        if (!playMode && m_Preview->Active)
        {
            float timeline = progress;
            if (ui.SliderFloat("Timeline", timeline, 0.0F, 1.0F))
                m_Preview->Seek(timeline);
        }
        if (!playbackDiagnostic.empty())
            ui.TextColored(theme.MutedText, playbackDiagnostic);
        ui.Separator();

        auto graph = document.Definition();
        auto before = graph;
        std::string selectedParameter(document.SelectedParameter());
        std::string selectedLayer(document.SelectedLayer());
        std::string selectedState(document.SelectedState());
        std::string editName;
        bool changed = false;
        const auto markChanged = [&](const std::string_view name)
        {
            changed = true;
            if (editName.empty())
                editName = name;
        };

        const auto available = ui.ContentAvailable();
        const float leftWidth = 224.0F;
        const float inspectorWidth = 318.0F;
        const float graphWidth = std::max(340.0F, available.Width - leftWidth - inspectorWidth - 18.0F);
        if (auto navigation = ui.BeginChild("AnimatorNavigation", {leftWidth, 0.0F}, true); navigation)
        {
            ui.TextColored(theme.Accent, "PARAMETERS");
            std::string removeParameter;
            for (const auto& parameter : graph.ParameterDefinitions)
            {
                auto id = ui.PushId(parameter.Id);
                if (ui.Selectable(parameter.Name, selectedParameter == parameter.Id))
                {
                    selectedParameter = parameter.Id;
                    selectedLayer.clear();
                    selectedState.clear();
                    m_SelectedTransition.clear();
                }
            }
            if (ui.Button("+ Parameter"))
            {
                Keire::AnimationParameterDefinition parameter;
                parameter.Id = Keire::AssetId::Generate().ToString();
                parameter.Name =
                    UniqueName(graph.ParameterDefinitions, "New Parameter", &Keire::AnimationParameterDefinition::Name);
                selectedParameter = parameter.Id;
                selectedLayer.clear();
                selectedState.clear();
                graph.ParameterDefinitions.push_back(std::move(parameter));
                markChanged("Add Animator Parameter");
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(selectedParameter.empty()); disabled)
            {
                if (ui.Button("Delete##Parameter"))
                    removeParameter = selectedParameter;
            }
            if (!removeParameter.empty())
            {
                RemoveParameterReferences(graph, removeParameter);
                std::erase_if(graph.ParameterDefinitions,
                              [&](const auto& parameter) { return parameter.Id == removeParameter; });
                selectedParameter.clear();
                markChanged("Delete Animator Parameter");
            }

            ui.Separator();
            ui.TextColored(theme.Accent, "LAYERS");
            std::string removeLayer;
            for (const auto& layer : graph.Layers)
            {
                auto id = ui.PushId(layer.Id);
                if (ui.Selectable(layer.Name, selectedLayer == layer.Id && selectedState.empty()))
                {
                    selectedParameter.clear();
                    selectedLayer = layer.Id;
                    selectedState.clear();
                    m_SelectedTransition.clear();
                }
                if (selectedLayer == layer.Id)
                {
                    for (const auto& state : layer.States)
                    {
                        auto stateId = ui.PushId(state.Id);
                        if (ui.Selectable("   " + state.Name, selectedState == state.Id))
                        {
                            selectedParameter.clear();
                            selectedLayer = layer.Id;
                            selectedState = state.Id;
                            m_SelectedTransition.clear();
                        }
                    }
                }
            }
            if (ui.Button("+ Layer"))
            {
                Keire::AnimationLayerDefinition layer;
                layer.Id = Keire::AssetId::Generate().ToString();
                layer.Name = UniqueName(graph.Layers, "New Layer", &Keire::AnimationLayerDefinition::Name);
                selectedParameter.clear();
                selectedLayer = layer.Id;
                selectedState.clear();
                graph.Layers.push_back(std::move(layer));
                markChanged("Add Animator Layer");
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(selectedLayer.empty()); disabled)
            {
                if (ui.Button("Delete##Layer"))
                    removeLayer = selectedLayer;
            }
            if (!removeLayer.empty())
            {
                std::erase_if(graph.Layers, [&](const auto& layer) { return layer.Id == removeLayer; });
                selectedLayer.clear();
                selectedState.clear();
                m_SelectedTransition.clear();
                markChanged("Delete Animator Layer");
            }
        }
        ui.SameLine();

        if (auto stateMachine = ui.BeginChild("AnimatorStateMachine", {graphWidth, 0.0F}, true); stateMachine)
        {
            ui.TextColored(theme.Accent, "STATE MACHINE");
            ui.SameLine();
            auto* layer = FindLayer(graph, selectedLayer);
            if (!layer && !graph.Layers.empty())
            {
                layer = &graph.Layers.front();
                selectedLayer = layer->Id;
            }
            if (layer)
            {
                ui.TextColored(theme.MutedText, layer->Name);
                ui.SameLine();
                if (ui.Button("Auto Layout"))
                {
                    for (std::size_t index = 0; index < layer->States.size(); ++index)
                    {
                        layer->States[index].EditorPosition = {static_cast<float>(index % 3) * 190.0F,
                                                               static_cast<float>(index / 3) * 104.0F};
                    }
                    m_FocusGraph = true;
                    markChanged("Auto Layout Animator States");
                }
            }
            ui.Separator();

            std::vector<NodeGraphNode> graphNodes;
            std::vector<NodeGraphConnection> graphConnections;
            const Keire::AnimatorLayerDebugState* layerPlayback = nullptr;
            if (layer)
            {
                if (playbackSnapshot)
                {
                    const auto found =
                        std::ranges::find(playbackSnapshot->Layers, layer->Id, &Keire::AnimatorLayerDebugState::Id);
                    if (found != playbackSnapshot->Layers.end())
                        layerPlayback = &*found;
                }

                graphNodes.reserve(layer->States.size());
                for (std::size_t index = 0; index < layer->States.size(); ++index)
                {
                    const auto& state = layer->States[index];
                    const bool entry = state.Id == layer->EntryStateId;
                    const bool active = layerPlayback && state.Id == layerPlayback->StateId;
                    std::string subtitle;
                    if (active)
                    {
                        subtitle =
                            "PLAYING  " +
                            std::to_string(static_cast<int>(TimelineFraction(layerPlayback->NormalizedTime) * 100.0F)) +
                            "%";
                    }
                    else if (entry)
                    {
                        subtitle = "ENTRY  |  ";
                        subtitle += MotionTypeNames[static_cast<std::size_t>(state.Motion.Type)];
                    }
                    else
                    {
                        subtitle = MotionTypeNames[static_cast<std::size_t>(state.Motion.Type)];
                        subtitle += "  |  ";
                        subtitle += std::to_string(state.Transitions.size());
                        subtitle += state.Transitions.size() == 1 ? " TRANSITION" : " TRANSITIONS";
                    }

                    NodeGraphNode node{
                        .Id = AnimatorCanvasId(state.Id, 0x414e494d4e4f4445ULL),
                        .Label = state.Name,
                        .Position = DisplayPosition(state, index),
                        .Size = {214.0F, 86.0F},
                        .Color = active  ? Keire::UiColor{0.08F, 0.48F, 0.33F, 1.0F}
                                 : entry ? Keire::UiColor{0.10F, 0.40F, 0.30F, 1.0F}
                                         : Keire::UiColor{0.10F, 0.32F, 0.52F, 1.0F},
                        .Subtitle = std::move(subtitle),
                    };
                    node.Pins.push_back({.Id = AnimatorCanvasId(state.Id, 0x414e494d494e5054ULL),
                                         .Label = "Enter",
                                         .Direction = NodeGraphPinDirection::Input,
                                         .Type = 1,
                                         .Color = {0.24F, 0.78F, 1.0F, 1.0F}});
                    node.Pins.push_back({.Id = AnimatorCanvasId(state.Id, 0x414e494d4f555450ULL),
                                         .Label = "Transition",
                                         .Direction = NodeGraphPinDirection::Output,
                                         .Type = 1,
                                         .Color = {0.24F, 0.78F, 1.0F, 1.0F}});
                    graphNodes.push_back(std::move(node));
                }
                for (const auto& source : layer->States)
                {
                    for (const auto& transition : source.Transitions)
                    {
                        const auto destination = std::ranges::find(layer->States, transition.DestinationId,
                                                                   &Keire::AnimationStateDefinition::Id);
                        if (destination == layer->States.end())
                            continue;
                        std::string label = std::to_string(transition.Conditions.size());
                        label += transition.Conditions.size() == 1 ? " condition" : " conditions";
                        if (transition.HasExitTime)
                            label += "  |  exit";
                        graphConnections.push_back(
                            {.Id = AnimatorCanvasId(transition.Id, 0x414e494d4c494e4bULL),
                             .Source = AnimatorCanvasId(source.Id, 0x414e494d4e4f4445ULL),
                             .Target = AnimatorCanvasId(destination->Id, 0x414e494d4e4f4445ULL),
                             .Label = std::move(label),
                             .SourcePin = AnimatorCanvasId(source.Id, 0x414e494d4f555450ULL),
                             .TargetPin = AnimatorCanvasId(destination->Id, 0x414e494d494e5054ULL)});
                    }
                }
            }

            const auto findStateByCanvasId = [&](const StableNodeId id) -> Keire::AnimationStateDefinition*
            {
                if (!layer)
                    return nullptr;
                const auto found =
                    std::ranges::find_if(layer->States, [&](const auto& state)
                                         { return AnimatorCanvasId(state.Id, 0x414e494d4e4f4445ULL) == id; });
                return found == layer->States.end() ? nullptr : std::addressof(*found);
            };
            const auto findTransitionByCanvasId = [&](const StableNodeId id)
                -> std::optional<std::pair<Keire::AnimationStateDefinition*, Keire::AnimationTransition*>>
            {
                if (!layer)
                    return std::nullopt;
                for (auto& state : layer->States)
                {
                    const auto transition =
                        std::ranges::find_if(state.Transitions, [&](const auto& candidate)
                                             { return AnimatorCanvasId(candidate.Id, 0x414e494d4c494e4bULL) == id; });
                    if (transition != state.Transitions.end())
                        return std::pair{std::addressof(state), std::addressof(*transition)};
                }
                return std::nullopt;
            };
            const auto removeState = [&](const std::string_view stateId)
            {
                if (!layer)
                    return;
                RemoveStateReferences(*layer, stateId);
                std::erase_if(layer->States, [&](const auto& candidate) { return candidate.Id == stateId; });
                if (layer->EntryStateId == stateId)
                    layer->EntryStateId = layer->States.empty() ? std::string{} : layer->States.front().Id;
                selectedState.clear();
                m_SelectedTransition.clear();
                m_GraphCanvas.Select(std::nullopt);
                m_GraphCanvas.SelectConnection(std::nullopt);
                markChanged("Delete Animator State");
            };

            m_GraphCanvas.Select(selectedState.empty()
                                     ? std::nullopt
                                     : std::optional{AnimatorCanvasId(selectedState, 0x414e494d4e4f4445ULL)});
            m_GraphCanvas.SelectConnection(
                m_SelectedTransition.empty()
                    ? std::nullopt
                    : std::optional{AnimatorCanvasId(m_SelectedTransition, 0x414e494d4c494e4bULL)});
            if (m_GraphLayer != selectedLayer)
            {
                m_GraphLayer = selectedLayer;
                m_FocusGraph = true;
            }
            if (m_FocusGraph)
            {
                m_GraphCanvas.Focus(graphNodes, ui.ContentAvailable());
                m_FocusGraph = false;
            }
            NodeGraphCanvasOptions graphOptions{
                .Editable = layer != nullptr,
                .InteractiveConnections = layer != nullptr,
                .ValidateConnection =
                    [&](const NodeGraphConnectionRequest& request)
                {
                    const auto* source = findStateByCanvasId(request.SourceNode);
                    const auto* destination = findStateByCanvasId(request.TargetNode);
                    if (!source || !destination)
                    {
                        return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                             "A transition endpoint is unavailable."};
                    }
                    if (source == destination)
                    {
                        return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                             "Self-transitions are not supported."};
                    }
                    if (std::ranges::any_of(source->Transitions, [&](const auto& transition)
                                            { return transition.DestinationId == destination->Id; }))
                    {
                        return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::AcceptWithWarning,
                                                             "Adds another transition between these states."};
                    }
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Accept, {}};
                },
            };
            const auto graphResult =
                m_GraphCanvas.Draw(ui, "AnimatorStateGraphCanvas", graphNodes, graphConnections, graphOptions);
            const auto canvas = ui.LastItemRect();
            if (!layer)
            {
                ui.DrawOverlayText({canvas.Minimum.X + 24.0F, canvas.Minimum.Y + 24.0F}, theme.MutedText,
                                   "Create a layer, then drag animation clips here.");
            }
            if (graphResult.ActivatedNode)
            {
                if (const auto* state = findStateByCanvasId(*graphResult.ActivatedNode); state)
                {
                    selectedParameter.clear();
                    selectedState = state->Id;
                    m_SelectedTransition.clear();
                }
            }
            if (graphResult.ActivatedConnection)
            {
                if (const auto transition = findTransitionByCanvasId(*graphResult.ActivatedConnection); transition)
                {
                    selectedParameter.clear();
                    selectedState = transition->first->Id;
                    m_SelectedTransition = transition->second->Id;
                }
            }
            if (graphResult.BackgroundActivated)
            {
                selectedState.clear();
                m_SelectedTransition.clear();
            }
            if (graphResult.MoveCompletedNode)
            {
                auto state = findStateByCanvasId(*graphResult.MoveCompletedNode);
                const auto node = std::ranges::find(graphNodes, *graphResult.MoveCompletedNode, &NodeGraphNode::Id);
                if (state && node != graphNodes.end() && state->EditorPosition != node->Position)
                {
                    state->EditorPosition = node->Position;
                    markChanged("Move Animator State");
                }
            }
            if (graphResult.ConnectionRequested)
            {
                auto* source = findStateByCanvasId(graphResult.ConnectionRequested->SourceNode);
                auto* destination = findStateByCanvasId(graphResult.ConnectionRequested->TargetNode);
                if (source && destination && source != destination)
                {
                    Keire::AnimationTransition transition;
                    transition.Id = Keire::AssetId::Generate().ToString();
                    transition.DestinationId = destination->Id;
                    transition.Destination = destination->Name;
                    m_SelectedTransition = transition.Id;
                    selectedParameter.clear();
                    selectedState = source->Id;
                    source->Transitions.push_back(std::move(transition));
                    markChanged("Connect Animator States");
                }
            }
            if (graphResult.DeleteConnectionRequested)
            {
                if (const auto transition = findTransitionByCanvasId(*graphResult.DeleteConnectionRequested);
                    transition)
                {
                    const auto transitionId = transition->second->Id;
                    std::erase_if(transition->first->Transitions,
                                  [&](const auto& candidate) { return candidate.Id == transitionId; });
                    m_SelectedTransition.clear();
                    markChanged("Delete Animator Transition");
                }
            }
            if (graphResult.DeleteNodeRequested)
            {
                if (const auto* state = findStateByCanvasId(*graphResult.DeleteNodeRequested); state)
                    removeState(state->Id);
            }
            if (graphResult.ContextRequested)
            {
                m_GraphContext = graphResult.ContextRequested;
                ui.OpenPopup("AnimatorGraphContext");
            }

            if (auto popup = ui.BeginPopup("AnimatorGraphContext"); popup)
            {
                if (!m_GraphContext)
                {
                    ui.TextColored(theme.MutedText, "The graph item is no longer available.");
                }
                else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Background)
                {
                    ui.TextColored(theme.Accent, "STATE MACHINE");
                    ui.TextColored(theme.MutedText, "Drop animation clips to create states.");
                    ui.Separator();
                    if (ui.MenuItem("Frame All States"))
                        m_GraphCanvas.Focus(graphNodes, canvas.Size());
                }
                else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Node)
                {
                    if (auto* state = findStateByCanvasId(m_GraphContext->Node); state)
                    {
                        ui.TextColored(theme.Accent, state->Name);
                        ui.TextColored(theme.MutedText, MotionTypeNames[static_cast<std::size_t>(state->Motion.Type)]);
                        ui.Separator();
                        if (ui.MenuItem("Set As Entry", false, state->Id != layer->EntryStateId))
                        {
                            layer->EntryStateId = state->Id;
                            markChanged("Set Animator Entry State");
                        }
                        if (ui.MenuItem("Remove All Transitions", false, !state->Transitions.empty()))
                        {
                            state->Transitions.clear();
                            m_SelectedTransition.clear();
                            markChanged("Remove Animator State Transitions");
                        }
                        ui.Separator();
                        if (ui.MenuItem("Delete State"))
                            removeState(state->Id);
                    }
                }
                else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Connection)
                {
                    if (const auto transition = findTransitionByCanvasId(m_GraphContext->Connection); transition)
                    {
                        ui.TextColored(theme.Accent, "TRANSITION");
                        ui.TextColored(theme.MutedText,
                                       transition->first->Name + "  ->  " + transition->second->Destination);
                        ui.Separator();
                        if (ui.MenuItem("Delete Transition"))
                        {
                            const auto transitionId = transition->second->Id;
                            std::erase_if(transition->first->Transitions,
                                          [&](const auto& candidate) { return candidate.Id == transitionId; });
                            m_SelectedTransition.clear();
                            markChanged("Delete Animator Transition");
                        }
                    }
                }
                else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Pin)
                {
                    if (auto* state = findStateByCanvasId(m_GraphContext->Node); state)
                    {
                        const bool output = m_GraphContext->Pin == AnimatorCanvasId(state->Id, 0x414e494d4f555450ULL);
                        ui.TextColored(theme.Accent, output ? "TRANSITION OUTPUT" : "STATE INPUT");
                        ui.TextColored(theme.MutedText, state->Name);
                        ui.Separator();
                        if (output && ui.MenuItem("Unlink Outgoing", false, !state->Transitions.empty()))
                        {
                            state->Transitions.clear();
                            m_SelectedTransition.clear();
                            markChanged("Unlink Animator State Output");
                        }
                        if (!output)
                        {
                            const bool connected = std::ranges::any_of(
                                layer->States,
                                [&](const auto& source)
                                {
                                    return std::ranges::any_of(source.Transitions, [&](const auto& transition)
                                                               { return transition.DestinationId == state->Id; });
                                });
                            if (ui.MenuItem("Unlink Incoming", false, connected))
                            {
                                RemoveStateReferences(*layer, state->Id);
                                m_SelectedTransition.clear();
                                markChanged("Unlink Animator State Input");
                            }
                        }
                    }
                }
            }

            if (auto target = ui.BeginDragTarget(canvas, "AnimatorControllerClipDrop"); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
                {
                    try
                    {
                        const auto droppedAssets = AssetBrowserPanel::DecodeDragPayload(payload);
                        struct ClipCandidate final
                        {
                            Keire::AssetId Id;
                            std::string Name;
                        };
                        std::vector<ClipCandidate> clips;
                        const auto appendClip = [&clips](const Keire::AssetId id, std::string name)
                        {
                            if (!id || std::ranges::any_of(clips, [id](const auto& value) { return value.Id == id; }))
                                return;
                            clips.push_back({id, std::move(name)});
                        };
                        for (const auto dropped : droppedAssets)
                        {
                            const auto record = database ? database->Find(dropped) : std::nullopt;
                            if (record && record->Type == Keire::AnimationClipAsset::StaticType())
                            {
                                appendClip(dropped, record->RelativePath.stem().string());
                                continue;
                            }
                            if (!record)
                            {
                                if (assets && assets->TryGetType(dropped) == Keire::AnimationClipAsset::StaticType())
                                    appendClip(dropped, "Animation");
                                continue;
                            }
                            if (record->Type != Keire::AnimationSourceAsset::StaticType() &&
                                record->Type != Keire::MeshAsset::StaticType())
                            {
                                continue;
                            }
                            std::size_t generatedClip = 0;
                            for (const auto subAsset : record->SubAssets)
                            {
                                if (!assets || assets->TryGetType(subAsset) != Keire::AnimationClipAsset::StaticType())
                                {
                                    continue;
                                }
                                ++generatedClip;
                                auto name = record->RelativePath.stem().string();
                                if (generatedClip > 1)
                                    name += " " + std::to_string(generatedClip);
                                appendClip(subAsset, std::move(name));
                            }
                        }
                        if (clips.empty())
                        {
                            throw std::invalid_argument(
                                "Drop an Animation Clip, Animation Source, or animated model asset.");
                        }
                        if (!layer)
                        {
                            Keire::AnimationLayerDefinition base;
                            base.Id = Keire::AssetId::Generate().ToString();
                            base.Name = "Base Layer";
                            selectedLayer = base.Id;
                            graph.Layers.push_back(std::move(base));
                            layer = &graph.Layers.back();
                        }
                        std::size_t added = 0;
                        for (const auto& clip : clips)
                        {
                            Keire::AnimationStateDefinition state;
                            state.Id = Keire::AssetId::Generate().ToString();
                            state.Name = UniqueName(layer->States, clip.Name, &Keire::AnimationStateDefinition::Name);
                            state.Clip = clip.Id;
                            state.Motion.Clip = clip.Id;
                            state.EditorPosition = {
                                graphResult.PointerGraphPosition.X + static_cast<float>(added) * 36.0F,
                                graphResult.PointerGraphPosition.Y + static_cast<float>(added) * 28.0F};
                            selectedState = state.Id;
                            if (layer->EntryStateId.empty())
                                layer->EntryStateId = state.Id;
                            layer->States.push_back(std::move(state));
                            ++added;
                        }
                        selectedParameter.clear();
                        m_Message = "Added " + std::to_string(added) + " animation state(s).";
                        markChanged("Add Animator States");
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                        m_Controller.ReportAnimatorControllerError(m_Message);
                    }
                }
            }
        }
        ui.SameLine();

        if (auto inspector = ui.BeginChild("AnimatorInspector", {inspectorWidth, 0.0F}, true); inspector)
        {
            ui.TextColored(theme.Accent, "INSPECTOR");
            ui.Separator();
            if (auto* parameter = FindParameter(graph, selectedParameter))
            {
                if (ui.InputText("Name", parameter->Name))
                    markChanged("Rename Animator Parameter");
                auto type = static_cast<std::uint8_t>(parameter->Type);
                if (DrawEnumCombo(ui, "Type", type, ParameterTypeNames))
                {
                    parameter->Type = static_cast<Keire::AnimationParameterType>(type);
                    markChanged("Change Animator Parameter Type");
                }
                switch (parameter->Type)
                {
                case Keire::AnimationParameterType::Float:
                {
                    double value = parameter->FloatDefault;
                    if (ui.DragScalar("Default", value, 0.01))
                    {
                        parameter->FloatDefault = static_cast<float>(value);
                        markChanged("Edit Animator Parameter");
                    }
                    break;
                }
                case Keire::AnimationParameterType::Integer:
                {
                    std::int64_t value = parameter->IntegerDefault;
                    if (ui.DragInteger("Default", value))
                    {
                        parameter->IntegerDefault = static_cast<std::int32_t>(value);
                        markChanged("Edit Animator Parameter");
                    }
                    break;
                }
                case Keire::AnimationParameterType::Boolean:
                    if (ui.Checkbox("Default", parameter->BooleanDefault))
                        markChanged("Edit Animator Parameter");
                    break;
                case Keire::AnimationParameterType::Trigger:
                    ui.TextColored(theme.MutedText, "Triggers always reset after consumption.");
                    break;
                }
            }
            else if (auto* layer = FindLayer(graph, selectedLayer))
            {
                auto* state = FindState(*layer, selectedState);
                if (!state)
                {
                    if (ui.InputText("Name", layer->Name))
                        markChanged("Rename Animator Layer");
                    auto mode = static_cast<std::uint8_t>(layer->Mode);
                    if (DrawEnumCombo(ui, "Blend Mode", mode, LayerModeNames))
                    {
                        layer->Mode = static_cast<Keire::AnimationLayerMode>(mode);
                        markChanged("Change Animator Layer Mode");
                    }
                    double weight = layer->DefaultWeight;
                    if (ui.DragScalar("Default Weight", weight, 0.01, 0.0, 1.0))
                    {
                        layer->DefaultWeight = static_cast<float>(weight);
                        markChanged("Edit Animator Layer Weight");
                    }
                    if (EditAssetReference(ui, "Avatar Mask", layer->AvatarMask, Keire::AvatarMaskAsset::StaticType(),
                                           database, m_Message))
                        markChanged("Assign Animator Avatar Mask");
                    if (layer->States.empty())
                        ui.TextColored(theme.MutedText, "Drag animation clips onto the state machine.");
                }
                else
                {
                    if (ui.InputText("Name", state->Name))
                        markChanged("Rename Animator State");
                    double speed = state->Speed;
                    if (ui.DragScalar("Speed", speed, 0.01))
                    {
                        state->Speed = static_cast<float>(speed);
                        markChanged("Edit Animator State Speed");
                    }
                    if (ui.Checkbox("Loop", state->Loop))
                        markChanged("Edit Animator State Looping");
                    if (ui.DragVector2("Graph Position", state->EditorPosition, 1.0F))
                        markChanged("Move Animator State");
                    if (state->Id != layer->EntryStateId && ui.Button("Set As Entry"))
                    {
                        layer->EntryStateId = state->Id;
                        markChanged("Set Animator Entry State");
                    }
                    auto motion = static_cast<std::uint8_t>(state->Motion.Type);
                    if (DrawEnumCombo(ui, "Motion", motion, MotionTypeNames))
                    {
                        const auto replacement = static_cast<Keire::AnimationMotionType>(motion);
                        if (replacement == Keire::AnimationMotionType::Clip)
                        {
                            if (!state->Motion.Children.empty())
                                state->Motion.Clip = state->Motion.Children.front().Clip;
                            state->Clip = state->Motion.Clip;
                            state->Motion.Children.clear();
                            state->Motion.ParameterX.clear();
                            state->Motion.ParameterY.clear();
                        }
                        else
                        {
                            if (state->Motion.Children.empty() && state->Motion.Clip)
                            {
                                state->Motion.Children.push_back(
                                    {Keire::AssetId::Generate().ToString(), state->Motion.Clip});
                            }
                            state->Clip = {};
                            state->Motion.Clip = {};
                        }
                        state->Motion.Type = replacement;
                        markChanged("Change Animator State Motion");
                    }
                    if (state->Motion.Type == Keire::AnimationMotionType::Clip)
                    {
                        if (EditAssetReference(ui, "Animation Clip", state->Motion.Clip,
                                               Keire::AnimationClipAsset::StaticType(), database, m_Message))
                        {
                            state->Clip = state->Motion.Clip;
                            markChanged("Assign Animator State Clip");
                        }
                    }
                    else
                    {
                        const auto parameterCombo = [&](const std::string_view label, std::string& parameterId)
                        {
                            const auto selected = std::ranges::find(graph.ParameterDefinitions, parameterId,
                                                                    &Keire::AnimationParameterDefinition::Id);
                            const std::string_view preview = selected == graph.ParameterDefinitions.end()
                                                                 ? "Select Float Parameter"
                                                                 : selected->Name;
                            bool parameterChanged = false;
                            if (auto combo = ui.BeginCombo(label, preview); combo)
                            {
                                for (const auto& candidate : graph.ParameterDefinitions)
                                {
                                    if (candidate.Type != Keire::AnimationParameterType::Float)
                                        continue;
                                    if (ui.Selectable(candidate.Name, candidate.Id == parameterId))
                                    {
                                        parameterId = candidate.Id;
                                        parameterChanged = true;
                                    }
                                }
                            }
                            return parameterChanged;
                        };
                        if (parameterCombo("Parameter X", state->Motion.ParameterX))
                            markChanged("Assign Animator Blend Parameter");
                        if (state->Motion.Type == Keire::AnimationMotionType::BlendTree2D &&
                            parameterCombo("Parameter Y", state->Motion.ParameterY))
                            markChanged("Assign Animator Blend Parameter");
                        ui.Separator();
                        ui.TextColored(theme.MutedText, "BLEND CHILDREN");
                        std::string removeChild;
                        for (auto& child : state->Motion.Children)
                        {
                            auto id = ui.PushId(child.Id);
                            if (EditAssetReference(ui, "Clip", child.Clip, Keire::AnimationClipAsset::StaticType(),
                                                   database, m_Message))
                                markChanged("Assign Animator Blend Clip");
                            double childSpeed = child.Speed;
                            if (ui.DragScalar("Speed", childSpeed, 0.01))
                            {
                                child.Speed = static_cast<float>(childSpeed);
                                markChanged("Edit Animator Blend Child");
                            }
                            if (state->Motion.Type == Keire::AnimationMotionType::BlendTree1D)
                            {
                                double threshold = child.Threshold;
                                if (ui.DragScalar("Threshold", threshold, 0.01))
                                {
                                    child.Threshold = static_cast<float>(threshold);
                                    markChanged("Edit Animator Blend Threshold");
                                }
                            }
                            else if (ui.DragVector2("Position", child.Position, 0.01F))
                                markChanged("Edit Animator Blend Position");
                            if (ui.Button("Remove Child"))
                                removeChild = child.Id;
                            ui.Separator();
                        }
                        if (!removeChild.empty())
                        {
                            std::erase_if(state->Motion.Children,
                                          [&](const auto& child) { return child.Id == removeChild; });
                            markChanged("Remove Animator Blend Child");
                        }
                        if (ui.Button("+ Blend Child"))
                        {
                            Keire::AnimationBlendTreeChild child;
                            child.Id = Keire::AssetId::Generate().ToString();
                            child.Threshold = static_cast<float>(state->Motion.Children.size());
                            child.Position = {static_cast<float>(state->Motion.Children.size()), 0.0F};
                            state->Motion.Children.push_back(std::move(child));
                            markChanged("Add Animator Blend Child");
                        }
                    }

                    ui.Separator();
                    ui.TextColored(theme.Accent, "TRANSITIONS");
                    for (const auto& transition : state->Transitions)
                    {
                        const auto destination = std::ranges::find(layer->States, transition.DestinationId,
                                                                   &Keire::AnimationStateDefinition::Id);
                        const auto name = destination == layer->States.end() ? "Missing State" : destination->Name;
                        auto id = ui.PushId(transition.Id);
                        if (ui.Selectable(name, m_SelectedTransition == transition.Id))
                            m_SelectedTransition = transition.Id;
                    }
                    if (auto add = ui.BeginCombo("+ Transition", "Choose destination"); add)
                    {
                        for (const auto& destination : layer->States)
                        {
                            if (destination.Id == state->Id)
                                continue;
                            if (ui.Selectable(destination.Name))
                            {
                                Keire::AnimationTransition transition;
                                transition.Id = Keire::AssetId::Generate().ToString();
                                transition.DestinationId = destination.Id;
                                transition.Destination = destination.Name;
                                m_SelectedTransition = transition.Id;
                                state->Transitions.push_back(std::move(transition));
                                markChanged("Add Animator Transition");
                            }
                        }
                    }
                    auto* transition = FindTransition(*state, m_SelectedTransition);
                    if (transition)
                    {
                        ui.Separator();
                        if (auto destinationCombo = ui.BeginCombo("Destination", transition->Destination);
                            destinationCombo)
                        {
                            for (const auto& destination : layer->States)
                            {
                                if (destination.Id == state->Id)
                                    continue;
                                if (ui.Selectable(destination.Name, destination.Id == transition->DestinationId))
                                {
                                    transition->DestinationId = destination.Id;
                                    transition->Destination = destination.Name;
                                    markChanged("Retarget Animator Transition");
                                }
                            }
                        }
                        double duration = transition->Duration;
                        if (ui.DragScalar("Duration", duration, 0.01, 0.0))
                        {
                            transition->Duration = static_cast<float>(duration);
                            markChanged("Edit Animator Transition");
                        }
                        if (ui.Checkbox("Has Exit Time", transition->HasExitTime))
                            markChanged("Edit Animator Transition");
                        if (transition->HasExitTime)
                        {
                            double exitTime = transition->ExitTime;
                            if (ui.DragScalar("Exit Time", exitTime, 0.01, 0.0))
                            {
                                transition->ExitTime = static_cast<float>(exitTime);
                                markChanged("Edit Animator Transition");
                            }
                        }
                        ui.TextColored(theme.MutedText, "CONDITIONS");
                        std::size_t removeCondition = transition->Conditions.size();
                        for (std::size_t index = 0; index < transition->Conditions.size(); ++index)
                        {
                            auto id = ui.PushId(std::to_string(index));
                            auto& condition = transition->Conditions[index];
                            auto conditionParameter =
                                std::ranges::find(graph.ParameterDefinitions, condition.ParameterId,
                                                  &Keire::AnimationParameterDefinition::Id);
                            const std::string_view preview = conditionParameter == graph.ParameterDefinitions.end()
                                                                 ? "Missing Parameter"
                                                                 : conditionParameter->Name;
                            if (auto parameterCombo = ui.BeginCombo("Parameter", preview); parameterCombo)
                            {
                                for (const auto& candidate : graph.ParameterDefinitions)
                                {
                                    if (ui.Selectable(candidate.Name, candidate.Id == condition.ParameterId))
                                    {
                                        condition.ParameterId = candidate.Id;
                                        condition.Parameter = candidate.Name;
                                        if (candidate.Type == Keire::AnimationParameterType::Boolean ||
                                            candidate.Type == Keire::AnimationParameterType::Trigger)
                                            condition.Comparison = Keire::AnimationConditionComparison::Equal;
                                        markChanged("Edit Animator Transition Condition");
                                    }
                                }
                            }
                            conditionParameter = std::ranges::find(graph.ParameterDefinitions, condition.ParameterId,
                                                                   &Keire::AnimationParameterDefinition::Id);
                            auto comparison = static_cast<std::uint8_t>(condition.Comparison);
                            if (DrawEnumCombo(ui, "Comparison", comparison, ComparisonNames))
                            {
                                condition.Comparison = static_cast<Keire::AnimationConditionComparison>(comparison);
                                markChanged("Edit Animator Transition Condition");
                            }
                            if (conditionParameter != graph.ParameterDefinitions.end())
                            {
                                if (conditionParameter->Type == Keire::AnimationParameterType::Float)
                                {
                                    double value = condition.Value;
                                    if (ui.DragScalar("Value", value, 0.01))
                                    {
                                        condition.Value = static_cast<float>(value);
                                        markChanged("Edit Animator Transition Condition");
                                    }
                                }
                                else if (conditionParameter->Type == Keire::AnimationParameterType::Integer)
                                {
                                    std::int64_t value = condition.IntegerValue;
                                    if (ui.DragInteger("Value", value))
                                    {
                                        condition.IntegerValue = static_cast<std::int32_t>(value);
                                        markChanged("Edit Animator Transition Condition");
                                    }
                                }
                                else if (ui.Checkbox("Value", condition.BooleanValue))
                                    markChanged("Edit Animator Transition Condition");
                            }
                            if (ui.Button("Remove Condition"))
                                removeCondition = index;
                            ui.Separator();
                        }
                        if (removeCondition < transition->Conditions.size())
                        {
                            transition->Conditions.erase(transition->Conditions.begin() +
                                                         static_cast<std::ptrdiff_t>(removeCondition));
                            markChanged("Remove Animator Transition Condition");
                        }
                        if (!graph.ParameterDefinitions.empty() && ui.Button("+ Condition"))
                        {
                            Keire::AnimationTransitionCondition condition;
                            condition.ParameterId = graph.ParameterDefinitions.front().Id;
                            condition.Parameter = graph.ParameterDefinitions.front().Name;
                            transition->Conditions.push_back(std::move(condition));
                            markChanged("Add Animator Transition Condition");
                        }
                        if (ui.Button("Delete Transition"))
                        {
                            const auto transitionId = m_SelectedTransition;
                            std::erase_if(state->Transitions,
                                          [&](const auto& candidate) { return candidate.Id == transitionId; });
                            m_SelectedTransition.clear();
                            markChanged("Delete Animator Transition");
                        }
                    }

                    ui.Separator();
                    if (ui.Button("Delete State"))
                    {
                        const auto stateId = state->Id;
                        RemoveStateReferences(*layer, stateId);
                        std::erase_if(layer->States, [&](const auto& candidate) { return candidate.Id == stateId; });
                        if (layer->EntryStateId == stateId)
                            layer->EntryStateId = layer->States.empty() ? std::string{} : layer->States.front().Id;
                        selectedState.clear();
                        m_SelectedTransition.clear();
                        markChanged("Delete Animator State");
                    }
                }
            }
            else
                ui.TextColored(theme.MutedText, "Select a parameter, layer, or state.");
        }

        if (changed)
        {
            document.ReplaceDefinition(std::move(graph));
            document.RecordApplied(editName.empty() ? "Edit Animator Controller" : editName, std::move(before));
            m_Preview->Invalidate();
        }
        if (!selectedParameter.empty())
            document.SelectParameter(std::move(selectedParameter));
        else if (!selectedState.empty())
            document.SelectState(std::move(selectedLayer), std::move(selectedState));
        else if (!selectedLayer.empty())
            document.SelectLayer(std::move(selectedLayer));
        else
            document.ClearSelection();
    }
} // namespace KeireEditor
