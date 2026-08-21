#include "Keire/Assets/AssetPackage.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::array<char, 9> ArchiveMagic{'K', 'E', 'I', 'R', 'A', 'S', 'P', 'K', '1'};
        constexpr std::uint32_t ArchiveSchemaVersion = 1;
        constexpr std::size_t BufferBytes = std::size_t{256U} * 1024U;

        struct ZstdContextDeleter final
        {
            void operator()(ZSTD_CCtx* context) const noexcept { ZSTD_freeCCtx(context); }
            void operator()(ZSTD_DCtx* context) const noexcept { ZSTD_freeDCtx(context); }
        };

        template <typename Integer>
        [[nodiscard]] std::array<std::byte, sizeof(Integer)> EncodeLittleEndian(Integer value)
        {
            std::array<std::byte, sizeof(Integer)> result{};
            for (std::size_t index = 0; index < result.size(); ++index)
            {
                result[index] = static_cast<std::byte>(value & 0xffU);
                value >>= 8U;
            }
            return result;
        }

        template <typename Integer>
        [[nodiscard]] Integer DecodeLittleEndian(const std::span<const std::byte, sizeof(Integer)> bytes)
        {
            Integer result = 0;
            for (std::size_t index = 0; index < bytes.size(); ++index)
                result |= static_cast<Integer>(std::to_integer<unsigned>(bytes[index])) << (index * 8U);
            return result;
        }

        class CompressedWriter final
        {
          public:
            CompressedWriter(const std::filesystem::path& path, const int level)
                : m_Output(path, std::ios::binary | std::ios::trunc), m_Context(ZSTD_createCCtx())
            {
                if (!m_Output || !m_Context)
                    throw std::runtime_error("Could not create the asset-package output.");
                const auto result = ZSTD_CCtx_setParameter(m_Context.get(), ZSTD_c_compressionLevel, level);
                if (ZSTD_isError(result))
                    throw std::runtime_error(std::string("Could not configure Zstandard: ") +
                                             ZSTD_getErrorName(result));
            }

            ~CompressedWriter()
            {
                if (!m_Finished)
                {
                    try
                    {
                        Finish();
                    }
                    catch (...)
                    {
                    }
                }
            }

            void Write(const std::span<const std::byte> bytes)
            {
                ZSTD_inBuffer input{bytes.data(), bytes.size(), 0};
                while (input.pos < input.size)
                    Pump(input, ZSTD_e_continue);
            }

            void Write(const std::string_view text) { Write(std::as_bytes(std::span(text.data(), text.size()))); }

            void Finish()
            {
                if (m_Finished)
                    return;
                ZSTD_inBuffer input{nullptr, 0, 0};
                std::size_t remaining = 1;
                while (remaining != 0)
                    remaining = Pump(input, ZSTD_e_end);
                m_Output.flush();
                if (!m_Output)
                    throw std::runtime_error("Could not finish the asset package.");
                m_Finished = true;
            }

          private:
            std::size_t Pump(ZSTD_inBuffer& input, const ZSTD_EndDirective directive)
            {
                ZSTD_outBuffer output{m_Buffer.data(), m_Buffer.size(), 0};
                const auto result = ZSTD_compressStream2(m_Context.get(), &output, &input, directive);
                if (ZSTD_isError(result))
                    throw std::runtime_error(std::string("Zstandard compression failed: ") + ZSTD_getErrorName(result));
                m_Output.write(m_Buffer.data(), static_cast<std::streamsize>(output.pos));
                if (!m_Output)
                    throw std::runtime_error("Could not write the asset package.");
                return result;
            }

            std::ofstream m_Output;
            std::unique_ptr<ZSTD_CCtx, ZstdContextDeleter> m_Context;
            std::vector<char> m_Buffer = std::vector<char>(BufferBytes);
            bool m_Finished = false;
        };

        class CompressedReader final
        {
          public:
            explicit CompressedReader(const std::filesystem::path& path)
                : m_Input(path, std::ios::binary), m_Context(ZSTD_createDCtx())
            {
                if (!m_Input || !m_Context)
                    throw std::runtime_error("Could not open the asset package.");
            }

            void Read(const std::span<std::byte> destination)
            {
                if (destination.size() > AssetPackageArchiveLimits::MaximumPayloadBytes - m_DecodedBytes)
                    throw std::runtime_error("The asset package exceeds its decoded-size limit.");
                std::size_t written = 0;
                while (written < destination.size())
                {
                    if (m_OutputPosition == m_OutputSize)
                        Fill();
                    const auto count = std::min(destination.size() - written, m_OutputSize - m_OutputPosition);
                    std::memcpy(destination.data() + written, m_Output.data() + m_OutputPosition, count);
                    written += count;
                    m_OutputPosition += count;
                    m_DecodedBytes += count;
                }
            }

            [[nodiscard]] std::vector<std::byte> ReadBytes(const std::size_t size)
            {
                std::vector<std::byte> result(size);
                Read(result);
                return result;
            }

            void RequireEnd()
            {
                if (m_OutputPosition != m_OutputSize)
                    throw std::runtime_error("The asset package contains undeclared payload data.");
                while (!m_FrameEnded)
                {
                    Fill();
                    if (m_OutputSize != 0)
                        throw std::runtime_error("The asset package contains undeclared payload data.");
                }
                if (m_Compressed.pos != m_Compressed.size || m_Input.peek() != std::ifstream::traits_type::eof())
                    throw std::runtime_error("The asset package contains trailing compressed data.");
            }

          private:
            void Fill()
            {
                if (m_FrameEnded)
                    throw std::runtime_error("The asset package is truncated.");
                m_OutputPosition = 0;
                m_OutputSize = 0;
                while (m_OutputSize == 0 && !m_FrameEnded)
                {
                    if (m_Compressed.pos == m_Compressed.size)
                    {
                        m_Input.read(m_InputBuffer.data(), static_cast<std::streamsize>(m_InputBuffer.size()));
                        const auto count = m_Input.gcount();
                        if (count <= 0)
                            throw std::runtime_error("The asset package is truncated.");
                        m_Compressed = {m_InputBuffer.data(), static_cast<std::size_t>(count), 0};
                    }
                    ZSTD_outBuffer output{m_Output.data(), m_Output.size(), 0};
                    const auto remaining = ZSTD_decompressStream(m_Context.get(), &output, &m_Compressed);
                    if (ZSTD_isError(remaining))
                        throw std::runtime_error(std::string("Zstandard decompression failed: ") +
                                                 ZSTD_getErrorName(remaining));
                    m_OutputSize = output.pos;
                    m_FrameEnded = remaining == 0;
                }
            }

            std::ifstream m_Input;
            std::unique_ptr<ZSTD_DCtx, ZstdContextDeleter> m_Context;
            std::vector<char> m_InputBuffer = std::vector<char>(BufferBytes);
            std::vector<std::byte> m_Output = std::vector<std::byte>(BufferBytes);
            ZSTD_inBuffer m_Compressed{nullptr, 0, 0};
            std::size_t m_OutputPosition = 0;
            std::size_t m_OutputSize = 0;
            std::uint64_t m_DecodedBytes = 0;
            bool m_FrameEnded = false;
        };

        [[nodiscard]] std::string PathText(const std::filesystem::path& path)
        {
            return Detail::PathToUtf8(path.lexically_normal());
        }

        [[nodiscard]] std::string CaseFolded(const std::filesystem::path& path)
        {
            auto value = PathText(path);
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] bool IsPortablePathComponent(const std::string_view value)
        {
            if (value.empty() || value == "." || value == ".." || value.back() == '.' || value.back() == ' ')
                return false;
            for (const char character : value)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (!(std::isalnum(byte) || byte == '.' || byte == '_' || byte == '-'))
                    return false;
            }
            auto stem = std::string(value.substr(0, value.find('.')));
            std::ranges::transform(stem, stem.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::toupper(character)); });
            static const std::set<std::string> reserved{"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                                        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                                        "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
            return !reserved.contains(stem);
        }

        [[nodiscard]] bool IsPortableRelativePath(const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
                return false;
            const auto normalized = path.lexically_normal();
            if (normalized == "." || normalized.empty() ||
                PathText(normalized).size() > AssetPackageArchiveLimits::MaximumPathBytes)
                return false;
            for (const auto& component : normalized)
            {
                const auto text = PathText(component);
                if (!IsPortablePathComponent(text))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool IsSafeToken(const std::string_view value, const std::size_t maximum = 128)
        {
            return !value.empty() && value.size() <= maximum &&
                   std::ranges::all_of(
                       value, [](const unsigned char character)
                       { return std::isalnum(character) || character == '.' || character == '_' || character == '-'; });
        }

        [[nodiscard]] bool IsPackageId(const std::string_view value)
        {
            if (!IsSafeToken(value) || value.find('.') == std::string_view::npos || value.starts_with('.') ||
                value.ends_with('.') || value.find("..") != std::string_view::npos)
                return false;
            return std::ranges::all_of(
                value, [](const unsigned char character)
                { return std::islower(character) || std::isdigit(character) || character == '.' || character == '-'; });
        }

        [[nodiscard]] bool IsSemanticVersion(const std::string_view value)
        {
            if (value.empty() || value.size() > 128)
                return false;
            const auto coreEnd = value.find_first_of("-+");
            const auto core = value.substr(0, coreEnd);
            std::size_t begin = 0;
            for (std::size_t componentIndex = 0; componentIndex < 3; ++componentIndex)
            {
                const auto end = core.find('.', begin);
                const auto component =
                    core.substr(begin, end == std::string_view::npos ? core.size() - begin : end - begin);
                if (component.empty() || (component.size() > 1 && component.front() == '0') ||
                    !std::ranges::all_of(component,
                                         [](const unsigned char character) { return std::isdigit(character); }))
                    return false;
                if (componentIndex < 2 && end == std::string_view::npos)
                    return false;
                if (componentIndex == 2 && end != std::string_view::npos)
                    return false;
                begin = end == std::string_view::npos ? core.size() : end + 1;
            }
            if (coreEnd == std::string_view::npos)
                return true;
            const auto suffix = value.substr(coreEnd + 1);
            return !suffix.empty() &&
                   std::ranges::all_of(suffix, [](const unsigned char character)
                                       { return std::isalnum(character) || character == '.' || character == '-'; });
        }

        [[nodiscard]] std::string_view InstallKindText(const AssetPackageInstallKind value) noexcept
        {
            switch (value)
            {
            case AssetPackageInstallKind::Registry:
                return "registry";
            case AssetPackageInstallKind::AssetImport:
                return "assetImport";
            case AssetPackageInstallKind::CompleteProject:
                return "completeProject";
            }
            return {};
        }

        [[nodiscard]] AssetPackageInstallKind ParseInstallKind(const std::string_view value)
        {
            if (value == "registry")
                return AssetPackageInstallKind::Registry;
            if (value == "assetImport")
                return AssetPackageInstallKind::AssetImport;
            if (value == "completeProject")
                return AssetPackageInstallKind::CompleteProject;
            throw std::invalid_argument("Asset-package installation kind is invalid.");
        }

        [[nodiscard]] std::string_view AssemblyScopeText(const AssetPackageManagedAssemblyScope value) noexcept
        {
            switch (value)
            {
            case AssetPackageManagedAssemblyScope::Runtime:
                return "runtime";
            case AssetPackageManagedAssemblyScope::Editor:
                return "editor";
            case AssetPackageManagedAssemblyScope::Test:
                return "test";
            }
            return {};
        }

        [[nodiscard]] AssetPackageManagedAssemblyScope ParseAssemblyScope(const std::string_view value)
        {
            if (value == "runtime")
                return AssetPackageManagedAssemblyScope::Runtime;
            if (value == "editor")
                return AssetPackageManagedAssemblyScope::Editor;
            if (value == "test")
                return AssetPackageManagedAssemblyScope::Test;
            throw std::invalid_argument("Asset-package managed assembly scope is invalid.");
        }

        void ThrowIfCancelled(const AssetPackageCallbacks& callbacks)
        {
            if (callbacks.Cancelled && callbacks.Cancelled())
                throw std::runtime_error("Asset-package operation was cancelled.");
        }

        void Report(const AssetPackageCallbacks& callbacks, const std::uint64_t completed, const std::uint64_t total,
                    const std::filesystem::path& path)
        {
            if (callbacks.Progress)
                callbacks.Progress({.CompletedBytes = completed, .TotalBytes = total, .CurrentFile = path});
        }

        [[nodiscard]] Json StringArray(const std::vector<std::string>& values)
        {
            Json result = Json::array();
            for (const auto& value : values)
                result.push_back(value);
            return result;
        }

        [[nodiscard]] std::vector<std::string> ParseStringArray(const Json& value, const std::string_view name)
        {
            if (!value.is_array())
                throw std::invalid_argument(std::string(name) + " must be an array.");
            std::vector<std::string> result;
            result.reserve(value.size());
            for (const auto& entry : value)
            {
                if (!entry.is_string())
                    throw std::invalid_argument(std::string(name) + " contains a non-string value.");
                result.push_back(entry.get<std::string>());
            }
            return result;
        }

        [[nodiscard]] Json EncodeManifestJson(const AssetPackageManifest& manifest)
        {
            Json dependencies = Json::array();
            for (const auto& dependency : manifest.Dependencies)
                dependencies.push_back({{"packageId", dependency.PackageId}, {"version", dependency.VersionRange}});
            Json conflicts = Json::array();
            for (const auto& conflict : manifest.Conflicts)
                conflicts.push_back({{"packageId", conflict.PackageId}, {"version", conflict.VersionRange}});
            Json files = Json::array();
            for (const auto& file : manifest.Files)
            {
                files.push_back({{"path", PathText(file.Path)},
                                 {"sizeBytes", file.SizeBytes},
                                 {"sha256", file.Sha256},
                                 {"mode", file.Mode}});
            }
            Json assets = Json::array();
            for (const auto& asset : manifest.Assets)
            {
                Json dependenciesJson = Json::array();
                for (const auto dependency : asset.Dependencies)
                    dependenciesJson.push_back(dependency.ToString());
                assets.push_back({{"id", asset.Id.ToString()},
                                  {"type", asset.Type.ToString()},
                                  {"source", PathText(asset.SourcePath)},
                                  {"metadata", PathText(asset.MetadataPath)},
                                  {"dependencies", std::move(dependenciesJson)}});
            }
            Json samples = Json::array();
            for (const auto& sample : manifest.Samples)
            {
                samples.push_back({{"id", sample.Id},
                                   {"displayName", sample.DisplayName},
                                   {"description", sample.Description},
                                   {"root", PathText(sample.Root)}});
            }
            Json assemblies = Json::array();
            for (const auto& assembly : manifest.ManagedAssemblies)
            {
                assemblies.push_back({{"name", assembly.Name},
                                      {"definition", PathText(assembly.DefinitionPath)},
                                      {"scope", AssemblyScopeText(assembly.Scope)}});
            }
            Json licenses = Json::array();
            for (const auto& license : manifest.Licenses)
                licenses.push_back({{"id", license.Id}, {"path", PathText(license.Path)}});
            Json compatibility{{"minimumEngineVersion", manifest.Compatibility.MinimumEngineVersion},
                               {"platforms", StringArray(manifest.Compatibility.Platforms)},
                               {"architectures", StringArray(manifest.Compatibility.Architectures)},
                               {"rendererCapabilities", StringArray(manifest.Compatibility.RendererCapabilities)},
                               {"managedApiVersion", manifest.Compatibility.ManagedApiVersion}};
            if (manifest.Compatibility.MaximumEngineVersion)
                compatibility["maximumEngineVersion"] = *manifest.Compatibility.MaximumEngineVersion;
            return {{"schemaVersion", manifest.SchemaVersion},
                    {"packageId", manifest.PackageId},
                    {"version", manifest.Version},
                    {"publisherId", manifest.PublisherId},
                    {"displayName", manifest.DisplayName},
                    {"summary", manifest.Summary},
                    {"channel", manifest.Channel},
                    {"installKind", InstallKindText(manifest.InstallKind)},
                    {"compatibility", std::move(compatibility)},
                    {"dependencies", std::move(dependencies)},
                    {"conflicts", std::move(conflicts)},
                    {"files", std::move(files)},
                    {"assets", std::move(assets)},
                    {"samples", std::move(samples)},
                    {"managedAssemblies", std::move(assemblies)},
                    {"licenses", std::move(licenses)},
                    {"entryPoints", StringArray(manifest.EntryPoints)},
                    {"installedSizeBytes", manifest.InstalledSizeBytes},
                    {"signatureKeyId", manifest.SignatureKeyId}};
        }

        [[nodiscard]] std::pair<AssetPackageArchiveMetadata, std::unique_ptr<CompressedReader>>
        ReadHeader(const std::filesystem::path& archive)
        {
            if (archive.extension() != ".keireassetpackage" || !std::filesystem::is_regular_file(archive))
                throw std::invalid_argument("Asset-package inspection requires a .keireassetpackage file.");
            const auto archiveSize = std::filesystem::file_size(archive);
            if (archiveSize == 0 || archiveSize > AssetPackageArchiveLimits::MaximumArchiveBytes)
                throw std::runtime_error("The asset package exceeds its archive-size limit.");
            auto reader = std::make_unique<CompressedReader>(archive);
            std::array<std::byte, ArchiveMagic.size()> magic{};
            reader->Read(magic);
            if (!std::ranges::equal(magic, std::as_bytes(std::span(ArchiveMagic))))
                throw std::runtime_error("Asset-package magic is invalid.");
            std::array<std::byte, sizeof(std::uint32_t)> schemaBytes{};
            reader->Read(schemaBytes);
            if (DecodeLittleEndian<std::uint32_t>(schemaBytes) != ArchiveSchemaVersion)
                throw std::runtime_error("Asset-package archive schema is unsupported.");
            std::array<std::byte, sizeof(std::uint64_t)> manifestSizeBytes{};
            reader->Read(manifestSizeBytes);
            const auto manifestSize = DecodeLittleEndian<std::uint64_t>(manifestSizeBytes);
            if (manifestSize == 0 || manifestSize > AssetPackageArchiveLimits::MaximumManifestBytes)
                throw std::runtime_error("Asset-package manifest size is invalid.");
            std::array<std::byte, sizeof(std::uint32_t)> signatureSizeBytes{};
            reader->Read(signatureSizeBytes);
            const auto signatureSize = DecodeLittleEndian<std::uint32_t>(signatureSizeBytes);
            if (signatureSize > AssetPackageArchiveLimits::MaximumSignatureBytes)
                throw std::runtime_error("Asset-package signature size is invalid.");
            auto exactManifest = reader->ReadBytes(static_cast<std::size_t>(manifestSize));
            const auto manifestText =
                std::string_view(reinterpret_cast<const char*>(exactManifest.data()), exactManifest.size());
            auto manifest = DecodeAssetPackageManifest(manifestText);
            std::optional<AssetPackageSignature> signature;
            if (signatureSize != 0)
            {
                if (manifest.SignatureKeyId.empty())
                    throw std::runtime_error("Signed asset package does not declare a signature key.");
                signature =
                    AssetPackageSignature{.KeyId = manifest.SignatureKeyId, .Bytes = reader->ReadBytes(signatureSize)};
            }
            AssetPackageArchiveMetadata metadata{.Manifest = std::move(manifest),
                                                 .Signature = std::move(signature),
                                                 .ExactManifestBytes = std::move(exactManifest),
                                                 .ArchiveSizeBytes = archiveSize,
                                                 .ArchiveSha256 = Detail::DigestToString(Detail::Sha256File(
                                                     archive, AssetPackageArchiveLimits::MaximumArchiveBytes))};
            return {std::move(metadata), std::move(reader)};
        }

        void VerifyMetadata(const AssetPackageArchiveMetadata& metadata, const AssetPackageVerification& verification)
        {
            if (verification.ExpectedArchiveSizeBytes &&
                *verification.ExpectedArchiveSizeBytes != metadata.ArchiveSizeBytes)
                throw std::runtime_error("Asset-package archive size does not match the trusted catalog.");
            if (!verification.ExpectedArchiveSha256.empty() &&
                verification.ExpectedArchiveSha256 != metadata.ArchiveSha256)
                throw std::runtime_error("Asset-package archive hash does not match the trusted catalog.");
            if (verification.RequireSignature && !metadata.Signature)
                throw std::runtime_error("Asset package is unsigned.");
            if (metadata.Signature)
            {
                if (!verification.VerifySignature)
                {
                    if (verification.RequireSignature)
                        throw std::runtime_error("No asset-package signature verifier is configured.");
                    return;
                }
                if (metadata.Signature->KeyId != metadata.Manifest.SignatureKeyId ||
                    !verification.VerifySignature(metadata.Signature->Algorithm, metadata.Signature->KeyId,
                                                  metadata.ExactManifestBytes, metadata.Signature->Bytes))
                    throw std::runtime_error("Asset-package signature verification failed.");
            }
        }

        [[nodiscard]] std::filesystem::path CanonicalStagingParent(const AssetPackageExtractionRequest& request)
        {
            if (request.AllowedStagingParent.empty() || request.StagingRoot.empty() ||
                !std::filesystem::is_directory(request.AllowedStagingParent) ||
                std::filesystem::exists(request.StagingRoot))
                throw std::invalid_argument("Asset-package extraction requires a new staging root under an existing "
                                            "authorized parent.");
            const auto parent = Detail::CanonicalExistingPath(request.AllowedStagingParent);
            const auto staging = std::filesystem::absolute(request.StagingRoot).lexically_normal();
            if (staging.parent_path() != parent)
                throw std::invalid_argument("Asset-package staging root escapes its authorized parent.");
            return parent;
        }
    } // namespace

    void ValidateAssetPackageManifest(const AssetPackageManifest& manifest)
    {
        if (manifest.SchemaVersion != AssetPackageManifest::CurrentSchemaVersion)
            throw std::invalid_argument("Asset-package manifest schema is unsupported.");
        if (!IsPackageId(manifest.PackageId) || !IsSemanticVersion(manifest.Version) ||
            !IsSafeToken(manifest.PublisherId) || manifest.DisplayName.empty() || manifest.DisplayName.size() > 128 ||
            manifest.Summary.size() > 512 || !IsSafeToken(manifest.Channel, 32) ||
            InstallKindText(manifest.InstallKind).empty())
            throw std::invalid_argument("Asset-package identity is invalid.");
        if (!IsSemanticVersion(manifest.Compatibility.MinimumEngineVersion) ||
            (manifest.Compatibility.MaximumEngineVersion &&
             !IsSemanticVersion(*manifest.Compatibility.MaximumEngineVersion)) ||
            manifest.Compatibility.ManagedApiVersion.size() > 128)
            throw std::invalid_argument("Asset-package compatibility is invalid.");
        const auto validateTokens = [](const std::vector<std::string>& values, const std::string_view label)
        {
            std::set<std::string> unique;
            for (const auto& value : values)
            {
                if (!IsSafeToken(value) || !unique.emplace(value).second)
                    throw std::invalid_argument(std::string("Asset-package ") + std::string(label) + " are invalid.");
            }
        };
        validateTokens(manifest.Compatibility.Platforms, "platforms");
        validateTokens(manifest.Compatibility.Architectures, "architectures");
        validateTokens(manifest.Compatibility.RendererCapabilities, "renderer capabilities");
        if (manifest.Files.empty() || manifest.Files.size() > AssetPackageArchiveLimits::MaximumFiles ||
            manifest.Assets.size() > AssetPackageArchiveLimits::MaximumAssets)
            throw std::invalid_argument("Asset-package inventory is empty or oversized.");

        std::set<std::string> paths;
        std::set<std::string> exactPaths;
        std::uint64_t total = 0;
        std::string previous;
        for (const auto& file : manifest.Files)
        {
            const auto path = PathText(file.Path);
            if (!IsPortableRelativePath(file.Path) || file.SizeBytes > AssetPackageArchiveLimits::MaximumFileBytes ||
                file.Sha256.size() != 64 ||
                !std::ranges::all_of(file.Sha256, [](const unsigned char character)
                                     { return std::isdigit(character) || (character >= 'a' && character <= 'f'); }) ||
                (file.Mode != 0644U && file.Mode != 0755U) ||
                total > AssetPackageArchiveLimits::MaximumPayloadBytes - file.SizeBytes ||
                !paths.emplace(CaseFolded(file.Path)).second || !exactPaths.emplace(path).second ||
                (!previous.empty() && previous >= path))
                throw std::invalid_argument("Asset-package file inventory is unsafe or noncanonical.");
            previous = path;
            total += file.SizeBytes;
        }
        if (manifest.InstalledSizeBytes != total)
            throw std::invalid_argument("Asset-package installed size does not match its file inventory.");

        std::set<AssetId> assetIds;
        for (const auto& asset : manifest.Assets)
        {
            if (!asset.Id || !asset.Type || !IsPortableRelativePath(asset.SourcePath) ||
                !IsPortableRelativePath(asset.MetadataPath) || !exactPaths.contains(PathText(asset.SourcePath)) ||
                !exactPaths.contains(PathText(asset.MetadataPath)) || !assetIds.emplace(asset.Id).second)
                throw std::invalid_argument("Asset-package asset inventory is invalid.");
            std::set<AssetId> dependencies;
            for (const auto dependency : asset.Dependencies)
            {
                if (!dependency || dependency == asset.Id || !dependencies.emplace(dependency).second)
                    throw std::invalid_argument("Asset-package asset dependencies are invalid.");
            }
        }

        const auto validateRelations = [&](const auto& values, const std::string_view label)
        {
            std::set<std::string> identities;
            for (const auto& value : values)
            {
                if (!IsPackageId(value.PackageId) || value.PackageId == manifest.PackageId ||
                    value.VersionRange.empty() || value.VersionRange.size() > 128 ||
                    !identities.emplace(value.PackageId).second)
                    throw std::invalid_argument(std::string("Asset-package ") + std::string(label) + " are invalid.");
            }
        };
        validateRelations(manifest.Dependencies, "dependencies");
        validateRelations(manifest.Conflicts, "conflicts");

        std::set<std::string> sampleIds;
        for (const auto& sample : manifest.Samples)
        {
            const auto root = PathText(sample.Root);
            const auto prefix = root + '/';
            const bool hasFile =
                std::ranges::any_of(exactPaths, [&](const auto& path) { return path.starts_with(prefix); });
            if (!IsSafeToken(sample.Id) || !sampleIds.emplace(sample.Id).second || sample.DisplayName.empty() ||
                sample.DisplayName.size() > 128 || sample.Description.size() > 512 ||
                !IsPortableRelativePath(sample.Root) || !hasFile)
                throw std::invalid_argument("Asset-package sample inventory is invalid.");
        }

        std::set<std::string> assemblyNames;
        for (const auto& assembly : manifest.ManagedAssemblies)
        {
            if (!IsSafeToken(assembly.Name) || !assemblyNames.emplace(assembly.Name).second ||
                !IsPortableRelativePath(assembly.DefinitionPath) ||
                !exactPaths.contains(PathText(assembly.DefinitionPath)) || AssemblyScopeText(assembly.Scope).empty())
                throw std::invalid_argument("Asset-package managed assembly inventory is invalid.");
        }
        std::set<std::string> licenseIds;
        for (const auto& license : manifest.Licenses)
        {
            if (!IsSafeToken(license.Id) || !licenseIds.emplace(license.Id).second ||
                !IsPortableRelativePath(license.Path) || !exactPaths.contains(PathText(license.Path)))
                throw std::invalid_argument("Asset-package license inventory is invalid.");
        }
        for (const auto& entry : manifest.EntryPoints)
        {
            if (!exactPaths.contains(entry))
                throw std::invalid_argument("Asset-package entry point is not present in the file inventory.");
        }
        if (!manifest.SignatureKeyId.empty() && !IsSafeToken(manifest.SignatureKeyId))
            throw std::invalid_argument("Asset-package signature key identity is invalid.");
    }

    std::string EncodeAssetPackageManifest(const AssetPackageManifest& manifest)
    {
        ValidateAssetPackageManifest(manifest);
        return EncodeManifestJson(manifest).dump();
    }

    AssetPackageManifest DecodeAssetPackageManifest(const std::string_view document)
    {
        if (document.empty() || document.size() > AssetPackageArchiveLimits::MaximumManifestBytes)
            throw std::invalid_argument("Asset-package manifest is empty or oversized.");
        auto parsed = Json::parse(document);
        if (!parsed.is_object() || parsed.size() != 19)
            throw std::invalid_argument("Asset-package manifest root shape is invalid.");
        AssetPackageManifest result;
        result.SchemaVersion = parsed.at("schemaVersion").get<std::uint32_t>();
        result.PackageId = parsed.at("packageId").get<std::string>();
        result.Version = parsed.at("version").get<std::string>();
        result.PublisherId = parsed.at("publisherId").get<std::string>();
        result.DisplayName = parsed.at("displayName").get<std::string>();
        result.Summary = parsed.at("summary").get<std::string>();
        result.Channel = parsed.at("channel").get<std::string>();
        result.InstallKind = ParseInstallKind(parsed.at("installKind").get<std::string>());
        const auto& compatibility = parsed.at("compatibility");
        if (!compatibility.is_object())
            throw std::invalid_argument("Asset-package compatibility shape is invalid.");
        result.Compatibility.MinimumEngineVersion = compatibility.at("minimumEngineVersion").get<std::string>();
        if (compatibility.contains("maximumEngineVersion"))
            result.Compatibility.MaximumEngineVersion = compatibility.at("maximumEngineVersion").get<std::string>();
        result.Compatibility.Platforms = ParseStringArray(compatibility.at("platforms"), "platforms");
        result.Compatibility.Architectures = ParseStringArray(compatibility.at("architectures"), "architectures");
        result.Compatibility.RendererCapabilities =
            ParseStringArray(compatibility.at("rendererCapabilities"), "renderer capabilities");
        result.Compatibility.ManagedApiVersion = compatibility.at("managedApiVersion").get<std::string>();
        for (const auto& value : parsed.at("dependencies"))
            result.Dependencies.push_back(
                {value.at("packageId").get<std::string>(), value.at("version").get<std::string>()});
        for (const auto& value : parsed.at("conflicts"))
            result.Conflicts.push_back(
                {value.at("packageId").get<std::string>(), value.at("version").get<std::string>()});
        for (const auto& value : parsed.at("files"))
        {
            result.Files.push_back({.Path = Detail::PathFromUtf8(value.at("path").get<std::string>()),
                                    .SizeBytes = value.at("sizeBytes").get<std::uint64_t>(),
                                    .Sha256 = value.at("sha256").get<std::string>(),
                                    .Mode = value.at("mode").get<std::uint32_t>()});
        }
        for (const auto& value : parsed.at("assets"))
        {
            AssetPackageAsset asset{.Id = AssetId::Parse(value.at("id").get<std::string>()),
                                    .Type = AssetTypeId::Parse(value.at("type").get<std::string>()),
                                    .SourcePath = Detail::PathFromUtf8(value.at("source").get<std::string>()),
                                    .MetadataPath = Detail::PathFromUtf8(value.at("metadata").get<std::string>())};
            for (const auto& dependency : value.at("dependencies"))
                asset.Dependencies.push_back(AssetId::Parse(dependency.get<std::string>()));
            result.Assets.push_back(std::move(asset));
        }
        for (const auto& value : parsed.at("samples"))
        {
            result.Samples.push_back({.Id = value.at("id").get<std::string>(),
                                      .DisplayName = value.at("displayName").get<std::string>(),
                                      .Description = value.at("description").get<std::string>(),
                                      .Root = Detail::PathFromUtf8(value.at("root").get<std::string>())});
        }
        for (const auto& value : parsed.at("managedAssemblies"))
        {
            result.ManagedAssemblies.push_back(
                {.Name = value.at("name").get<std::string>(),
                 .DefinitionPath = Detail::PathFromUtf8(value.at("definition").get<std::string>()),
                 .Scope = ParseAssemblyScope(value.at("scope").get<std::string>())});
        }
        for (const auto& value : parsed.at("licenses"))
        {
            result.Licenses.push_back({.Id = value.at("id").get<std::string>(),
                                       .Path = Detail::PathFromUtf8(value.at("path").get<std::string>())});
        }
        result.EntryPoints = ParseStringArray(parsed.at("entryPoints"), "entry points");
        result.InstalledSizeBytes = parsed.at("installedSizeBytes").get<std::uint64_t>();
        result.SignatureKeyId = parsed.at("signatureKeyId").get<std::string>();
        ValidateAssetPackageManifest(result);
        if (EncodeAssetPackageManifest(result) != document)
            throw std::invalid_argument("Asset-package manifest is not canonical.");
        return result;
    }

    AssetPackageManifest InventoryAssetPackagePayload(AssetPackageManifest manifest,
                                                      const std::filesystem::path& payloadRoot)
    {
        const auto root = Detail::CanonicalExistingPath(payloadRoot);
        if (!std::filesystem::is_directory(root))
            throw std::invalid_argument("Asset-package inventory requires a payload directory.");
        manifest.Files.clear();
        manifest.InstalledSizeBytes = 0;
        std::set<std::string> paths;
        for (std::filesystem::recursive_directory_iterator iterator(root), end; iterator != end; ++iterator)
        {
            const auto status = iterator->symlink_status();
            if (std::filesystem::is_symlink(status))
                throw std::runtime_error("Asset-package payload may not contain symbolic links.");
            if (std::filesystem::is_directory(status))
                continue;
            if (!std::filesystem::is_regular_file(status))
                throw std::runtime_error("Asset-package payload may contain only regular files.");
            const auto relative = iterator->path().lexically_relative(root).lexically_normal();
            const auto size = iterator->file_size();
            if (!IsPortableRelativePath(relative) || !paths.emplace(CaseFolded(relative)).second ||
                size > AssetPackageArchiveLimits::MaximumFileBytes ||
                manifest.InstalledSizeBytes > AssetPackageArchiveLimits::MaximumPayloadBytes - size ||
                manifest.Files.size() >= AssetPackageArchiveLimits::MaximumFiles)
                throw std::runtime_error("Asset-package payload exceeds its safety limits.");
            manifest.InstalledSizeBytes += size;
            manifest.Files.push_back({.Path = relative,
                                      .SizeBytes = size,
                                      .Sha256 = Detail::DigestToString(Detail::Sha256File(
                                          iterator->path(), AssetPackageArchiveLimits::MaximumFileBytes)),
                                      .Mode = 0644U});
        }
        std::ranges::sort(manifest.Files, {}, [](const auto& file) { return PathText(file.Path); });
        ValidateAssetPackageManifest(manifest);
        return manifest;
    }

    AssetPackageArchiveMetadata WriteAssetPackageArchive(const AssetPackageWriteRequest& request)
    {
        if (request.Output.extension() != ".keireassetpackage" || request.PayloadRoot.empty() ||
            std::filesystem::exists(request.Output))
            throw std::invalid_argument("Asset-package output must be a new .keireassetpackage file.");
        ValidateAssetPackageManifest(request.Manifest);
        const auto root = Detail::CanonicalExistingPath(request.PayloadRoot);
        if (request.Signature)
        {
            if (request.Signature->Algorithm != "ed25519" ||
                request.Signature->KeyId != request.Manifest.SignatureKeyId || request.Signature->Bytes.empty() ||
                request.Signature->Bytes.size() > AssetPackageArchiveLimits::MaximumSignatureBytes)
                throw std::invalid_argument("Asset-package detached signature is invalid.");
        }
        else if (!request.Manifest.SignatureKeyId.empty())
        {
            throw std::invalid_argument("Asset-package manifest declares a signature key without a signature.");
        }
        const auto manifestText = EncodeAssetPackageManifest(request.Manifest);
        const auto temporary = Detail::PathWithSuffix(request.Output, ".tmp-" + AssetId::Generate().ToString());
        std::filesystem::create_directories(request.Output.parent_path());
        try
        {
            {
                CompressedWriter writer(temporary, request.CompressionLevel);
                writer.Write(std::as_bytes(std::span(ArchiveMagic)));
                writer.Write(EncodeLittleEndian(ArchiveSchemaVersion));
                writer.Write(EncodeLittleEndian(static_cast<std::uint64_t>(manifestText.size())));
                writer.Write(EncodeLittleEndian(
                    static_cast<std::uint32_t>(request.Signature ? request.Signature->Bytes.size() : 0U)));
                writer.Write(manifestText);
                if (request.Signature)
                    writer.Write(request.Signature->Bytes);
                std::uint64_t completed = 0;
                std::vector<std::byte> buffer(BufferBytes);
                for (const auto& file : request.Manifest.Files)
                {
                    ThrowIfCancelled(request.Callbacks);
                    const auto source = root / file.Path;
                    if (!std::filesystem::is_regular_file(source) ||
                        std::filesystem::file_size(source) != file.SizeBytes ||
                        Detail::DigestToString(
                            Detail::Sha256File(source, AssetPackageArchiveLimits::MaximumFileBytes)) != file.Sha256)
                        throw std::runtime_error("Asset-package payload changed after inventory: " +
                                                 PathText(file.Path));
                    std::ifstream input(source, std::ios::binary);
                    std::uint64_t remaining = file.SizeBytes;
                    while (remaining != 0)
                    {
                        ThrowIfCancelled(request.Callbacks);
                        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
                        if (input.gcount() != static_cast<std::streamsize>(count))
                            throw std::runtime_error("Asset-package payload changed while writing.");
                        writer.Write(std::span(buffer).first(count));
                        remaining -= count;
                        completed += count;
                        Report(request.Callbacks, completed, request.Manifest.InstalledSizeBytes, file.Path);
                    }
                }
                writer.Finish();
            }
            Detail::PublishFileAtomically(temporary, request.Output);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
        return InspectAssetPackageArchive(request.Output);
    }

    AssetPackageArchiveMetadata InspectAssetPackageArchive(const std::filesystem::path& archive,
                                                           const AssetPackageVerification& verification)
    {
        auto [metadata, reader] = ReadHeader(archive);
        VerifyMetadata(metadata, verification);
        return metadata;
    }

    AssetPackageExtractionResult ExtractAssetPackageToStaging(const AssetPackageExtractionRequest& request)
    {
        static_cast<void>(CanonicalStagingParent(request));
        auto [metadata, reader] = ReadHeader(request.Archive);
        VerifyMetadata(metadata, request.Verification);
        std::filesystem::create_directory(request.StagingRoot);
        std::uint64_t completed = 0;
        try
        {
            std::vector<std::byte> buffer(BufferBytes);
            for (const auto& file : metadata.Manifest.Files)
            {
                ThrowIfCancelled(request.Callbacks);
                const auto target = request.StagingRoot / file.Path;
                std::filesystem::create_directories(target.parent_path());
                std::ofstream output(target, std::ios::binary | std::ios::trunc);
                if (!output)
                    throw std::runtime_error("Could not create an extracted asset-package file.");
                std::uint64_t remaining = file.SizeBytes;
                while (remaining != 0)
                {
                    ThrowIfCancelled(request.Callbacks);
                    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                    reader->Read(std::span(buffer).first(count));
                    output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(count));
                    if (!output)
                        throw std::runtime_error("Could not write an extracted asset-package file.");
                    remaining -= count;
                    completed += count;
                    Report(request.Callbacks, completed, metadata.Manifest.InstalledSizeBytes, file.Path);
                }
                output.close();
                if (Detail::DigestToString(Detail::Sha256File(target, AssetPackageArchiveLimits::MaximumFileBytes)) !=
                    file.Sha256)
                    throw std::runtime_error("Extracted asset-package file failed hash verification.");
#if !defined(_WIN32)
                std::filesystem::permissions(
                    target,
                    file.Mode == 0755U ? std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                             std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                             std::filesystem::perms::others_exec
                                       : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                             std::filesystem::perms::group_read | std::filesystem::perms::others_read,
                    std::filesystem::perm_options::replace);
#endif
            }
            reader->RequireEnd();
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove_all(request.StagingRoot, ignored);
            throw;
        }
        return {.Metadata = std::move(metadata), .StagingRoot = request.StagingRoot};
    }
} // namespace Keire
