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
#include "Keire/Vfx/VfxSubgraph.h"
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

        [[nodiscard]] Ref<const AnimationClipAsset> ResolveClip(AnimationRuntimeState& state, const AssetId id);

        [[nodiscard]] Ref<const AvatarMaskAsset> ResolveMask(AnimationRuntimeState& state, const AssetId id);

        [[nodiscard]] bool DependenciesReady(AnimationRuntimeState& state, const AnimationGraphAsset& graph);

        [[nodiscard]] static RigDefinition BestRuntimeRig(const SkeletonAsset& skeleton);

        static void ApplyCommands(AnimatorInstance& instance, std::span<const AnimatorCommand> commands);

        static void SkinPalette(const SkeletonAsset& skeleton, const std::span<const BoneTransform> localPose,
                                std::vector<Matrix4>& world, std::vector<Matrix4>& palette);

        [[nodiscard]] static std::vector<Matrix4> SkinPalette(const SkeletonAsset& skeleton,
                                                              const std::span<const BoneTransform> localPose);

        static void ModelBoneMatrices(const SkeletonAsset& skeleton, const std::span<const BoneTransform> localPose,
                                      std::vector<Matrix4>& world);

        [[nodiscard]] static std::vector<Matrix4> ModelBoneMatrices(const SkeletonAsset& skeleton,
                                                                    const std::span<const BoneTransform> localPose);

        [[nodiscard]] static bool SetBoneModelRotationCached(const SkeletonAsset& skeleton,
                                                             const std::span<BoneTransform> localPose,
                                                             const std::uint32_t bone, const Quaternion modelRotation,
                                                             const float weight, std::vector<Matrix4>& world);

        [[nodiscard]] static bool ApplyBoneModelRotationDeltaCached(const SkeletonAsset& skeleton,
                                                                    const std::span<BoneTransform> localPose,
                                                                    const std::uint32_t bone, const Quaternion delta,
                                                                    const float weight, std::vector<Matrix4>& world);

        [[nodiscard]] static bool SolveTwoBoneIkCached(const SkeletonAsset& skeleton,
                                                       const std::span<BoneTransform> localPose,
                                                       const TwoBoneIkRequest& request, std::vector<Matrix4>& world);

        [[nodiscard]] static std::shared_ptr<AnimatorDebugSnapshot>
        ProceduralDebugSnapshot(AnimationRuntimeState& state, const SkeletonAsset& skeleton,
                                const std::span<const BoneTransform> localPose,
                                const std::span<const Matrix4> modelBones);

        [[nodiscard]] static std::shared_ptr<const AnimatorDebugSnapshot>
        FinalPoseDebugSnapshot(const SkeletonAsset& skeleton, const std::span<const BoneTransform> localPose,
                               const std::shared_ptr<const AnimatorDebugSnapshot>& source);

        [[nodiscard]] static std::optional<std::uint32_t>
        ResolveIkBone(const std::map<std::string, std::uint32_t, std::less<>>& names,
                      const std::map<RigBoneSemantic, std::uint32_t>& semantics, const bool automatic,
                      const std::string_view fallback, const RigBoneSemantic semantic);

        [[nodiscard]] std::string ApplyAuthoredArmIk(const Entity& entity, const SkeletonAsset& skeleton,
                                                     const AnimatorComponent& animator,
                                                     const std::span<BoneTransform> localPose,
                                                     const std::map<std::string, std::uint32_t, std::less<>>& names,
                                                     const std::map<RigBoneSemantic, std::uint32_t>& semantics,
                                                     AnimationRuntimeState& runtimeState);

        [[nodiscard]] static std::string ApplyIkGoals(const Entity& entity, const SkeletonAsset& skeleton,
                                                      const AnimatorComponent& animator,
                                                      std::span<BoneTransform> localPose,
                                                      const std::map<std::string, std::uint32_t, std::less<>>& indices);

        [[nodiscard]] std::string ApplyFootGrounding(
            const Entity& entity, const SkeletonAsset& skeleton, const AnimatorFootGroundingSettings& settings,
            const float runtimeWeight, const AssetId skinnedMesh, const float deltaSeconds,
            std::span<BoneTransform> localPose, const std::map<std::string, std::uint32_t, std::less<>>& indices,
            const std::map<RigBoneSemantic, std::uint32_t>& semantics, AnimationRuntimeState& runtimeState,
            const std::optional<float> horizontalPelvisRatio = std::nullopt,
            const std::optional<float> maximumFootRotationDegrees = std::nullopt,
            const std::optional<std::array<float, 2>> proceduralFootWeights = std::nullopt,
            const std::optional<float> unsupportedFootDropRatio = std::nullopt);

        static void ApplyRootMotion(const Entity& entity, const AnimatorSample& sample, AnimatorComponent& animator);

        [[nodiscard]] bool PrepareProceduralAnimator(const Entity& entity, AnimatorComponent& animator,
                                                     AnimationRuntimeState& state, Ref<const SkeletonAsset>& skeleton,
                                                     Ref<const ProceduralMotionProfileAsset>& profile);

        [[nodiscard]] static float VectorLength(const Vector3 value) noexcept;

        [[nodiscard]] static Vector3 NormalizeHorizontal(const Vector3 value, const Vector3 fallback = {}) noexcept;

        [[nodiscard]] static bool CrossedPhase(const float previous, const float current, const float target) noexcept;

        [[nodiscard]] static float SignedHorizontalAngleDegrees(const Vector3 from, const Vector3 to) noexcept;

        [[nodiscard]] static Vector3 RespondHorizontalDirection(const Vector3 current, const Vector3 target,
                                                                const float blend) noexcept;

        [[nodiscard]] float PreLandingAmount(const Entity& entity, const Entity& characterRoot,
                                             const Ref<CharacterControllerComponent>& character, const Vector3 velocity,
                                             const ProceduralMotionProfile& profile,
                                             AnimationRuntimeState& state) const;

        void DispatchProceduralFootEvents(const Entity& entity, AnimationRuntimeState& state,
                                          const ProceduralLocomotionState& procedural);

        void AdvanceProceduralAnimation(float deltaSeconds);
        void PublishProceduralAnimation(const Entity& entity, AnimatorComponent& animator,
                                        AnimationRuntimeState& state);

        void SynchronizeAnimation(const float deltaSeconds);

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
        std::map<AssetId, AssetHandle<VfxSubgraphAsset>> VfxSubgraphs;
        std::map<AssetId, VfxMeshShapeState> VfxMeshShapes;
        std::map<AssetId, AssetHandle<VfxVolumeAsset>> VfxVolumes;
        float PresentationWidth = 1920.0F;
        float PresentationHeight = 1080.0F;
        float PresentationInterpolationAlpha = 1.0F;
        RuntimeUiInsets SafeArea;
    };
} // namespace Keire
