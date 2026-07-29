#include "Keire/Audio/AudioAssets.h"

#include "KeireInternal/Audio/AudioImportBackend.h"

#include <miniaudio.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        constexpr std::byte Magic[] = {
            std::byte{'K'}, std::byte{'E'}, std::byte{'A'}, std::byte{'U'},
            std::byte{'D'}, std::byte{'I'}, std::byte{'O'}, std::byte{'1'},
        };
        constexpr std::uint32_t LegacySchemaVersion = 1;
        constexpr std::uint32_t SchemaVersion = 2;
        constexpr std::uint32_t ResidentPcmStorage = 0;
        constexpr std::uint32_t EncodedStreamStorage = 1;
        constexpr std::uint64_t MaximumFrames = 384000ULL * 60ULL * 60ULL * 4ULL;
        constexpr std::size_t MaximumEncodedSourceBytes = 768ULL * 1024ULL * 1024ULL;
        constexpr std::size_t StreamingPcmThresholdBytes = 64ULL * 1024ULL * 1024ULL;

        void AppendU32(std::vector<std::byte>& output, const std::uint32_t value)
        {
            for (std::uint32_t shift = 0; shift < 32; shift += 8)
                output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }

        void AppendU64(std::vector<std::byte>& output, const std::uint64_t value)
        {
            for (std::uint32_t shift = 0; shift < 64; shift += 8)
                output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }

        [[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes, std::size_t& cursor)
        {
            if (bytes.size() - cursor < 4)
                throw std::invalid_argument("Audio clip payload is truncated.");
            std::uint32_t result = 0;
            for (std::uint32_t shift = 0; shift < 32; shift += 8)
                result |= std::to_integer<std::uint32_t>(bytes[cursor++]) << shift;
            return result;
        }

        [[nodiscard]] std::uint64_t ReadU64(const std::span<const std::byte> bytes, std::size_t& cursor)
        {
            if (bytes.size() - cursor < 8)
                throw std::invalid_argument("Audio clip payload is truncated.");
            std::uint64_t result = 0;
            for (std::uint32_t shift = 0; shift < 64; shift += 8)
                result |= std::to_integer<std::uint64_t>(bytes[cursor++]) << shift;
            return result;
        }

        void ValidateClip(const AudioClipData& clip)
        {
            const auto residentFrames =
                clip.Channels == 0 ? 0ULL : static_cast<std::uint64_t>(clip.Samples.size() / clip.Channels);
            const auto frames = clip.Frames == 0 ? residentFrames : clip.Frames;
            const bool resident =
                !clip.Streaming && !clip.Samples.empty() && clip.EncodedSource.empty() &&
                clip.Samples.size() % clip.Channels == 0 && residentFrames == frames &&
                std::ranges::all_of(clip.Samples, [](const float sample) { return std::isfinite(sample); });
            const bool streaming = clip.Streaming && clip.Samples.empty() && !clip.EncodedSource.empty() &&
                                   clip.EncodedSource.size() <= MaximumEncodedSourceBytes;
            if (clip.SampleRate < 8000 || clip.SampleRate > 384000 || clip.Channels == 0 || clip.Channels > 8 ||
                frames == 0 || frames > MaximumFrames || (!resident && !streaming))
                throw std::invalid_argument("Audio clip data is empty, non-finite, or outside supported limits.");
        }

        void AppendHeader(std::vector<std::byte>& output, const std::uint32_t sampleRate, const std::uint32_t channels,
                          const std::uint64_t frames, const std::uint32_t storage, const std::uint64_t payloadBytes)
        {
            output.insert(output.end(), std::begin(Magic), std::end(Magic));
            AppendU32(output, SchemaVersion);
            AppendU32(output, sampleRate);
            AppendU32(output, channels);
            AppendU64(output, frames);
            AppendU32(output, storage);
            AppendU64(output, payloadBytes);
        }

        [[nodiscard]] std::vector<std::byte> ImportSource(const std::span<const std::byte> bytes)
        {
            if (bytes.empty() || bytes.size() > MaximumEncodedSourceBytes)
                throw std::invalid_argument("Audio source is empty or exceeds the 768 MiB import limit.");
            ma_decoder decoder{};
            auto configuration = ma_decoder_config_init(ma_format_f32, 0, 0);
            if (ma_decoder_init_memory(bytes.data(), bytes.size(), &configuration, &decoder) != MA_SUCCESS)
                throw std::invalid_argument("Audio source is unsupported or corrupt.");
            const auto close = [&decoder] { ma_decoder_uninit(&decoder); };

            ma_format format = ma_format_unknown;
            ma_uint32 channels = 0;
            ma_uint32 sampleRate = 0;
            if (ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0) != MA_SUCCESS ||
                format != ma_format_f32 || channels == 0 || channels > 8 || sampleRate < 8000 || sampleRate > 384000)
            {
                close();
                throw std::invalid_argument("Audio source channel or sample-rate layout is unsupported.");
            }
            ma_uint64 frames = 0;
            if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) != MA_SUCCESS || frames == 0 ||
                frames > MaximumFrames)
            {
                close();
                throw std::invalid_argument("Audio source duration is unavailable or exceeds four hours.");
            }
            if (frames > std::numeric_limits<std::size_t>::max() / channels)
            {
                close();
                throw std::invalid_argument("Audio source dimensions exceed the current platform.");
            }
            const auto sampleCount = static_cast<std::size_t>(frames) * channels;
            if (sampleCount > std::numeric_limits<std::size_t>::max() / sizeof(float))
            {
                close();
                throw std::invalid_argument("Audio source decoded size exceeds the current platform.");
            }
            if (sampleCount * sizeof(float) > StreamingPcmThresholdBytes)
            {
                close();
                std::vector<std::byte> result;
                result.reserve(std::size(Magic) + 32U + bytes.size());
                AppendHeader(result, sampleRate, channels, frames, EncodedStreamStorage, bytes.size());
                result.insert(result.end(), bytes.begin(), bytes.end());
                return result;
            }

            AudioClipData result;
            result.SampleRate = sampleRate;
            result.Channels = channels;
            result.Frames = frames;
            result.Samples.resize(sampleCount);
            ma_uint64 decoded = 0;
            const auto status = ma_decoder_read_pcm_frames(&decoder, result.Samples.data(), frames, &decoded);
            close();
            if ((status != MA_SUCCESS && status != MA_AT_END) || decoded != frames)
                throw std::invalid_argument("Audio source decoding did not produce the expected frame count.");
            ValidateClip(result);
            return AudioClipAsset::Encode(result);
        }
    } // namespace

    AudioClipAsset::AudioClipAsset(std::shared_ptr<const AudioClipData> clip) : m_Clip(std::move(clip))
    {
        if (!m_Clip)
            throw std::invalid_argument("AudioClipAsset requires decoded clip data.");
        ValidateClip(*m_Clip);
    }

    std::size_t AudioClipAsset::ResidentBytes() const noexcept
    {
        return sizeof(AudioClipData) + m_Clip->Samples.size() * sizeof(float) + m_Clip->EncodedSource.size();
    }

    std::uint64_t AudioClipAsset::FrameCount() const noexcept
    {
        return m_Clip->Frames == 0 ? static_cast<std::uint64_t>(m_Clip->Samples.size() / m_Clip->Channels)
                                   : m_Clip->Frames;
    }

    float AudioClipAsset::DurationSeconds() const noexcept
    {
        return static_cast<float>(FrameCount()) / static_cast<float>(m_Clip->SampleRate);
    }

    std::vector<std::byte> AudioClipAsset::Encode(const AudioClipData& clip)
    {
        ValidateClip(clip);
        const auto frames =
            clip.Frames == 0 ? static_cast<std::uint64_t>(clip.Samples.size() / clip.Channels) : clip.Frames;
        std::vector<std::byte> result;
        const auto payloadBytes = clip.Streaming ? clip.EncodedSource.size() : clip.Samples.size() * sizeof(float);
        result.reserve(std::size(Magic) + 32U + payloadBytes);
        AppendHeader(result, clip.SampleRate, clip.Channels, frames,
                     clip.Streaming ? EncodedStreamStorage : ResidentPcmStorage, payloadBytes);
        if (clip.Streaming)
        {
            result.insert(result.end(), clip.EncodedSource.begin(), clip.EncodedSource.end());
            return result;
        }
        for (const float sample : clip.Samples)
            AppendU32(result, std::bit_cast<std::uint32_t>(sample));
        return result;
    }

    Ref<AudioClipAsset> AudioClipAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.size() < std::size(Magic) + 20 || !std::equal(std::begin(Magic), std::end(Magic), bytes.begin()))
            throw std::invalid_argument("Audio clip payload has an invalid signature.");
        std::size_t cursor = std::size(Magic);
        const auto version = ReadU32(bytes, cursor);
        if (version != LegacySchemaVersion && version != SchemaVersion)
            throw std::invalid_argument("Audio clip payload schema is unsupported.");
        auto clip = std::make_shared<AudioClipData>();
        clip->SampleRate = ReadU32(bytes, cursor);
        clip->Channels = ReadU32(bytes, cursor);
        const auto frames = ReadU64(bytes, cursor);
        if (clip->Channels == 0 || frames == 0 || frames > MaximumFrames ||
            frames > std::numeric_limits<std::size_t>::max() / clip->Channels)
            throw std::invalid_argument("Audio clip payload dimensions are invalid.");
        clip->Frames = frames;
        if (version == SchemaVersion)
        {
            const auto storage = ReadU32(bytes, cursor);
            const auto payloadBytes = ReadU64(bytes, cursor);
            if (payloadBytes > std::numeric_limits<std::size_t>::max() ||
                bytes.size() - cursor != static_cast<std::size_t>(payloadBytes))
                throw std::invalid_argument("Audio clip payload size does not match its header.");
            if (storage == EncodedStreamStorage)
            {
                clip->Streaming = true;
                clip->EncodedSource.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end());
                ValidateClip(*clip);
                return CreateRef<AudioClipAsset>(std::move(clip));
            }
            if (storage != ResidentPcmStorage)
                throw std::invalid_argument("Audio clip payload storage mode is unsupported.");
        }
        const auto sampleCount = static_cast<std::size_t>(frames) * clip->Channels;
        if (bytes.size() - cursor != sampleCount * sizeof(float))
            throw std::invalid_argument("Audio clip payload sample count does not match its header.");
        clip->Samples.resize(sampleCount);
        for (auto& sample : clip->Samples)
            sample = std::bit_cast<float>(ReadU32(bytes, cursor));
        ValidateClip(*clip);
        return CreateRef<AudioClipAsset>(std::move(clip));
    }

    Ref<AudioClipAsset> AudioClipAsset::Silence()
    {
        auto clip = std::make_shared<AudioClipData>();
        clip->Samples.assign(480, 0.0F);
        clip->Frames = clip->Samples.size();
        return CreateRef<AudioClipAsset>(std::move(clip));
    }

    namespace
    {
        [[nodiscard]] AssetImportOutput
        ImportAudioWithFallback(const AssetImportContext& context, const std::span<const std::byte> source,
                                const std::function<std::vector<std::byte>(std::span<const std::byte>)>& nativeImport,
                                const Detail::AudioTranscodeBackend& backend)
        {
            std::string nativeDiagnostic;
            try
            {
                AssetImportOutput output;
                output.Bytes = nativeImport(source);
                return output;
            }
            catch (const std::exception& error)
            {
                nativeDiagnostic = error.what();
            }

            if (!backend)
                throw std::runtime_error(
                    "This audio codec or media container requires the private FFmpeg asset-worker backend. "
                    "Regenerate the engine after building its locked FFmpeg dependency. Native decoder diagnostic: " +
                    nativeDiagnostic);

            AssetImportOutput output;
            auto transcoded = backend(context, source);
            if (transcoded.EncodedAudio.empty())
                throw std::runtime_error("The private FFmpeg backend produced an empty audio stream.");
            if (transcoded.EncodedAudio.size() > MaximumEncodedSourceBytes || transcoded.SampleRate < 8000 ||
                transcoded.SampleRate > 384000 || transcoded.Channels == 0 || transcoded.Channels > 8 ||
                transcoded.Frames == 0 || transcoded.Frames > MaximumFrames)
                throw std::runtime_error("The private FFmpeg backend produced audio outside supported limits.");
            output.Bytes.reserve(std::size(Magic) + 32U + transcoded.EncodedAudio.size());
            AppendHeader(output.Bytes, transcoded.SampleRate, transcoded.Channels, transcoded.Frames,
                         EncodedStreamStorage, transcoded.EncodedAudio.size());
            output.Bytes.insert(output.Bytes.end(), transcoded.EncodedAudio.begin(), transcoded.EncodedAudio.end());
            const auto runtimeEncoding =
                transcoded.RuntimeEncoding.empty() ? std::string_view("runtime audio") : transcoded.RuntimeEncoding;
            std::string diagnostic = "Audio stream was converted in-process from " + transcoded.SourceContainer + "/" +
                                     transcoded.SourceCodec + " to " + std::string(runtimeEncoding);
            if (transcoded.SampleRate != 0 && transcoded.Channels != 0)
                diagnostic += " (" + std::to_string(transcoded.SampleRate) + " Hz, " +
                              std::to_string(transcoded.Channels) + " channels)";
            diagnostic += ".";
            output.Diagnostics.push_back({AssetDiagnosticSeverity::Information, {}, 0, 0, std::move(diagnostic)});
            return output;
        }
    } // namespace

    AssetImporterRegistration Detail::CreateAudioClipAssetImporter(Detail::AudioTranscodeBackend backend)
    {
        AssetImporterRegistration result;
        result.Name = "Keire.AudioClip";
        result.Version = 7;
        result.Type = AudioClipAsset::StaticType();
        result.Extensions = {
            ".wav",  ".wave", ".flac", ".ogg",  ".oga",  ".mp3",  ".mp2", ".aac", ".ac3", ".eac3", ".m4a",
            ".m4b",  ".mp4",  ".mka",  ".mkv",  ".webm", ".weba", ".mov", ".wma", ".asf", ".aif",  ".aiff",
            ".aifc", ".caf",  ".opus", ".spx",  ".amr",  ".ape",  ".wv",  ".tta", ".mpc", ".mpeg", ".mpg",
            ".3gp",  ".3g2",  ".ts",   ".m2ts", ".mts",  ".au",   ".snd", ".voc", ".ra",  ".rm",
        };
        result.Import = [](const std::span<const std::byte> bytes) { return ImportSource(bytes); };
        const auto nativeImport = result.Import;
        result.ContextualImport = [nativeImport, backend = std::move(backend)](const AssetImportContext& context,
                                                                               const std::span<const std::byte> bytes)
        { return ImportAudioWithFallback(context, bytes, nativeImport, backend); };
        result.ImportOptions = {
            {"audioStream",
             "Audio Stream",
             "Source",
             AssetImportOptionKind::Integer,
             std::int64_t{0},
             0.0,
             255.0,
             1.0,
             {}},
            {"transcodeMode",
             "Transcode Mode",
             "Compression",
             AssetImportOptionKind::Choice,
             std::string("fast"),
             {},
             {},
             1.0,
             {"fast", "compressed"}},
        };
        result.RestoreCachedOutput = [](const std::span<const std::byte> bytes)
        {
            (void)AudioClipAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes.assign(bytes.begin(), bytes.end());
            return output;
        };
        return result;
    }

    AssetImporterRegistration CreateAudioClipAssetImporter() { return Detail::CreateAudioClipAssetImporter({}); }

    AssetDecoderRegistration CreateAudioClipAssetDecoder()
    {
        return {
            AudioClipAsset::StaticType(),
            AudioClipAsset::Silence(),
            [](const std::span<const std::byte> bytes) -> Ref<Asset> { return AudioClipAsset::Decode(bytes); },
        };
    }

    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumMixerBuses = 256;
        constexpr std::size_t MaximumMixerEffectsPerBus = 128;
        constexpr std::size_t MaximumMixerSendsPerBus = 64;
        constexpr std::size_t MaximumMixerEffectParameters = 64;
        constexpr std::size_t MaximumMixerSnapshots = 128;
        constexpr std::size_t MaximumMixerSnapshotParameters = 4096;
        constexpr std::size_t MaximumMixerDuckingRules = 128;
        constexpr std::size_t MaximumMixerNameLength = 128;

        [[nodiscard]] std::string IdText(const AssetId id) { return id ? id.ToString() : std::string{}; }

        [[nodiscard]] AssetId ParseId(const Json& object, const char* key)
        {
            const auto text = object.value(key, std::string{});
            return text.empty() ? AssetId{} : AssetId::Parse(text);
        }

        [[nodiscard]] std::string_view GraphNodeTypeName(const AudioGraphNodeType type)
        {
            switch (type)
            {
            case AudioGraphNodeType::Input:
                return "input";
            case AudioGraphNodeType::Gain:
                return "gain";
            case AudioGraphNodeType::LowPass:
                return "lowPass";
            case AudioGraphNodeType::HighPass:
                return "highPass";
            case AudioGraphNodeType::Equalizer:
                return "equalizer";
            case AudioGraphNodeType::Compressor:
                return "compressor";
            case AudioGraphNodeType::Limiter:
                return "limiter";
            case AudioGraphNodeType::Gate:
                return "gate";
            case AudioGraphNodeType::Delay:
                return "delay";
            case AudioGraphNodeType::Chorus:
                return "chorus";
            case AudioGraphNodeType::Distortion:
                return "distortion";
            case AudioGraphNodeType::AlgorithmicReverb:
                return "algorithmicReverb";
            case AudioGraphNodeType::ConvolutionReverb:
                return "convolutionReverb";
            case AudioGraphNodeType::Meter:
                return "meter";
            case AudioGraphNodeType::Capture:
                return "capture";
            case AudioGraphNodeType::Output:
                return "output";
            }
            throw std::invalid_argument("Audio mixer effect type is unsupported.");
        }

        [[nodiscard]] AudioGraphNodeType ParseGraphNodeType(const std::string_view value)
        {
            if (value == "input")
                return AudioGraphNodeType::Input;
            if (value == "gain")
                return AudioGraphNodeType::Gain;
            if (value == "lowPass")
                return AudioGraphNodeType::LowPass;
            if (value == "highPass")
                return AudioGraphNodeType::HighPass;
            if (value == "equalizer")
                return AudioGraphNodeType::Equalizer;
            if (value == "compressor")
                return AudioGraphNodeType::Compressor;
            if (value == "limiter")
                return AudioGraphNodeType::Limiter;
            if (value == "gate")
                return AudioGraphNodeType::Gate;
            if (value == "delay")
                return AudioGraphNodeType::Delay;
            if (value == "chorus")
                return AudioGraphNodeType::Chorus;
            if (value == "distortion")
                return AudioGraphNodeType::Distortion;
            if (value == "algorithmicReverb")
                return AudioGraphNodeType::AlgorithmicReverb;
            if (value == "convolutionReverb")
                return AudioGraphNodeType::ConvolutionReverb;
            if (value == "meter")
                return AudioGraphNodeType::Meter;
            if (value == "capture")
                return AudioGraphNodeType::Capture;
            if (value == "output")
                return AudioGraphNodeType::Output;
            throw std::invalid_argument("Audio mixer effect type is unsupported.");
        }

        [[nodiscard]] std::string_view SendStageName(const AudioMixerSendStage stage)
        {
            switch (stage)
            {
            case AudioMixerSendStage::PreFader:
                return "preFader";
            case AudioMixerSendStage::PostFader:
                return "postFader";
            }
            throw std::invalid_argument("Audio mixer send stage is unsupported.");
        }

        [[nodiscard]] AudioMixerSendStage ParseSendStage(const std::string_view value)
        {
            if (value == "preFader")
                return AudioMixerSendStage::PreFader;
            if (value == "postFader")
                return AudioMixerSendStage::PostFader;
            throw std::invalid_argument("Audio mixer send stage is unsupported.");
        }

        [[nodiscard]] std::string_view SnapshotParameterTypeName(const AudioMixerSnapshotParameterType type)
        {
            switch (type)
            {
            case AudioMixerSnapshotParameterType::BusGain:
                return "busGain";
            case AudioMixerSnapshotParameterType::BusMute:
                return "busMute";
            case AudioMixerSnapshotParameterType::BusSolo:
                return "busSolo";
            case AudioMixerSnapshotParameterType::SendGain:
                return "sendGain";
            case AudioMixerSnapshotParameterType::EffectBypass:
                return "effectBypass";
            case AudioMixerSnapshotParameterType::EffectParameter:
                return "effectParameter";
            }
            throw std::invalid_argument("Audio mixer snapshot parameter type is unsupported.");
        }

        [[nodiscard]] AudioMixerSnapshotParameterType ParseSnapshotParameterType(const std::string_view value)
        {
            if (value == "busGain")
                return AudioMixerSnapshotParameterType::BusGain;
            if (value == "busMute")
                return AudioMixerSnapshotParameterType::BusMute;
            if (value == "busSolo")
                return AudioMixerSnapshotParameterType::BusSolo;
            if (value == "sendGain")
                return AudioMixerSnapshotParameterType::SendGain;
            if (value == "effectBypass")
                return AudioMixerSnapshotParameterType::EffectBypass;
            if (value == "effectParameter")
                return AudioMixerSnapshotParameterType::EffectParameter;
            throw std::invalid_argument("Audio mixer snapshot parameter type is unsupported.");
        }

        void RequireStableId(const AssetId id, std::set<AssetId>& stableIds)
        {
            if (!id || !stableIds.insert(id).second)
                throw std::invalid_argument("Audio mixer contains an empty or duplicate stable ID.");
        }

        void RequireName(const std::string_view name)
        {
            if (name.empty() || name.size() > MaximumMixerNameLength)
                throw std::invalid_argument("Audio mixer contains an empty or excessive name.");
        }

        [[nodiscard]] bool IsBooleanValue(const float value) noexcept { return value == 0.0F || value == 1.0F; }
    } // namespace

    void ValidateAudioMixer(const AudioMixerDefinition& definition)
    {
        if (definition.SchemaVersion != 1 || !definition.MasterBus || definition.Buses.empty() ||
            definition.Buses.size() > MaximumMixerBuses || definition.Snapshots.size() > MaximumMixerSnapshots ||
            definition.Ducking.size() > MaximumMixerDuckingRules)
            throw std::invalid_argument("Audio mixer header is invalid.");

        std::set<AssetId> stableIds;
        std::set<std::string, std::less<>> busNames;
        std::set<std::string, std::less<>> snapshotNames;
        std::set<std::string, std::less<>> duckingNames;
        std::map<AssetId, const AudioMixerBusDefinition*> buses;
        std::map<AssetId, const AudioMixerEffectDefinition*> effects;
        std::map<AssetId, const AudioMixerSendDefinition*> sends;

        for (const auto& bus : definition.Buses)
        {
            RequireStableId(bus.Id, stableIds);
            RequireName(bus.Name);
            if (!busNames.insert(bus.Name).second || !std::isfinite(bus.Gain) || bus.Gain < 0.0F || bus.Gain > 16.0F ||
                bus.Effects.size() > MaximumMixerEffectsPerBus || bus.Sends.size() > MaximumMixerSendsPerBus ||
                !buses.emplace(bus.Id, &bus).second)
                throw std::invalid_argument("Audio mixer contains an invalid or duplicate bus.");
            for (const auto& effect : bus.Effects)
            {
                RequireStableId(effect.Id, stableIds);
                RequireName(effect.Name);
                const bool processingType =
                    effect.Type >= AudioGraphNodeType::Gain && effect.Type <= AudioGraphNodeType::Capture;
                const bool validImpulseResponse = effect.Type == AudioGraphNodeType::ConvolutionReverb
                                                      ? static_cast<bool>(effect.ImpulseResponse)
                                                      : !effect.ImpulseResponse;
                if (!processingType || !validImpulseResponse ||
                    effect.Parameters.size() > MaximumMixerEffectParameters ||
                    !std::ranges::all_of(effect.Parameters, [](const float value) { return std::isfinite(value); }) ||
                    !effects.emplace(effect.Id, &effect).second)
                    throw std::invalid_argument("Audio mixer contains an invalid or duplicate effect.");
            }
            for (const auto& send : bus.Sends)
            {
                RequireStableId(send.Id, stableIds);
                if (!send.DestinationBus || send.DestinationBus == bus.Id || !std::isfinite(send.Gain) ||
                    send.Gain < 0.0F || send.Gain > 16.0F ||
                    (send.Stage != AudioMixerSendStage::PreFader && send.Stage != AudioMixerSendStage::PostFader) ||
                    !sends.emplace(send.Id, &send).second)
                    throw std::invalid_argument("Audio mixer contains an invalid or duplicate send.");
            }
        }

        const auto master = buses.find(definition.MasterBus);
        if (master == buses.end() || master->second->Name != "Master" || master->second->Parent)
            throw std::invalid_argument("Audio mixer Master bus is unavailable or malformed.");
        for (const auto& [id, bus] : buses)
        {
            if (id == definition.MasterBus)
                continue;
            if (!bus->Parent || !buses.contains(bus->Parent))
                throw std::invalid_argument("Audio mixer bus parent is unavailable.");
        }

        std::map<AssetId, std::vector<AssetId>> routing;
        for (const auto& [id, bus] : buses)
        {
            auto& destinations = routing[id];
            if (bus->Parent)
                destinations.push_back(bus->Parent);
            for (const auto& send : bus->Sends)
            {
                if (!buses.contains(send.DestinationBus))
                    throw std::invalid_argument("Audio mixer send destination is unavailable.");
                destinations.push_back(send.DestinationBus);
            }
        }
        std::map<AssetId, std::uint8_t> visitState;
        const auto visit = [&](const auto& self, const AssetId bus) -> void
        {
            if (visitState[bus] == 1)
                throw std::invalid_argument("Audio mixer routing contains a cycle.");
            if (visitState[bus] == 2)
                return;
            visitState[bus] = 1;
            for (const auto destination : routing.at(bus))
                self(self, destination);
            visitState[bus] = 2;
        };
        for (const auto& [id, bus] : buses)
        {
            (void)bus;
            visit(visit, id);
        }

        for (const auto& snapshot : definition.Snapshots)
        {
            RequireStableId(snapshot.Id, stableIds);
            RequireName(snapshot.Name);
            if (!snapshotNames.insert(snapshot.Name).second ||
                snapshot.Parameters.size() > MaximumMixerSnapshotParameters)
                throw std::invalid_argument("Audio mixer contains an invalid or duplicate snapshot.");
            std::set<std::tuple<AudioMixerSnapshotParameterType, AssetId, std::uint32_t>> targets;
            for (const auto& parameter : snapshot.Parameters)
            {
                if (!parameter.Target || !std::isfinite(parameter.Value) ||
                    !targets.emplace(parameter.Type, parameter.Target, parameter.Parameter).second)
                    throw std::invalid_argument("Audio mixer snapshot contains an invalid or duplicate parameter.");
                switch (parameter.Type)
                {
                case AudioMixerSnapshotParameterType::BusGain:
                    if (!buses.contains(parameter.Target) || parameter.Parameter != 0 || parameter.Value < 0.0F ||
                        parameter.Value > 16.0F)
                        throw std::invalid_argument("Audio mixer snapshot bus gain is invalid.");
                    break;
                case AudioMixerSnapshotParameterType::BusMute:
                case AudioMixerSnapshotParameterType::BusSolo:
                    if (!buses.contains(parameter.Target) || parameter.Parameter != 0 ||
                        !IsBooleanValue(parameter.Value))
                        throw std::invalid_argument("Audio mixer snapshot bus switch is invalid.");
                    break;
                case AudioMixerSnapshotParameterType::SendGain:
                    if (!sends.contains(parameter.Target) || parameter.Parameter != 0 || parameter.Value < 0.0F ||
                        parameter.Value > 16.0F)
                        throw std::invalid_argument("Audio mixer snapshot send gain is invalid.");
                    break;
                case AudioMixerSnapshotParameterType::EffectBypass:
                    if (!effects.contains(parameter.Target) || parameter.Parameter != 0 ||
                        !IsBooleanValue(parameter.Value))
                        throw std::invalid_argument("Audio mixer snapshot effect bypass is invalid.");
                    break;
                case AudioMixerSnapshotParameterType::EffectParameter:
                {
                    const auto found = effects.find(parameter.Target);
                    if (found == effects.end() || parameter.Parameter >= found->second->Parameters.size())
                        throw std::invalid_argument("Audio mixer snapshot effect parameter is unavailable.");
                    break;
                }
                default:
                    throw std::invalid_argument("Audio mixer snapshot parameter type is unsupported.");
                }
            }
        }

        for (const auto& ducking : definition.Ducking)
        {
            RequireStableId(ducking.Id, stableIds);
            RequireName(ducking.Name);
            if (!duckingNames.insert(ducking.Name).second || !buses.contains(ducking.SidechainBus) ||
                !buses.contains(ducking.TargetBus) || ducking.SidechainBus == ducking.TargetBus ||
                !std::isfinite(ducking.ThresholdDb) || ducking.ThresholdDb < -96.0F || ducking.ThresholdDb > 0.0F ||
                !std::isfinite(ducking.Ratio) || ducking.Ratio < 1.0F || ducking.Ratio > 100.0F ||
                !std::isfinite(ducking.AttackSeconds) || ducking.AttackSeconds < 0.0F ||
                ducking.AttackSeconds > 10.0F || !std::isfinite(ducking.HoldSeconds) || ducking.HoldSeconds < 0.0F ||
                ducking.HoldSeconds > 10.0F || !std::isfinite(ducking.ReleaseSeconds) ||
                ducking.ReleaseSeconds < 0.0F || ducking.ReleaseSeconds > 30.0F ||
                !std::isfinite(ducking.MaximumAttenuationDb) || ducking.MaximumAttenuationDb < 0.0F ||
                ducking.MaximumAttenuationDb > 96.0F)
                throw std::invalid_argument("Audio mixer contains an invalid or duplicate ducking rule.");
        }
    }

    std::vector<AssetId> AudioMixerDependencies(const AudioMixerDefinition& definition)
    {
        ValidateAudioMixer(definition);
        std::vector<AssetId> result;
        for (const auto& bus : definition.Buses)
            for (const auto& effect : bus.Effects)
                if (effect.ImpulseResponse)
                    result.push_back(effect.ImpulseResponse);
        std::ranges::sort(result);
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    AudioMixerAsset::AudioMixerAsset(AudioMixerDefinition definition) : m_Definition(std::move(definition))
    {
        ValidateAudioMixer(m_Definition);
    }

    std::size_t AudioMixerAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& bus : m_Definition.Buses)
        {
            result += sizeof(bus) + bus.Name.size();
            for (const auto& effect : bus.Effects)
                result += sizeof(effect) + effect.Name.size() + effect.Parameters.size() * sizeof(float);
            result += bus.Sends.size() * sizeof(AudioMixerSendDefinition);
        }
        for (const auto& snapshot : m_Definition.Snapshots)
            result += sizeof(snapshot) + snapshot.Name.size() +
                      snapshot.Parameters.size() * sizeof(AudioMixerSnapshotParameterDefinition);
        for (const auto& ducking : m_Definition.Ducking)
            result += sizeof(ducking) + ducking.Name.size();
        return result;
    }

    AudioMixerDefinition AudioMixerAsset::DefaultDefinition()
    {
        const AssetId master(0x4b454952454d4958ULL, 0x45524d4153544552ULL);
        return {.SchemaVersion = 1, .MasterBus = master, .Buses = {{.Id = master, .Name = "Master", .Gain = 1.0F}}};
    }

    std::vector<std::byte> AudioMixerAsset::Encode(const AudioMixerDefinition& definition)
    {
        ValidateAudioMixer(definition);
        Json buses = Json::array();
        for (const auto& bus : definition.Buses)
        {
            Json effects = Json::array();
            for (const auto& effect : bus.Effects)
                effects.push_back({{"id", IdText(effect.Id)},
                                   {"name", effect.Name},
                                   {"type", GraphNodeTypeName(effect.Type)},
                                   {"bypassed", effect.Bypassed},
                                   {"parameters", effect.Parameters},
                                   {"impulseResponse", IdText(effect.ImpulseResponse)}});
            Json sends = Json::array();
            for (const auto& send : bus.Sends)
                sends.push_back({{"id", IdText(send.Id)},
                                 {"destinationBus", IdText(send.DestinationBus)},
                                 {"stage", SendStageName(send.Stage)},
                                 {"gain", send.Gain}});
            buses.push_back({{"id", IdText(bus.Id)},
                             {"name", bus.Name},
                             {"parent", IdText(bus.Parent)},
                             {"mute", bus.Mute},
                             {"solo", bus.Solo},
                             {"gain", bus.Gain},
                             {"effects", std::move(effects)},
                             {"sends", std::move(sends)}});
        }

        Json snapshots = Json::array();
        for (const auto& snapshot : definition.Snapshots)
        {
            Json parameters = Json::array();
            for (const auto& parameter : snapshot.Parameters)
                parameters.push_back({{"type", SnapshotParameterTypeName(parameter.Type)},
                                      {"target", IdText(parameter.Target)},
                                      {"parameter", parameter.Parameter},
                                      {"value", parameter.Value}});
            snapshots.push_back(
                {{"id", IdText(snapshot.Id)}, {"name", snapshot.Name}, {"parameters", std::move(parameters)}});
        }

        Json ducking = Json::array();
        for (const auto& rule : definition.Ducking)
            ducking.push_back({{"id", IdText(rule.Id)},
                               {"name", rule.Name},
                               {"sidechainBus", IdText(rule.SidechainBus)},
                               {"targetBus", IdText(rule.TargetBus)},
                               {"thresholdDb", rule.ThresholdDb},
                               {"ratio", rule.Ratio},
                               {"attackSeconds", rule.AttackSeconds},
                               {"holdSeconds", rule.HoldSeconds},
                               {"releaseSeconds", rule.ReleaseSeconds},
                               {"maximumAttenuationDb", rule.MaximumAttenuationDb}});

        const Json document{{"schemaVersion", definition.SchemaVersion},
                            {"masterBus", IdText(definition.MasterBus)},
                            {"buses", std::move(buses)},
                            {"snapshots", std::move(snapshots)},
                            {"ducking", std::move(ducking)}};
        const auto text = document.dump(2) + '\n';
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<AudioMixerAsset> AudioMixerAsset::Decode(const std::span<const std::byte> bytes)
    {
        try
        {
            const Json document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                              reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            AudioMixerDefinition definition;
            definition.SchemaVersion = document.value("schemaVersion", 0U);
            definition.MasterBus = ParseId(document, "masterBus");
            for (const auto& encodedBus : document.at("buses"))
            {
                AudioMixerBusDefinition bus;
                bus.Id = ParseId(encodedBus, "id");
                bus.Name = encodedBus.at("name").get<std::string>();
                bus.Parent = ParseId(encodedBus, "parent");
                bus.Mute = encodedBus.value("mute", false);
                bus.Solo = encodedBus.value("solo", false);
                bus.Gain = encodedBus.value("gain", 1.0F);
                for (const auto& encodedEffect : encodedBus.value("effects", Json::array()))
                {
                    AudioMixerEffectDefinition effect;
                    effect.Id = ParseId(encodedEffect, "id");
                    effect.Name = encodedEffect.at("name").get<std::string>();
                    effect.Type = ParseGraphNodeType(encodedEffect.at("type").get<std::string>());
                    effect.Bypassed = encodedEffect.value("bypassed", false);
                    effect.Parameters = encodedEffect.value("parameters", std::vector<float>{});
                    effect.ImpulseResponse = ParseId(encodedEffect, "impulseResponse");
                    bus.Effects.push_back(std::move(effect));
                }
                for (const auto& encodedSend : encodedBus.value("sends", Json::array()))
                    bus.Sends.push_back({.Id = ParseId(encodedSend, "id"),
                                         .DestinationBus = ParseId(encodedSend, "destinationBus"),
                                         .Stage = ParseSendStage(encodedSend.value("stage", std::string("postFader"))),
                                         .Gain = encodedSend.value("gain", 1.0F)});
                definition.Buses.push_back(std::move(bus));
            }
            for (const auto& encodedSnapshot : document.value("snapshots", Json::array()))
            {
                AudioMixerSnapshotDefinition snapshot;
                snapshot.Id = ParseId(encodedSnapshot, "id");
                snapshot.Name = encodedSnapshot.at("name").get<std::string>();
                for (const auto& encodedParameter : encodedSnapshot.value("parameters", Json::array()))
                    snapshot.Parameters.push_back(
                        {.Type = ParseSnapshotParameterType(encodedParameter.at("type").get<std::string>()),
                         .Target = ParseId(encodedParameter, "target"),
                         .Parameter = encodedParameter.value("parameter", 0U),
                         .Value = encodedParameter.at("value").get<float>()});
                definition.Snapshots.push_back(std::move(snapshot));
            }
            for (const auto& encodedRule : document.value("ducking", Json::array()))
                definition.Ducking.push_back(
                    {.Id = ParseId(encodedRule, "id"),
                     .Name = encodedRule.at("name").get<std::string>(),
                     .SidechainBus = ParseId(encodedRule, "sidechainBus"),
                     .TargetBus = ParseId(encodedRule, "targetBus"),
                     .ThresholdDb = encodedRule.value("thresholdDb", -24.0F),
                     .Ratio = encodedRule.value("ratio", 4.0F),
                     .AttackSeconds = encodedRule.value("attackSeconds", 0.01F),
                     .HoldSeconds = encodedRule.value("holdSeconds", 0.0F),
                     .ReleaseSeconds = encodedRule.value("releaseSeconds", 0.1F),
                     .MaximumAttenuationDb = encodedRule.value("maximumAttenuationDb", 12.0F)});
            return CreateRef<AudioMixerAsset>(std::move(definition));
        }
        catch (const nlohmann::json::exception& exception)
        {
            throw std::invalid_argument(std::string("Audio mixer source is malformed: ") + exception.what());
        }
    }

    Ref<AudioMixerAsset> AudioMixerAsset::Default() { return CreateRef<AudioMixerAsset>(DefaultDefinition()); }

    AssetImporterRegistration CreateAudioMixerAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.AudioMixer";
        result.Version = 1;
        result.Type = AudioMixerAsset::StaticType();
        result.Extensions = {".keiremixer"};
        result.Import = [](const std::span<const std::byte> bytes)
        {
            const auto mixer = AudioMixerAsset::Decode(bytes);
            return AudioMixerAsset::Encode(mixer->Definition());
        };
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto mixer = AudioMixerAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = AudioMixerAsset::Encode(mixer->Definition());
            output.AssetDependencies = AudioMixerDependencies(mixer->Definition());
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateAudioMixerAssetDecoder()
    {
        return {AudioMixerAsset::StaticType(), AudioMixerAsset::Default(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return AudioMixerAsset::Decode(bytes); }};
    }
} // namespace Keire
