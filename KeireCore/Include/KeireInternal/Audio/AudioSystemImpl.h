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
            AudioMixerImpulseResponses ImpulseResponseAssets;
            std::map<AssetId, Detail::PreparedAudioImpulseResponse> ImpulseResponses;
            mutable std::map<AssetId, Detail::AudioEffectProcessor> OfflineEffects;
            std::size_t ConvolutionChannelFrames = 0;
            std::map<AssetId, float> BusGains;
            std::map<AssetId, std::string> BusNamesById;
            std::map<std::string, AssetId, std::less<>> BusIdsByName;

            void Swap(MixerRoutingSnapshot& other) noexcept
            {
                using std::swap;
                swap(MasterBus, other.MasterBus);
                swap(Definition, other.Definition);
                swap(ImpulseResponseAssets, other.ImpulseResponseAssets);
                swap(ImpulseResponses, other.ImpulseResponses);
                swap(OfflineEffects, other.OfflineEffects);
                swap(ConvolutionChannelFrames, other.ConvolutionChannelFrames);
                swap(BusGains, other.BusGains);
                swap(BusNamesById, other.BusNamesById);
                swap(BusIdsByName, other.BusIdsByName);
            }
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
            std::uint32_t Channels = 2;
            std::unique_ptr<Detail::AudioEffectProcessor> Processor;
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
            std::map<AssetId, NativeEffectNode*> EffectControls;
            std::map<AssetId, NativeEffectNode*> BusFaders;
            std::map<AssetId, ma_splitter_node*> SendControls;

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

        struct PreparedNativeVoiceAttachment final
        {
            ma_node* Source = nullptr;
            ma_node* Destination = nullptr;
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

        static void PopulateMixerValues(MixerRoutingSnapshot& result, const AudioMixerDefinition& definition)
        {
            result.MasterBus = definition.MasterBus;
            result.Definition = definition;
            result.BusGains.clear();
            result.BusNamesById.clear();
            result.BusIdsByName.clear();
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
        }

        [[nodiscard]] static std::map<AssetId, float> NativeMixerBusFaders(const AudioMixerDefinition& definition)
        {
            std::map<AssetId, const AudioMixerBusDefinition*> buses;
            std::set<AssetId> soloed;
            for (const auto& bus : definition.Buses)
            {
                buses.emplace(bus.Id, &bus);
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
            std::map<AssetId, float> result;
            for (const auto& bus : definition.Buses)
                result.emplace(bus.Id,
                               bus.Mute || (!soloed.empty() && !audibleWhenSoloed.contains(bus.Id)) ? 0.0F : bus.Gain);
            return result;
        }

        [[nodiscard]] MixerRoutingSnapshot CompileMixer(const AudioMixerDefinition& definition,
                                                        const AudioMixerImpulseResponses& impulseResponses) const
        {
            ValidateAudioMixer(definition);

            MixerRoutingSnapshot result;
            PopulateMixerValues(result, definition);
            result.ImpulseResponseAssets = impulseResponses;
            std::size_t preparedConvolutionChannelFrames = 0;
            for (const auto& bus : definition.Buses)
                for (const auto& effect : bus.Effects)
                {
                    if (effect.Type != AudioGraphNodeType::ConvolutionReverb ||
                        result.ImpulseResponses.contains(effect.ImpulseResponse))
                        continue;
                    const auto found = impulseResponses.find(effect.ImpulseResponse);
                    if (found == impulseResponses.end() || !found->second)
                        throw std::invalid_argument("Audio mixer convolution reverb impulse response is unresolved.");
                    auto prepared = Detail::PrepareAudioImpulseResponse(*found->second, SampleRate, Channels);
                    if (prepared.Samples.size() >
                        Detail::MaximumAudioConvolutionChannelFrames - preparedConvolutionChannelFrames)
                    {
                        throw std::invalid_argument(
                            "Audio mixer impulse responses exceed the aggregate convolution channel-work limit.");
                    }
                    preparedConvolutionChannelFrames += prepared.Samples.size();
                    result.ImpulseResponses.emplace(effect.ImpulseResponse, std::move(prepared));
                }
            std::size_t activeConvolutionChannelFrames = 0;
            for (const auto& bus : definition.Buses)
                for (const auto& effect : bus.Effects)
                {
                    if (effect.Bypassed || effect.Type != AudioGraphNodeType::ConvolutionReverb)
                        continue;
                    const auto impulse = result.ImpulseResponses.find(effect.ImpulseResponse);
                    if (impulse->second.Samples.size() >
                        Detail::MaximumAudioConvolutionChannelFrames - activeConvolutionChannelFrames)
                    {
                        throw std::invalid_argument(
                            "Audio mixer active convolution effects exceed the aggregate channel-work limit.");
                    }
                    activeConvolutionChannelFrames += impulse->second.Samples.size();
                }
            result.ConvolutionChannelFrames = activeConvolutionChannelFrames;
            for (const auto& bus : definition.Buses)
                for (const auto& effect : bus.Effects)
                {
                    if (effect.Bypassed)
                        continue;
                    const auto impulse = result.ImpulseResponses.find(effect.ImpulseResponse);
                    const auto* impulseResponse = impulse == result.ImpulseResponses.end() ? nullptr : &impulse->second;
                    result.OfflineEffects.emplace(effect.Id,
                                                  Detail::AudioEffectProcessor(effect.Type, SampleRate, Channels,
                                                                               effect.Parameters, impulseResponse));
                }
            return result;
        }

        [[nodiscard]] static bool AddConvolutionChannelFrames(std::size_t& total, const std::size_t additional) noexcept
        {
            if (total > Detail::MaximumAudioSystemConvolutionChannelFrames ||
                additional > Detail::MaximumAudioSystemConvolutionChannelFrames - total)
                return false;
            total += additional;
            return true;
        }

        [[nodiscard]] bool NativeMixerWillExist() const noexcept { return EngineOpen && Mode != AudioMode::Headless; }

        [[nodiscard]] bool MixerSubmissionFitsConvolutionBudget(const AssetId mixer,
                                                                const std::size_t replacementFrames) const noexcept
        {
            std::size_t total = 0;
            bool replacesStoredMixer = false;
            for (const auto& [id, stored] : Mixers)
            {
                const auto frames = id == mixer ? replacementFrames : stored.ConvolutionChannelFrames;
                replacesStoredMixer = replacesStoredMixer || id == mixer;
                if (!AddConvolutionChannelFrames(total, frames))
                    return false;
            }
            if (!replacesStoredMixer && !AddConvolutionChannelFrames(total, replacementFrames))
                return false;
            for (const auto& [id, registered] : MixerRoutings)
            {
                const bool replaced = registered.Mixer == mixer;
                const auto frames = replaced ? replacementFrames : registered.Routing.ConvolutionChannelFrames;
                if (!AddConvolutionChannelFrames(total, frames))
                    return false;
                const bool hasNative = replaced ? NativeMixerWillExist() : NativeMixerRoutings.contains(id);
                if (hasNative && !AddConvolutionChannelFrames(total, frames))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool MixerRegistrationFitsConvolutionBudget(const std::size_t additionalFrames) const noexcept
        {
            std::size_t total = 0;
            for (const auto& [id, stored] : Mixers)
            {
                (void)id;
                if (!AddConvolutionChannelFrames(total, stored.ConvolutionChannelFrames))
                    return false;
            }
            for (const auto& [id, registered] : MixerRoutings)
            {
                if (!AddConvolutionChannelFrames(total, registered.Routing.ConvolutionChannelFrames))
                    return false;
                if (NativeMixerRoutings.contains(id) &&
                    !AddConvolutionChannelFrames(total, registered.Routing.ConvolutionChannelFrames))
                    return false;
            }
            if (!AddConvolutionChannelFrames(total, additionalFrames))
                return false;
            return !NativeMixerWillExist() || AddConvolutionChannelFrames(total, additionalFrames);
        }

        [[nodiscard]] bool MixerUpdateFitsConvolutionBudget(const AudioMixerRoutingId routing,
                                                            const std::size_t replacementFrames) const noexcept
        {
            std::size_t total = 0;
            for (const auto& [id, stored] : Mixers)
            {
                (void)id;
                if (!AddConvolutionChannelFrames(total, stored.ConvolutionChannelFrames))
                    return false;
            }
            for (const auto& [id, registered] : MixerRoutings)
            {
                const bool replaced = id == routing;
                const auto frames = replaced ? replacementFrames : registered.Routing.ConvolutionChannelFrames;
                if (!AddConvolutionChannelFrames(total, frames))
                    return false;
                const bool hasNative = replaced ? NativeMixerWillExist() : NativeMixerRoutings.contains(id);
                if (hasNative && !AddConvolutionChannelFrames(total, frames))
                    return false;
            }
            return true;
        }

        [[nodiscard]] static bool MixerTopologyIsCompatible(const MixerRoutingSnapshot& current,
                                                            const AudioMixerDefinition& definition,
                                                            const AudioMixerImpulseResponses& impulseResponses) noexcept
        {
            if (current.Definition.MasterBus != definition.MasterBus ||
                current.Definition.Buses.size() != definition.Buses.size() ||
                current.Definition.Ducking != definition.Ducking)
                return false;
            for (std::size_t busIndex = 0; busIndex < definition.Buses.size(); ++busIndex)
            {
                const auto& previousBus = current.Definition.Buses[busIndex];
                const auto& replacementBus = definition.Buses[busIndex];
                if (previousBus.Id != replacementBus.Id || previousBus.Parent != replacementBus.Parent ||
                    previousBus.Effects.size() != replacementBus.Effects.size() ||
                    previousBus.Sends.size() != replacementBus.Sends.size())
                    return false;
                for (std::size_t effectIndex = 0; effectIndex < replacementBus.Effects.size(); ++effectIndex)
                {
                    const auto& previousEffect = previousBus.Effects[effectIndex];
                    const auto& replacementEffect = replacementBus.Effects[effectIndex];
                    if (previousEffect.Id != replacementEffect.Id || previousEffect.Type != replacementEffect.Type ||
                        previousEffect.Bypassed != replacementEffect.Bypassed ||
                        previousEffect.ImpulseResponse != replacementEffect.ImpulseResponse)
                        return false;
                    if (replacementEffect.Type != AudioGraphNodeType::ConvolutionReverb)
                        continue;
                    const auto previousImpulse = current.ImpulseResponseAssets.find(replacementEffect.ImpulseResponse);
                    const auto replacementImpulse = impulseResponses.find(replacementEffect.ImpulseResponse);
                    if (previousImpulse == current.ImpulseResponseAssets.end() ||
                        replacementImpulse == impulseResponses.end() ||
                        previousImpulse->second != replacementImpulse->second)
                        return false;
                }
                for (std::size_t sendIndex = 0; sendIndex < replacementBus.Sends.size(); ++sendIndex)
                {
                    const auto& previousSend = previousBus.Sends[sendIndex];
                    const auto& replacementSend = replacementBus.Sends[sendIndex];
                    if (previousSend.Id != replacementSend.Id ||
                        previousSend.DestinationBus != replacementSend.DestinationBus ||
                        previousSend.Stage != replacementSend.Stage)
                        return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool UpdateMixerControls(MixerRoutingSnapshot& current, const AudioMixerDefinition& definition,
                                               const AudioMixerImpulseResponses& impulseResponses,
                                               const std::shared_ptr<NativeMixerGraph>& native)
        {
            if (!MixerTopologyIsCompatible(current, definition, impulseResponses))
                return false;
            MixerRoutingSnapshot values;
            PopulateMixerValues(values, definition);
            values.ImpulseResponseAssets = impulseResponses;
            const auto busFaders = NativeMixerBusFaders(definition);
            for (const auto& bus : definition.Buses)
            {
                for (const auto& effect : bus.Effects)
                {
                    if (effect.Bypassed)
                        continue;
                    if (!current.OfflineEffects.contains(effect.Id))
                        return false;
                    if (native && !native->EffectControls.contains(effect.Id))
                        return false;
                }
                if (native && !native->BusFaders.contains(bus.Id))
                    return false;
                for (const auto& send : bus.Sends)
                    if (native && !native->SendControls.contains(send.Id))
                        return false;
            }

            for (const auto& bus : definition.Buses)
            {
                for (const auto& effect : bus.Effects)
                {
                    if (effect.Bypassed)
                        continue;
                    current.OfflineEffects.at(effect.Id).UpdateParameters(effect.Parameters);
                    if (native)
                        native->EffectControls.at(effect.Id)->Processor->UpdateParameters(effect.Parameters);
                }
                if (native)
                {
                    const std::array fader{busFaders.at(bus.Id)};
                    native->BusFaders.at(bus.Id)->Processor->UpdateParameters(fader);
                    for (const auto& send : bus.Sends)
                        (void)ma_node_set_output_bus_volume(native->SendControls.at(send.Id), 1, send.Gain);
                }
            }

            current.MasterBus = values.MasterBus;
            current.Definition = std::move(values.Definition);
            current.ImpulseResponseAssets = std::move(values.ImpulseResponseAssets);
            current.BusGains = std::move(values.BusGains);
            current.BusNamesById = std::move(values.BusNamesById);
            current.BusIdsByName = std::move(values.BusIdsByName);
            return true;
        }

        static void ProcessNativeEffect(ma_node* base, const float** inputs, ma_uint32* inputFrames, float** outputs,
                                        ma_uint32* outputFrames)
        {
            auto& node = *reinterpret_cast<NativeEffectNode*>(base);
            const auto frames = std::min(*inputFrames, *outputFrames);
            const auto channels = static_cast<std::size_t>(node.Channels);
            const auto sampleCount = static_cast<std::size_t>(frames) * channels;
            node.Processor->Process(std::span<const float>(inputs[0], sampleCount),
                                    std::span<float>(outputs[0], sampleCount));
            float meterPeak = 0.0F;
            double meterSquareSum = 0.0;
            if (node.Type == AudioGraphNodeType::Meter)
            {
                for (std::size_t index = 0; index < sampleCount; ++index)
                {
                    const float value = outputs[0][index];
                    meterPeak = std::max(meterPeak, std::abs(value));
                    meterSquareSum += static_cast<double>(value) * value;
                }
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

        [[nodiscard]] std::shared_ptr<NativeMixerGraph> BuildNativeMixer(const MixerRoutingSnapshot& routing)
        {
            if (!EngineOpen || Mode == AudioMode::Headless)
                return {};
            const auto& definition = routing.Definition;
            const auto busFaders = NativeMixerBusFaders(definition);
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
                                        const Detail::PreparedAudioImpulseResponse* impulseResponse = nullptr,
                                        const AssetId meterBus = AssetId{})
            {
                auto node = std::make_unique<NativeEffectNode>();
                node->Type = type;
                node->Channels = Channels;
                node->MeterBus = meterBus;
                node->Processor = std::make_unique<Detail::AudioEffectProcessor>(type, SampleRate, Channels, parameters,
                                                                                 impulseResponse);
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
            const auto makeDucking = [&](const AudioMixerDuckingDefinition& duckingDefinition)
            {
                auto node = std::make_unique<NativeDuckingNode>();
                node->SampleRate = SampleRate;
                node->Channels = Channels;
                node->ThresholdDb = duckingDefinition.ThresholdDb;
                node->Ratio = duckingDefinition.Ratio;
                node->AttackSeconds = duckingDefinition.AttackSeconds;
                node->HoldSeconds = duckingDefinition.HoldSeconds;
                node->ReleaseSeconds = duckingDefinition.ReleaseSeconds;
                node->MaximumAttenuationDb = duckingDefinition.MaximumAttenuationDb;
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
            struct DuckingConnection final
            {
                AssetId Sidechain;
                ma_node* Node = nullptr;
            };
            std::vector<DuckingConnection> duckingConnections;
            std::map<AssetId, ma_node*> busOutputs;
            for (const auto& bus : definition.Buses)
            {
                auto* current = reinterpret_cast<ma_node*>(result->Groups.at(bus.Id).get());
                for (const auto& effect : bus.Effects)
                {
                    if (effect.Bypassed)
                        continue;
                    const auto impulse = routing.ImpulseResponses.find(effect.ImpulseResponse);
                    const auto* impulseResponse =
                        impulse == routing.ImpulseResponses.end() ? nullptr : &impulse->second;
                    auto* next = makeEffect(effect.Type, effect.Parameters, impulseResponse,
                                            effect.Type == AudioGraphNodeType::Meter ? bus.Id : AssetId{});
                    result->EffectControls.emplace(effect.Id, result->Effects.back().get());
                    attach(current, 0, next);
                    current = next;
                }
                for (const auto& send : bus.Sends)
                {
                    if (send.Stage != AudioMixerSendStage::PreFader)
                        continue;
                    auto* splitter = makeSplitter();
                    result->SendControls.emplace(send.Id, result->Splitters.back().get());
                    attach(current, 0, splitter);
                    attach(splitter, 1, reinterpret_cast<ma_node*>(result->Groups.at(send.DestinationBus).get()));
                    (void)ma_node_set_output_bus_volume(splitter, 1, send.Gain);
                    current = splitter;
                }
                const std::array fader{busFaders.at(bus.Id)};
                auto* faderNode = makeEffect(AudioGraphNodeType::Gain, fader);
                result->BusFaders.emplace(bus.Id, result->Effects.back().get());
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
                auto* meterNode = makeEffect(AudioGraphNodeType::Meter, std::span<const float>{}, nullptr, bus.Id);
                attach(current, 0, meterNode);
                current = meterNode;
                for (const auto& send : bus.Sends)
                {
                    if (send.Stage != AudioMixerSendStage::PostFader)
                        continue;
                    auto* splitter = makeSplitter();
                    result->SendControls.emplace(send.Id, result->Splitters.back().get());
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

        [[nodiscard]] static bool PrepareDeviceMixerAttachment(Voice& voice, const MixerRoutingSnapshot& routing,
                                                               const std::shared_ptr<NativeMixerGraph>& native,
                                                               PreparedNativeVoiceAttachment& attachment) noexcept
        {
            attachment = {};
            if (!voice.Device || !voice.Device->SoundOpen || !voice.Specification.MixerRouting || !native)
                return true;
            auto bus = voice.Specification.BusId;
            if (!bus || !routing.BusGains.contains(bus))
            {
                const auto named = routing.BusIdsByName.find(voice.Specification.Bus);
                bus = named == routing.BusIdsByName.end() ? routing.MasterBus : named->second;
            }
            const auto group = native->Groups.find(bus);
            if (group == native->Groups.end())
                return false;
            auto* source = reinterpret_cast<ma_node*>(&voice.Device->Sound);
            auto* destination = reinterpret_cast<ma_node*>(group->second.get());
            if (source == destination || ma_node_get_output_bus_count(source) == 0 ||
                ma_node_get_input_bus_count(destination) == 0 ||
                ma_node_get_output_channels(source, 0) != ma_node_get_input_channels(destination, 0))
                return false;
            attachment = {.Source = source, .Destination = destination};
            return true;
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

        void UpdateVirtualization(std::vector<Voice*>& ranked) noexcept
        {
            ranked.clear();
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

        void UpdateVirtualization()
        {
            std::vector<Voice*> ranked;
            ranked.reserve(Voices.size());
            UpdateVirtualization(ranked);
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
