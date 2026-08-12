#include "Keire/Audio/AudioSystem.h"

#include "Keire/Audio/AudioAssets.h"
#include "KeireInternal/Audio/AudioMixerRuntime.h"
#include "KeireInternal/Audio/NativeAudioNodeContract.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace Keire
{
    std::uint32_t AudioChannelCount(const AudioChannelLayout layout) noexcept
    {
        switch (layout)
        {
        case AudioChannelLayout::Mono:
            return 1;
        case AudioChannelLayout::Stereo:
            return 2;
        case AudioChannelLayout::Surround51:
            return 6;
        case AudioChannelLayout::Surround71:
            return 8;
        }
        return 0;
    }

    float DecibelsToLinear(const float decibels) noexcept
    {
        if (!std::isfinite(decibels) || decibels <= -96.0F)
            return 0.0F;
        return std::pow(10.0F, decibels / 20.0F);
    }

    float LinearToDecibels(const float gain) noexcept
    {
        if (!std::isfinite(gain) || gain <= 0.0F)
            return -96.0F;
        return std::max(-96.0F, 20.0F * std::log10(gain));
    }

    namespace
    {
        [[nodiscard]] std::string EncodeAudioDeviceId(const ma_device_id& id)
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

        [[nodiscard]] float VectorLength(const Vector3 value) noexcept
        {
            return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        }

        [[nodiscard]] float Dot(const Vector3 first, const Vector3 second) noexcept
        {
            return first.X * second.X + first.Y * second.Y + first.Z * second.Z;
        }

        [[nodiscard]] Vector3 NormalizeVector(const Vector3 value)
        {
            const auto length = VectorLength(value);
            if (!std::isfinite(length) || length <= 1.0e-6F)
                throw std::invalid_argument("Audio orientation vector must be finite and non-zero.");
            return {value.X / length, value.Y / length, value.Z / length};
        }

        void ValidateVoiceSpecification(const AudioVoiceSpecification& specification)
        {
            const auto attenuationKeys = specification.Attenuation.Keys();
            if (!specification.Clip || specification.Bus.empty() || specification.Bus.size() > 128 ||
                specification.Clip->SampleRate < 8000 || specification.Clip->SampleRate > 384000 ||
                specification.Clip->Channels == 0 || specification.Clip->Channels > 8 ||
                specification.Clip->Samples.size() % specification.Clip->Channels != 0 ||
                !std::ranges::all_of(specification.Clip->Samples,
                                     [](const float value) { return std::isfinite(value); }) ||
                !std::isfinite(specification.Gain) || specification.Gain < 0.0F || specification.Gain > 16.0F ||
                !std::isfinite(specification.Pitch) || specification.Pitch <= 0.01F || specification.Pitch > 8.0F ||
                !Math::IsFinite(specification.Position) || !Math::IsFinite(specification.Velocity) ||
                !std::isfinite(specification.MinimumDistance) || !std::isfinite(specification.MaximumDistance) ||
                specification.MinimumDistance < 0.0F ||
                specification.MaximumDistance <= specification.MinimumDistance || attenuationKeys.empty() ||
                attenuationKeys.front().Time < 0.0F || attenuationKeys.back().Time > 1.0F ||
                !std::ranges::all_of(attenuationKeys,
                                     [](const CurveKey& key) { return key.Value >= 0.0F && key.Value <= 1.0F; }) ||
                !std::isfinite(specification.Occlusion) || specification.Occlusion < 0.0F ||
                specification.Occlusion > 1.0F)
                throw std::invalid_argument("Audio voice specification is invalid.");
        }
    } // namespace

    std::vector<AudioDeviceInfo> EnumerateAudioPlaybackDevices()
    {
        ma_context context{};
        const auto initialized = ma_context_init(nullptr, 0, nullptr, &context);
        if (initialized != MA_SUCCESS)
            throw std::runtime_error("Audio device enumeration could not initialize the platform context.");
        try
        {
            ma_device_info* playbackDevices = nullptr;
            ma_uint32 playbackDeviceCount = 0;
            if (ma_context_get_devices(&context, &playbackDevices, &playbackDeviceCount, nullptr, nullptr) !=
                MA_SUCCESS)
            {
                throw std::runtime_error("Audio playback devices could not be enumerated.");
            }
            std::vector<AudioDeviceInfo> result;
            result.reserve(playbackDeviceCount);
            for (ma_uint32 index = 0; index < playbackDeviceCount; ++index)
            {
                const auto& device = playbackDevices[index];
                result.push_back({EncodeAudioDeviceId(device.id), device.name, device.isDefault == MA_TRUE});
            }
            ma_context_uninit(&context);
            return result;
        }
        catch (...)
        {
            ma_context_uninit(&context);
            throw;
        }
    }

    std::shared_ptr<const AudioClipData> LoadAudioClipData(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
            throw std::runtime_error("Audio clip could not be opened: " + path.string());
        const auto length = stream.tellg();
        if (length <= 0 || length > static_cast<std::streamoff>(768ULL * 1024ULL * 1024ULL))
            throw std::runtime_error("Audio clip is empty or exceeds the 768 MiB import limit.");
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!stream)
            throw std::runtime_error("Audio clip could not be read completely: " + path.string());

        ma_decoder decoder{};
        auto configuration = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_memory(bytes.data(), bytes.size(), &configuration, &decoder) != MA_SUCCESS)
            throw std::runtime_error("Audio clip format is unsupported or corrupt: " + path.string());
        const auto close = [&decoder] { ma_decoder_uninit(&decoder); };
        ma_format format = ma_format_unknown;
        ma_uint32 channels = 0;
        ma_uint32 sampleRate = 0;
        if (ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0) != MA_SUCCESS ||
            format != ma_format_f32 || channels == 0 || channels > 8 || sampleRate < 8000 || sampleRate > 384000)
        {
            close();
            throw std::runtime_error("Audio clip exposes an unsupported channel or sample-rate layout.");
        }
        ma_uint64 frames = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) != MA_SUCCESS || frames == 0 ||
            frames > 384000ULL * 60ULL * 60ULL * 4ULL)
        {
            close();
            throw std::runtime_error("Audio clip duration is unavailable or exceeds four hours.");
        }
        auto clip = std::make_shared<AudioClipData>();
        clip->SampleRate = sampleRate;
        clip->Channels = channels;
        clip->Frames = frames;
        if (frames > std::numeric_limits<std::size_t>::max() / channels)
        {
            close();
            throw std::runtime_error("Audio clip dimensions exceed the current platform.");
        }
        const auto sampleCount = static_cast<std::size_t>(frames) * channels;
        if (sampleCount > std::numeric_limits<std::size_t>::max() / sizeof(float))
        {
            close();
            throw std::runtime_error("Audio clip decoded size exceeds the current platform.");
        }
        if (sampleCount * sizeof(float) > 64ULL * 1024ULL * 1024ULL)
        {
            close();
            clip->Streaming = true;
            clip->EncodedSource = std::move(bytes);
            return clip;
        }
        clip->Samples.resize(sampleCount);
        ma_uint64 decoded = 0;
        const auto result = ma_decoder_read_pcm_frames(&decoder, clip->Samples.data(), frames, &decoded);
        close();
        if ((result != MA_SUCCESS && result != MA_AT_END) || decoded != frames)
            throw std::runtime_error("Audio clip decoding did not produce the expected frame count.");
        return clip;
    }

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

    void ValidateAudioGraph(const AudioGraphSnapshot& snapshot)
    {
        if (snapshot.Revision == 0 || snapshot.SampleRate < 8000 || snapshot.SampleRate > 384000 ||
            snapshot.Channels == 0 || snapshot.Channels > 16 || !snapshot.Output || snapshot.Nodes.empty() ||
            snapshot.Nodes.size() > 4096)
            throw std::invalid_argument("Audio graph header is invalid.");

        std::map<AudioGraphNodeId, const AudioGraphNode*> nodes;
        for (const auto& node : snapshot.Nodes)
        {
            if (!node.Id || node.Name.empty() || node.Name.size() > 256 ||
                !std::ranges::all_of(node.Parameters, [](const float value) { return std::isfinite(value); }) ||
                !nodes.emplace(node.Id, &node).second)
                throw std::invalid_argument("Audio graph contains an invalid or duplicate node.");
            for (const auto& input : node.Inputs)
                if (!input.Source || (input.DelayedFeedback && node.Type != AudioGraphNodeType::Delay))
                    throw std::invalid_argument("Audio graph connection is invalid.");
        }
        if (!nodes.contains(snapshot.Output) || nodes.at(snapshot.Output)->Type != AudioGraphNodeType::Output)
            throw std::invalid_argument("Audio graph output node is unavailable or has the wrong type.");
        for (const auto& node : snapshot.Nodes)
            for (const auto& input : node.Inputs)
                if (!nodes.contains(input.Source))
                    throw std::invalid_argument("Audio graph references an unavailable source node.");

        std::map<AudioGraphNodeId, std::uint8_t> states;
        const auto visit = [&](const auto& self, const AudioGraphNodeId id) -> void
        {
            if (states[id] == 1)
                throw std::invalid_argument("Audio graph contains a zero-delay cycle.");
            if (states[id] == 2)
                return;
            states[id] = 1;
            for (const auto& input : nodes.at(id)->Inputs)
                if (!input.DelayedFeedback)
                    self(self, input.Source);
            states[id] = 2;
        };
        for (const auto& [id, node] : nodes)
        {
            (void)node;
            visit(visit, id);
        }
    }

    AudioSystem::AudioSystem(const AudioSystemSpecification specification)
    {
        if (specification.Mode == AudioMode::Disabled || specification.MaximumVoices == 0 ||
            specification.MaximumVoices > 65536 || specification.MaximumVirtualVoices < specification.MaximumVoices ||
            specification.MaximumVirtualVoices > 262144 || specification.MaximumMeterReadings == 0 ||
            specification.MaximumMeterReadings > 4096 || specification.MixSampleRate < 8000 ||
            specification.MixSampleRate > 192000 ||
            (specification.PeriodFrames != 128 && specification.PeriodFrames != 256 &&
             specification.PeriodFrames != 512 && specification.PeriodFrames != 1024) ||
            AudioChannelCount(specification.OutputLayout) == 0 ||
            (!specification.PlaybackDeviceId.empty() &&
             (specification.PlaybackDeviceId.size() > 512U || specification.PlaybackDeviceId.size() % 2U != 0 ||
              !std::ranges::all_of(specification.PlaybackDeviceId, [](const char character)
                                   { return std::isxdigit(static_cast<unsigned char>(character)) != 0; }))))
            throw std::invalid_argument("AudioSystem specification is invalid.");
        m_Impl = std::make_unique<Impl>(specification);
    }
    AudioSystem::~AudioSystem() = default;
    bool AudioSystem::IsOpen() const noexcept { return m_Impl->State.IsOpen(); }
    void AudioSystem::SubmitGraph(std::shared_ptr<const AudioGraphSnapshot> snapshot)
    {
        m_Impl->RequireOwner("SubmitGraph");
        if (!snapshot)
            throw std::invalid_argument("Audio graph snapshot is empty.");
        ValidateAudioGraph(*snapshot);
        std::scoped_lock lock(m_Impl->Mutex);
        if (!IsOpen())
            throw std::logic_error("AudioSystem closed while submitting the graph.");
        if (snapshot->Revision <= m_Impl->Revision.load(std::memory_order_relaxed))
            throw std::invalid_argument("Audio graph revision must increase monotonically.");
        m_Impl->Graph = std::move(snapshot);
        m_Impl->Revision.store(m_Impl->Graph->Revision, std::memory_order_release);
    }
    std::shared_ptr<const AudioGraphSnapshot> AudioSystem::CurrentGraph() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Graph;
    }
    std::uint64_t AudioSystem::GraphRevision() const noexcept
    {
        return m_Impl->Revision.load(std::memory_order_acquire);
    }
    std::vector<float> AudioSystem::RenderOffline(const std::span<const float> interleavedInput,
                                                  const std::uint64_t frameCount) const
    {
        m_Impl->RequireOwner("RenderOffline");
        if (m_Impl->Mode != AudioMode::Headless)
            throw std::logic_error("Offline rendering requires a headless AudioSystem.");
        std::shared_ptr<const AudioGraphSnapshot> graph;
        {
            std::scoped_lock lock(m_Impl->Mutex);
            graph = m_Impl->Graph;
        }
        if (!graph)
            throw std::logic_error("AudioSystem has no submitted graph.");
        if (frameCount > std::uint64_t{16} * 1024U * 1024U ||
            frameCount * graph->Channels != static_cast<std::uint64_t>(interleavedInput.size()) ||
            !std::ranges::all_of(interleavedInput, [](const float sample) { return std::isfinite(sample); }))
            throw std::invalid_argument("Offline audio input dimensions or samples are invalid.");

        const auto sampleCount = interleavedInput.size();
        std::map<AudioGraphNodeId, const AudioGraphNode*> nodes;
        for (const auto& node : graph->Nodes)
            nodes.emplace(node.Id, &node);
        std::map<AudioGraphNodeId, std::vector<float>> outputs;
        const auto render = [&](const auto& self, const AudioGraphNodeId id) -> const std::vector<float>&
        {
            if (const auto cached = outputs.find(id); cached != outputs.end())
                return cached->second;
            const auto& node = *nodes.at(id);
            auto& result = outputs[id];
            result.assign(sampleCount, 0.0F);
            if (node.Type == AudioGraphNodeType::Input)
            {
                std::ranges::copy(interleavedInput, result.begin());
                return result;
            }
            for (const auto& input : node.Inputs)
            {
                if (input.DelayedFeedback)
                    continue;
                const auto& source = self(self, input.Source);
                for (std::size_t index = 0; index < result.size(); ++index)
                    result[index] += source[index];
            }
            if (node.Type == AudioGraphNodeType::Gain)
            {
                const auto gain = node.Parameters.empty() ? 1.0F : node.Parameters.front();
                for (auto& sample : result)
                    sample *= gain;
            }
            else if (node.Type == AudioGraphNodeType::LowPass || node.Type == AudioGraphNodeType::HighPass)
            {
                const auto cutoff = std::clamp(node.Parameters.empty() ? 1'000.0F : node.Parameters.front(), 10.0F,
                                               static_cast<float>(graph->SampleRate) * 0.49F);
                std::vector<float> filtered(sampleCount);
                if (node.Type == AudioGraphNodeType::LowPass)
                {
                    auto config = ma_lpf_config_init(ma_format_f32, graph->Channels, graph->SampleRate, cutoff, 2);
                    ma_lpf filter{};
                    if (ma_lpf_init(&config, nullptr, &filter) != MA_SUCCESS)
                        throw std::runtime_error("miniaudio low-pass initialization failed.");
                    const auto status = ma_lpf_process_pcm_frames(&filter, filtered.data(), result.data(), frameCount);
                    ma_lpf_uninit(&filter, nullptr);
                    if (status != MA_SUCCESS)
                        throw std::runtime_error("miniaudio low-pass processing failed.");
                }
                else
                {
                    auto config = ma_hpf_config_init(ma_format_f32, graph->Channels, graph->SampleRate, cutoff, 2);
                    ma_hpf filter{};
                    if (ma_hpf_init(&config, nullptr, &filter) != MA_SUCCESS)
                        throw std::runtime_error("miniaudio high-pass initialization failed.");
                    const auto status = ma_hpf_process_pcm_frames(&filter, filtered.data(), result.data(), frameCount);
                    ma_hpf_uninit(&filter, nullptr);
                    if (status != MA_SUCCESS)
                        throw std::runtime_error("miniaudio high-pass processing failed.");
                }
                result = std::move(filtered);
            }
            else if (node.Type == AudioGraphNodeType::Equalizer)
            {
                const auto gain = node.Parameters.empty() ? 1.0F : node.Parameters.front();
                for (auto& sample : result)
                    sample *= gain;
            }
            else if (node.Type == AudioGraphNodeType::Compressor)
            {
                const auto threshold = std::clamp(node.Parameters.empty() ? 0.5F : node.Parameters[0], 0.001F, 1.0F);
                const auto ratio = std::clamp(node.Parameters.size() > 1 ? node.Parameters[1] : 4.0F, 1.0F, 100.0F);
                for (auto& sample : result)
                {
                    const auto magnitude = std::abs(sample);
                    if (magnitude > threshold)
                        sample = std::copysign(threshold + (magnitude - threshold) / ratio, sample);
                }
            }
            else if (node.Type == AudioGraphNodeType::Limiter)
            {
                const auto ceiling = std::clamp(node.Parameters.empty() ? 0.98F : node.Parameters[0], 0.001F, 1.0F);
                for (auto& sample : result)
                    sample = std::clamp(sample, -ceiling, ceiling);
            }
            else if (node.Type == AudioGraphNodeType::Gate)
            {
                const auto threshold = std::clamp(node.Parameters.empty() ? 0.01F : node.Parameters[0], 0.0F, 1.0F);
                for (auto& sample : result)
                    if (std::abs(sample) < threshold)
                        sample = 0.0F;
            }
            else if (node.Type == AudioGraphNodeType::Distortion)
            {
                const auto drive = std::clamp(node.Parameters.empty() ? 1.0F : node.Parameters[0], 0.0F, 100.0F);
                const auto normalization = std::tanh(std::max(drive, 1.0F));
                for (auto& sample : result)
                    sample = std::tanh(sample * drive) / normalization;
            }
            else if (node.Type == AudioGraphNodeType::Delay || node.Type == AudioGraphNodeType::Chorus)
            {
                const auto delayMilliseconds =
                    std::clamp(node.Parameters.empty() ? 80.0F : node.Parameters[0], 1.0F, 2000.0F);
                const auto feedback = std::clamp(node.Parameters.size() > 1 ? node.Parameters[1] : 0.25F, 0.0F, 0.99F);
                const auto wet = std::clamp(node.Parameters.size() > 2 ? node.Parameters[2] : 0.25F, 0.0F, 1.0F);
                const auto baseDelayFrames =
                    static_cast<std::size_t>(delayMilliseconds * static_cast<float>(graph->SampleRate) / 1000.0F);
                auto delayed = result;
                for (std::uint64_t frame = 0; frame < frameCount; ++frame)
                {
                    auto delayFrames = baseDelayFrames;
                    if (node.Type == AudioGraphNodeType::Chorus)
                    {
                        const auto phase = static_cast<float>(frame) / static_cast<float>(graph->SampleRate);
                        const auto modulation = 1.0F + 0.1F * std::sin(phase * 2.0F * 3.14159265F * 0.8F);
                        delayFrames = static_cast<std::size_t>(static_cast<float>(delayFrames) * modulation);
                    }
                    if (frame < delayFrames)
                        continue;
                    for (std::uint32_t channel = 0; channel < graph->Channels; ++channel)
                    {
                        const auto index = static_cast<std::size_t>(frame) * graph->Channels + channel;
                        const auto delayedIndex =
                            static_cast<std::size_t>(frame - delayFrames) * graph->Channels + channel;
                        const auto echo = delayed[delayedIndex];
                        delayed[index] = result[index] * (1.0F - wet) + echo * wet * (1.0F + feedback);
                    }
                }
                result = std::move(delayed);
            }
            else if (node.Type == AudioGraphNodeType::AlgorithmicReverb)
            {
                Detail::ApplyAlgorithmicReverb(result, graph->SampleRate, graph->Channels, node.Parameters);
            }
            else if (node.Type == AudioGraphNodeType::ConvolutionReverb)
            {
                const auto impulseCount = std::min<std::size_t>(node.Parameters.size(), 256);
                if (impulseCount != 0)
                {
                    auto convolved = result;
                    for (std::uint64_t frame = 0; frame < frameCount; ++frame)
                        for (std::uint32_t channel = 0; channel < graph->Channels; ++channel)
                        {
                            float sample = 0.0F;
                            for (std::size_t tap = 0; tap < impulseCount && tap <= frame; ++tap)
                                sample += result[(static_cast<std::size_t>(frame) - tap) * graph->Channels + channel] *
                                          node.Parameters[tap];
                            convolved[static_cast<std::size_t>(frame) * graph->Channels + channel] = sample;
                        }
                    result = std::move(convolved);
                }
            }
            return result;
        };
        return render(render, graph->Output);
    }

    AudioVoiceId AudioSystem::Play(AudioVoiceSpecification specification)
    {
        m_Impl->RequireOwner("Play");
        ValidateVoiceSpecification(specification);
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Voices.size() >= m_Impl->MaximumVirtualVoices)
            throw std::runtime_error("Audio virtual voice capacity was exhausted.");
        const AudioVoiceId id(m_Impl->NextVoice++);
        auto [iterator, inserted] =
            m_Impl->Voices.emplace(id, AudioSystem::Impl::Voice{.Id = id, .Specification = std::move(specification)});
        if (!inserted)
            throw std::runtime_error("Audio voice identity allocation failed.");
        auto& voice = iterator->second;
        try
        {
            if (m_Impl->Mode == AudioMode::Enabled)
            {
                voice.Device = std::make_unique<AudioSystem::Impl::Voice::Native>();
                ma_result soundResult = MA_ERROR;
                if (voice.Specification.Clip->Streaming)
                {
                    auto decoderConfig = ma_decoder_config_init(ma_format_f32, voice.Specification.Clip->Channels,
                                                                voice.Specification.Clip->SampleRate);
                    if (ma_decoder_init_memory(voice.Specification.Clip->EncodedSource.data(),
                                               voice.Specification.Clip->EncodedSource.size(), &decoderConfig,
                                               &voice.Device->Decoder) != MA_SUCCESS)
                        throw std::runtime_error("miniaudio streaming decoder initialization failed.");
                    voice.Device->DecoderOpen = true;
                    soundResult = ma_sound_init_from_data_source(&m_Impl->Engine, &voice.Device->Decoder, 0, nullptr,
                                                                 &voice.Device->Sound);
                }
                else
                {
                    const auto frames = voice.Specification.Clip->Samples.size() / voice.Specification.Clip->Channels;
                    const auto bufferConfig =
                        ma_audio_buffer_config_init(ma_format_f32, voice.Specification.Clip->Channels, frames,
                                                    voice.Specification.Clip->Samples.data(), nullptr);
                    if (ma_audio_buffer_init(&bufferConfig, &voice.Device->Buffer) != MA_SUCCESS)
                        throw std::runtime_error("miniaudio voice buffer initialization failed.");
                    voice.Device->BufferOpen = true;
                    soundResult = ma_sound_init_from_data_source(&m_Impl->Engine, &voice.Device->Buffer, 0, nullptr,
                                                                 &voice.Device->Sound);
                }
                if (soundResult != MA_SUCCESS)
                    throw std::runtime_error("miniaudio voice initialization failed.");
                voice.Device->SoundOpen = true;
                m_Impl->AttachDeviceMixer(voice);
                m_Impl->ApplyDeviceProperties(voice);
                if (ma_sound_start(&voice.Device->Sound) != MA_SUCCESS)
                    throw std::runtime_error("miniaudio voice start failed.");
            }
            m_Impl->UpdateVirtualization();
        }
        catch (...)
        {
            m_Impl->DestroyVoice(voice);
            m_Impl->Voices.erase(iterator);
            throw;
        }
        return id;
    }

    bool AudioSystem::Stop(const AudioVoiceId voice)
    {
        m_Impl->RequireOwner("Stop");
        if (!voice)
            return false;
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->Voices.find(voice);
        if (found == m_Impl->Voices.end())
            return false;
        m_Impl->DestroyVoice(found->second);
        m_Impl->Voices.erase(found);
        m_Impl->UpdateVirtualization();
        return true;
    }

    bool AudioSystem::Pause(const AudioVoiceId voice, const bool paused)
    {
        m_Impl->RequireOwner("Pause");
        if (!voice)
            return false;
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->Voices.find(voice);
        if (found == m_Impl->Voices.end())
            return false;
        auto& runtime = found->second;
        if (runtime.Paused == paused)
            return true;
        if (runtime.Device && runtime.Device->SoundOpen)
        {
            const auto result = paused ? ma_sound_stop(&runtime.Device->Sound) : ma_sound_start(&runtime.Device->Sound);
            if (result != MA_SUCCESS)
                throw std::runtime_error(paused ? "miniaudio voice pause failed." : "miniaudio voice resume failed.");
        }
        runtime.Paused = paused;
        m_Impl->UpdateVirtualization();
        return true;
    }

    bool AudioSystem::Seek(const AudioVoiceId voice, const std::uint64_t frame)
    {
        m_Impl->RequireOwner("Seek");
        if (!voice)
            return false;
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->Voices.find(voice);
        if (found == m_Impl->Voices.end())
            return false;
        auto& runtime = found->second;
        const auto frames = runtime.Specification.Clip->Frames == 0
                                ? runtime.Specification.Clip->Samples.size() / runtime.Specification.Clip->Channels
                                : runtime.Specification.Clip->Frames;
        if (frame > frames)
            throw std::out_of_range("Audio seek frame exceeds the clip duration.");
        if (runtime.Device && runtime.Device->SoundOpen &&
            ma_sound_seek_to_pcm_frame(&runtime.Device->Sound, frame) != MA_SUCCESS)
        {
            throw std::runtime_error("miniaudio voice seek failed.");
        }
        runtime.Frame = static_cast<double>(frame);
        return true;
    }

    bool AudioSystem::SetVoice(const AudioVoiceId voice, AudioVoiceSpecification specification)
    {
        m_Impl->RequireOwner("SetVoice");
        ValidateVoiceSpecification(specification);
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->Voices.find(voice);
        if (found == m_Impl->Voices.end())
            return false;
        if (found->second.Specification.Clip != specification.Clip)
            throw std::invalid_argument("Replacing a playing voice clip requires stopping and replaying the voice.");
        found->second.Specification = std::move(specification);
        m_Impl->ApplyDeviceProperties(found->second);
        m_Impl->UpdateVirtualization();
        return true;
    }

    void AudioSystem::SubmitMixer(const AssetId mixer, const AudioMixerDefinition& definition)
    {
        m_Impl->RequireOwner("SubmitMixer");
        if (!mixer)
            throw std::invalid_argument("Audio mixer asset ID is empty.");
        auto routing = AudioSystem::Impl::CompileMixer(definition);
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Mixers.insert_or_assign(mixer, routing);
        for (auto& [id, registered] : m_Impl->MixerRoutings)
        {
            if (registered.Mixer == mixer)
            {
                registered.Routing = routing;
                const auto previous = m_Impl->NativeMixerRoutings.find(id);
                const auto previousNative = previous == m_Impl->NativeMixerRoutings.end() ? nullptr : previous->second;
                const auto replacementNative = m_Impl->BuildNativeMixer(definition);
                if (replacementNative)
                    m_Impl->NativeMixerRoutings.insert_or_assign(id, replacementNative);
                else
                    m_Impl->NativeMixerRoutings.erase(id);
                for (auto& [voiceId, voice] : m_Impl->Voices)
                {
                    (void)voiceId;
                    if (voice.Specification.MixerRouting == id)
                        m_Impl->AttachDeviceMixer(voice);
                }
                (void)previousNative;
            }
        }
        m_Impl->UpdateVirtualization();
    }

    bool AudioSystem::RemoveMixer(const AssetId mixer)
    {
        m_Impl->RequireOwner("RemoveMixer");
        if (!mixer)
            return false;
        std::scoped_lock lock(m_Impl->Mutex);
        const auto removed = m_Impl->Mixers.erase(mixer) != 0;
        if (removed)
            m_Impl->UpdateVirtualization();
        return removed;
    }

    AudioMixerRoutingId AudioSystem::RegisterMixer(const AssetId mixer, const AudioMixerDefinition& definition)
    {
        m_Impl->RequireOwner("RegisterMixer");
        if (!mixer)
            throw std::invalid_argument("Audio mixer asset ID is empty.");
        auto routing = AudioSystem::Impl::CompileMixer(definition);
        auto native = m_Impl->BuildNativeMixer(definition);
        std::scoped_lock lock(m_Impl->Mutex);
        auto id = AudioMixerRoutingId(m_Impl->NextMixerRouting++);
        while (!id || m_Impl->MixerRoutings.contains(id))
            id = AudioMixerRoutingId(m_Impl->NextMixerRouting++);
        m_Impl->MixerRoutings.emplace(
            id, AudioSystem::Impl::RegisteredMixer{.Mixer = mixer, .Routing = std::move(routing)});
        if (native)
            m_Impl->NativeMixerRoutings.emplace(id, std::move(native));
        return id;
    }

    bool AudioSystem::UpdateMixer(const AudioMixerRoutingId routing, const AudioMixerDefinition& definition)
    {
        m_Impl->RequireOwner("UpdateMixer");
        if (!routing)
            return false;
        auto replacement = AudioSystem::Impl::CompileMixer(definition);
        auto native = m_Impl->BuildNativeMixer(definition);
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->MixerRoutings.find(routing);
        if (found == m_Impl->MixerRoutings.end())
            return false;
        found->second.Routing = std::move(replacement);
        const auto previous = m_Impl->NativeMixerRoutings.find(routing);
        const auto previousNative = previous == m_Impl->NativeMixerRoutings.end() ? nullptr : previous->second;
        if (native)
            m_Impl->NativeMixerRoutings.insert_or_assign(routing, std::move(native));
        else
            m_Impl->NativeMixerRoutings.erase(routing);
        for (auto& [id, voice] : m_Impl->Voices)
        {
            (void)id;
            if (voice.Specification.MixerRouting == routing)
                m_Impl->AttachDeviceMixer(voice);
        }
        (void)previousNative;
        m_Impl->UpdateVirtualization();
        return true;
    }

    bool AudioSystem::UnregisterMixer(const AudioMixerRoutingId routing)
    {
        m_Impl->RequireOwner("UnregisterMixer");
        if (!routing)
            return false;
        std::scoped_lock lock(m_Impl->Mutex);
        for (auto& [id, voice] : m_Impl->Voices)
        {
            (void)id;
            if (voice.Specification.MixerRouting == routing && voice.Device && voice.Device->SoundOpen)
                (void)ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&voice.Device->Sound), 0,
                                                ma_node_graph_get_endpoint(ma_engine_get_node_graph(&m_Impl->Engine)),
                                                0);
        }
        m_Impl->NativeMixerRoutings.erase(routing);
        const auto removed = m_Impl->MixerRoutings.erase(routing) != 0;
        if (removed)
            m_Impl->UpdateVirtualization();
        return removed;
    }

    std::size_t AudioSystem::StopAll(const std::string_view bus)
    {
        m_Impl->RequireOwner("StopAll");
        std::scoped_lock lock(m_Impl->Mutex);
        std::size_t stopped = 0;
        for (auto iterator = m_Impl->Voices.begin(); iterator != m_Impl->Voices.end();)
        {
            if (!bus.empty() && m_Impl->ResolveVoiceBus(iterator->second.Specification).Name != bus)
            {
                ++iterator;
                continue;
            }
            m_Impl->DestroyVoice(iterator->second);
            iterator = m_Impl->Voices.erase(iterator);
            ++stopped;
        }
        return stopped;
    }

    void AudioSystem::SetBusGain(std::string bus, const float gain)
    {
        m_Impl->RequireOwner("SetBusGain");
        if (bus.empty() || bus.size() > 128 || !std::isfinite(gain) || gain < 0.0F || gain > 16.0F)
            throw std::invalid_argument("Audio bus name or gain is invalid.");
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->BusGains.insert_or_assign(std::move(bus), gain);
        m_Impl->UpdateVirtualization();
    }

    float AudioSystem::BusGain(const std::string_view bus) const
    {
        m_Impl->RequireOwner("BusGain");
        if (bus.empty())
            throw std::invalid_argument("Audio bus name is empty.");
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->LegacyBusGain(bus);
    }

    std::vector<AudioBusInfo> AudioSystem::Buses() const
    {
        m_Impl->RequireOwner("Buses");
        std::scoped_lock lock(m_Impl->Mutex);
        std::vector<AudioBusInfo> result;
        result.reserve(m_Impl->BusGains.size());
        for (const auto& [name, gain] : m_Impl->BusGains)
            result.push_back({name, gain});
        return result;
    }

    void AudioSystem::SetListener(const AudioListenerState& listener)
    {
        m_Impl->RequireOwner("SetListener");
        if (!Math::IsFinite(listener.Position) || !Math::IsFinite(listener.Forward) || !Math::IsFinite(listener.Up) ||
            !Math::IsFinite(listener.Velocity) || !std::isfinite(listener.Gain) || listener.Gain < 0.0F ||
            listener.Gain > 16.0F)
            throw std::invalid_argument("Audio listener contains non-finite values.");
        const auto forward = NormalizeVector(listener.Forward);
        const auto up = NormalizeVector(listener.Up);
        if (std::abs(Dot(forward, up)) > 0.999F)
            throw std::invalid_argument("Audio listener forward and up vectors cannot be parallel.");
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Listener = listener;
        m_Impl->Listener.Forward = forward;
        m_Impl->Listener.Up = up;
        if (m_Impl->Mode == AudioMode::Enabled)
        {
            ma_engine_listener_set_position(&m_Impl->Engine, 0, listener.Position.X, listener.Position.Y,
                                            listener.Position.Z);
            ma_engine_listener_set_direction(&m_Impl->Engine, 0, forward.X, forward.Y, forward.Z);
            ma_engine_listener_set_world_up(&m_Impl->Engine, 0, up.X, up.Y, up.Z);
            ma_engine_listener_set_velocity(&m_Impl->Engine, 0, listener.Velocity.X, listener.Velocity.Y,
                                            listener.Velocity.Z);
        }
        m_Impl->UpdateVirtualization();
    }

    void AudioSystem::SubmitSnapshot(const AudioMixerSnapshot& snapshot)
    {
        m_Impl->RequireOwner("SubmitSnapshot");
        if (snapshot.Revision == 0 || snapshot.Revision <= m_Impl->SnapshotRevision ||
            snapshot.Transition.count() < 0 || snapshot.BusGains.empty() || snapshot.BusGains.size() > 1024)
            throw std::invalid_argument("Audio mixer snapshot header is invalid or stale.");
        std::map<std::string, float, std::less<>> target;
        for (const auto& [bus, gain] : snapshot.BusGains)
            if (bus.empty() || bus.size() > 128 || !std::isfinite(gain) || gain < 0.0F || gain > 16.0F ||
                !target.emplace(bus, gain).second)
                throw std::invalid_argument("Audio mixer snapshot contains an invalid or duplicate bus.");
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->SnapshotStart = m_Impl->BusGains;
        m_Impl->SnapshotTarget = std::move(target);
        m_Impl->SnapshotElapsed = {};
        m_Impl->SnapshotDuration = snapshot.Transition;
        m_Impl->SnapshotRevision = snapshot.Revision;
        if (snapshot.Transition.count() == 0)
            m_Impl->BusGains = m_Impl->SnapshotTarget;
        m_Impl->UpdateVirtualization();
    }

    void AudioSystem::Update(const std::chrono::duration<float> elapsed)
    {
        m_Impl->RequireOwner("Update");
        if (!std::isfinite(elapsed.count()) || elapsed.count() < 0.0F || elapsed > std::chrono::seconds(10))
            throw std::invalid_argument("Audio update elapsed time is invalid.");
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->SnapshotDuration.count() > 0.0F && m_Impl->SnapshotElapsed < m_Impl->SnapshotDuration)
        {
            m_Impl->SnapshotElapsed = std::min(m_Impl->SnapshotElapsed + elapsed, m_Impl->SnapshotDuration);
            const auto alpha = m_Impl->SnapshotElapsed.count() / m_Impl->SnapshotDuration.count();
            std::set<std::string, std::less<>> buses;
            for (const auto& [bus, gain] : m_Impl->SnapshotStart)
            {
                (void)gain;
                buses.insert(bus);
            }
            for (const auto& [bus, gain] : m_Impl->SnapshotTarget)
            {
                (void)gain;
                buses.insert(bus);
            }
            for (const auto& bus : buses)
            {
                const auto start = m_Impl->SnapshotStart.contains(bus) ? m_Impl->SnapshotStart.at(bus) : 1.0F;
                const auto target = m_Impl->SnapshotTarget.contains(bus) ? m_Impl->SnapshotTarget.at(bus) : 1.0F;
                m_Impl->BusGains[bus] = start + (target - start) * alpha;
            }
        }
        for (auto iterator = m_Impl->Voices.begin(); iterator != m_Impl->Voices.end();)
        {
            auto& voice = iterator->second;
            if (voice.Device && voice.Device->SoundOpen && !voice.Paused)
            {
                ma_uint64 cursor = 0;
                (void)ma_sound_get_cursor_in_pcm_frames(&voice.Device->Sound, &cursor);
                voice.Frame = static_cast<double>(cursor);
                if (ma_sound_at_end(&voice.Device->Sound) && !voice.Specification.Loop)
                {
                    m_Impl->DestroyVoice(voice);
                    iterator = m_Impl->Voices.erase(iterator);
                    continue;
                }
                m_Impl->ApplyDeviceProperties(voice);
            }
            ++iterator;
        }
        m_Impl->UpdateVirtualization();
    }

    std::vector<float> AudioSystem::RenderVoicesOffline(const std::uint64_t frameCount)
    {
        m_Impl->RequireOwner("RenderVoicesOffline");
        if (m_Impl->Mode != AudioMode::Headless)
            throw std::logic_error("Offline voice rendering requires a headless AudioSystem.");
        if (frameCount > std::uint64_t{16} * 1024U * 1024U)
            throw std::invalid_argument("Offline voice render frame count is excessive.");
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->UpdateVirtualization();
        std::vector<float> output(static_cast<std::size_t>(frameCount) * m_Impl->Channels, 0.0F);
        const auto forward = NormalizeVector(m_Impl->Listener.Forward);
        const auto up = NormalizeVector(m_Impl->Listener.Up);
        const Vector3 right{forward.Y * up.Z - forward.Z * up.Y, forward.Z * up.X - forward.X * up.Z,
                            forward.X * up.Y - forward.Y * up.X};
        std::map<const AudioSystem::Impl::MixerRoutingSnapshot*, std::map<AssetId, std::vector<float>>> mixerInputs;
        for (auto& [id, voice] : m_Impl->Voices)
        {
            (void)id;
            if (voice.Paused)
                continue;
            const auto& clip = *voice.Specification.Clip;
            std::vector<float> streamedSamples;
            const std::vector<float>* samples = &clip.Samples;
            if (clip.Streaming)
            {
                if (clip.Frames > std::numeric_limits<std::size_t>::max() / clip.Channels)
                    throw std::runtime_error("Streaming audio clip dimensions exceed the current platform.");
                streamedSamples.resize(static_cast<std::size_t>(clip.Frames) * clip.Channels);
                ma_decoder decoder{};
                auto decoderConfig = ma_decoder_config_init(ma_format_f32, clip.Channels, clip.SampleRate);
                if (ma_decoder_init_memory(clip.EncodedSource.data(), clip.EncodedSource.size(), &decoderConfig,
                                           &decoder) != MA_SUCCESS)
                    throw std::runtime_error("Offline streaming audio decoder initialization failed.");
                ma_uint64 decoded = 0;
                const auto status = ma_decoder_read_pcm_frames(&decoder, streamedSamples.data(), clip.Frames, &decoded);
                ma_decoder_uninit(&decoder);
                if ((status != MA_SUCCESS && status != MA_AT_END) || decoded != clip.Frames)
                    throw std::runtime_error("Offline streaming audio decode was incomplete.");
                samples = &streamedSamples;
            }
            const auto clipFrames = samples->size() / clip.Channels;
            if (clipFrames == 0)
                continue;
            const auto resolved = m_Impl->ResolveVoiceBus(voice.Specification);
            auto* destination = &output;
            if (resolved.Authored)
            {
                const auto registration = m_Impl->FindMixerRegistration(voice.Specification);
                auto& input = mixerInputs[registration.Routing][resolved.Bus];
                if (input.empty())
                    input.assign(output.size(), 0.0F);
                destination = &input;
            }
            const float sourceGain = resolved.Authored ? m_Impl->AuthoredVoiceSourceGain(voice.Specification, resolved)
                                                       : m_Impl->VoiceGain(voice.Specification);
            std::array<float, 8> outputGains{};
            double pitch = static_cast<double>(voice.Specification.Pitch) * clip.SampleRate /
                           static_cast<double>(m_Impl->SampleRate);
            if (voice.Specification.Spatial)
            {
                const Vector3 delta{voice.Specification.Position.X - m_Impl->Listener.Position.X,
                                    voice.Specification.Position.Y - m_Impl->Listener.Position.Y,
                                    voice.Specification.Position.Z - m_Impl->Listener.Position.Z};
                const auto distance = VectorLength(delta);
                const auto attenuation = distance <= voice.Specification.MinimumDistance
                                             ? 1.0F
                                             : (distance >= voice.Specification.MaximumDistance
                                                    ? 0.0F
                                                    : voice.Specification.MinimumDistance /
                                                          std::max(distance, voice.Specification.MinimumDistance));
                const auto normalizedDistance =
                    std::clamp((distance - voice.Specification.MinimumDistance) /
                                   (voice.Specification.MaximumDistance - voice.Specification.MinimumDistance),
                               0.0F, 1.0F);
                const auto curveAttenuation =
                    std::clamp(voice.Specification.Attenuation.Evaluate(normalizedDistance), 0.0F, 1.0F);
                const auto direction = distance > 1.0e-6F
                                           ? Vector3{delta.X / distance, delta.Y / distance, delta.Z / distance}
                                           : Vector3{};
                const auto spatialGain = sourceGain * attenuation * curveAttenuation;
                if (m_Impl->Channels == 1)
                {
                    outputGains[0] = spatialGain;
                }
                else
                {
                    constexpr float pi = 3.14159265358979323846F;
                    const std::array<float, 8> azimuths =
                        m_Impl->Channels == 2 ? std::array<float, 8>{-30.0F, 30.0F}
                        : m_Impl->Channels == 6
                            ? std::array<float, 8>{-30.0F, 30.0F, 0.0F, 0.0F, -110.0F, 110.0F}
                            : std::array<float, 8>{-30.0F, 30.0F, 0.0F, 0.0F, -150.0F, 150.0F, -90.0F, 90.0F};
                    float squareSum = 0.0F;
                    for (std::uint32_t channel = 0; channel < m_Impl->Channels; ++channel)
                    {
                        if (channel == 3U && m_Impl->Channels >= 6U)
                            continue;
                        const float angle = azimuths[channel] * pi / 180.0F;
                        const Vector3 speaker{right.X * std::sin(angle) + forward.X * std::cos(angle),
                                              right.Y * std::sin(angle) + forward.Y * std::cos(angle),
                                              right.Z * std::sin(angle) + forward.Z * std::cos(angle)};
                        const float weight = std::pow(std::max(0.0F, Dot(direction, speaker)), 2.0F);
                        outputGains[channel] = weight;
                        squareSum += weight * weight;
                    }
                    if (squareSum <= 1.0e-8F)
                    {
                        outputGains[0] = 0.70710678F;
                        outputGains[1] = 0.70710678F;
                        squareSum = 1.0F;
                    }
                    const float normalization = spatialGain / std::sqrt(squareSum);
                    for (std::uint32_t channel = 0; channel < m_Impl->Channels; ++channel)
                        outputGains[channel] *= normalization;
                }
                constexpr float SpeedOfSound = 343.0F;
                const auto listenerRadial = Dot(m_Impl->Listener.Velocity, direction);
                const auto sourceRadial = Dot(voice.Specification.Velocity, direction);
                pitch *= std::clamp(
                    static_cast<double>((SpeedOfSound + listenerRadial) / std::max(1.0F, SpeedOfSound + sourceRadial)),
                    0.5, 2.0);
            }
            for (std::uint64_t frame = 0; frame < frameCount; ++frame)
            {
                auto sourceFrame = static_cast<std::uint64_t>(voice.Frame);
                if (sourceFrame >= clipFrames)
                {
                    if (!voice.Specification.Loop)
                        break;
                    voice.Frame = std::fmod(voice.Frame, static_cast<double>(clipFrames));
                    sourceFrame = static_cast<std::uint64_t>(voice.Frame);
                }
                if (!voice.Virtualized)
                {
                    const auto nextFrame = (sourceFrame + 1U) % clipFrames;
                    const auto alpha = static_cast<float>(voice.Frame - static_cast<double>(sourceFrame));
                    const auto sample = [&](const std::uint32_t channel)
                    {
                        const auto selected = std::min(channel, clip.Channels - 1U);
                        const auto first = (*samples)[sourceFrame * clip.Channels + selected];
                        const auto second = (*samples)[nextFrame * clip.Channels + selected];
                        return first + (second - first) * alpha;
                    };
                    const auto destinationOffset = static_cast<std::size_t>(frame) * m_Impl->Channels;
                    if (voice.Specification.Spatial)
                    {
                        float mono = 0.0F;
                        for (std::uint32_t channel = 0; channel < clip.Channels; ++channel)
                            mono += sample(channel);
                        mono /= static_cast<float>(clip.Channels);
                        for (std::uint32_t channel = 0; channel < m_Impl->Channels; ++channel)
                            (*destination)[destinationOffset + channel] += mono * outputGains[channel];
                    }
                    else if (m_Impl->Channels == 1)
                    {
                        float mono = 0.0F;
                        for (std::uint32_t channel = 0; channel < clip.Channels; ++channel)
                            mono += sample(channel);
                        (*destination)[destinationOffset] += mono / static_cast<float>(clip.Channels) * sourceGain;
                    }
                    else if (clip.Channels == 1)
                    {
                        const auto mono = sample(0) * sourceGain * (m_Impl->Channels == 2 ? 1.0F : 0.70710678F);
                        (*destination)[destinationOffset] += mono;
                        (*destination)[destinationOffset + 1U] += mono;
                    }
                    else
                    {
                        const auto copiedChannels = std::min(clip.Channels, m_Impl->Channels);
                        for (std::uint32_t channel = 0; channel < copiedChannels; ++channel)
                            (*destination)[destinationOffset + channel] += sample(channel) * sourceGain;
                    }
                }
                voice.Frame += pitch;
            }
        }
        std::map<AssetId, AudioMeterReading> meterReadings;
        std::uint64_t droppedMeterReadings = 0;
        for (auto& [routing, inputs] : mixerInputs)
        {
            auto mixed = Detail::ProcessAudioMixer(routing->Definition, std::move(inputs), m_Impl->SampleRate,
                                                   m_Impl->Channels, m_Impl->MaximumMeterReadings);
            for (std::size_t index = 0; index < output.size(); ++index)
                output[index] += mixed.Output[index];
            droppedMeterReadings += mixed.DroppedMeterReadings;
            for (const auto& reading : mixed.Meters)
            {
                auto& aggregate = meterReadings[reading.Bus];
                aggregate.Bus = reading.Bus;
                aggregate.Peak = std::max(aggregate.Peak, reading.Peak);
                aggregate.Rms = std::max(aggregate.Rms, reading.Rms);
                aggregate.Clipping = aggregate.Clipping || reading.Clipping;
            }
        }
        if (!mixerInputs.empty())
        {
            ++m_Impl->Meters.Revision;
            m_Impl->Meters.DroppedReadings = droppedMeterReadings;
            m_Impl->Meters.Readings.clear();
            for (const auto& [bus, reading] : meterReadings)
            {
                (void)bus;
                if (m_Impl->Meters.Readings.size() >= m_Impl->MaximumMeterReadings)
                {
                    ++m_Impl->Meters.DroppedReadings;
                    continue;
                }
                m_Impl->Meters.Readings.push_back(reading);
            }
        }
        for (auto& sample : output)
            sample = std::clamp(sample, -1.0F, 1.0F);
        m_Impl->RenderedFrames += frameCount;
        for (auto iterator = m_Impl->Voices.begin(); iterator != m_Impl->Voices.end();)
        {
            auto& voice = iterator->second;
            const auto frames = voice.Specification.Clip->Frames == 0
                                    ? voice.Specification.Clip->Samples.size() / voice.Specification.Clip->Channels
                                    : voice.Specification.Clip->Frames;
            if (!voice.Paused && !voice.Specification.Loop && voice.Frame >= static_cast<double>(frames))
            {
                m_Impl->DestroyVoice(voice);
                iterator = m_Impl->Voices.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        return output;
    }

    std::optional<AudioVoiceInfo> AudioSystem::Voice(const AudioVoiceId voice) const
    {
        m_Impl->RequireOwner("Voice");
        if (!voice)
            return std::nullopt;
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->Voices.find(voice);
        if (found == m_Impl->Voices.end())
            return std::nullopt;
        const auto& runtime = found->second;
        const auto resolved = m_Impl->ResolveVoiceBus(runtime.Specification);
        const auto frames = runtime.Specification.Clip->Frames == 0
                                ? runtime.Specification.Clip->Samples.size() / runtime.Specification.Clip->Channels
                                : runtime.Specification.Clip->Frames;
        return AudioVoiceInfo{
            voice,
            std::string(resolved.Name),
            static_cast<std::uint64_t>(runtime.Frame),
            frames,
            runtime.Specification.Priority,
            !runtime.Paused,
            runtime.Paused,
            runtime.Virtualized,
            resolved.Mixer,
            resolved.Bus,
            runtime.Specification.MixerRouting,
        };
    }

    std::vector<AudioVoiceInfo> AudioSystem::Voices() const
    {
        m_Impl->RequireOwner("Voices");
        std::scoped_lock lock(m_Impl->Mutex);
        std::vector<AudioVoiceInfo> result;
        result.reserve(m_Impl->Voices.size());
        for (const auto& [id, voice] : m_Impl->Voices)
        {
            const auto resolved = m_Impl->ResolveVoiceBus(voice.Specification);
            const auto frames = voice.Specification.Clip->Frames == 0
                                    ? voice.Specification.Clip->Samples.size() / voice.Specification.Clip->Channels
                                    : voice.Specification.Clip->Frames;
            result.push_back({id, std::string(resolved.Name), static_cast<std::uint64_t>(voice.Frame), frames,
                              voice.Specification.Priority, !voice.Paused, voice.Paused, voice.Virtualized,
                              resolved.Mixer, resolved.Bus, voice.Specification.MixerRouting});
        }
        return result;
    }

    AudioSystemStatistics AudioSystem::Statistics() const
    {
        m_Impl->RequireOwner("Statistics");
        std::scoped_lock lock(m_Impl->Mutex);
        AudioSystemStatistics result;
        result.Voices = m_Impl->Voices.size();
        result.VirtualVoices = static_cast<std::size_t>(
            std::ranges::count_if(m_Impl->Voices, [](const auto& item) { return item.second.Virtualized; }));
        result.AudibleVoices = static_cast<std::size_t>(std::ranges::count_if(
            m_Impl->Voices, [](const auto& item) { return !item.second.Virtualized && !item.second.Paused; }));
        result.MixerAssets = m_Impl->Mixers.size();
        result.MixerRoutings = m_Impl->MixerRoutings.size();
        const auto countMixer = [&](const AudioSystem::Impl::MixerRoutingSnapshot& mixer)
        {
            result.MixerBuses += mixer.Definition.Buses.size();
            for (const auto& bus : mixer.Definition.Buses)
                result.MixerEffects += bus.Effects.size();
        };
        for (const auto& [id, mixer] : m_Impl->Mixers)
        {
            (void)id;
            countMixer(mixer);
        }
        for (const auto& [id, mixer] : m_Impl->MixerRoutings)
        {
            (void)id;
            countMixer(mixer.Routing);
        }
        result.MeterReadings = m_Impl->Meters.Readings.size();
        if (m_Impl->Mode == AudioMode::Enabled)
        {
            std::set<AssetId> meteredBuses;
            for (const auto& [routing, graph] : m_Impl->NativeMixerRoutings)
            {
                (void)routing;
                if (!graph)
                    continue;
                for (const auto& effect : graph->Effects)
                    if (effect->MeterBus && effect->MeterRevision.load(std::memory_order_acquire) != 0)
                        meteredBuses.insert(effect->MeterBus);
            }
            result.MeterReadings = std::max(result.MeterReadings, meteredBuses.size());
        }
        result.RenderedFrames = m_Impl->RenderedFrames;
        result.Underruns = m_Impl->Underruns;
        result.MixSampleRate = m_Impl->SampleRate;
        result.OutputChannels = m_Impl->Channels;
        result.PeriodFrames = m_Impl->SpecificationValue.PeriodFrames;
        result.PlaybackDeviceName = m_Impl->PlaybackDeviceName;
        result.PlaybackDeviceFallback = m_Impl->PlaybackDeviceFallback;
        return result;
    }

    void AudioSystem::SubmitMeterSnapshot(AudioMeterSnapshot snapshot)
    {
        m_Impl->RequireOwner("SubmitMeterSnapshot");
        if (snapshot.Revision == 0 || snapshot.Readings.size() > m_Impl->MaximumMeterReadings)
            throw std::invalid_argument("Audio meter snapshot header is invalid.");
        std::ranges::sort(snapshot.Readings, {}, &AudioMeterReading::Bus);
        AssetId previous;
        for (const auto& reading : snapshot.Readings)
        {
            if (!reading.Bus || reading.Bus == previous || !std::isfinite(reading.Peak) || reading.Peak < 0.0F ||
                reading.Peak > 64.0F || !std::isfinite(reading.Rms) || reading.Rms < 0.0F || reading.Rms > reading.Peak)
                throw std::invalid_argument("Audio meter snapshot contains an invalid or duplicate reading.");
            previous = reading.Bus;
        }
        std::scoped_lock lock(m_Impl->Mutex);
        if (snapshot.Revision <= m_Impl->Meters.Revision)
            throw std::invalid_argument("Audio meter snapshot revision is stale.");
        m_Impl->Meters = std::move(snapshot);
    }

    AudioMeterSnapshot AudioSystem::LatestMeterSnapshot() const
    {
        m_Impl->RequireOwner("LatestMeterSnapshot");
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Mode == AudioMode::Enabled)
        {
            std::map<AssetId, AudioMeterReading> readings;
            for (const auto& [routing, graph] : m_Impl->NativeMixerRoutings)
            {
                (void)routing;
                if (!graph)
                    continue;
                for (const auto& effect : graph->Effects)
                {
                    if (!effect->MeterBus || effect->MeterRevision.load(std::memory_order_acquire) == 0)
                        continue;
                    const auto peak = effect->MeterPeak.load(std::memory_order_relaxed);
                    const auto rms = effect->MeterRms.load(std::memory_order_relaxed);
                    auto& reading = readings[effect->MeterBus];
                    reading.Bus = effect->MeterBus;
                    reading.Peak = std::max(reading.Peak, peak);
                    reading.Rms = std::max(reading.Rms, rms);
                    reading.Clipping = reading.Clipping || peak >= 1.0F;
                }
            }
            AudioMeterSnapshot result;
            result.Revision = ++m_Impl->NativeMeterSnapshotRevision;
            result.Readings.reserve(std::min(readings.size(), static_cast<std::size_t>(m_Impl->MaximumMeterReadings)));
            for (const auto& [bus, reading] : readings)
            {
                (void)bus;
                if (result.Readings.size() >= m_Impl->MaximumMeterReadings)
                {
                    ++result.DroppedReadings;
                    continue;
                }
                result.Readings.push_back(reading);
            }
            return result;
        }
        return m_Impl->Meters;
    }

    AudioSystemSpecification AudioSystem::Specification() const
    {
        m_Impl->RequireOwner("Specification");
        return m_Impl->SpecificationValue;
    }

    void AudioSystem::Close()
    {
        m_Impl->State.Close();
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Graph.reset();
        for (auto& [id, voice] : m_Impl->Voices)
        {
            (void)id;
            m_Impl->DestroyVoice(voice);
        }
        m_Impl->Voices.clear();
        m_Impl->Mixers.clear();
        m_Impl->MixerRoutings.clear();
        m_Impl->NativeMixerRoutings.clear();
        m_Impl->Meters = {};
        m_Impl->NativeMeterSnapshotRevision = 0;
        if (std::exchange(m_Impl->EngineOpen, false))
            ma_engine_uninit(&m_Impl->Engine);
        if (std::exchange(m_Impl->ContextOpen, false))
            ma_context_uninit(&m_Impl->Context);
    }

} // namespace Keire
