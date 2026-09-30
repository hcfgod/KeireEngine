#include "KeireClient/Editor/EditorAssetFileService.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <system_error>

namespace KeireEditor
{
    namespace Detail
    {
        namespace
        {
            constexpr std::size_t Mebibyte = 1024U * 1024U;
            constexpr std::size_t MaximumAuthoringBytes = 64U * Mebibyte;

            [[nodiscard]] std::size_t SourceLimit(const std::filesystem::path& path)
            {
                auto extension = path.extension().string();
                std::ranges::transform(extension, extension.begin(), [](const unsigned char value)
                                       { return static_cast<char>(std::tolower(value)); });
                // Match decoder ceilings before allocating their source buffers. Unlisted authoring formats still
                // have a finite editor boundary; these readers are not for streaming texture/audio payloads.
                if (extension == ".asmref" || extension == ".keirephysicsmaterial")
                    return 64U * 1024U;
                if (extension == ".keireasm" || extension == ".keireparametercollection")
                    return Mebibyte;
                if (extension == ".keireinput" || extension == ".keirematerial" ||
                    extension == ".keirematerialinstance" || extension == ".keirevfx" ||
                    extension == ".keirevfxsubgraph")
                    return 4U * Mebibyte;
                if (extension == ".keiredata" || extension == ".keireui" || extension == ".keirestyle" ||
                    extension == ".keireuipanel" || extension == ".keiresubgraph" ||
                    extension == ".keirematerialfunction" || extension == ".keireshaderfunction" ||
                    extension == ".keiremateriallayer" || extension == ".keirematerialblend")
                    return 16U * Mebibyte;
                if (extension == ".keireshadergraph")
                    return 32U * Mebibyte;
                return MaximumAuthoringBytes;
            }
        } // namespace

        void RequireCompiledVfxSystems(const Keire::VfxEffectDefinition& definition, const Keire::VfxBackend backend)
        {
            const auto programs = Keire::CompileVfxEffectSystems(definition, backend);
            if (programs.empty())
                throw std::runtime_error("VFX preview compilation produced no systems.");
            for (const auto& program : programs)
            {
                if (!program.Valid)
                {
                    throw std::runtime_error(program.Diagnostics.empty() ? "VFX preview compilation failed."
                                                                         : program.Diagnostics.front().Message);
                }
            }
        }

        [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path,
                                                       const std::string_view assetKind)
        {
            return ReadBytes(path, assetKind, SourceLimit(path));
        }

        std::vector<std::byte> ReadAssetStreamBytes(std::istream& input, const std::size_t expectedBytes,
                                                    const std::size_t maximumBytes, const std::string_view assetKind)
        {
            const auto description = std::string(assetKind);
            if (expectedBytes > maximumBytes)
                throw std::runtime_error("Cannot read " + description + ": source exceeds its byte limit.");
            if (!input)
                throw std::runtime_error("Cannot read " + description + ": source stream is unavailable.");
            std::vector<std::byte> bytes(expectedBytes);
            std::size_t offset = 0;
            while (offset < expectedBytes)
            {
                const auto count = std::min(expectedBytes - offset, std::size_t{64U * 1024U});
                input.read(reinterpret_cast<char*>(bytes.data() + offset), static_cast<std::streamsize>(count));
                if (!input || input.gcount() != static_cast<std::streamsize>(count))
                    throw std::runtime_error("Cannot read " + description + ": incomplete or failed source read.");
                offset += count;
            }
            // Never grow the allocation in response to a concurrently growing file.
            if (input.peek() != std::char_traits<char>::eof())
                throw std::runtime_error("Cannot read " + description + ": source changed size while reading.");
            if (input.bad() || (input.fail() && !input.eof()))
                throw std::runtime_error("Cannot read " + description + ": failed source read.");
            return bytes;
        }

        [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path,
                                                       const std::string_view assetKind, const std::size_t maximumBytes)
        {
            const auto description = std::string(assetKind) + ": " + Keire::Detail::PathToUtf8(path);
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error)
                throw std::runtime_error("Cannot open " + description);
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Cannot open " + description);
            const auto length = input.tellg();
            const auto limit = std::min(maximumBytes, SourceLimit(path));
            if (length < 0 || static_cast<std::uintmax_t>(length) > limit)
                throw std::runtime_error("Cannot read " + description +
                                         ": source exceeds its byte limit or has no size.");
            input.seekg(0);
            return ReadAssetStreamBytes(input, static_cast<std::size_t>(length), limit, description);
        }

        std::vector<std::byte> ReadSceneBytes(const std::filesystem::path& path)
        {
            return ReadBytes(path, "scene asset");
        }

        [[nodiscard]] std::string FormatAssetDiagnostic(const Keire::AssetImportDiagnostic& diagnostic)
        {
            auto result = diagnostic.RelativePath.generic_string();
            if (diagnostic.Line != 0)
            {
                result += ':' + std::to_string(diagnostic.Line);
                if (diagnostic.Column != 0)
                    result += ':' + std::to_string(diagnostic.Column);
            }
            if (!result.empty())
                result += ": ";
            result += diagnostic.Message;
            return result;
        }

        void WriteBytesAtomically(const std::filesystem::path& path, const std::span<const std::byte> bytes)
        {
            const std::string text =
                bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            Keire::Detail::WriteTextFileAtomically(path, text);
        }

        [[nodiscard]] bool IsCSharpIdentifier(const std::string_view value)
        {
            return !value.empty() &&
                   (std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_') &&
                   std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_'; });
        }

        [[nodiscard]] std::vector<std::byte> TextBytes(const std::string_view text)
        {
            const auto bytes = std::as_bytes(std::span(text));
            return {bytes.begin(), bytes.end()};
        }

        [[nodiscard]] bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate)
        {
            const auto relative = candidate.lexically_normal().lexically_relative(parent.lexically_normal());
            return relative.empty() || (!relative.is_absolute() && !relative.generic_string().starts_with(".."));
        }
    } // namespace Detail
} // namespace KeireEditor
