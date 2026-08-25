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
        constexpr std::size_t MaximumDirectConvolutionFrames = 128;

        [[nodiscard]] std::size_t ConvolutionPartitionFrames(const std::uint32_t sampleRate) noexcept
        {
            auto result = std::size_t{256};
            const auto target = std::max<std::size_t>(result, sampleRate / 50U);
            while (result < target)
                result *= 2U;
            return result;
        }

        void Transform(std::span<std::complex<float>> values, const bool inverse) noexcept
        {
            const auto count = values.size();
            for (std::size_t index = 1, reversed = 0; index < count; ++index)
            {
                auto bit = count >> 1U;
                for (; reversed & bit; bit >>= 1U)
                    reversed ^= bit;
                reversed ^= bit;
                if (index < reversed)
                    std::swap(values[index], values[reversed]);
            }
            constexpr float pi = 3.14159265358979323846F;
            for (std::size_t length = 2; length <= count; length <<= 1U)
            {
                const float angle = (inverse ? 2.0F : -2.0F) * pi / static_cast<float>(length);
                const std::complex<float> step(std::cos(angle), std::sin(angle));
                for (std::size_t start = 0; start < count; start += length)
                {
                    auto factor = std::complex<float>(1.0F, 0.0F);
                    for (std::size_t offset = 0; offset < length / 2U; ++offset)
                    {
                        const auto even = values[start + offset];
                        const auto odd = values[start + offset + length / 2U] * factor;
                        values[start + offset] = even + odd;
                        values[start + offset + length / 2U] = even - odd;
                        factor *= step;
                    }
                }
            }
            if (inverse)
                for (auto& value : values)
                    value /= static_cast<float>(count);
        }
    } // namespace

    PreparedAudioImpulseResponse PrepareAudioImpulseResponse(const AudioClipData& source,
                                                             const std::uint32_t sampleRate,
                                                             const std::uint32_t channels)
    {
        if (sampleRate < 8000 || sampleRate > 384000 || channels == 0 || channels > 16 || source.Streaming ||
            source.SampleRate < 8000 || source.SampleRate > 384000 || source.Channels == 0 || source.Channels > 8 ||
            source.Samples.empty() || source.Samples.size() % source.Channels != 0 ||
            !std::ranges::all_of(source.Samples, [](const float sample) { return std::isfinite(sample); }))
        {
            throw std::invalid_argument("Audio mixer impulse response must be resident finite PCM data.");
        }

        const auto sourceFrames = source.Samples.size() / source.Channels;
        const auto scaledFrames =
            static_cast<long double>(sourceFrames) * sampleRate / static_cast<long double>(source.SampleRate);
        const auto maximumConvertibleFrames = static_cast<long double>(
            std::min<std::size_t>(std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::int64_t>::max()));
        if (scaledFrames > maximumConvertibleFrames)
            throw std::invalid_argument("Audio mixer impulse response dimensions exceed the current platform.");
        const auto targetFrames = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(scaledFrames)));
        if (targetFrames > static_cast<std::size_t>(sampleRate) * MaximumAudioConvolutionSeconds ||
            targetFrames > MaximumAudioConvolutionChannelFrames / channels)
            throw std::invalid_argument(
                "Audio impulse response exceeds the five-second or channel-work convolution limit.");

        PreparedAudioImpulseResponse result{.Channels = channels};
        result.Samples.resize(targetFrames * channels);
        const auto sourceSample = [&](const std::size_t frame, const std::uint32_t channel)
        {
            if (source.Channels == 1)
                return source.Samples[frame];
            if (source.Channels == channels)
                return source.Samples[frame * source.Channels + channel];
            float mono = 0.0F;
            for (std::uint32_t sourceChannel = 0; sourceChannel < source.Channels; ++sourceChannel)
                mono += source.Samples[frame * source.Channels + sourceChannel];
            return mono / static_cast<float>(source.Channels);
        };
        for (std::size_t frame = 0; frame < targetFrames; ++frame)
        {
            const double sourcePosition = static_cast<double>(frame) * source.SampleRate / sampleRate;
            const auto first = std::min(static_cast<std::size_t>(sourcePosition), sourceFrames - 1U);
            const auto second = std::min(first + 1U, sourceFrames - 1U);
            const auto alpha = static_cast<float>(sourcePosition - static_cast<double>(first));
            for (std::uint32_t channel = 0; channel < channels; ++channel)
            {
                const auto firstSample = sourceSample(first, channel);
                result.Samples[frame * channels + channel] =
                    firstSample + (sourceSample(second, channel) - firstSample) * alpha;
            }
        }
        return result;
    }

    PreparedAudioImpulseResponse PrepareMonoAudioImpulseResponse(const std::span<const float> samples,
                                                                 const std::uint32_t channels)
    {
        if (channels == 0 || channels > 16)
            throw std::invalid_argument("Audio impulse response samples are invalid.");
        if (samples.size() > MaximumAudioConvolutionChannelFrames / channels)
            throw std::invalid_argument("Audio impulse response exceeds the channel-work convolution limit.");
        if (!std::ranges::all_of(samples, [](const float sample) { return std::isfinite(sample); }))
            throw std::invalid_argument("Audio impulse response samples are invalid.");
        PreparedAudioImpulseResponse result{.Channels = channels};
        result.Samples.reserve(samples.size() * channels);
        for (const auto sample : samples)
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                result.Samples.push_back(sample);
        return result;
    }

    AudioEffectProcessor::AudioEffectProcessor(const AudioGraphNodeType type, const std::uint32_t sampleRate,
                                               const std::uint32_t channels, const std::span<const float> parameters,
                                               const PreparedAudioImpulseResponse* impulseResponse)
        : m_Type(type), m_SampleRate(sampleRate), m_Channels(channels)
    {
        if (sampleRate < 8000 || sampleRate > 384000 || channels == 0 || channels > 16 ||
            !std::ranges::all_of(parameters, [](const float parameter) { return std::isfinite(parameter); }))
            throw std::invalid_argument("Audio effect processor specification is invalid.");
        UpdateParameters(parameters);
        m_CallbackParameters.Count = std::min(parameters.size(), m_CallbackParameters.Values.size());
        std::ranges::copy(parameters.first(m_CallbackParameters.Count), m_CallbackParameters.Values.begin());
        m_CallbackParameters.FilterCoefficient = m_ParameterState.FilterCoefficient.load(std::memory_order_relaxed);
        m_CallbackParameters.EqualizerHighCoefficient =
            m_ParameterState.EqualizerHighCoefficient.load(std::memory_order_relaxed);
        for (std::size_t band = 0; band < m_CallbackParameters.EqualizerGains.size(); ++band)
            m_CallbackParameters.EqualizerGains[band] =
                m_ParameterState.EqualizerGains[band].load(std::memory_order_relaxed);
        m_CallbackParameters.EqualizerOutputGain = m_ParameterState.EqualizerOutputGain.load(std::memory_order_relaxed);

        if (type == AudioGraphNodeType::LowPass || type == AudioGraphNodeType::HighPass)
            m_FilterState.resize(channels);
        else if (type == AudioGraphNodeType::Equalizer)
            m_FilterState.resize(static_cast<std::size_t>(channels) * 2U);
        else if (type == AudioGraphNodeType::Delay || type == AudioGraphNodeType::Chorus)
            m_Delay.resize((static_cast<std::size_t>(sampleRate) * 2U + 2U) * channels);
        else if (type == AudioGraphNodeType::AlgorithmicReverb)
            m_Delay.resize((static_cast<std::size_t>(sampleRate) / 2U + 2U) * channels);
        else if (type == AudioGraphNodeType::ConvolutionReverb && impulseResponse)
        {
            if (impulseResponse->Channels != channels || impulseResponse->Samples.empty() ||
                impulseResponse->Samples.size() % channels != 0)
                throw std::invalid_argument("Prepared audio impulse response dimensions are invalid.");
            const auto impulseFrames = impulseResponse->FrameCount();
            if (impulseFrames > static_cast<std::size_t>(sampleRate) * MaximumAudioConvolutionSeconds ||
                impulseFrames > MaximumAudioConvolutionChannelFrames / channels)
                throw std::invalid_argument(
                    "Audio impulse response exceeds the five-second or channel-work convolution limit.");
            if (!std::ranges::all_of(impulseResponse->Samples,
                                     [](const float sample) { return std::isfinite(sample); }))
                throw std::invalid_argument("Prepared audio impulse response dimensions are invalid.");
            m_ImpulseResponse = *impulseResponse;
            if (impulseFrames <= MaximumDirectConvolutionFrames)
            {
                m_ConvolutionHistory.resize(m_ImpulseResponse.Samples.size());
            }
            else
            {
                m_ConvolutionPartitionFrames = ConvolutionPartitionFrames(sampleRate);
                m_ConvolutionPartitionCount =
                    (impulseFrames + m_ConvolutionPartitionFrames - 1U) / m_ConvolutionPartitionFrames;
                const auto transformFrames = m_ConvolutionPartitionFrames * 2U;
                const auto spectrumBins = m_ConvolutionPartitionFrames + 1U;
                const auto spectrumValues =
                    static_cast<std::size_t>(channels) * m_ConvolutionPartitionCount * spectrumBins;
                m_ConvolutionInput.resize(m_ConvolutionPartitionFrames * channels);
                m_ConvolutionReady.resize(m_ConvolutionPartitionFrames * channels);
                m_ConvolutionOverlap.resize(m_ConvolutionPartitionFrames * channels);
                m_ConvolutionImpulseSpectra.resize(spectrumValues);
                m_ConvolutionInputSpectra.resize(spectrumValues);
                m_ConvolutionScratch.resize(transformFrames);
                for (std::uint32_t channel = 0; channel < channels; ++channel)
                    for (std::size_t partition = 0; partition < m_ConvolutionPartitionCount; ++partition)
                    {
                        std::ranges::fill(m_ConvolutionScratch, std::complex<float>{});
                        for (std::size_t frame = 0; frame < m_ConvolutionPartitionFrames; ++frame)
                        {
                            const auto impulseFrame = partition * m_ConvolutionPartitionFrames + frame;
                            if (impulseFrame >= impulseFrames)
                                break;
                            m_ConvolutionScratch[frame] = m_ImpulseResponse.Samples[impulseFrame * channels + channel];
                        }
                        Transform(m_ConvolutionScratch, false);
                        const auto destination =
                            (static_cast<std::size_t>(channel) * m_ConvolutionPartitionCount + partition) *
                            spectrumBins;
                        std::ranges::copy(m_ConvolutionScratch.begin(),
                                          m_ConvolutionScratch.begin() + static_cast<std::ptrdiff_t>(spectrumBins),
                                          m_ConvolutionImpulseSpectra.begin() +
                                              static_cast<std::ptrdiff_t>(destination));
                    }
            }
        }
    }

    void AudioEffectProcessor::UpdateParameters(const std::span<const float> parameters)
    {
        if (!std::ranges::all_of(parameters, [](const float parameter) { return std::isfinite(parameter); }))
            throw std::invalid_argument("Audio effect processor parameters are invalid.");
        const auto parameterCount = std::min(parameters.size(), m_ParameterState.Values.size());
        const auto parameter = [&](const std::size_t index, const float fallback)
        { return index < parameterCount ? parameters[index] : fallback; };
        constexpr float pi = 3.14159265358979323846F;
        const auto cutoffCoefficient = [&](const float cutoff)
        {
            const auto maximum = static_cast<float>(m_SampleRate) * 0.49F;
            return std::exp(-2.0F * pi * std::clamp(cutoff, 10.0F, maximum) / static_cast<float>(m_SampleRate));
        };
        auto filterCoefficient = 0.0F;
        auto equalizerHighCoefficient = 0.0F;
        std::array equalizerGains{1.0F, 1.0F, 1.0F};
        auto equalizerOutputGain = 1.0F;
        if (m_Type == AudioGraphNodeType::LowPass || m_Type == AudioGraphNodeType::HighPass)
        {
            filterCoefficient = cutoffCoefficient(parameter(0, 1000.0F));
        }
        else if (m_Type == AudioGraphNodeType::Equalizer)
        {
            auto lowCrossover = parameter(4, 250.0F);
            auto highCrossover = parameter(5, 4000.0F);
            if (lowCrossover > highCrossover)
                std::swap(lowCrossover, highCrossover);
            filterCoefficient = cutoffCoefficient(lowCrossover);
            equalizerHighCoefficient = cutoffCoefficient(highCrossover);
            for (std::size_t band = 0; band < equalizerGains.size(); ++band)
                equalizerGains[band] = std::pow(10.0F, std::clamp(parameter(band + 1U, 0.0F), -24.0F, 24.0F) / 20.0F);
            equalizerOutputGain = std::clamp(parameter(0, 1.0F), 0.0F, 16.0F);
        }

        m_ParameterState.Revision.fetch_add(1, std::memory_order_seq_cst);
        for (std::size_t index = 0; index < parameterCount; ++index)
            m_ParameterState.Values[index].store(parameters[index], std::memory_order_seq_cst);
        m_ParameterState.Count.store(parameterCount, std::memory_order_seq_cst);
        m_ParameterState.FilterCoefficient.store(filterCoefficient, std::memory_order_seq_cst);
        m_ParameterState.EqualizerHighCoefficient.store(equalizerHighCoefficient, std::memory_order_seq_cst);
        for (std::size_t band = 0; band < equalizerGains.size(); ++band)
            m_ParameterState.EqualizerGains[band].store(equalizerGains[band], std::memory_order_seq_cst);
        m_ParameterState.EqualizerOutputGain.store(equalizerOutputGain, std::memory_order_seq_cst);
        m_ParameterState.Revision.fetch_add(1, std::memory_order_seq_cst);
    }

    void AudioEffectProcessor::ProcessConvolutionBlock() noexcept
    {
        const auto transformFrames = m_ConvolutionPartitionFrames * 2U;
        const auto spectrumBins = m_ConvolutionPartitionFrames + 1U;
        for (std::uint32_t channel = 0; channel < m_Channels; ++channel)
        {
            std::ranges::fill(m_ConvolutionScratch, std::complex<float>{});
            for (std::size_t frame = 0; frame < m_ConvolutionPartitionFrames; ++frame)
                m_ConvolutionScratch[frame] = m_ConvolutionInput[frame * m_Channels + channel];
            Transform(m_ConvolutionScratch, false);
            const auto currentSpectrum =
                (static_cast<std::size_t>(channel) * m_ConvolutionPartitionCount + m_ConvolutionSpectrumCursor) *
                spectrumBins;
            std::ranges::copy(m_ConvolutionScratch.begin(),
                              m_ConvolutionScratch.begin() + static_cast<std::ptrdiff_t>(spectrumBins),
                              m_ConvolutionInputSpectra.begin() + static_cast<std::ptrdiff_t>(currentSpectrum));

            for (std::size_t bin = 0; bin < spectrumBins; ++bin)
            {
                auto sum = std::complex<float>{};
                for (std::size_t partition = 0; partition < m_ConvolutionPartitionCount; ++partition)
                {
                    const auto inputPartition =
                        (m_ConvolutionSpectrumCursor + m_ConvolutionPartitionCount - partition) %
                        m_ConvolutionPartitionCount;
                    const auto inputIndex =
                        (static_cast<std::size_t>(channel) * m_ConvolutionPartitionCount + inputPartition) *
                            spectrumBins +
                        bin;
                    const auto impulseIndex =
                        (static_cast<std::size_t>(channel) * m_ConvolutionPartitionCount + partition) * spectrumBins +
                        bin;
                    sum += m_ConvolutionInputSpectra[inputIndex] * m_ConvolutionImpulseSpectra[impulseIndex];
                }
                m_ConvolutionScratch[bin] = sum;
            }
            for (std::size_t bin = spectrumBins; bin < transformFrames; ++bin)
                m_ConvolutionScratch[bin] = std::conj(m_ConvolutionScratch[transformFrames - bin]);
            Transform(m_ConvolutionScratch, true);

            for (std::size_t frame = 0; frame < m_ConvolutionPartitionFrames; ++frame)
            {
                const auto index = frame * m_Channels + channel;
                m_ConvolutionReady[index] = m_ConvolutionScratch[frame].real() + m_ConvolutionOverlap[index];
                m_ConvolutionOverlap[index] = m_ConvolutionScratch[m_ConvolutionPartitionFrames + frame].real();
            }
        }
        m_ConvolutionSpectrumCursor = (m_ConvolutionSpectrumCursor + 1U) % m_ConvolutionPartitionCount;
    }

    void AudioEffectProcessor::Process(const std::span<const float> input, const std::span<float> output) noexcept
    {
        if (input.size() != output.size() || input.size() % m_Channels != 0)
            return;
        auto candidateParameters = m_CallbackParameters;
        for (std::size_t attempt = 0; attempt < 2; ++attempt)
        {
            const auto before = m_ParameterState.Revision.load(std::memory_order_seq_cst);
            candidateParameters.Count =
                std::min(m_ParameterState.Count.load(std::memory_order_seq_cst), candidateParameters.Values.size());
            for (std::size_t index = 0; index < candidateParameters.Count; ++index)
                candidateParameters.Values[index] = m_ParameterState.Values[index].load(std::memory_order_seq_cst);
            candidateParameters.FilterCoefficient = m_ParameterState.FilterCoefficient.load(std::memory_order_seq_cst);
            candidateParameters.EqualizerHighCoefficient =
                m_ParameterState.EqualizerHighCoefficient.load(std::memory_order_seq_cst);
            for (std::size_t band = 0; band < candidateParameters.EqualizerGains.size(); ++band)
                candidateParameters.EqualizerGains[band] =
                    m_ParameterState.EqualizerGains[band].load(std::memory_order_seq_cst);
            candidateParameters.EqualizerOutputGain =
                m_ParameterState.EqualizerOutputGain.load(std::memory_order_seq_cst);
            const auto after = m_ParameterState.Revision.load(std::memory_order_seq_cst);
            if (before == after && (after & 1U) == 0)
            {
                m_CallbackParameters = candidateParameters;
                break;
            }
        }
        const auto parameter = [&](const std::size_t index, const float fallback)
        { return index < m_CallbackParameters.Count ? m_CallbackParameters.Values[index] : fallback; };
        const auto frames = input.size() / m_Channels;

        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            for (std::uint32_t channel = 0; channel < m_Channels; ++channel)
            {
                const auto index = frame * m_Channels + channel;
                const float sample = input[index];
                float value = sample;
                switch (m_Type)
                {
                case AudioGraphNodeType::Gain:
                    value *= parameter(0, 1.0F);
                    break;
                case AudioGraphNodeType::Equalizer:
                {
                    auto& lowState = m_FilterState[channel];
                    auto& highState = m_FilterState[m_Channels + channel];
                    lowState = (1.0F - m_CallbackParameters.FilterCoefficient) * sample +
                               m_CallbackParameters.FilterCoefficient * lowState;
                    highState = (1.0F - m_CallbackParameters.EqualizerHighCoefficient) * sample +
                                m_CallbackParameters.EqualizerHighCoefficient * highState;
                    const float low = lowState;
                    const float high = sample - highState;
                    const float middle = highState - low;
                    value = (low * m_CallbackParameters.EqualizerGains[0] +
                             middle * m_CallbackParameters.EqualizerGains[1] +
                             high * m_CallbackParameters.EqualizerGains[2]) *
                            m_CallbackParameters.EqualizerOutputGain;
                    break;
                }
                case AudioGraphNodeType::LowPass:
                case AudioGraphNodeType::HighPass:
                {
                    const float low = (1.0F - m_CallbackParameters.FilterCoefficient) * sample +
                                      m_CallbackParameters.FilterCoefficient * m_FilterState[channel];
                    m_FilterState[channel] = low;
                    value = m_Type == AudioGraphNodeType::HighPass ? sample - low : low;
                    break;
                }
                case AudioGraphNodeType::Compressor:
                {
                    const float threshold = std::clamp(parameter(0, 0.5F), 0.001F, 1.0F);
                    const float ratio = std::clamp(parameter(1, 4.0F), 1.0F, 100.0F);
                    const float magnitude = std::abs(value);
                    if (magnitude > threshold)
                        value = std::copysign(threshold + (magnitude - threshold) / ratio, value);
                    break;
                }
                case AudioGraphNodeType::Limiter:
                {
                    const float ceiling = std::clamp(parameter(0, 0.98F), 0.001F, 1.0F);
                    value = std::clamp(value, -ceiling, ceiling);
                    break;
                }
                case AudioGraphNodeType::Gate:
                    if (std::abs(value) < std::clamp(parameter(0, 0.01F), 0.0F, 1.0F))
                        value = 0.0F;
                    break;
                case AudioGraphNodeType::Distortion:
                {
                    const float drive = std::clamp(parameter(0, 1.0F), 0.0F, 100.0F);
                    value = std::tanh(value * drive) / std::tanh(std::max(drive, 1.0F));
                    break;
                }
                case AudioGraphNodeType::Delay:
                case AudioGraphNodeType::Chorus:
                {
                    const float modulation = m_Type == AudioGraphNodeType::Chorus
                                                 ? 1.0F + 0.1F * static_cast<float>(std::sin(m_Phase))
                                                 : 1.0F;
                    const auto delayFrames = std::clamp<std::size_t>(
                        static_cast<std::size_t>(
                            std::clamp(parameter(0, m_Type == AudioGraphNodeType::Chorus ? 20.0F : 80.0F), 1.0F,
                                       2000.0F) *
                            static_cast<float>(m_SampleRate) / 1000.0F * modulation),
                        1, m_Delay.size() / m_Channels - 1U);
                    const auto delayedFrame =
                        (m_DelayCursor + m_Delay.size() / m_Channels - delayFrames) % (m_Delay.size() / m_Channels);
                    const float delayed = m_Delay[delayedFrame * m_Channels + channel];
                    const float feedback = std::clamp(parameter(1, 0.25F), 0.0F, 0.99F);
                    const float wet = std::clamp(parameter(2, 0.25F), 0.0F, 1.0F);
                    m_Delay[m_DelayCursor * m_Channels + channel] = sample + delayed * feedback;
                    value = sample * (1.0F - wet) + delayed * wet;
                    break;
                }
                case AudioGraphNodeType::AlgorithmicReverb:
                {
                    constexpr std::array tapScales{1.0F, 0.73F, 0.51F, 0.37F};
                    const float roomMilliseconds = std::clamp(parameter(0, 68.0F), 5.0F, 500.0F);
                    const float decay = std::clamp(parameter(1, 0.55F), 0.0F, 0.97F);
                    const float wet = std::clamp(parameter(2, 0.3F), 0.0F, 1.0F);
                    float reflected = 0.0F;
                    for (std::size_t tap = 0; tap < tapScales.size(); ++tap)
                    {
                        const auto delayFrames = std::clamp<std::size_t>(
                            static_cast<std::size_t>(roomMilliseconds * tapScales[tap] *
                                                     static_cast<float>(m_SampleRate) / 1000.0F) +
                                (channel + tap) % 3U,
                            1, m_Delay.size() / m_Channels - 1U);
                        const auto delayedFrame =
                            (m_DelayCursor + m_Delay.size() / m_Channels - delayFrames) % (m_Delay.size() / m_Channels);
                        reflected += m_Delay[delayedFrame * m_Channels + channel] *
                                     std::pow(decay, static_cast<float>(tap + 1U));
                    }
                    m_Delay[m_DelayCursor * m_Channels + channel] = sample + reflected;
                    value = sample * (1.0F - wet) + reflected * (wet / static_cast<float>(tapScales.size()));
                    break;
                }
                case AudioGraphNodeType::ConvolutionReverb:
                {
                    if (m_ImpulseResponse.Samples.empty())
                        break;
                    float convolved = 0.0F;
                    if (m_ConvolutionPartitionFrames != 0)
                    {
                        const auto blockIndex = m_ConvolutionInputFrames * m_Channels + channel;
                        convolved = m_ConvolutionReady[blockIndex];
                        m_ConvolutionInput[blockIndex] = sample;
                    }
                    else
                    {
                        const auto impulseFrames = m_ImpulseResponse.FrameCount();
                        convolved = sample * m_ImpulseResponse.Samples[channel];
                        for (std::size_t tap = 1; tap < impulseFrames; ++tap)
                        {
                            const auto historyFrame = (m_ConvolutionCursor + impulseFrames - tap) % impulseFrames;
                            convolved += m_ConvolutionHistory[historyFrame * m_Channels + channel] *
                                         m_ImpulseResponse.Samples[tap * m_Channels + channel];
                        }
                        m_ConvolutionHistory[m_ConvolutionCursor * m_Channels + channel] = sample;
                    }
                    const float wet = std::clamp(parameter(0, 1.0F), 0.0F, 1.0F);
                    value = sample * (1.0F - wet) + convolved * wet * std::clamp(parameter(1, 1.0F), 0.0F, 16.0F);
                    break;
                }
                case AudioGraphNodeType::Meter:
                case AudioGraphNodeType::Capture:
                case AudioGraphNodeType::Input:
                case AudioGraphNodeType::Output:
                    break;
                }
                output[index] = value;
            }
            if (!m_Delay.empty())
                m_DelayCursor = (m_DelayCursor + 1U) % (m_Delay.size() / m_Channels);
            if (!m_ConvolutionHistory.empty())
                m_ConvolutionCursor = (m_ConvolutionCursor + 1U) % m_ImpulseResponse.FrameCount();
            if (m_ConvolutionPartitionFrames != 0 && ++m_ConvolutionInputFrames == m_ConvolutionPartitionFrames)
            {
                ProcessConvolutionBlock();
                m_ConvolutionInputFrames = 0;
            }
            m_Phase += 5.0265482457 / static_cast<double>(m_SampleRate);
        }
    }

    namespace
    {
        void AddScaled(std::vector<float>& destination, const std::vector<float>& source, const float gain)
        {
            if (destination.size() != source.size())
                throw std::logic_error("Audio mixer bus buffers have inconsistent dimensions.");
            for (std::size_t index = 0; index < destination.size(); ++index)
                destination[index] += source[index] * gain;
        }

        void ApplyEffect(std::vector<float>& samples, const AudioMixerEffectDefinition& effect,
                         std::map<AssetId, AudioEffectProcessor>& effectProcessors)
        {
            if (effect.Bypassed)
                return;
            const auto processor = effectProcessors.find(effect.Id);
            if (processor == effectProcessors.end())
                throw std::logic_error("Audio mixer effect processor is unavailable.");
            processor->second.Process(samples);
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
        AudioEffectProcessor processor(AudioGraphNodeType::AlgorithmicReverb, sampleRate, channels, parameters);
        processor.Process(samples);
    }

    AudioMixerProcessResult ProcessAudioMixer(const AudioMixerDefinition& definition,
                                              std::map<AssetId, std::vector<float>> busInputs,
                                              std::map<AssetId, AudioEffectProcessor>& effectProcessors,
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
                ApplyEffect(processed, effect, effectProcessors);

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
