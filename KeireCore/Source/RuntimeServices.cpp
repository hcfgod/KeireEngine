#include "Keire/Audio/AudioSystem.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
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
    namespace
    {
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
                !std::isfinite(specification.Gain) || specification.Gain < 0.0F ||
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
            bool Virtualized = false;
            std::unique_ptr<Native> Device;
        };

        explicit Impl(const AudioSystemSpecification specification)
            : Owner(std::this_thread::get_id()), Mode(specification.Mode), MaximumVoices(specification.MaximumVoices),
              MaximumMeterReadings(specification.MaximumMeterReadings)
        {
            auto config = ma_engine_config_init();
            config.noDevice = specification.Mode == AudioMode::Headless ? MA_TRUE : MA_FALSE;
            config.channels = 2;
            config.sampleRate = 48'000;
            const auto result = ma_engine_init(&config, &Engine);
            if (result != MA_SUCCESS)
                throw std::runtime_error("miniaudio engine initialization failed with code " +
                                         std::to_string(static_cast<int>(result)) + ".");
            EngineOpen = true;
        }

        ~Impl()
        {
            for (auto& [id, voice] : Voices)
            {
                (void)id;
                DestroyVoice(voice);
            }
            if (EngineOpen)
                ma_engine_uninit(&Engine);
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error(std::string("AudioSystem::") + operation + " must run on the owner thread.");
            if (!State.IsOpen())
                throw std::logic_error("AudioSystem is closed.");
        }

        [[nodiscard]] float BusGain(const std::string& bus) const noexcept
        {
            const auto master = BusGains.find("Master");
            const auto selected = BusGains.find(bus);
            const auto masterGain = master == BusGains.end() ? 1.0F : master->second;
            if (bus == "Master")
                return masterGain;
            return masterGain * (selected == BusGains.end() ? 1.0F : selected->second);
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
                ranked.push_back(&voice);
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
                    const auto gain =
                        voice.Virtualized ? 0.0F : voice.Specification.Gain * BusGain(voice.Specification.Bus);
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
            ma_sound_set_position(&voice.Device->Sound, specification.Position.X, specification.Position.Y,
                                  specification.Position.Z);
            ma_sound_set_velocity(&voice.Device->Sound, specification.Velocity.X, specification.Velocity.Y,
                                  specification.Velocity.Z);
            ma_sound_set_min_distance(&voice.Device->Sound, specification.MinimumDistance);
            ma_sound_set_max_distance(&voice.Device->Sound, specification.MaximumDistance);
            ma_sound_set_volume(&voice.Device->Sound, voice.Virtualized
                                                          ? 0.0F
                                                          : specification.Gain * BusGain(specification.Bus) *
                                                                (1.0F - specification.Occlusion * 0.75F));
        }

        RuntimeServiceState State{"AudioSystem"};
        std::thread::id Owner;
        AudioMode Mode;
        std::uint32_t MaximumVoices;
        std::uint32_t MaximumMeterReadings;
        ma_engine Engine{};
        bool EngineOpen = false;
        mutable std::mutex Mutex;
        std::shared_ptr<const AudioGraphSnapshot> Graph;
        std::atomic<std::uint64_t> Revision{0};
        std::map<AudioVoiceId, Voice> Voices;
        AudioListenerState Listener;
        std::map<std::string, float, std::less<>> BusGains{{"Master", 1.0F}};
        std::map<std::string, float, std::less<>> SnapshotStart;
        std::map<std::string, float, std::less<>> SnapshotTarget;
        std::chrono::duration<float> SnapshotElapsed{};
        std::chrono::duration<float> SnapshotDuration{};
        std::uint64_t SnapshotRevision = 0;
        std::uint64_t NextVoice = 1;
        std::uint64_t RenderedFrames = 0;
        std::uint64_t Underruns = 0;
        AudioMeterSnapshot Meters;
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
            specification.MaximumVoices > 65536 || specification.MaximumMeterReadings == 0 ||
            specification.MaximumMeterReadings > 4096)
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
        if (frameCount > 16U * 1024U * 1024U ||
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
            else if (node.Type == AudioGraphNodeType::Delay || node.Type == AudioGraphNodeType::Chorus ||
                     node.Type == AudioGraphNodeType::AlgorithmicReverb)
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
                        delayed[index] = result[index] * (1.0F - wet) + echo * wet;
                        if (node.Type == AudioGraphNodeType::AlgorithmicReverb)
                            delayed[index] += echo * feedback * 0.5F;
                    }
                }
                result = std::move(delayed);
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
        if (m_Impl->Voices.size() >= static_cast<std::size_t>(m_Impl->MaximumVoices) * 4U)
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

    std::size_t AudioSystem::StopAll(const std::string_view bus)
    {
        m_Impl->RequireOwner("StopAll");
        std::scoped_lock lock(m_Impl->Mutex);
        std::size_t stopped = 0;
        for (auto iterator = m_Impl->Voices.begin(); iterator != m_Impl->Voices.end();)
        {
            if (!bus.empty() && iterator->second.Specification.Bus != bus)
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
        return m_Impl->BusGain(std::string(bus));
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
            !Math::IsFinite(listener.Velocity))
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
            if (voice.Device && voice.Device->SoundOpen)
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
        if (frameCount > 16U * 1024U * 1024U)
            throw std::invalid_argument("Offline voice render frame count is excessive.");
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->UpdateVirtualization();
        std::vector<float> output(static_cast<std::size_t>(frameCount) * 2U, 0.0F);
        const auto forward = NormalizeVector(m_Impl->Listener.Forward);
        const auto up = NormalizeVector(m_Impl->Listener.Up);
        const Vector3 right{forward.Y * up.Z - forward.Z * up.Y, forward.Z * up.X - forward.X * up.Z,
                            forward.X * up.Y - forward.Y * up.X};
        for (auto& [id, voice] : m_Impl->Voices)
        {
            (void)id;
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
            float leftGain = voice.Specification.Gain * m_Impl->BusGain(voice.Specification.Bus);
            float rightGain = leftGain;
            double pitch = static_cast<double>(voice.Specification.Pitch) * clip.SampleRate / 48'000.0;
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
                const auto pan = std::clamp(Dot(direction, right), -1.0F, 1.0F);
                leftGain *= attenuation * curveAttenuation * std::sqrt(0.5F * (1.0F - pan));
                rightGain *= attenuation * curveAttenuation * std::sqrt(0.5F * (1.0F + pan));
                constexpr float SpeedOfSound = 343.0F;
                const auto listenerRadial = Dot(m_Impl->Listener.Velocity, direction);
                const auto sourceRadial = Dot(voice.Specification.Velocity, direction);
                pitch *= std::clamp(
                    static_cast<double>((SpeedOfSound + listenerRadial) / std::max(1.0F, SpeedOfSound + sourceRadial)),
                    0.5, 2.0);
            }
            leftGain *= 1.0F - voice.Specification.Occlusion * 0.75F;
            rightGain *= 1.0F - voice.Specification.Occlusion * 0.75F;
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
                    const auto left = sample(0);
                    const auto rightSample = clip.Channels == 1 ? left : sample(1);
                    output[static_cast<std::size_t>(frame) * 2U] += left * leftGain;
                    output[static_cast<std::size_t>(frame) * 2U + 1U] += rightSample * rightGain;
                }
                voice.Frame += pitch;
            }
        }
        for (auto& sample : output)
            sample = std::clamp(sample, -1.0F, 1.0F);
        m_Impl->RenderedFrames += frameCount;
        std::erase_if(m_Impl->Voices,
                      [implementation = m_Impl.get()](auto& item)
                      {
                          auto& voice = item.second;
                          const auto frames =
                              voice.Specification.Clip->Frames == 0
                                  ? voice.Specification.Clip->Samples.size() / voice.Specification.Clip->Channels
                                  : voice.Specification.Clip->Frames;
                          if (!voice.Specification.Loop && voice.Frame >= static_cast<double>(frames))
                          {
                              implementation->DestroyVoice(voice);
                              return true;
                          }
                          return false;
                      });
        return output;
    }

    std::vector<AudioVoiceInfo> AudioSystem::Voices() const
    {
        m_Impl->RequireOwner("Voices");
        std::scoped_lock lock(m_Impl->Mutex);
        std::vector<AudioVoiceInfo> result;
        result.reserve(m_Impl->Voices.size());
        for (const auto& [id, voice] : m_Impl->Voices)
            result.push_back({id, voice.Specification.Bus, static_cast<std::uint64_t>(voice.Frame),
                              voice.Specification.Priority, true, voice.Virtualized});
        return result;
    }

    AudioSystemStatistics AudioSystem::Statistics() const
    {
        m_Impl->RequireOwner("Statistics");
        std::scoped_lock lock(m_Impl->Mutex);
        AudioSystemStatistics result;
        result.Voices = m_Impl->Voices.size();
        result.VirtualVoices =
            std::ranges::count_if(m_Impl->Voices, [](const auto& item) { return item.second.Virtualized; });
        result.AudibleVoices = result.Voices - result.VirtualVoices;
        result.RenderedFrames = m_Impl->RenderedFrames;
        result.Underruns = m_Impl->Underruns;
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
        return m_Impl->Meters;
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
        m_Impl->Meters = {};
        if (std::exchange(m_Impl->EngineOpen, false))
            ma_engine_uninit(&m_Impl->Engine);
    }

} // namespace Keire
