#pragma once

#include "Keire/Api.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <chrono>
#include <compare>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
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
        std::vector<float> Samples;
        bool Streaming = false;
    };

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
    };

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
        [[nodiscard]] AudioVoiceId Play(AudioVoiceSpecification specification);
        [[nodiscard]] bool Stop(AudioVoiceId voice);
        [[nodiscard]] bool SetVoice(AudioVoiceId voice, AudioVoiceSpecification specification);
        void SetListener(const AudioListenerState& listener);
        void SubmitSnapshot(const AudioMixerSnapshot& snapshot);
        void Update(std::chrono::duration<float> elapsed);
        [[nodiscard]] std::vector<float> RenderVoicesOffline(std::uint64_t frameCount);
        [[nodiscard]] std::vector<AudioVoiceInfo> Voices() const;
        [[nodiscard]] AudioSystemStatistics Statistics() const;
        void Close();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
