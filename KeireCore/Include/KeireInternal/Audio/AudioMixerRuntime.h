#pragma once

#include "Keire/Audio/AudioAssets.h"

#include <array>
#include <atomic>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace Keire::Detail
{
    inline constexpr std::size_t MaximumAudioConvolutionSeconds = 5;
    inline constexpr std::size_t MaximumAudioConvolutionChannelFrames = 4U * 1024U * 1024U;
    inline constexpr std::size_t MaximumAudioSystemConvolutionChannelFrames = 8U * 1024U * 1024U;

    struct PreparedAudioImpulseResponse final
    {
        std::uint32_t Channels = 0;
        std::vector<float> Samples;

        [[nodiscard]] std::size_t FrameCount() const noexcept { return Channels == 0 ? 0 : Samples.size() / Channels; }
    };

    [[nodiscard]] PreparedAudioImpulseResponse
    PrepareAudioImpulseResponse(const AudioClipData& source, std::uint32_t sampleRate, std::uint32_t channels);
    [[nodiscard]] PreparedAudioImpulseResponse PrepareMonoAudioImpulseResponse(std::span<const float> samples,
                                                                               std::uint32_t channels);

    class AudioEffectProcessor final
    {
      public:
        AudioEffectProcessor(AudioGraphNodeType type, std::uint32_t sampleRate, std::uint32_t channels,
                             std::span<const float> parameters,
                             const PreparedAudioImpulseResponse* impulseResponse = nullptr);

        void UpdateParameters(std::span<const float> parameters);
        void Process(std::span<const float> input, std::span<float> output) noexcept;
        void Process(std::span<float> samples) noexcept { Process(samples, samples); }

      private:
        struct ParameterState final
        {
            // The owner thread is the sole publisher and the audio callback is the sole reader. UpdateParameters and
            // Process use a sequentially consistent revision bracket so weakly ordered CPUs cannot accept torn fields.
            std::atomic<std::uint64_t> Revision{0};
            std::array<std::atomic<float>, 64> Values{};
            std::atomic<std::size_t> Count{0};
            std::atomic<float> FilterCoefficient{0.0F};
            std::atomic<float> EqualizerHighCoefficient{0.0F};
            std::array<std::atomic<float>, 3> EqualizerGains{1.0F, 1.0F, 1.0F};
            std::atomic<float> EqualizerOutputGain{1.0F};

            ParameterState() noexcept = default;
            ParameterState(const ParameterState& other) noexcept { *this = other; }
            ParameterState& operator=(const ParameterState& other) noexcept
            {
                if (this == &other)
                    return *this;
                for (std::size_t index = 0; index < Values.size(); ++index)
                    Values[index].store(other.Values[index].load(std::memory_order_relaxed), std::memory_order_relaxed);
                Count.store(other.Count.load(std::memory_order_relaxed), std::memory_order_relaxed);
                FilterCoefficient.store(other.FilterCoefficient.load(std::memory_order_relaxed),
                                        std::memory_order_relaxed);
                EqualizerHighCoefficient.store(other.EqualizerHighCoefficient.load(std::memory_order_relaxed),
                                               std::memory_order_relaxed);
                for (std::size_t index = 0; index < EqualizerGains.size(); ++index)
                    EqualizerGains[index].store(other.EqualizerGains[index].load(std::memory_order_relaxed),
                                                std::memory_order_relaxed);
                EqualizerOutputGain.store(other.EqualizerOutputGain.load(std::memory_order_relaxed),
                                          std::memory_order_relaxed);
                Revision.store(0, std::memory_order_relaxed);
                return *this;
            }
        };

        struct DecodedParameterState final
        {
            std::array<float, 64> Values{};
            std::size_t Count = 0;
            float FilterCoefficient = 0.0F;
            float EqualizerHighCoefficient = 0.0F;
            std::array<float, 3> EqualizerGains{1.0F, 1.0F, 1.0F};
            float EqualizerOutputGain = 1.0F;
        };

        void ProcessConvolutionBlock() noexcept;

        AudioGraphNodeType m_Type = AudioGraphNodeType::Gain;
        std::uint32_t m_SampleRate = 48000;
        std::uint32_t m_Channels = 2;
        ParameterState m_ParameterState;
        DecodedParameterState m_CallbackParameters;
        std::vector<float> m_FilterState;
        std::vector<float> m_Delay;
        std::size_t m_DelayCursor = 0;
        double m_Phase = 0.0;
        PreparedAudioImpulseResponse m_ImpulseResponse;
        std::vector<float> m_ConvolutionHistory;
        std::size_t m_ConvolutionCursor = 0;
        std::size_t m_ConvolutionPartitionFrames = 0;
        std::size_t m_ConvolutionPartitionCount = 0;
        std::size_t m_ConvolutionSpectrumCursor = 0;
        std::size_t m_ConvolutionInputFrames = 0;
        std::vector<float> m_ConvolutionInput;
        std::vector<float> m_ConvolutionReady;
        std::vector<float> m_ConvolutionOverlap;
        std::vector<std::complex<float>> m_ConvolutionImpulseSpectra;
        std::vector<std::complex<float>> m_ConvolutionInputSpectra;
        std::vector<std::complex<float>> m_ConvolutionScratch;
    };

    struct AudioMixerProcessResult
    {
        std::vector<float> Output;
        std::vector<AudioMeterReading> Meters;
        std::uint64_t DroppedMeterReadings = 0;
    };

    void ApplyAlgorithmicReverb(std::vector<float>& samples, std::uint32_t sampleRate, std::uint32_t channels,
                                std::span<const float> parameters);
    [[nodiscard]] AudioMixerProcessResult ProcessAudioMixer(const AudioMixerDefinition& definition,
                                                            std::map<AssetId, std::vector<float>> busInputs,
                                                            std::map<AssetId, AudioEffectProcessor>& effectProcessors,
                                                            std::uint32_t sampleRate, std::uint32_t channels,
                                                            std::size_t maximumMeterReadings);
} // namespace Keire::Detail
