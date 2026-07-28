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

    struct TwoBoneIkRequest
    {
        std::uint32_t Root = 0;
        std::uint32_t Middle = 0;
        std::uint32_t End = 0;
        Vector3 Target;
        Vector3 Pole{0.0F, 0.0F, 1.0F};
        float Weight = 1.0F;
    };

    struct FabrikIkRequest
    {
        std::vector<std::uint32_t> Chain;
        Vector3 Target;
        std::uint32_t MaximumIterations = 12;
        float Tolerance = 0.001F;
        float Weight = 1.0F;
    };

    KEIRE_API void ValidateRigDefinition(const RigDefinition& definition);
    [[nodiscard]] KEIRE_API std::string_view RigBoneSemanticName(RigBoneSemantic semantic) noexcept;
    [[nodiscard]] KEIRE_API RigDefinition InferRigDefinition(const SkeletonAsset& skeleton,
                                                             RigProfileType profile = RigProfileType::Humanoid,
                                                             SkinningMethod skinning = SkinningMethod::LinearBlend,
                                                             std::uint8_t maximumInfluences = 4);
    [[nodiscard]] KEIRE_API AutoRigResult GenerateRig(const MeshAsset& mesh, const AutoRigRequest& request);
    [[nodiscard]] KEIRE_API Ref<AnimationClipAsset>
    RetargetAnimationClip(const SkeletonAsset& sourceSkeleton, const RigDefinition& sourceRig,
                          const AnimationClipAsset& sourceClip, AssetId targetSkeletonId,
                          const SkeletonAsset& targetSkeleton, const RigDefinition& targetRig);
    [[nodiscard]] KEIRE_API bool SolveTwoBoneIk(const SkeletonAsset& skeleton, std::span<BoneTransform> localPose,
                                                const TwoBoneIkRequest& request);
    [[nodiscard]] KEIRE_API bool SolveFabrikIk(const SkeletonAsset& skeleton, std::span<BoneTransform> localPose,
                                               const FabrikIkRequest& request);

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateRigDefinitionAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateRigDefinitionAssetDecoder();
} // namespace Keire
