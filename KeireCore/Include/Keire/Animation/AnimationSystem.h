#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Math/Math.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    struct BoneTransform
    {
        Vector3 Translation;
        Quaternion Rotation;
        Vector3 Scale{1.0F, 1.0F, 1.0F};
        [[nodiscard]] bool operator==(const BoneTransform&) const noexcept = default;
    };

    struct SkeletonBone
    {
        std::string Name;
        std::int32_t Parent = -1;
        BoneTransform BindPose;
        Matrix4 InverseBindPose;
    };

    class KEIRE_API SkeletonAsset final : public Asset
    {
      public:
        explicit SkeletonAsset(std::vector<SkeletonBone> bones = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245534b45ULL, 0x4c45544f4e000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] std::span<const SkeletonBone> Bones() const noexcept { return m_Bones; }
        [[nodiscard]] static Ref<SkeletonAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(std::span<const SkeletonBone> bones);

      private:
        std::vector<SkeletonBone> m_Bones;
    };

    struct SkinVertexInfluence
    {
        std::array<std::uint16_t, 4> Bones{};
        std::array<float, 4> Weights{};
        [[nodiscard]] bool operator==(const SkinVertexInfluence&) const noexcept = default;
    };

    class KEIRE_API SkinnedMeshAsset final : public Asset
    {
      public:
        SkinnedMeshAsset(AssetId mesh = {}, AssetId skeleton = {}, std::vector<SkinVertexInfluence> influences = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245534b49ULL, 0x4e4d455348000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] AssetId Mesh() const noexcept { return m_Mesh; }
        [[nodiscard]] AssetId Skeleton() const noexcept { return m_Skeleton; }
        [[nodiscard]] std::span<const SkinVertexInfluence> Influences() const noexcept { return m_Influences; }
        [[nodiscard]] static Ref<SkinnedMeshAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(AssetId mesh, AssetId skeleton,
                                                           std::span<const SkinVertexInfluence> influences);

      private:
        AssetId m_Mesh;
        AssetId m_Skeleton;
        std::vector<SkinVertexInfluence> m_Influences;
    };

    struct AnimationKeyframe
    {
        float Time = 0.0F;
        BoneTransform Value;
    };

    struct AnimationTrack
    {
        std::uint32_t Bone = 0;
        std::vector<AnimationKeyframe> Keys;
    };

    struct AnimationEvent
    {
        float Time = 0.0F;
        std::string Name;
        std::string Payload;
    };

    class KEIRE_API AnimationClipAsset final : public Asset
    {
      public:
        AnimationClipAsset(AssetId skeleton = {}, float duration = 0.0F, std::vector<AnimationTrack> tracks = {},
                           std::vector<AnimationEvent> events = {}, bool rootMotion = false);
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245414e49ULL, 0x4d434c4950000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] AssetId Skeleton() const noexcept { return m_Skeleton; }
        [[nodiscard]] float Duration() const noexcept { return m_Duration; }
        [[nodiscard]] std::span<const AnimationTrack> Tracks() const noexcept { return m_Tracks; }
        [[nodiscard]] std::span<const AnimationEvent> Events() const noexcept { return m_Events; }
        [[nodiscard]] bool RootMotion() const noexcept { return m_RootMotion; }
        [[nodiscard]] static Ref<AnimationClipAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(AssetId skeleton, float duration,
                                                           std::span<const AnimationTrack> tracks,
                                                           std::span<const AnimationEvent> events, bool rootMotion);

      private:
        AssetId m_Skeleton;
        float m_Duration = 0.0F;
        std::vector<AnimationTrack> m_Tracks;
        std::vector<AnimationEvent> m_Events;
        bool m_RootMotion = false;
    };

    enum class AnimationConditionComparison : std::uint8_t
    {
        Greater,
        Less,
        Equal,
        NotEqual
    };

    struct AnimationTransitionCondition
    {
        std::string Parameter;
        AnimationConditionComparison Comparison = AnimationConditionComparison::Greater;
        float Value = 0.0F;
    };

    struct AnimationTransition
    {
        std::string Destination;
        float Duration = 0.1F;
        bool HasExitTime = false;
        float ExitTime = 1.0F;
        std::vector<AnimationTransitionCondition> Conditions;
    };

    struct AnimationStateDefinition
    {
        std::string Name;
        AssetId Clip;
        float Speed = 1.0F;
        bool Loop = true;
        std::vector<AnimationTransition> Transitions;
    };

    struct AnimationGraphDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::string EntryState;
        std::vector<std::string> Parameters;
        std::vector<AnimationStateDefinition> States;
    };

    class KEIRE_API AnimationGraphAsset final : public Asset
    {
      public:
        explicit AnimationGraphAsset(AnimationGraphDefinition definition = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245414e49ULL, 0x4d47524150480001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const AnimationGraphDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] static Ref<AnimationGraphAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const AnimationGraphDefinition& definition);

      private:
        AnimationGraphDefinition m_Definition;
    };

    struct AnimatorSample
    {
        std::string State;
        float NormalizedTime = 0.0F;
        std::vector<BoneTransform> LocalPose;
        Vector3 RootMotion;
        std::vector<AnimationEvent> Events;
    };

    class KEIRE_API AnimatorInstance final
    {
      public:
        using ClipResolver = std::function<Ref<AnimationClipAsset>(AssetId)>;
        AnimatorInstance(Ref<SkeletonAsset> skeleton, Ref<AnimationGraphAsset> graph, ClipResolver resolver);
        void SetFloat(std::string parameter, float value);
        [[nodiscard]] float Float(std::string_view parameter) const;
        [[nodiscard]] AnimatorSample Update(float deltaSeconds);
        void Reset();

      private:
        Ref<SkeletonAsset> m_Skeleton;
        Ref<AnimationGraphAsset> m_Graph;
        ClipResolver m_Resolver;
        std::map<std::string, float, std::less<>> m_Parameters;
        std::string m_State;
        float m_Time = 0.0F;
        BoneTransform m_PreviousRoot;
    };

    KEIRE_API void ValidateSkeleton(std::span<const SkeletonBone> bones);
    KEIRE_API void ValidateAnimationGraph(const AnimationGraphDefinition& definition);
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateSkeletonAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateSkinnedMeshAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateAnimationClipAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateAnimationGraphAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateAnimationGraphAssetImporter();
} // namespace Keire
