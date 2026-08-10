#pragma once

#include "Keire/Audio/AudioAssets.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace Keire::Detail
{
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
                                                            std::uint32_t sampleRate, std::uint32_t channels,
                                                            std::size_t maximumMeterReadings);
} // namespace Keire::Detail
