#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Math/Curves.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class AudioMode : std::uint8_t
    {
        Disabled,
        Headless,
        Enabled
    };

    struct AudioSystemSpecification
    {
        AudioMode Mode = AudioMode::Disabled;
        std::uint32_t MaximumVoices = 256;
        std::uint32_t MaximumMeterReadings = 256;
    };

    enum class AudioGraphNodeType : std::uint8_t
    {
        Input,
        Gain,
        LowPass,
        HighPass,
        Equalizer,
        Compressor,
        Limiter,
        Gate,
        Delay,
        Chorus,
        Distortion,
        AlgorithmicReverb,
        ConvolutionReverb,
        Meter,
        Capture,
        Output
    };

    class KEIRE_API AudioGraphNodeId final
    {
      public:
        constexpr AudioGraphNodeId() noexcept = default;
        explicit constexpr AudioGraphNodeId(const std::uint32_t value) noexcept : m_Value(value) {}
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr auto operator<=>(const AudioGraphNodeId&) const noexcept = default;
        [[nodiscard]] constexpr std::uint32_t Value() const noexcept { return m_Value; }

      private:
        std::uint32_t m_Value = 0;
    };

    struct AudioGraphConnection
    {
        AudioGraphNodeId Source;
        bool DelayedFeedback = false;
    };

    struct AudioGraphNode
    {
        AudioGraphNodeId Id;
        std::string Name;
        AudioGraphNodeType Type = AudioGraphNodeType::Gain;
        std::vector<AudioGraphConnection> Inputs;
        std::vector<float> Parameters;
    };

    struct AudioGraphSnapshot
    {
        std::uint64_t Revision = 1;
        std::uint32_t SampleRate = 48000;
        std::uint32_t Channels = 2;
        AudioGraphNodeId Output;
        std::vector<AudioGraphNode> Nodes;
    };

    KEIRE_API void ValidateAudioGraph(const AudioGraphSnapshot& snapshot);

    class KEIRE_API AudioVoiceId final
    {
      public:
        constexpr AudioVoiceId() noexcept = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr auto operator<=>(const AudioVoiceId&) const noexcept = default;

      private:
        friend class AudioSystem;
        explicit constexpr AudioVoiceId(const std::uint64_t value) noexcept : m_Value(value) {}
        std::uint64_t m_Value = 0;
    };

    struct AudioClipData
    {
        std::uint32_t SampleRate = 48000;
        std::uint32_t Channels = 1;
        std::uint64_t Frames = 0;
        std::vector<float> Samples;
        std::vector<std::byte> EncodedSource;
        bool Streaming = false;
    };

    [[nodiscard]] KEIRE_API std::shared_ptr<const AudioClipData> LoadAudioClipData(const std::filesystem::path& path);

    struct AudioVoiceSpecification
    {
        std::shared_ptr<const AudioClipData> Clip;
        std::string Bus = "Master";
        float Gain = 1.0F;
        float Pitch = 1.0F;
        std::uint32_t Priority = 128;
        bool Loop = false;
        bool Spatial = false;
        Vector3 Position;
        Vector3 Velocity;
        float MinimumDistance = 1.0F;
        float MaximumDistance = 100.0F;
        float Occlusion = 0.0F;
        AssetId Mixer;
        AssetId BusId;
        Curve1D Attenuation = Curve1D::Constant(1.0F);
    };

    using AudioPlaybackRequest = AudioVoiceSpecification;

    struct AudioListenerState
    {
        Vector3 Position;
        Vector3 Forward{0.0F, 0.0F, -1.0F};
        Vector3 Up{0.0F, 1.0F, 0.0F};
        Vector3 Velocity;
    };

    struct AudioVoiceInfo
    {
        AudioVoiceId Id;
        std::string Bus;
        std::uint64_t Frame = 0;
        std::uint32_t Priority = 0;
        bool Playing = false;
        bool Virtualized = false;
    };

    struct AudioMixerSnapshot
    {
        std::uint64_t Revision = 1;
        std::chrono::milliseconds Transition{0};
        std::vector<std::pair<std::string, float>> BusGains;
    };

    struct AudioSystemStatistics
    {
        std::size_t Voices = 0;
        std::size_t AudibleVoices = 0;
        std::size_t VirtualVoices = 0;
        std::uint64_t RenderedFrames = 0;
        std::uint64_t Underruns = 0;
    };

    struct AudioBusInfo
    {
        std::string Name;
        float Gain = 1.0F;
    };

    struct AudioMeterReading
    {
        AssetId Bus;
        float Peak = 0.0F;
        float Rms = 0.0F;
        bool Clipping = false;
    };

    struct AudioMeterSnapshot
    {
        std::uint64_t Revision = 0;
        std::uint64_t DroppedReadings = 0;
        std::vector<AudioMeterReading> Readings;
    };

    class KEIRE_API AudioSystem final : public RefCounted
    {
      public:
        explicit AudioSystem(AudioSystemSpecification specification = {});
        ~AudioSystem() override;
        [[nodiscard]] bool IsOpen() const noexcept;
        void SubmitGraph(std::shared_ptr<const AudioGraphSnapshot> snapshot);
        [[nodiscard]] std::shared_ptr<const AudioGraphSnapshot> CurrentGraph() const;
        [[nodiscard]] std::uint64_t GraphRevision() const noexcept;
        [[nodiscard]] std::vector<float> RenderOffline(std::span<const float> interleavedInput,
                                                       std::uint64_t frameCount) const;
        [[nodiscard]] AudioVoiceId Play(AudioPlaybackRequest request);
        [[nodiscard]] bool Stop(AudioVoiceId voice);
        [[nodiscard]] bool SetVoice(AudioVoiceId voice, AudioPlaybackRequest request);
        [[nodiscard]] std::size_t StopAll(std::string_view bus = {});
        void SetBusGain(std::string bus, float gain);
        [[nodiscard]] float BusGain(std::string_view bus) const;
        [[nodiscard]] std::vector<AudioBusInfo> Buses() const;
        void SetListener(const AudioListenerState& listener);
        void SubmitSnapshot(const AudioMixerSnapshot& snapshot);
        void Update(std::chrono::duration<float> elapsed);
        [[nodiscard]] std::vector<float> RenderVoicesOffline(std::uint64_t frameCount);
        [[nodiscard]] std::vector<AudioVoiceInfo> Voices() const;
        [[nodiscard]] AudioSystemStatistics Statistics() const;
        void SubmitMeterSnapshot(AudioMeterSnapshot snapshot);
        [[nodiscard]] AudioMeterSnapshot LatestMeterSnapshot() const;
        void Close();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
