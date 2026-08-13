#pragma once

#include "Keire/Audio/AudioSystem.h"

#include "Keire/Audio/AudioAssets.h"
#include "KeireInternal/Audio/AudioMixerRuntime.h"
#include "KeireInternal/Audio/NativeAudioNodeContract.h"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    [[nodiscard]] inline std::string EncodeAudioDeviceId(const ma_device_id& id)
    {
        constexpr std::string_view digits = "0123456789abcdef";
        const auto* bytes = reinterpret_cast<const unsigned char*>(&id);
        std::string result(sizeof(id) * 2U, '0');
        for (std::size_t index = 0; index < sizeof(id); ++index)
        {
            result[index * 2U] = digits[bytes[index] >> 4U];
            result[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
        }
        return result;
    }

    class RuntimeServiceState final
    {
      public:
        explicit RuntimeServiceState(std::string name) : m_Name(std::move(name)) {}

        [[nodiscard]] bool IsOpen() const noexcept { return m_Open.load(std::memory_order_acquire); }

        void Close()
        {
            if (std::this_thread::get_id() != m_Owner)
                throw std::logic_error(m_Name + "::Close must run on the owner thread.");
            m_Open.store(false, std::memory_order_release);
        }

      private:
        std::string m_Name;
        std::thread::id m_Owner = std::this_thread::get_id();
        std::atomic<bool> m_Open{true};
    };

    [[nodiscard]] inline float VectorLength(const Vector3 value) noexcept
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
    }

    [[nodiscard]] inline float Dot(const Vector3 first, const Vector3 second) noexcept
    {
        return first.X * second.X + first.Y * second.Y + first.Z * second.Z;
    }

    [[nodiscard]] inline Vector3 NormalizeVector(const Vector3 value)
    {
        const auto length = VectorLength(value);
        if (!std::isfinite(length) || length <= 1.0e-6F)
            throw std::invalid_argument("Audio orientation vector must be finite and non-zero.");
        return {value.X / length, value.Y / length, value.Z / length};
    }

    inline void ValidateVoiceSpecification(const AudioVoiceSpecification& specification)
    {
        const auto attenuationKeys = specification.Attenuation.Keys();
        if (!specification.Clip || specification.Bus.empty() || specification.Bus.size() > 128 ||
            specification.Clip->SampleRate < 8000 || specification.Clip->SampleRate > 384000 ||
            specification.Clip->Channels == 0 || specification.Clip->Channels > 8 ||
            specification.Clip->Samples.size() % specification.Clip->Channels != 0 ||
            !std::ranges::all_of(specification.Clip->Samples, [](const float value) { return std::isfinite(value); }) ||
            !std::isfinite(specification.Gain) || specification.Gain < 0.0F || specification.Gain > 16.0F ||
            !std::isfinite(specification.Pitch) || specification.Pitch <= 0.01F || specification.Pitch > 8.0F ||
            !Math::IsFinite(specification.Position) || !Math::IsFinite(specification.Velocity) ||
            !std::isfinite(specification.MinimumDistance) || !std::isfinite(specification.MaximumDistance) ||
            specification.MinimumDistance < 0.0F || specification.MaximumDistance <= specification.MinimumDistance ||
            attenuationKeys.empty() || attenuationKeys.front().Time < 0.0F || attenuationKeys.back().Time > 1.0F ||
            !std::ranges::all_of(attenuationKeys,
                                 [](const CurveKey& key) { return key.Value >= 0.0F && key.Value <= 1.0F; }) ||
            !std::isfinite(specification.Occlusion) || specification.Occlusion < 0.0F || specification.Occlusion > 1.0F)
            throw std::invalid_argument("Audio voice specification is invalid.");
    }
} // namespace Keire::Detail

namespace Keire
{
    using Detail::Dot;
    using Detail::EncodeAudioDeviceId;
    using Detail::NormalizeVector;
    using Detail::RuntimeServiceState;
    using Detail::VectorLength;

    class AudioSystem::Impl final
    {
      public:
        struct MixerRoutingSnapshot final
        {
            AssetId MasterBus;
            AudioMixerDefinition Definition;
            std::map<AssetId, float> BusGains;
            std::map<AssetId, std::string> BusNamesById;
            std::map<std::string, AssetId, std::less<>> BusIdsByName;
        };

        struct RegisteredMixer final
        {
            AssetId Mixer;
            MixerRoutingSnapshot Routing;
        };

        struct NativeEffectNode final
        {
            ma_node_base Base{};
            AudioGraphNodeType Type = AudioGraphNodeType::Gain;
            std::uint32_t SampleRate = 48000;
            std::uint32_t Channels = 2;
            std::array<float, 64> Parameters{};
            std::size_t ParameterCount = 0;
            std::array<float, 16> FilterState{};
            std::vector<float> Delay;
            std::size_t DelayCursor = 0;
            double Phase = 0.0;
            AssetId MeterBus;
            std::atomic<float> MeterPeak{0.0F};
            std::atomic<float> MeterRms{0.0F};
            std::atomic<std::uint64_t> MeterRevision{0};
        };

        struct NativeDuckingNode final
        {
            ma_node_base Base{};
            std::uint32_t SampleRate = 48000;
            std::uint32_t Channels = 2;
            float ThresholdDb = -24.0F;
            float Ratio = 4.0F;
            float AttackSeconds = 0.01F;
            float HoldSeconds = 0.0F;
            float ReleaseSeconds = 0.1F;
            float MaximumAttenuationDb = 12.0F;
            float Gain = 1.0F;
            std::uint32_t HoldFrames = 0;
        };

        struct NativeMixerGraph final
        {
            ma_node_graph* Graph = nullptr;
            std::map<AssetId, std::unique_ptr<ma_sound_group>> Groups;
            std::vector<std::unique_ptr<NativeEffectNode>> Effects;
            std::vector<std::unique_ptr<NativeDuckingNode>> Ducking;
            std::vector<std::unique_ptr<ma_splitter_node>> Splitters;

            ~NativeMixerGraph()
            {
                for (auto iterator = Splitters.rbegin(); iterator != Splitters.rend(); ++iterator)
                    ma_splitter_node_uninit(iterator->get(), nullptr);
                for (auto iterator = Ducking.rbegin(); iterator != Ducking.rend(); ++iterator)
                    ma_node_uninit(reinterpret_cast<ma_node*>(iterator->get()), nullptr);
                for (auto iterator = Effects.rbegin(); iterator != Effects.rend(); ++iterator)
                    ma_node_uninit(reinterpret_cast<ma_node*>(iterator->get()), nullptr);
                for (auto iterator = Groups.rbegin(); iterator != Groups.rend(); ++iterator)
                    ma_sound_group_uninit(iterator->second.get());
            }
        };

        struct MixerRegistrationView final
        {
            AssetId Mixer;
            const MixerRoutingSnapshot* Routing = nullptr;
        };

        struct ResolvedVoiceBus final
        {
            AssetId Mixer;
            AssetId Bus;
            std::string_view Name;
            float Gain = 1.0F;
            bool Authored = false;
        };

        struct Voice final
        {
            struct Native final
            {
                ma_audio_buffer Buffer{};
                ma_decoder Decoder{};
                ma_sound Sound{};
                bool BufferOpen = false;
                bool DecoderOpen = false;
                bool SoundOpen = false;
            };
            AudioVoiceId Id;
            AudioVoiceSpecification Specification;
            double Frame = 0.0;
            bool Paused = false;
            bool Virtualized = false;
            std::unique_ptr<Native> Device;
        };

        explicit Impl(const AudioSystemSpecification specification)
            : Owner(std::this_thread::get_id()), SpecificationValue(specification), Mode(specification.Mode),
              MaximumVoices(specification.MaximumVoices), MaximumVirtualVoices(specification.MaximumVirtualVoices),
              MaximumMeterReadings(specification.MaximumMeterReadings), SampleRate(specification.MixSampleRate),
              Channels(AudioChannelCount(specification.OutputLayout))
        {
            auto config = ma_engine_config_init();
            config.noDevice = specification.Mode == AudioMode::Headless ? MA_TRUE : MA_FALSE;
            config.channels = Channels;
            config.sampleRate = SampleRate;
            config.periodSizeInFrames = specification.PeriodFrames;
            try
            {
                if (specification.Mode == AudioMode::Enabled)
                {
                    if (ma_context_init(nullptr, 0, nullptr, &Context) != MA_SUCCESS)
                        throw std::runtime_error("miniaudio playback context initialization failed.");
                    ContextOpen = true;
                    config.pContext = &Context;
                    ma_device_info* playbackDevices = nullptr;
                    ma_uint32 playbackDeviceCount = 0;
                    if (ma_context_get_devices(&Context, &playbackDevices, &playbackDeviceCount, nullptr, nullptr) !=
                        MA_SUCCESS)
                    {
                        throw std::runtime_error("Audio playback devices could not be enumerated.");
                    }
                    if (playbackDeviceCount > 0)
                    {
                        const auto defaultDevice =
                            std::find_if(playbackDevices, playbackDevices + playbackDeviceCount,
                                         [](const ma_device_info& device) { return device.isDefault == MA_TRUE; });
                        if (defaultDevice != playbackDevices + playbackDeviceCount)
                            PlaybackDeviceName = defaultDevice->name;
                        if (!specification.PlaybackDeviceId.empty())
                        {
                            const auto selected = std::find_if(
                                playbackDevices, playbackDevices + playbackDeviceCount,
                                [&](const ma_device_info& device)
                                { return EncodeAudioDeviceId(device.id) == specification.PlaybackDeviceId; });
                            if (selected == playbackDevices + playbackDeviceCount)
                            {
                                PlaybackDeviceFallback = true;
                            }
                            else
                            {
                                PlaybackDevice = selected->id;
                                PlaybackDeviceName = selected->name;
                                config.pPlaybackDeviceID = &PlaybackDevice;
                            }
                        }
                    }
                    else if (!specification.PlaybackDeviceId.empty())
                        PlaybackDeviceFallback = true;
                }
                const auto result = ma_engine_init(&config, &Engine);
                if (result != MA_SUCCESS)
                    throw std::runtime_error("miniaudio engine initialization failed with code " +
                                             std::to_string(static_cast<int>(result)) + ".");
                EngineOpen = true;
            }
            catch (...)
            {
                if (ContextOpen)
                {
                    ma_context_uninit(&Context);
                    ContextOpen = false;
                }
                throw;
            }
        }

        ~Impl()
        {
            for (auto& [id, voice] : Voices)
            {
                (void)id;
                DestroyVoice(voice);
            }
            NativeMixerRoutings.clear();
            if (EngineOpen)
                ma_engine_uninit(&Engine);
            if (ContextOpen)
                ma_context_uninit(&Context);
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error(std::string("AudioSystem::") + operation + " must run on the owner thread.");
            if (!State.IsOpen())
                throw std::logic_error("AudioSystem is closed.");
        }

        [[nodiscard]] static MixerRoutingSnapshot CompileMixer(const AudioMixerDefinition& definition)
        {
            ValidateAudioMixer(definition);

            MixerRoutingSnapshot result;
            result.MasterBus = definition.MasterBus;
            result.Definition = definition;
            std::map<AssetId, const AudioMixerBusDefinition*> buses;
            std::set<AssetId> soloed;
            for (const auto& bus : definition.Buses)
            {
                buses.emplace(bus.Id, &bus);
                result.BusNamesById.emplace(bus.Id, bus.Name);
                result.BusIdsByName.emplace(bus.Name, bus.Id);
                if (bus.Solo)
                    soloed.insert(bus.Id);
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

            for (const auto& [id, bus] : buses)
            {
                auto gain = 1.0;
                auto current = id;
                while (current)
                {
                    const auto* routedBus = buses.at(current);
                    if (routedBus->Mute)
                    {
                        gain = 0.0F;
                        break;
                    }
                    gain *= routedBus->Gain;
                    if (!std::isfinite(gain) || gain > std::numeric_limits<float>::max())
                        throw std::invalid_argument("Audio mixer effective bus gain exceeds the runtime range.");
                    current = routedBus->Parent;
                }
                if (!soloed.empty() && !audibleWhenSoloed.contains(id))
                    gain = 0.0;
                result.BusGains.emplace(id, static_cast<float>(gain));
            }
            return result;
        }

        static void ProcessNativeEffect(ma_node* base, const float** inputs, ma_uint32* inputFrames, float** outputs,
                                        ma_uint32* outputFrames)
        {
            auto& node = *reinterpret_cast<NativeEffectNode*>(base);
            const auto frames = std::min(*inputFrames, *outputFrames);
            const auto channels = static_cast<std::size_t>(node.Channels);
            const auto parameter = [&](const std::size_t index, const float fallback)
            { return index < node.ParameterCount ? node.Parameters[index] : fallback; };
            const auto cutoffCoefficient = [&](const float cutoff)
            {
                constexpr float pi = 3.14159265358979323846F;
                const auto maximum = static_cast<float>(node.SampleRate) * 0.49F;
                return std::exp(-2.0F * pi * std::clamp(cutoff, 10.0F, maximum) / static_cast<float>(node.SampleRate));
            };
            float meterPeak = 0.0F;
            double meterSquareSum = 0.0;
            for (std::size_t frame = 0; frame < frames; ++frame)
            {
                for (std::size_t channel = 0; channel < channels; ++channel)
                {
                    const auto index = frame * channels + channel;
                    const float input = inputs[0][index];
                    float value = input;
                    switch (node.Type)
                    {
                    case AudioGraphNodeType::Gain:
                    case AudioGraphNodeType::Equalizer:
                        value *= parameter(0, 1.0F);
                        break;
                    case AudioGraphNodeType::LowPass:
                    case AudioGraphNodeType::HighPass:
                    {
                        const float coefficient = cutoffCoefficient(parameter(0, 1000.0F));
                        const float low = (1.0F - coefficient) * input + coefficient * node.FilterState[channel];
                        node.FilterState[channel] = low;
                        value = node.Type == AudioGraphNodeType::HighPass ? input - low : low;
                        break;
                    }
                    case AudioGraphNodeType::Compressor:
                    {
                        const float threshold = std::clamp(parameter(0, 0.5F), 0.001F, 1.0F);
                        const float ratio = std::clamp(parameter(1, 4.0F), 1.0F, 100.0F);
                        if (std::abs(value) > threshold)
                            value = std::copysign(threshold + (std::abs(value) - threshold) / ratio, value);
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
                    case AudioGraphNodeType::AlgorithmicReverb:
                    case AudioGraphNodeType::ConvolutionReverb:
                    {
                        if (node.Type == AudioGraphNodeType::ConvolutionReverb && node.ParameterCount == 0)
                            break;
                        if (!node.Delay.empty())
                        {
                            const float milliseconds = node.Type == AudioGraphNodeType::AlgorithmicReverb
                                                           ? parameter(0, 68.0F)
                                                           : parameter(0, 80.0F);
                            const float modulation = node.Type == AudioGraphNodeType::Chorus
                                                         ? 1.0F + 0.1F * static_cast<float>(std::sin(node.Phase))
                                                         : 1.0F;
                            const auto delayFrames = std::clamp<std::size_t>(
                                static_cast<std::size_t>(milliseconds * static_cast<float>(node.SampleRate) / 1000.0F *
                                                         modulation),
                                1, node.Delay.size() / channels - 1U);
                            const auto delayedFrame = (node.DelayCursor + node.Delay.size() / channels - delayFrames) %
                                                      (node.Delay.size() / channels);
                            const float delayed = node.Delay[delayedFrame * channels + channel];
                            const float feedback = node.Type == AudioGraphNodeType::AlgorithmicReverb
                                                       ? std::clamp(parameter(1, 0.55F), 0.0F, 0.97F)
                                                       : std::clamp(parameter(1, 0.25F), 0.0F, 0.99F);
                            const float wet = node.Type == AudioGraphNodeType::AlgorithmicReverb
                                                  ? std::clamp(parameter(2, 0.3F), 0.0F, 1.0F)
                                                  : std::clamp(parameter(2, 0.25F), 0.0F, 1.0F);
                            node.Delay[node.DelayCursor * channels + channel] = input + delayed * feedback;
                            value = input * (1.0F - wet) + delayed * wet;
                        }
                        break;
                    }
                    case AudioGraphNodeType::Meter:
                    case AudioGraphNodeType::Capture:
                        break;
                    case AudioGraphNodeType::Input:
                    case AudioGraphNodeType::Output:
                        value = 0.0F;
                        break;
                    }
                    outputs[0][index] = value;
                    if (node.Type == AudioGraphNodeType::Meter)
                    {
                        meterPeak = std::max(meterPeak, std::abs(value));
                        meterSquareSum += static_cast<double>(value) * value;
                    }
                }
                if (!node.Delay.empty())
                    node.DelayCursor = (node.DelayCursor + 1U) % (node.Delay.size() / channels);
                node.Phase += 5.0265482457 / static_cast<double>(node.SampleRate);
            }
            if (node.Type == AudioGraphNodeType::Meter)
            {
                const auto sampleCount = static_cast<std::size_t>(frames) * channels;
                node.MeterPeak.store(meterPeak, std::memory_order_relaxed);
                node.MeterRms.store(
                    sampleCount == 0 ? 0.0F
                                     : static_cast<float>(std::sqrt(meterSquareSum / static_cast<double>(sampleCount))),
                    std::memory_order_relaxed);
                node.MeterRevision.fetch_add(1, std::memory_order_release);
            }
            *inputFrames = frames;
            *outputFrames = frames;
        }

        static void ProcessNativeDucking(ma_node* base, const float** inputs, ma_uint32* inputFrames, float** outputs,
                                         ma_uint32* outputFrames)
        {
            auto& node = *reinterpret_cast<NativeDuckingNode*>(base);
            Detail::NativeAudioNodeFrameCounts frameCounts(inputFrames, outputFrames);
            const auto frames = frameCounts.Capacity();
            const auto channels = static_cast<std::size_t>(node.Channels);
            const auto sampleRate = static_cast<float>(node.SampleRate);
            const float threshold = std::pow(10.0F, node.ThresholdDb / 20.0F);
            const auto smoothing = [sampleRate](const float seconds)
            { return seconds <= 0.0F ? 0.0F : std::exp(-1.0F / (seconds * sampleRate)); };
            const float attack = smoothing(node.AttackSeconds);
            const float release = smoothing(node.ReleaseSeconds);
            const auto holdFrames = static_cast<std::uint32_t>(node.HoldSeconds * sampleRate);
            for (std::size_t frame = 0; frame < frames; ++frame)
            {
                const auto index = frame * channels;
                float sidechain = 0.0F;
                for (std::size_t channel = 0; channel < channels; ++channel)
                    sidechain = std::max(sidechain, std::abs(inputs[1][index + channel]));
                float desired = 1.0F;
                if (sidechain > threshold && threshold > 0.0F)
                {
                    const float overDb = 20.0F * std::log10(sidechain / threshold);
                    const float reduction = std::min(node.MaximumAttenuationDb, overDb * (1.0F - 1.0F / node.Ratio));
                    desired = std::pow(10.0F, -reduction / 20.0F);
                }
                if (desired < node.Gain)
                {
                    node.Gain = desired + attack * (node.Gain - desired);
                    node.HoldFrames = holdFrames;
                }
                else if (node.HoldFrames > 0)
                {
                    --node.HoldFrames;
                }
                else
                {
                    node.Gain = desired + release * (node.Gain - desired);
                }
                for (std::size_t channel = 0; channel < channels; ++channel)
                    outputs[0][index + channel] = inputs[0][index + channel] * node.Gain;
            }
            frameCounts.Commit(frames);
        }

        [[nodiscard]] std::shared_ptr<NativeMixerGraph> BuildNativeMixer(const AudioMixerDefinition& definition)
        {
            if (!EngineOpen || Mode == AudioMode::Headless)
                return {};
            static const ma_node_vtable effectVtable{ProcessNativeEffect, nullptr, 1, 1, 0};
            static const ma_node_vtable duckingVtable{ProcessNativeDucking, nullptr, 2, 1, 0};
            auto result = std::make_shared<NativeMixerGraph>();
            result->Graph = ma_engine_get_node_graph(&Engine);
            for (const auto& bus : definition.Buses)
            {
                auto group = std::make_unique<ma_sound_group>();
                if (ma_sound_group_init(&Engine, MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT, nullptr, group.get()) !=
                    MA_SUCCESS)
                    throw std::runtime_error("Audio mixer bus node initialization failed.");
                result->Groups.emplace(bus.Id, std::move(group));
            }
            const auto makeEffect = [&](const AudioGraphNodeType type, const std::span<const float> parameters,
                                        const AssetId meterBus = AssetId{})
            {
                auto node = std::make_unique<NativeEffectNode>();
                node->Type = type;
                node->SampleRate = SampleRate;
                node->Channels = Channels;
                node->MeterBus = meterBus;
                node->ParameterCount = std::min(parameters.size(), node->Parameters.size());
                std::ranges::copy(parameters.first(node->ParameterCount), node->Parameters.begin());
                if (type == AudioGraphNodeType::Delay || type == AudioGraphNodeType::Chorus ||
                    type == AudioGraphNodeType::AlgorithmicReverb || type == AudioGraphNodeType::ConvolutionReverb)
                    node->Delay.resize(static_cast<std::size_t>(SampleRate) * Channels * 2U);
                const ma_uint32 channels = Channels;
                auto configuration = ma_node_config_init();
                configuration.vtable = &effectVtable;
                configuration.pInputChannels = &channels;
                configuration.pOutputChannels = &channels;
                if (ma_node_init(result->Graph, &configuration, nullptr, reinterpret_cast<ma_node*>(node.get())) !=
                    MA_SUCCESS)
                    throw std::runtime_error("Audio mixer effect node initialization failed.");
                auto* base = reinterpret_cast<ma_node*>(node.get());
                result->Effects.push_back(std::move(node));
                return base;
            };
            const auto makeSplitter = [&]
            {
                auto splitter = std::make_unique<ma_splitter_node>();
                const auto configuration = ma_splitter_node_config_init(Channels);
                if (ma_splitter_node_init(result->Graph, &configuration, nullptr, splitter.get()) != MA_SUCCESS)
                    throw std::runtime_error("Audio mixer send splitter initialization failed.");
                auto* base = reinterpret_cast<ma_node*>(splitter.get());
                result->Splitters.push_back(std::move(splitter));
                return base;
            };
            const auto makeDucking = [&](const AudioMixerDuckingDefinition& definition)
            {
                auto node = std::make_unique<NativeDuckingNode>();
                node->SampleRate = SampleRate;
                node->Channels = Channels;
                node->ThresholdDb = definition.ThresholdDb;
                node->Ratio = definition.Ratio;
                node->AttackSeconds = definition.AttackSeconds;
                node->HoldSeconds = definition.HoldSeconds;
                node->ReleaseSeconds = definition.ReleaseSeconds;
                node->MaximumAttenuationDb = definition.MaximumAttenuationDb;
                const std::array<ma_uint32, 2> inputChannels{Channels, Channels};
                const ma_uint32 outputChannels = Channels;
                auto configuration = ma_node_config_init();
                configuration.vtable = &duckingVtable;
                configuration.pInputChannels = inputChannels.data();
                configuration.pOutputChannels = &outputChannels;
                if (ma_node_init(result->Graph, &configuration, nullptr, reinterpret_cast<ma_node*>(node.get())) !=
                    MA_SUCCESS)
                    throw std::runtime_error("Audio mixer ducking node initialization failed.");
                auto* base = reinterpret_cast<ma_node*>(node.get());
                result->Ducking.push_back(std::move(node));
                return base;
            };
            const auto attach =
                [](ma_node* source, const ma_uint32 output, ma_node* destination, const ma_uint32 input = 0)
            {
                if (ma_node_attach_output_bus(source, output, destination, input) != MA_SUCCESS)
                    throw std::runtime_error("Audio mixer graph connection failed.");
            };
            std::set<AssetId> soloed;
            for (const auto& bus : definition.Buses)
                if (bus.Solo)
                    soloed.insert(bus.Id);
            std::set<AssetId> audibleWhenSoloed;
            for (const auto solo : soloed)
            {
                auto current = solo;
                while (current)
                {
                    audibleWhenSoloed.insert(current);
                    const auto found = std::ranges::find(definition.Buses, current, &AudioMixerBusDefinition::Id);
                    current = found == definition.Buses.end() ? AssetId{} : found->Parent;
                }
            }
            struct DuckingConnection final
            {
                AssetId Sidechain;
                ma_node* Node = nullptr;
            };
            std::vector<DuckingConnection> duckingConnections;
            std::map<AssetId, ma_node*> busOutputs;
            for (const auto& bus : definition.Buses)
            {
                auto current = bus.Id;
                while (current)
                {
                    if (soloed.contains(current))
                    {
                        audibleWhenSoloed.insert(bus.Id);
                        break;
                    }
                    const auto found = std::ranges::find(definition.Buses, current, &AudioMixerBusDefinition::Id);
                    current = found == definition.Buses.end() ? AssetId{} : found->Parent;
                }
            }
            for (const auto& bus : definition.Buses)
            {
                auto* current = reinterpret_cast<ma_node*>(result->Groups.at(bus.Id).get());
                for (const auto& effect : bus.Effects)
                {
                    if (effect.Bypassed)
                        continue;
                    auto* next = makeEffect(effect.Type, effect.Parameters,
                                            effect.Type == AudioGraphNodeType::Meter ? bus.Id : AssetId{});
                    attach(current, 0, next);
                    current = next;
                }
                for (const auto& send : bus.Sends)
                {
                    if (send.Stage != AudioMixerSendStage::PreFader)
                        continue;
                    auto* splitter = makeSplitter();
                    attach(current, 0, splitter);
                    attach(splitter, 1, reinterpret_cast<ma_node*>(result->Groups.at(send.DestinationBus).get()));
                    (void)ma_node_set_output_bus_volume(splitter, 1, send.Gain);
                    current = splitter;
                }
                const std::array fader{bus.Mute || (!soloed.empty() && !audibleWhenSoloed.contains(bus.Id)) ? 0.0F
                                                                                                            : bus.Gain};
                auto* faderNode = makeEffect(AudioGraphNodeType::Gain, fader);
                attach(current, 0, faderNode);
                current = faderNode;
                for (const auto& ducking : definition.Ducking)
                {
                    if (ducking.TargetBus != bus.Id)
                        continue;
                    auto* duckingNode = makeDucking(ducking);
                    attach(current, 0, duckingNode);
                    current = duckingNode;
                    duckingConnections.push_back({ducking.SidechainBus, duckingNode});
                }
                auto* meterNode = makeEffect(AudioGraphNodeType::Meter, std::span<const float>{}, bus.Id);
                attach(current, 0, meterNode);
                current = meterNode;
                for (const auto& send : bus.Sends)
                {
                    if (send.Stage != AudioMixerSendStage::PostFader)
                        continue;
                    auto* splitter = makeSplitter();
                    attach(current, 0, splitter);
                    attach(splitter, 1, reinterpret_cast<ma_node*>(result->Groups.at(send.DestinationBus).get()));
                    (void)ma_node_set_output_bus_volume(splitter, 1, send.Gain);
                    current = splitter;
                }
                busOutputs.emplace(bus.Id, current);
            }
            for (const auto& connection : duckingConnections)
            {
                auto* splitter = makeSplitter();
                attach(busOutputs.at(connection.Sidechain), 0, splitter);
                attach(splitter, 1, connection.Node, 1);
                busOutputs.at(connection.Sidechain) = splitter;
            }
            for (const auto& bus : definition.Buses)
            {
                auto* destination = bus.Parent ? reinterpret_cast<ma_node*>(result->Groups.at(bus.Parent).get())
                                               : ma_node_graph_get_endpoint(result->Graph);
                attach(busOutputs.at(bus.Id), 0, destination);
            }
            return result;
        }

        [[nodiscard]] float LegacyBusGain(const std::string_view bus) const noexcept
        {
            const auto master = BusGains.find("Master");
            const auto selected = BusGains.find(bus);
            const auto masterGain = master == BusGains.end() ? 1.0F : master->second;
            if (bus == "Master")
                return masterGain;
            return masterGain * (selected == BusGains.end() ? 1.0F : selected->second);
        }

        [[nodiscard]] MixerRegistrationView
        FindMixerRegistration(const AudioVoiceSpecification& specification) const noexcept
        {
            if (specification.MixerRouting)
            {
                const auto registered = MixerRoutings.find(specification.MixerRouting);
                if (registered == MixerRoutings.end() ||
                    (specification.Mixer && registered->second.Mixer != specification.Mixer))
                    return {};
                return {.Mixer = registered->second.Mixer, .Routing = &registered->second.Routing};
            }
            const auto mixer = Mixers.find(specification.Mixer);
            if (!specification.Mixer || mixer == Mixers.end())
                return {};
            return {.Mixer = mixer->first, .Routing = &mixer->second};
        }

        [[nodiscard]] ResolvedVoiceBus ResolveVoiceBus(const AudioVoiceSpecification& specification) const noexcept
        {
            const auto registration = FindMixerRegistration(specification);
            if (registration.Routing)
            {
                const auto& routing = *registration.Routing;
                auto bus = specification.BusId;
                if (!bus || !routing.BusGains.contains(bus))
                {
                    const auto named = routing.BusIdsByName.find(specification.Bus);
                    bus = named == routing.BusIdsByName.end() ? routing.MasterBus : named->second;
                }
                const auto gain = routing.BusGains.find(bus);
                const auto name = routing.BusNamesById.find(bus);
                return {.Mixer = registration.Mixer,
                        .Bus = bus,
                        .Name = name == routing.BusNamesById.end() ? std::string_view{} : name->second,
                        .Gain = gain == routing.BusGains.end() ? 1.0F : gain->second,
                        .Authored = true};
            }
            return {.Name = specification.Bus, .Gain = LegacyBusGain(specification.Bus)};
        }

        [[nodiscard]] float VoiceBusGain(const AudioVoiceSpecification& specification) const noexcept
        {
            const auto resolved = ResolveVoiceBus(specification);
            if (!resolved.Authored)
                return resolved.Gain;
            const auto master = BusGains.find("Master");
            const auto bus = resolved.Name == "Master" ? BusGains.end() : BusGains.find(resolved.Name);
            const auto gain = static_cast<double>(resolved.Gain) *
                              (master == BusGains.end() ? 1.0 : static_cast<double>(master->second)) *
                              (bus == BusGains.end() ? 1.0 : static_cast<double>(bus->second));
            return static_cast<float>(std::min(gain, static_cast<double>(std::numeric_limits<float>::max())));
        }

        [[nodiscard]] float VoiceGain(const AudioVoiceSpecification& specification) const noexcept
        {
            if (specification.Gain == 0.0F || Listener.Gain == 0.0F)
                return 0.0F;
            const auto gain = static_cast<double>(specification.Gain) * Listener.Gain * VoiceBusGain(specification) *
                              (1.0 - static_cast<double>(specification.Occlusion) * 0.75);
            return static_cast<float>(std::min(gain, static_cast<double>(std::numeric_limits<float>::max())));
        }

        [[nodiscard]] float SpatialDistanceGain(const AudioVoiceSpecification& specification) const
        {
            if (!specification.Spatial)
                return 1.0F;
            const Vector3 delta{specification.Position.X - Listener.Position.X,
                                specification.Position.Y - Listener.Position.Y,
                                specification.Position.Z - Listener.Position.Z};
            const auto distance = VectorLength(delta);
            if (distance <= specification.MinimumDistance)
                return 1.0F;
            if (distance >= specification.MaximumDistance)
                return 0.0F;
            const auto inverse =
                specification.MinimumDistance / std::max(distance, std::max(specification.MinimumDistance, 0.0001F));
            const auto normalizedDistance =
                std::clamp((distance - specification.MinimumDistance) /
                               (specification.MaximumDistance - specification.MinimumDistance),
                           0.0F, 1.0F);
            const auto authored = std::clamp(specification.Attenuation.Evaluate(normalizedDistance), 0.0F, 1.0F);
            return inverse * authored;
        }

        [[nodiscard]] float AudibilityGain(const AudioVoiceSpecification& specification) const
        {
            return VoiceGain(specification) * SpatialDistanceGain(specification);
        }

        [[nodiscard]] float AuthoredVoiceSourceGain(const AudioVoiceSpecification& specification,
                                                    const ResolvedVoiceBus& resolved) const noexcept
        {
            const auto master = BusGains.find("Master");
            const auto bus = resolved.Name == "Master" ? BusGains.end() : BusGains.find(resolved.Name);
            const auto gain = static_cast<double>(specification.Gain) * Listener.Gain *
                              (1.0 - static_cast<double>(specification.Occlusion) * 0.75) *
                              (master == BusGains.end() ? 1.0 : static_cast<double>(master->second)) *
                              (bus == BusGains.end() ? 1.0 : static_cast<double>(bus->second));
            return static_cast<float>(std::min(gain, static_cast<double>(std::numeric_limits<float>::max())));
        }

        [[nodiscard]] float DeviceVoiceGain(const AudioVoiceSpecification& specification) const
        {
            const auto resolved = ResolveVoiceBus(specification);
            const auto sourceGain = resolved.Authored && specification.MixerRouting &&
                                            NativeMixerRoutings.contains(specification.MixerRouting)
                                        ? AuthoredVoiceSourceGain(specification, resolved)
                                        : VoiceGain(specification);
            return sourceGain * SpatialDistanceGain(specification);
        }

        void AttachDeviceMixer(Voice& voice)
        {
            if (!voice.Device || !voice.Device->SoundOpen || !voice.Specification.MixerRouting)
                return;
            const auto native = NativeMixerRoutings.find(voice.Specification.MixerRouting);
            if (native == NativeMixerRoutings.end() || !native->second)
                return;
            const auto resolved = ResolveVoiceBus(voice.Specification);
            const auto group = native->second->Groups.find(resolved.Bus);
            if (group == native->second->Groups.end() ||
                ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&voice.Device->Sound), 0,
                                          reinterpret_cast<ma_node*>(group->second.get()), 0) != MA_SUCCESS)
                throw std::runtime_error("Audio voice could not attach to its mixer bus.");
        }

        void DestroyVoice(Voice& voice) noexcept
        {
            if (!voice.Device)
                return;
            if (voice.Device->SoundOpen)
            {
                ma_sound_uninit(&voice.Device->Sound);
                voice.Device->SoundOpen = false;
            }
            if (voice.Device->BufferOpen)
            {
                ma_audio_buffer_uninit(&voice.Device->Buffer);
                voice.Device->BufferOpen = false;
            }
            if (voice.Device->DecoderOpen)
            {
                ma_decoder_uninit(&voice.Device->Decoder);
                voice.Device->DecoderOpen = false;
            }
        }

        void UpdateVirtualization()
        {
            std::vector<Voice*> ranked;
            ranked.reserve(Voices.size());
            for (auto& [id, voice] : Voices)
            {
                (void)id;
                const auto gain = AudibilityGain(voice.Specification);
                voice.Virtualized = voice.Paused || gain <= 0.0F;
                if (!voice.Virtualized)
                    ranked.push_back(&voice);
                else if (voice.Device && voice.Device->SoundOpen)
                    ma_sound_set_volume(&voice.Device->Sound, 0.0F);
            }
            std::ranges::sort(ranked,
                              [](const Voice* first, const Voice* second)
                              {
                                  if (first->Specification.Priority != second->Specification.Priority)
                                      return first->Specification.Priority > second->Specification.Priority;
                                  return first->Id < second->Id;
                              });
            for (std::size_t index = 0; index < ranked.size(); ++index)
            {
                auto& voice = *ranked[index];
                voice.Virtualized = index >= MaximumVoices;
                if (voice.Device && voice.Device->SoundOpen)
                {
                    const auto gain = voice.Virtualized ? 0.0F : DeviceVoiceGain(voice.Specification);
                    ma_sound_set_volume(&voice.Device->Sound, gain);
                }
            }
        }

        void ApplyDeviceProperties(Voice& voice)
        {
            if (!voice.Device || !voice.Device->SoundOpen)
                return;
            const auto& specification = voice.Specification;
            ma_sound_set_looping(&voice.Device->Sound, specification.Loop ? MA_TRUE : MA_FALSE);
            ma_sound_set_pitch(&voice.Device->Sound, specification.Pitch);
            ma_sound_set_spatialization_enabled(&voice.Device->Sound, specification.Spatial ? MA_TRUE : MA_FALSE);
            ma_sound_set_rolloff(&voice.Device->Sound, 0.0F);
            ma_sound_set_position(&voice.Device->Sound, specification.Position.X, specification.Position.Y,
                                  specification.Position.Z);
            ma_sound_set_velocity(&voice.Device->Sound, specification.Velocity.X, specification.Velocity.Y,
                                  specification.Velocity.Z);
            ma_sound_set_min_distance(&voice.Device->Sound, specification.MinimumDistance);
            ma_sound_set_max_distance(&voice.Device->Sound, specification.MaximumDistance);
            ma_sound_set_volume(&voice.Device->Sound, voice.Virtualized ? 0.0F : DeviceVoiceGain(specification));
        }

        RuntimeServiceState State{"AudioSystem"};
        std::thread::id Owner;
        AudioSystemSpecification SpecificationValue;
        AudioMode Mode;
        std::uint32_t MaximumVoices;
        std::uint32_t MaximumVirtualVoices;
        std::uint32_t MaximumMeterReadings;
        std::uint32_t SampleRate;
        std::uint32_t Channels;
        ma_context Context{};
        ma_device_id PlaybackDevice{};
        std::string PlaybackDeviceName = "Headless";
        bool PlaybackDeviceFallback = false;
        bool ContextOpen = false;
        ma_engine Engine{};
        bool EngineOpen = false;
        mutable std::mutex Mutex;
        std::shared_ptr<const AudioGraphSnapshot> Graph;
        std::atomic<std::uint64_t> Revision{0};
        std::map<AudioVoiceId, Voice> Voices;
        AudioListenerState Listener;
        std::map<AssetId, MixerRoutingSnapshot> Mixers;
        std::map<AudioMixerRoutingId, RegisteredMixer> MixerRoutings;
        std::map<AudioMixerRoutingId, std::shared_ptr<NativeMixerGraph>> NativeMixerRoutings;
        std::map<std::string, float, std::less<>> BusGains{{"Master", 1.0F}};
        std::map<std::string, float, std::less<>> SnapshotStart;
        std::map<std::string, float, std::less<>> SnapshotTarget;
        std::chrono::duration<float> SnapshotElapsed{};
        std::chrono::duration<float> SnapshotDuration{};
        std::uint64_t SnapshotRevision = 0;
        std::uint64_t NextVoice = 1;
        std::uint64_t NextMixerRouting = 1;
        std::uint64_t RenderedFrames = 0;
        std::uint64_t Underruns = 0;
        AudioMeterSnapshot Meters;
        std::uint64_t NativeMeterSnapshotRevision = 0;
    };
} // namespace Keire
