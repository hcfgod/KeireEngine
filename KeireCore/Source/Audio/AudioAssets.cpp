#include "Keire/Audio/AudioAssets.h"

#include <miniaudio.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

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

    AssetImporterRegistration CreateAudioClipAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.AudioClip";
        result.Version = 2;
        result.Type = AudioClipAsset::StaticType();
        result.Extensions = {".wav", ".ogg", ".flac"};
        result.Import = [](const std::span<const std::byte> bytes) { return ImportSource(bytes); };
        return result;
    }

    AssetDecoderRegistration CreateAudioClipAssetDecoder()
    {
        return {
            AudioClipAsset::StaticType(),
            AudioClipAsset::Silence(),
            [](const std::span<const std::byte> bytes) -> Ref<Asset> { return AudioClipAsset::Decode(bytes); },
        };
    }
} // namespace Keire
