#pragma once

#include "Keire/Assets/AssetPipeline.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Keire::Detail
{
    struct AudioTranscodeResult
    {
        std::vector<std::byte> EncodedAudio;
        std::uint32_t SampleRate = 0;
        std::uint32_t Channels = 0;
        std::uint64_t Frames = 0;
        std::string SourceCodec;
        std::string SourceContainer;
        std::string RuntimeEncoding = "lossless FLAC";
    };

    using AudioTranscodeBackend =
        std::function<AudioTranscodeResult(const AssetImportContext&, std::span<const std::byte>)>;

    [[nodiscard]] AssetImporterRegistration CreateAudioClipAssetImporter(AudioTranscodeBackend backend);
} // namespace Keire::Detail
