#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Audio/AudioSystem.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    class KEIRE_API AudioClipAsset final : public Asset
    {
      public:
        explicit AudioClipAsset(std::shared_ptr<const AudioClipData> clip);

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245415544ULL, 0x494f434c49500001ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const std::shared_ptr<const AudioClipData>& Clip() const noexcept { return m_Clip; }
        [[nodiscard]] std::uint64_t FrameCount() const noexcept;
        [[nodiscard]] float DurationSeconds() const noexcept;

        [[nodiscard]] static std::vector<std::byte> Encode(const AudioClipData& clip);
        [[nodiscard]] static Ref<AudioClipAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static Ref<AudioClipAsset> Silence();

      private:
        std::shared_ptr<const AudioClipData> m_Clip;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateAudioClipAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateAudioClipAssetDecoder();

    enum class AudioMixerSendStage : std::uint8_t
    {
        PreFader,
        PostFader
    };

    enum class AudioMixerSnapshotParameterType : std::uint8_t
    {
        BusGain,
        BusMute,
        BusSolo,
        SendGain,
        EffectBypass,
        EffectParameter
    };

    struct AudioMixerEffectDefinition
    {
        AssetId Id;
        std::string Name;
        AudioGraphNodeType Type = AudioGraphNodeType::Gain;
        bool Bypassed = false;
        std::vector<float> Parameters;
        AssetId ImpulseResponse;

        bool operator==(const AudioMixerEffectDefinition&) const = default;
    };

    struct AudioMixerSendDefinition
    {
        AssetId Id;
        AssetId DestinationBus;
        AudioMixerSendStage Stage = AudioMixerSendStage::PostFader;
        float Gain = 1.0F;

        bool operator==(const AudioMixerSendDefinition&) const = default;
    };

    struct AudioMixerBusDefinition
    {
        AssetId Id;
        std::string Name;
        AssetId Parent;
        bool Mute = false;
        bool Solo = false;
        float Gain = 1.0F;
        std::vector<AudioMixerEffectDefinition> Effects;
        std::vector<AudioMixerSendDefinition> Sends;

        bool operator==(const AudioMixerBusDefinition&) const = default;
    };

    struct AudioMixerSnapshotParameterDefinition
    {
        AudioMixerSnapshotParameterType Type = AudioMixerSnapshotParameterType::BusGain;
        AssetId Target;
        std::uint32_t Parameter = 0;
        float Value = 0.0F;

        bool operator==(const AudioMixerSnapshotParameterDefinition&) const = default;
    };

    struct AudioMixerSnapshotDefinition
    {
        AssetId Id;
        std::string Name;
        std::vector<AudioMixerSnapshotParameterDefinition> Parameters;

        bool operator==(const AudioMixerSnapshotDefinition&) const = default;
    };

    struct AudioMixerDuckingDefinition
    {
        AssetId Id;
        std::string Name;
        AssetId SidechainBus;
        AssetId TargetBus;
        float ThresholdDb = -24.0F;
        float Ratio = 4.0F;
        float AttackSeconds = 0.01F;
        float HoldSeconds = 0.0F;
        float ReleaseSeconds = 0.1F;
        float MaximumAttenuationDb = 12.0F;

        bool operator==(const AudioMixerDuckingDefinition&) const = default;
    };

    struct AudioMixerDefinition
    {
        std::uint32_t SchemaVersion = 1;
        AssetId MasterBus;
        std::vector<AudioMixerBusDefinition> Buses;
        std::vector<AudioMixerSnapshotDefinition> Snapshots;
        std::vector<AudioMixerDuckingDefinition> Ducking;

        bool operator==(const AudioMixerDefinition&) const = default;
    };

    KEIRE_API void ValidateAudioMixer(const AudioMixerDefinition& definition);
    [[nodiscard]] KEIRE_API std::vector<AssetId> AudioMixerDependencies(const AudioMixerDefinition& definition);
    /// Returns a validated copy with one authored snapshot blended over its base values. Boolean targets switch at
    /// 50%; scalar targets interpolate linearly. The input definition is never mutated.
    [[nodiscard]] KEIRE_API AudioMixerDefinition BlendAudioMixerSnapshot(const AudioMixerDefinition& definition,
                                                                         AssetId snapshot, float weight);

    class KEIRE_API AudioMixerAsset final : public Asset
    {
      public:
        explicit AudioMixerAsset(AudioMixerDefinition definition);

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245415544ULL, 0x4d49584552303031ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const AudioMixerDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static AudioMixerDefinition DefaultDefinition();
        [[nodiscard]] static std::vector<std::byte> Encode(const AudioMixerDefinition& definition);
        [[nodiscard]] static Ref<AudioMixerAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static Ref<AudioMixerAsset> Default();

      private:
        AudioMixerDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateAudioMixerAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateAudioMixerAssetDecoder();
} // namespace Keire
