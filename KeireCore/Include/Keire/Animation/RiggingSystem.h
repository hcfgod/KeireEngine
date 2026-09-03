#pragma once

#include "Keire/Animation/Skinning.h"
#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class RigProfileType : std::uint8_t
    {
        Humanoid,
        Biped,
        Quadruped,
        Custom
    };

    enum class RigBoneSemantic : std::uint16_t
    {
        None,
        Root,
        Pelvis,
        Spine,
        Chest,
        Neck,
        Head,
        LeftShoulder,
        LeftUpperArm,
        LeftLowerArm,
        LeftHand,
        RightShoulder,
        RightUpperArm,
        RightLowerArm,
        RightHand,
        LeftUpperLeg,
        LeftLowerLeg,
        LeftFoot,
        RightUpperLeg,
        RightLowerLeg,
        RightFoot,
        LeftFrontUpperLeg,
        LeftFrontLowerLeg,
        LeftFrontFoot,
        RightFrontUpperLeg,
        RightFrontLowerLeg,
        RightFrontFoot,
        LeftRearUpperLeg,
        LeftRearLowerLeg,
        LeftRearFoot,
        RightRearUpperLeg,
        RightRearLowerLeg,
        RightRearFoot,
        TailBase,
        TailTip,
        LeftWingRoot,
        LeftWingTip,
        RightWingRoot,
        RightWingTip
    };

    struct RigBoneDefinition
    {
        RigBoneSemantic Semantic = RigBoneSemantic::None;
        std::string Name;
        std::int32_t Parent = -1;
        BoneTransform BindPose;
        bool Required = true;
    };

    struct RigChainDefinition
    {
        std::string Name;
        std::vector<RigBoneSemantic> Bones;
    };

    struct RigDefinition
    {
        std::uint32_t SchemaVersion = 1;
        RigProfileType Profile = RigProfileType::Humanoid;
        SkinningMethod Skinning = SkinningMethod::LinearBlend;
        std::uint8_t MaximumInfluences = 4;
        std::vector<RigBoneDefinition> Bones;
        std::vector<RigChainDefinition> Chains;
    };

    class KEIRE_API RigDefinitionAsset final : public Asset
    {
      public:
        explicit RigDefinitionAsset(RigDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245524947ULL, 0x4445460000000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const RigDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<RigDefinitionAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const RigDefinition& definition);

      private:
        RigDefinition m_Definition;
    };

    enum class RigDiagnosticSeverity : std::uint8_t
    {
        Information,
        Warning,
        Error
    };

    struct RigDiagnostic
    {
        RigDiagnosticSeverity Severity = RigDiagnosticSeverity::Information;
        std::string Code;
        std::string Message;
        RigBoneSemantic Bone = RigBoneSemantic::None;
    };

    struct RigMarker
    {
        RigBoneSemantic Bone = RigBoneSemantic::None;
        Vector3 Position;
        float Confidence = 1.0F;
    };

    struct AutoRigRequest
    {
        RigProfileType Profile = RigProfileType::Humanoid;
        SkinningMethod Skinning = SkinningMethod::LinearBlend;
        std::uint8_t MaximumInfluences = 4;
        std::vector<RigMarker> Markers;
        std::optional<RigDefinition> CustomProfile;
    };

    struct AutoRigResult
    {
        RigDefinition Rig;
        std::vector<SkeletonBone> Skeleton;
        std::vector<SkinVertexInfluence8> Influences;
        std::vector<RigDiagnostic> Diagnostics;
    };

    enum class AnimationRetargetMatch : std::uint8_t
    {
        Unmapped,
        ExactName,
        Semantic,
        TargetConflict,
        Hierarchy
    };

    struct AnimationRetargetBoneMapping
    {
        std::uint32_t SourceBone = 0;
        std::optional<std::uint32_t> TargetBone;
        std::string SourceName;
        std::string TargetName;
        RigBoneSemantic Semantic = RigBoneSemantic::None;
        AnimationRetargetMatch Match = AnimationRetargetMatch::Unmapped;
        float TranslationScale = 1.0F;
        bool PreservesAuthoredLocalTrack = false;
        std::size_t ScaleFallbackKeyCount = 0;
    };

    struct AnimationRetargetDiagnostics
    {
        std::size_t SourceTrackCount = 0;
        std::size_t MappedTrackCount = 0;
        std::size_t ExactNameMatchCount = 0;
        std::size_t HierarchyMatchCount = 0;
        std::size_t SemanticMatchCount = 0;
        bool RootMotionMapped = true;
        std::vector<AnimationRetargetBoneMapping> Mappings;
        std::vector<RigDiagnostic> Messages;

        [[nodiscard]] bool Compatible() const noexcept { return MappedTrackCount != 0; }
    };

    struct AnimationRetargetResult
    {
        Ref<AnimationClipAsset> Clip;
        AnimationRetargetDiagnostics Diagnostics;
    };

    struct TwoBoneIkRequest
    {
        std::uint32_t Root = 0;
        std::uint32_t Middle = 0;
        std::uint32_t End = 0;
        Vector3 Target;
        Vector3 Pole{0.0F, 0.0F, 1.0F};
        float Weight = 1.0F;
        std::optional<Quaternion> EndRotation;
        float EndRotationWeight = 0.0F;
    };

    struct FabrikIkRequest
    {
        std::vector<std::uint32_t> Chain;
        Vector3 Target;
        std::uint32_t MaximumIterations = 12;
        float Tolerance = 0.001F;
        float Weight = 1.0F;
    };

    struct FootGroundContact
    {
        std::uint32_t UpperLeg = 0;
        std::uint32_t LowerLeg = 0;
        std::uint32_t Foot = 0;
        Vector3 Position;
        Vector3 Normal{0.0F, 1.0F, 0.0F};
        Vector3 Pole{0.0F, 0.0F, 1.0F};
        float Weight = 1.0F;
        float RotationWeight = 1.0F;
        std::optional<std::uint32_t> Toe;
    };

    struct FootGroundingRequest
    {
        std::optional<std::uint32_t> Pelvis;
        std::optional<std::uint32_t> Torso;
        std::vector<FootGroundContact> Contacts;
        float FootHeight = 0.02F;
        float PelvisWeight = 1.0F;
        float MaximumPelvisAdjustment = 0.5F;
        float MaximumHorizontalPelvisAdjustment = 0.0F;
        float PelvisSupportRadius = 0.0F;
        float PelvisRotationWeight = 0.0F;
        float MaximumPelvisRotationDegrees = 0.0F;
        float PositionTolerance = 0.01F;
    };

    struct FootGroundingResult
    {
        std::size_t SolvedFeet = 0;
        std::size_t UnreachableFeet = 0;
        float PelvisAdjustment = 0.0F;
        Vector3 HorizontalPelvisAdjustment;
        float PelvisRotationAdjustmentDegrees = 0.0F;
        float MaximumPositionError = 0.0F;
    };

    enum class RagdollPoseMode : std::uint8_t
    {
        Animated,
        TransitionToRagdoll,
        Ragdoll,
        TransitionToAnimation
    };

    class KEIRE_API RagdollPoseTransition final
    {
      public:
        void SetRagdoll(bool enabled, float duration);
        [[nodiscard]] std::vector<BoneTransform> Update(float deltaSeconds,
                                                        std::span<const BoneTransform> animationPose,
                                                        std::span<const BoneTransform> ragdollPose);
        void Reset() noexcept;
        [[nodiscard]] RagdollPoseMode Mode() const noexcept { return m_Mode; }
        [[nodiscard]] float Weight() const noexcept { return m_Weight; }

      private:
        RagdollPoseMode m_Mode = RagdollPoseMode::Animated;
        float m_Weight = 0.0F;
        float m_StartWeight = 0.0F;
        float m_TargetWeight = 0.0F;
        float m_Duration = 0.0F;
        float m_Elapsed = 0.0F;
    };

    KEIRE_API void ValidateRigDefinition(const RigDefinition& definition);
    [[nodiscard]] KEIRE_API std::string_view RigBoneSemanticName(RigBoneSemantic semantic) noexcept;
    [[nodiscard]] KEIRE_API RigDefinition InferRigDefinition(const SkeletonAsset& skeleton,
                                                             RigProfileType profile = RigProfileType::Humanoid,
                                                             SkinningMethod skinning = SkinningMethod::LinearBlend,
                                                             std::uint8_t maximumInfluences = 4);
    [[nodiscard]] KEIRE_API AutoRigResult GenerateRig(const MeshAsset& mesh, const AutoRigRequest& request);
    [[nodiscard]] KEIRE_API AnimationRetargetDiagnostics DiagnoseAnimationRetargeting(
        const SkeletonAsset& sourceSkeleton, const RigDefinition& sourceRig, const AnimationClipAsset& sourceClip,
        const SkeletonAsset& targetSkeleton, const RigDefinition& targetRig);
    [[nodiscard]] KEIRE_API AnimationRetargetResult RetargetAnimationClipWithDiagnostics(
        const SkeletonAsset& sourceSkeleton, const RigDefinition& sourceRig, const AnimationClipAsset& sourceClip,
        AssetId targetSkeletonId, const SkeletonAsset& targetSkeleton, const RigDefinition& targetRig);
    [[nodiscard]] KEIRE_API Ref<AnimationClipAsset>
    RetargetAnimationClip(const SkeletonAsset& sourceSkeleton, const RigDefinition& sourceRig,
                          const AnimationClipAsset& sourceClip, AssetId targetSkeletonId,
                          const SkeletonAsset& targetSkeleton, const RigDefinition& targetRig);
    [[nodiscard]] KEIRE_API bool SolveTwoBoneIk(const SkeletonAsset& skeleton, std::span<BoneTransform> localPose,
                                                const TwoBoneIkRequest& request);
    [[nodiscard]] KEIRE_API bool SolveFabrikIk(const SkeletonAsset& skeleton, std::span<BoneTransform> localPose,
                                               const FabrikIkRequest& request);
    [[nodiscard]] KEIRE_API std::optional<FootGroundingResult> SolveFootGrounding(const SkeletonAsset& skeleton,
                                                                                  std::span<BoneTransform> localPose,
                                                                                  const FootGroundingRequest& request);

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateRigDefinitionAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateRigDefinitionAssetDecoder();
} // namespace Keire
