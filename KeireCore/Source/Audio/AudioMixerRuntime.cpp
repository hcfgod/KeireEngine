#include "KeireInternal/Audio/AudioMixerRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>

namespace Keire::Detail
{
    namespace
    {
        void AddScaled(std::vector<float>& destination, const std::vector<float>& source, const float gain)
        {
            if (destination.size() != source.size())
                throw std::logic_error("Audio mixer bus buffers have inconsistent dimensions.");
            for (std::size_t index = 0; index < destination.size(); ++index)
                destination[index] += source[index] * gain;
        }

        [[nodiscard]] float Parameter(const AudioMixerEffectDefinition& effect, const std::size_t index,
                                      const float fallback) noexcept
        {
            return index < effect.Parameters.size() ? effect.Parameters[index] : fallback;
        }

        void ApplyOnePoleFilter(std::vector<float>& samples, const std::uint32_t sampleRate,
                                const std::uint32_t channels, const float cutoff, const bool highPass)
        {
            constexpr float pi = 3.14159265358979323846F;
            const float clamped = std::clamp(cutoff, 10.0F, static_cast<float>(sampleRate) * 0.49F);
            const float coefficient = std::exp(-2.0F * pi * clamped / static_cast<float>(sampleRate));
            std::vector<float> previousOutput(channels);
            for (std::size_t index = 0; index < samples.size(); ++index)
            {
                const auto channel = index % channels;
                const float input = samples[index];
                const float low = (1.0F - coefficient) * input + coefficient * previousOutput[channel];
                samples[index] = highPass ? input - low : low;
                previousOutput[channel] = low;
            }
        }

        void ApplyDelay(std::vector<float>& samples, const std::uint32_t sampleRate, const std::uint32_t channels,
                        const std::span<const float> parameters, const bool chorus)
        {
            const float delayMilliseconds = std::clamp(parameters.empty() ? 80.0F : parameters[0], 1.0F, 2000.0F);
            const float feedback = std::clamp(parameters.size() > 1 ? parameters[1] : 0.25F, 0.0F, 0.99F);
            const float wet = std::clamp(parameters.size() > 2 ? parameters[2] : 0.25F, 0.0F, 1.0F);
            const auto baseDelay = std::max<std::size_t>(
                1, static_cast<std::size_t>(delayMilliseconds * static_cast<float>(sampleRate) / 1000.0F));
            const auto dry = samples;
            for (std::size_t frame = 0; frame < samples.size() / channels; ++frame)
            {
                auto delay = baseDelay;
                if (chorus)
                {
                    const float phase = static_cast<float>(frame) / static_cast<float>(sampleRate);
                    delay = std::max<std::size_t>(
                        1, static_cast<std::size_t>(static_cast<float>(baseDelay) *
                                                    (1.0F + 0.1F * std::sin(phase * 5.02654825F))));
                }
                for (std::uint32_t channel = 0; channel < channels; ++channel)
                {
                    const auto index = frame * channels + channel;
                    const float echo = frame >= delay ? samples[(frame - delay) * channels + channel] : 0.0F;
                    samples[index] = dry[index] * (1.0F - wet) + echo * wet;
                    if (frame >= delay)
                        samples[index] += echo * feedback;
                }
            }
        }

        void ApplyEffect(std::vector<float>& samples, const AudioMixerEffectDefinition& effect,
                         const std::uint32_t sampleRate, const std::uint32_t channels)
        {
            if (effect.Bypassed)
                return;
            switch (effect.Type)
            {
            case AudioGraphNodeType::Gain:
            case AudioGraphNodeType::Equalizer:
                for (auto& sample : samples)
                    sample *= Parameter(effect, 0, 1.0F);
                break;
            case AudioGraphNodeType::LowPass:
                ApplyOnePoleFilter(samples, sampleRate, channels, Parameter(effect, 0, 1000.0F), false);
                break;
            case AudioGraphNodeType::HighPass:
                ApplyOnePoleFilter(samples, sampleRate, channels, Parameter(effect, 0, 1000.0F), true);
                break;
            case AudioGraphNodeType::Compressor:
            {
                const float threshold = std::clamp(Parameter(effect, 0, 0.5F), 0.001F, 1.0F);
                const float ratio = std::clamp(Parameter(effect, 1, 4.0F), 1.0F, 100.0F);
                for (auto& sample : samples)
                {
                    const float magnitude = std::abs(sample);
                    if (magnitude > threshold)
                        sample = std::copysign(threshold + (magnitude - threshold) / ratio, sample);
                }
                break;
            }
            case AudioGraphNodeType::Limiter:
            {
                const float ceiling = std::clamp(Parameter(effect, 0, 0.98F), 0.001F, 1.0F);
                for (auto& sample : samples)
                    sample = std::clamp(sample, -ceiling, ceiling);
                break;
            }
            case AudioGraphNodeType::Gate:
            {
                const float threshold = std::clamp(Parameter(effect, 0, 0.01F), 0.0F, 1.0F);
                for (auto& sample : samples)
                    if (std::abs(sample) < threshold)
                        sample = 0.0F;
                break;
            }
            case AudioGraphNodeType::Delay:
                ApplyDelay(samples, sampleRate, channels, effect.Parameters, false);
                break;
            case AudioGraphNodeType::Chorus:
                ApplyDelay(samples, sampleRate, channels, effect.Parameters, true);
                break;
            case AudioGraphNodeType::Distortion:
            {
                const float drive = std::clamp(Parameter(effect, 0, 1.0F), 0.0F, 100.0F);
                const float normalization = std::tanh(std::max(drive, 1.0F));
                for (auto& sample : samples)
                    sample = std::tanh(sample * drive) / normalization;
                break;
            }
            case AudioGraphNodeType::AlgorithmicReverb:
                ApplyAlgorithmicReverb(samples, sampleRate, channels, effect.Parameters);
                break;
            case AudioGraphNodeType::ConvolutionReverb:
            {
                const auto taps = std::min<std::size_t>(effect.Parameters.size(), 256);
                if (taps == 0)
                    break;
                const auto dry = samples;
                for (std::size_t frame = 0; frame < samples.size() / channels; ++frame)
                    for (std::uint32_t channel = 0; channel < channels; ++channel)
                    {
                        float value = 0.0F;
                        for (std::size_t tap = 0; tap < taps && tap <= frame; ++tap)
                            value += dry[(frame - tap) * channels + channel] * effect.Parameters[tap];
                        samples[frame * channels + channel] = value;
                    }
                break;
            }
            case AudioGraphNodeType::Meter:
            case AudioGraphNodeType::Capture:
                break;
            case AudioGraphNodeType::Input:
            case AudioGraphNodeType::Output:
                throw std::logic_error("Input and output nodes cannot appear in an audio mixer effect rack.");
            }
        }

        [[nodiscard]] float Rms(const std::vector<float>& samples) noexcept
        {
            if (samples.empty())
                return 0.0F;
            double squareSum = 0.0;
            for (const auto sample : samples)
                squareSum += static_cast<double>(sample) * sample;
            return static_cast<float>(std::sqrt(squareSum / static_cast<double>(samples.size())));
        }
    } // namespace

    void ApplyAlgorithmicReverb(std::vector<float>& samples, const std::uint32_t sampleRate,
                                const std::uint32_t channels, const std::span<const float> parameters)
    {
        if (sampleRate == 0 || channels == 0 || samples.size() % channels != 0)
            throw std::invalid_argument("Algorithmic reverb buffer dimensions are invalid.");
        const float roomMilliseconds = std::clamp(parameters.empty() ? 68.0F : parameters[0], 5.0F, 500.0F);
        const float decay = std::clamp(parameters.size() > 1 ? parameters[1] : 0.55F, 0.0F, 0.97F);
        const float wet = std::clamp(parameters.size() > 2 ? parameters[2] : 0.3F, 0.0F, 1.0F);
        const auto dry = samples;
        constexpr std::array<float, 4> tapScales{1.0F, 0.73F, 0.51F, 0.37F};
        const auto frames = samples.size() / channels;
        for (std::size_t frame = 0; frame < frames; ++frame)
            for (std::uint32_t channel = 0; channel < channels; ++channel)
            {
                float reflected = 0.0F;
                for (std::size_t tap = 0; tap < tapScales.size(); ++tap)
                {
                    const auto delay =
                        std::max<std::size_t>(1, static_cast<std::size_t>(roomMilliseconds * tapScales[tap] *
                                                                          static_cast<float>(sampleRate) / 1000.0F) +
                                                     (channel + tap) % 3U);
                    if (frame < delay)
                        continue;
                    const auto delayedIndex = (frame - delay) * channels + channel;
                    reflected += samples[delayedIndex] * std::pow(decay, static_cast<float>(tap + 1));
                }
                samples[frame * channels + channel] =
                    dry[frame * channels + channel] * (1.0F - wet) + reflected * (wet / tapScales.size());
            }
    }

    AudioMixerProcessResult ProcessAudioMixer(const AudioMixerDefinition& definition,
                                              std::map<AssetId, std::vector<float>> busInputs,
                                              const std::uint32_t sampleRate, const std::uint32_t channels,
                                              const std::size_t maximumMeterReadings)
    {
        ValidateAudioMixer(definition);
        if (sampleRate < 8000 || channels == 0 || channels > 16 || maximumMeterReadings == 0)
            throw std::invalid_argument("Audio mixer processing specification is invalid.");
        std::size_t sampleCount = 0;
        for (const auto& [bus, input] : busInputs)
        {
            (void)bus;
            if (sampleCount == 0)
                sampleCount = input.size();
            if (input.size() != sampleCount || input.size() % channels != 0 ||
                !std::ranges::all_of(input, [](const float sample) { return std::isfinite(sample); }))
                throw std::invalid_argument("Audio mixer input buffers are invalid.");
        }

        std::map<AssetId, const AudioMixerBusDefinition*> buses;
        std::map<AssetId, std::size_t> incoming;
        std::set<AssetId> soloed;
        for (const auto& bus : definition.Buses)
        {
            buses.emplace(bus.Id, &bus);
            incoming.try_emplace(bus.Id, 0);
            busInputs.try_emplace(bus.Id, sampleCount, 0.0F);
            if (bus.Solo)
                soloed.insert(bus.Id);
        }
        for (const auto& bus : definition.Buses)
        {
            if (bus.Parent)
                ++incoming[bus.Parent];
            for (const auto& send : bus.Sends)
                ++incoming[send.DestinationBus];
        }
        std::set<AssetId> audibleWhenSoloed;
        for (const auto solo : soloed)
        {
            auto current = solo;
            while (current)
            {
                audibleWhenSoloed.insert(current);
                current = buses.at(current)->Parent;
            }
        }
        for (const auto& [id, bus] : buses)
        {
            (void)bus;
            auto current = id;
            while (current)
            {
                if (soloed.contains(current))
                {
                    audibleWhenSoloed.insert(id);
                    break;
                }
                current = buses.at(current)->Parent;
            }
        }

        std::deque<AssetId> ready;
        for (const auto& [id, count] : incoming)
            if (count == 0)
                ready.push_back(id);
        AudioMixerProcessResult result;
        while (!ready.empty())
        {
            const auto id = ready.front();
            ready.pop_front();
            const auto& bus = *buses.at(id);
            auto processed = busInputs.at(id);
            for (const auto& effect : bus.Effects)
                ApplyEffect(processed, effect, sampleRate, channels);

            float duckingGain = 1.0F;
            for (const auto& rule : definition.Ducking)
            {
                if (rule.TargetBus != id)
                    continue;
                const float sidechainRms = Rms(busInputs.at(rule.SidechainBus));
                const float threshold = std::pow(10.0F, rule.ThresholdDb / 20.0F);
                if (sidechainRms > threshold && threshold > 0.0F)
                {
                    const float overDb = 20.0F * std::log10(sidechainRms / threshold);
                    const float reduction = std::min(rule.MaximumAttenuationDb, overDb * (1.0F - 1.0F / rule.Ratio));
                    duckingGain *= std::pow(10.0F, -reduction / 20.0F);
                }
            }
            const bool audible = soloed.empty() || audibleWhenSoloed.contains(id);
            const float fader = bus.Mute || !audible ? 0.0F : bus.Gain * duckingGain;
            auto postFader = processed;
            for (auto& sample : postFader)
                sample *= fader;

            if (result.Meters.size() < maximumMeterReadings)
            {
                float peak = 0.0F;
                for (const auto sample : postFader)
                    peak = std::max(peak, std::abs(sample));
                result.Meters.push_back({.Bus = id, .Peak = peak, .Rms = Rms(postFader), .Clipping = peak > 1.0F});
            }
            else
            {
                ++result.DroppedMeterReadings;
            }

            const auto route = [&](const AssetId destination, const std::vector<float>& source, const float gain)
            {
                AddScaled(busInputs.at(destination), source, gain);
                if (--incoming[destination] == 0)
                    ready.push_back(destination);
            };
            if (bus.Parent)
                route(bus.Parent, postFader, 1.0F);
            for (const auto& send : bus.Sends)
                route(send.DestinationBus, send.Stage == AudioMixerSendStage::PreFader ? processed : postFader,
                      send.Gain);
            if (id == definition.MasterBus)
                result.Output = std::move(postFader);
        }
        if (result.Output.empty())
            result.Output.assign(sampleCount, 0.0F);
        std::ranges::sort(result.Meters, {}, &AudioMeterReading::Bus);
        return result;
    }
} // namespace Keire::Detail
