#include "KeireClient/Editor/EditorAssetFileService.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <ranges>
#include <stdexcept>

namespace KeireEditor
{
    namespace Detail
    {
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
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot open " + std::string(assetKind) + ": " + path.string());
            const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            std::vector<std::byte> bytes(characters.size());
            std::ranges::transform(characters, bytes.begin(), [](const char value) { return std::byte(value); });
            return bytes;
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
