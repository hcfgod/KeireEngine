#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Math/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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

    enum class SkinningMethod : std::uint8_t
    {
        LinearBlend,
        DualQuaternion
    };

    struct SkinVertexInfluence8
    {
        std::array<std::uint16_t, 8> Bones{};
        std::array<float, 8> Weights{};
        std::uint8_t Count = 0;

        [[nodiscard]] bool operator==(const SkinVertexInfluence8&) const noexcept = default;
    };

    class KEIRE_API SkinnedMeshAsset final : public Asset
    {
      public:
        SkinnedMeshAsset(AssetId mesh = {}, AssetId skeleton = {}, std::vector<SkinVertexInfluence> influences = {});
        SkinnedMeshAsset(AssetId mesh, AssetId skeleton, std::vector<SkinVertexInfluence8> influences,
                         SkinningMethod method);
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245534b49ULL, 0x4e4d455348000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] AssetId Mesh() const noexcept { return m_Mesh; }
        [[nodiscard]] AssetId Skeleton() const noexcept { return m_Skeleton; }
        [[nodiscard]] std::span<const SkinVertexInfluence> Influences() const noexcept { return m_Influences; }
        [[nodiscard]] std::span<const SkinVertexInfluence8> Influences8() const noexcept { return m_Influences8; }
        [[nodiscard]] SkinningMethod Method() const noexcept { return m_Method; }
        [[nodiscard]] std::uint8_t MaximumInfluences() const noexcept { return m_MaximumInfluences; }
        [[nodiscard]] static Ref<SkinnedMeshAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(AssetId mesh, AssetId skeleton,
                                                           std::span<const SkinVertexInfluence> influences);
        [[nodiscard]] static std::vector<std::byte>
        Encode(AssetId mesh, AssetId skeleton, std::span<const SkinVertexInfluence8> influences, SkinningMethod method);

      private:
        AssetId m_Mesh;
        AssetId m_Skeleton;
        std::vector<SkinVertexInfluence> m_Influences;
        std::vector<SkinVertexInfluence8> m_Influences8;
        SkinningMethod m_Method = SkinningMethod::LinearBlend;
        std::uint8_t m_MaximumInfluences = 4;
    };

    struct AnimationKeyframe
    {
        float Time = 0.0F;
        BoneTransform Value;
        [[nodiscard]] bool operator==(const AnimationKeyframe&) const noexcept = default;
    };

    struct AnimationTrack
    {
        std::uint32_t Bone = 0;
        std::vector<AnimationKeyframe> Keys;
    };

    enum class AnimationCompressionPreset : std::uint8_t
    {
        Disabled,
        Light,
        Balanced,
        Aggressive
    };

    struct AnimationCompressionSettings
    {
        bool Enabled = true;
        float MaximumTranslationError = 0.001F;
        float MaximumRotationErrorDegrees = 0.25F;
        float MaximumScaleError = 0.001F;
    };

    struct AnimationCompressionStatistics
    {
        std::size_t SourceKeyCount = 0;
        std::size_t CompressedKeyCount = 0;
        float MaximumTranslationError = 0.0F;
        float MaximumRotationErrorDegrees = 0.0F;
        float MaximumScaleError = 0.0F;
    };

    struct AnimationCompressionResult
    {
        std::vector<AnimationTrack> Tracks;
        AnimationCompressionStatistics Statistics;
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

    struct AnimationTakeDescriptor
    {
        AssetId Clip;
        std::string Name;
        float Duration = 0.0F;
    };

    struct AnimationSourceDefinition
    {
        std::uint32_t SchemaVersion = 1;
        AssetId Skeleton;
        AssetId Rig;
        std::vector<AnimationTakeDescriptor> Takes;
    };

    class KEIRE_API AnimationSourceAsset final : public Asset
    {
      public:
        explicit AnimationSourceAsset(AnimationSourceDefinition definition = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245414e49ULL, 0x4d534f5552434501ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const AnimationSourceDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] static Ref<AnimationSourceAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const AnimationSourceDefinition& definition);

      private:
        AnimationSourceDefinition m_Definition;
    };

    enum class AnimationConditionComparison : std::uint8_t
    {
        Greater,
        Less,
        Equal,
        NotEqual
    };

    enum class AnimationParameterType : std::uint8_t
    {
        Float,
        Integer,
        Boolean,
        Trigger
    };

    struct AnimationParameterDefinition
    {
        std::string Id;
        std::string Name;
        AnimationParameterType Type = AnimationParameterType::Float;
        float FloatDefault = 0.0F;
        std::int32_t IntegerDefault = 0;
        bool BooleanDefault = false;
    };

    struct AnimationTransitionCondition
    {
        std::string Parameter;
        AnimationConditionComparison Comparison = AnimationConditionComparison::Greater;
        float Value = 0.0F;
        std::string ParameterId;
        std::int32_t IntegerValue = 0;
        bool BooleanValue = true;
    };

    struct AnimationTransition
    {
        std::string Destination;
        float Duration = 0.1F;
        bool HasExitTime = false;
        float ExitTime = 1.0F;
        std::vector<AnimationTransitionCondition> Conditions;
        std::string Id;
        std::string DestinationId;
    };

    enum class AnimationMotionType : std::uint8_t
    {
        Clip,
        BlendTree1D,
        BlendTree2D
    };

    struct AnimationBlendTreeChild
    {
        std::string Id;
        AssetId Clip;
        float Threshold = 0.0F;
        Vector2 Position;
        float Speed = 1.0F;
    };

    struct AnimationMotionDefinition
    {
        AnimationMotionType Type = AnimationMotionType::Clip;
        AssetId Clip;
        std::string ParameterX;
        std::string ParameterY;
        std::vector<AnimationBlendTreeChild> Children;
    };

    struct AnimationStateDefinition
    {
        std::string Name;
        AssetId Clip;
        float Speed = 1.0F;
        bool Loop = true;
        std::vector<AnimationTransition> Transitions;
        std::string Id;
        AnimationMotionDefinition Motion;
        Vector2 EditorPosition;
        std::string SubgraphId;
    };

    enum class AnimationLayerMode : std::uint8_t
    {
        Override,
        Additive
    };

    struct AnimationStateMachineSubgraphDefinition
    {
        std::string Id;
        std::string Name;
        std::string EntryStateId;
    };

    struct AnimationLayerDefinition
    {
        std::string Id;
        std::string Name;
        AnimationLayerMode Mode = AnimationLayerMode::Override;
        float DefaultWeight = 1.0F;
        AssetId AvatarMask;
        std::string EntryStateId;
        std::vector<AnimationStateDefinition> States;
        std::vector<AnimationStateMachineSubgraphDefinition> Subgraphs;
    };

    struct AnimationGraphDefinition
    {
        std::uint32_t SchemaVersion = 3;
        std::string EntryState;
        std::vector<std::string> Parameters;
        std::vector<AnimationStateDefinition> States;
        std::vector<AnimationParameterDefinition> ParameterDefinitions;
        std::vector<AnimationLayerDefinition> Layers;
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

    struct AvatarMaskBoneWeight
    {
        std::string Bone;
        float Weight = 1.0F;
    };

    class KEIRE_API AvatarMaskAsset final : public Asset
    {
      public:
        AvatarMaskAsset(AssetId skeleton = {}, std::vector<AvatarMaskBoneWeight> bones = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245415641ULL, 0x5441524d41534b01ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] AssetId Skeleton() const noexcept { return m_Skeleton; }
        [[nodiscard]] std::span<const AvatarMaskBoneWeight> Bones() const noexcept { return m_Bones; }
        [[nodiscard]] float Weight(std::string_view bone) const noexcept;
        [[nodiscard]] static Ref<AvatarMaskAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(AssetId skeleton,
                                                           std::span<const AvatarMaskBoneWeight> bones);

      private:
        AssetId m_Skeleton;
        std::vector<AvatarMaskBoneWeight> m_Bones;
    };

    struct AnimatorSample
    {
        std::string State;
        float NormalizedTime = 0.0F;
        std::vector<BoneTransform> LocalPose;
        Vector3 RootMotion;
        Quaternion RootRotation;
        std::vector<AnimationEvent> Events;
    };

    struct AnimatorBlendWeight
    {
        std::string ChildId;
        AssetId Clip;
        float Weight = 0.0F;
    };

    struct AnimatorParameterDebugValue
    {
        std::string Id;
        std::string Name;
        AnimationParameterType Type = AnimationParameterType::Float;
        float FloatValue = 0.0F;
        std::int32_t IntegerValue = 0;
        bool BooleanValue = false;
    };

    struct AnimatorLayerDebugState
    {
        std::string Id;
        std::string Name;
        std::string StateId;
        std::string State;
        float NormalizedTime = 0.0F;
        float Weight = 1.0F;
        bool InTransition = false;
        std::string SourceStateId;
        std::string DestinationStateId;
        float TransitionProgress = 0.0F;
        std::vector<AnimatorBlendWeight> BlendWeights;
    };

    struct AnimatorPoseBoneDebugState
    {
        std::string Name;
        std::int32_t Parent = -1;
        BoneTransform LocalTransform;
        Vector3 WorldPosition;
    };

    struct AnimatorMotionTrajectoryPoint
    {
        float Time = 0.0F;
        Vector3 Position;
    };

    struct AnimatorStateMachineProfile
    {
        std::uint64_t UpdateCount = 0;
        double LastEvaluationMicroseconds = 0.0;
        double AverageEvaluationMicroseconds = 0.0;
        double PeakEvaluationMicroseconds = 0.0;
        std::uint32_t LayersEvaluated = 0;
        std::uint32_t TransitionsTested = 0;
        std::uint32_t MotionsEvaluated = 0;
        std::uint32_t ClipsSampled = 0;
    };

    struct AnimatorDebugSnapshot
    {
        std::uint64_t Revision = 0;
        std::vector<AnimatorParameterDebugValue> Parameters;
        std::vector<AnimatorLayerDebugState> Layers;
        Vector3 RootMotion;
        Quaternion RootRotation;
        std::vector<AnimationEvent> RecentEvents;
        std::vector<AnimatorPoseBoneDebugState> Pose;
        std::vector<AnimatorMotionTrajectoryPoint> MotionTrajectory;
        AnimatorStateMachineProfile Profile;
    };

    struct AnimatorCheckpointParameter
    {
        std::string Id;
        AnimationParameterType Type = AnimationParameterType::Float;
        float FloatValue = 0.0F;
        std::int32_t IntegerValue = 0;
        bool BooleanValue = false;
    };

    struct AnimatorCheckpointTransition
    {
        std::string Id;
        std::string SourceStateId;
        std::string DestinationStateId;
        float SourceTime = 0.0F;
        float DestinationTime = 0.0F;
        float Elapsed = 0.0F;
        float Duration = 0.0F;
    };

    struct AnimatorCheckpointLayer
    {
        std::string Id;
        std::string StateId;
        float Time = 0.0F;
        float Weight = 1.0F;
        float NormalizedTime = 0.0F;
        std::optional<AnimatorCheckpointTransition> Transition;
    };

    struct AnimatorCheckpoint
    {
        std::vector<AnimatorCheckpointParameter> Parameters;
        std::vector<AnimatorCheckpointLayer> Layers;
        BoneTransform PreviousRoot;
        bool Playing = true;
        bool HasPreviousRootRotation = false;
    };

    class KEIRE_API AnimatorInstance final
    {
      public:
        using ClipResolver = std::function<Ref<const AnimationClipAsset>(AssetId)>;
        using AvatarMaskResolver = std::function<Ref<const AvatarMaskAsset>(AssetId)>;
        AnimatorInstance(Ref<const SkeletonAsset> skeleton, Ref<const AnimationGraphAsset> graph, ClipResolver resolver,
                         AvatarMaskResolver maskResolver = {});
        void SetFloat(std::string parameter, float value);
        [[nodiscard]] float Float(std::string_view parameter) const;
        void SetInteger(std::string parameter, std::int32_t value);
        [[nodiscard]] std::int32_t Integer(std::string_view parameter) const;
        void SetBool(std::string parameter, bool value);
        [[nodiscard]] bool Bool(std::string_view parameter) const;
        void SetTrigger(std::string parameter);
        void ResetTrigger(std::string parameter);
        [[nodiscard]] bool Trigger(std::string_view parameter) const;
        void SetLayerWeight(std::string layer, float value);
        [[nodiscard]] float LayerWeight(std::string_view layer) const;
        void Play(std::string_view state, std::string_view layer = {}, float normalizedTime = 0.0F);
        void CrossFade(std::string_view state, float duration, std::string_view layer = {},
                       float normalizedTime = 0.0F);
        void Stop();
        [[nodiscard]] bool Playing() const noexcept { return m_Playing; }
        [[nodiscard]] std::shared_ptr<const AnimatorDebugSnapshot> DebugSnapshot() const noexcept
        {
            return m_DebugSnapshot;
        }
        [[nodiscard]] AnimatorSample Update(float deltaSeconds);
        [[nodiscard]] AnimatorCheckpoint CaptureCheckpoint() const;
        void RestoreCheckpoint(const AnimatorCheckpoint& checkpoint);
        [[nodiscard]] bool Reload(Ref<const AnimationGraphAsset> graph);
        void Reset();

      private:
        struct RuntimeParameter
        {
            AnimationParameterType Type = AnimationParameterType::Float;
            std::string Id;
            float FloatValue = 0.0F;
            std::int32_t IntegerValue = 0;
            bool BooleanValue = false;
        };

        struct RuntimeTransition
        {
            std::string SourceStateId;
            std::string DestinationStateId;
            float SourceTime = 0.0F;
            float DestinationTime = 0.0F;
            float Elapsed = 0.0F;
            float Duration = 0.0F;
            std::string Id;
        };

        struct RuntimeLayer
        {
            std::string Id;
            std::string StateId;
            float Time = 0.0F;
            float Weight = 1.0F;
            float NormalizedTime = 0.0F;
            std::vector<AnimatorBlendWeight> BlendWeights;
            std::optional<RuntimeTransition> Transition;
        };

        void PublishDebugSnapshot(Vector3 rootMotion = {}, Quaternion rootRotation = {},
                                  std::span<const AnimationEvent> events = {});

        Ref<const SkeletonAsset> m_Skeleton;
        Ref<const AnimationGraphAsset> m_Graph;
        ClipResolver m_Resolver;
        AvatarMaskResolver m_MaskResolver;
        std::map<std::string, RuntimeParameter, std::less<>> m_Parameters;
        std::vector<RuntimeLayer> m_Layers;
        std::string m_State;
        float m_Time = 0.0F;
        bool m_Playing = true;
        BoneTransform m_PreviousRoot;
        bool m_HasPreviousRootRotation = false;
        std::uint64_t m_DebugRevision = 0;
        std::vector<AnimationEvent> m_RecentEvents;
        std::vector<AnimatorPoseBoneDebugState> m_DebugPose;
        std::vector<AnimatorMotionTrajectoryPoint> m_DebugMotionTrajectory;
        Vector3 m_DebugTrajectoryPosition;
        float m_DebugTrajectoryTime = 0.0F;
        AnimatorStateMachineProfile m_Profile;
        std::shared_ptr<const AnimatorDebugSnapshot> m_DebugSnapshot;
    };

    KEIRE_API void ValidateSkeleton(std::span<const SkeletonBone> bones);
    [[nodiscard]] KEIRE_API AnimationCompressionSettings
    AnimationCompressionSettingsForPreset(AnimationCompressionPreset preset) noexcept;
    [[nodiscard]] KEIRE_API AnimationCompressionResult
    CompressAnimationTracks(std::span<const AnimationTrack> tracks, const AnimationCompressionSettings& settings);
    KEIRE_API void ValidateAnimationGraph(const AnimationGraphDefinition& definition);
    KEIRE_API void ValidateAvatarMask(AssetId skeleton, std::span<const AvatarMaskBoneWeight> bones);
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateSkeletonAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateSkinnedMeshAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateAnimationClipAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateAnimationSourceAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateAnimationGraphAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateAvatarMaskAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateAnimationClipAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateAnimationGraphAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateAvatarMaskAssetImporter();
} // namespace Keire
