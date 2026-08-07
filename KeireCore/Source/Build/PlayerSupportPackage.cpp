#include "KeireInternal/Build/PlayerSupportPackage.h"

#include "Keire/BuildInfo.h"
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
#include <span>
#include <stdexcept>
#include <system_error>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::array<char, 8> PackageMagic{'K', 'E', 'I', 'R', 'E', 'P', 'S', '1'};
        constexpr std::uint32_t PackageSchemaVersion = 1;
        constexpr std::uint64_t MaximumManifestBytes = 4ULL * 1024ULL * 1024ULL;
        constexpr std::uint64_t MaximumFileBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::uint64_t MaximumPackageBytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::size_t MaximumFiles = 32768;
        constexpr std::size_t BufferBytes = 256U * 1024U;

        struct ContextDeleter final
        {
            void operator()(ZSTD_CCtx* context) const noexcept { ZSTD_freeCCtx(context); }
            void operator()(ZSTD_DCtx* context) const noexcept { ZSTD_freeDCtx(context); }
        };

        [[nodiscard]] bool IsConfinedRelativePath(const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
                return false;
            const auto normalized = path.lexically_normal();
            return normalized != "." && !normalized.empty() && *normalized.begin() != "..";
        }

        [[nodiscard]] std::string CaseFoldedPath(const std::filesystem::path& path)
        {
            auto value = PathToUtf8(path.lexically_normal());
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] std::filesystem::path NativeIoPath(const std::filesystem::path& path)
        {
#if defined(_WIN32)
            auto value = std::filesystem::absolute(path).lexically_normal().native();
            if (value.starts_with(LR"(\\?\)"))
                return value;
            if (value.starts_with(LR"(\\)"))
                return LR"(\\?\UNC\)" + value.substr(2);
            return LR"(\\?\)" + value;
#else
            return path;
#endif
        }

        void ThrowIfCancelled(const PlayerSupportInstallCallbacks& callbacks)
        {
            if (callbacks.Cancelled && callbacks.Cancelled())
                throw std::runtime_error("Player support installation was cancelled.");
        }

        void Report(const PlayerSupportInstallCallbacks& callbacks, const float progress, const std::string_view text)
        {
            if (callbacks.Progress)
                callbacks.Progress(std::clamp(progress, 0.0F, 1.0F), text);
        }

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
                    throw std::runtime_error("Could not create player support package output.");
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
                    throw std::runtime_error("Could not finish the player support package.");
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
                    throw std::runtime_error("Could not write the player support package.");
                return result;
            }

            std::ofstream m_Output;
            std::unique_ptr<ZSTD_CCtx, ContextDeleter> m_Context;
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
                    throw std::runtime_error("Could not open the player support package.");
            }

            void Read(const std::span<std::byte> destination)
            {
                std::size_t written = 0;
                while (written < destination.size())
                {
                    if (m_OutputPosition == m_OutputSize)
                        Fill();
                    const auto count = std::min(destination.size() - written, m_OutputSize - m_OutputPosition);
                    std::memcpy(destination.data() + written, m_Output.data() + m_OutputPosition, count);
                    written += count;
                    m_OutputPosition += count;
                }
            }

            [[nodiscard]] std::string ReadText(const std::size_t size)
            {
                std::string result(size, '\0');
                Read(std::as_writable_bytes(std::span(result.data(), result.size())));
                return result;
            }

            void RequireEnd()
            {
                if (m_OutputPosition != m_OutputSize)
                    throw std::runtime_error("Player support package contains undeclared payload data.");
                while (!m_FrameEnded)
                {
                    Fill();
                    if (m_OutputSize != 0)
                        throw std::runtime_error("Player support package contains undeclared payload data.");
                }
                if (m_Compressed.pos != m_Compressed.size || m_Input.peek() != std::ifstream::traits_type::eof())
                    throw std::runtime_error("Player support package contains trailing compressed data.");
            }

          private:
            void Fill()
            {
                if (m_FrameEnded)
                    throw std::runtime_error("Player support package is truncated.");
                m_OutputPosition = 0;
                m_OutputSize = 0;
                while (m_OutputSize == 0 && !m_FrameEnded)
                {
                    if (m_Compressed.pos == m_Compressed.size)
                    {
                        m_Input.read(m_InputBuffer.data(), static_cast<std::streamsize>(m_InputBuffer.size()));
                        const auto count = m_Input.gcount();
                        if (count <= 0)
                            throw std::runtime_error("Player support package is truncated.");
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
            std::unique_ptr<ZSTD_DCtx, ContextDeleter> m_Context;
            std::vector<char> m_InputBuffer = std::vector<char>(BufferBytes);
            std::vector<std::byte> m_Output = std::vector<std::byte>(BufferBytes);
            ZSTD_inBuffer m_Compressed{nullptr, 0, 0};
            std::size_t m_OutputPosition = 0;
            std::size_t m_OutputSize = 0;
            bool m_FrameEnded = false;
        };

        [[nodiscard]] PlayerSupportManifest ReadHeader(CompressedReader& reader)
        {
            std::array<std::byte, PackageMagic.size()> magic{};
            reader.Read(magic);
            if (!std::ranges::equal(magic, std::as_bytes(std::span(PackageMagic))))
                throw std::runtime_error("Player support package magic is invalid.");
            std::array<std::byte, sizeof(std::uint32_t)> schemaBytes{};
            reader.Read(schemaBytes);
            if (DecodeLittleEndian<std::uint32_t>(schemaBytes) != PackageSchemaVersion)
                throw std::runtime_error("Player support package schema is incompatible.");
            std::array<std::byte, sizeof(std::uint64_t)> manifestBytes{};
            reader.Read(manifestBytes);
            const auto size = DecodeLittleEndian<std::uint64_t>(manifestBytes);
            if (size == 0 || size > MaximumManifestBytes)
                throw std::runtime_error("Player support package manifest size is invalid.");
            return DecodePlayerSupportManifest(reader.ReadText(static_cast<std::size_t>(size)));
        }

        void ValidatePackageManifest(const PlayerSupportManifest& manifest,
                                     const std::string_view expectedModuleFingerprint)
        {
            ValidatePlayerSupportManifest(manifest);
            if (manifest.EngineVersion != GetBuildInfo().Version || manifest.PlayerAbi != PlayerBuildAbiVersion)
                throw std::runtime_error("Player support package targets a different engine or player ABI.");
            if (!expectedModuleFingerprint.empty() && manifest.ModuleFingerprint != expectedModuleFingerprint)
                throw std::runtime_error("Player support package source-module catalog is incompatible.");
            if (manifest.Files.empty() || manifest.Files.size() > MaximumFiles)
                throw std::runtime_error("Player support package file catalog is empty or oversized.");
            std::uint64_t total = 0;
            std::set<std::string> paths;
            for (const auto& file : manifest.Files)
            {
                if (!IsConfinedRelativePath(file.Path) || file.Size > MaximumFileBytes ||
                    total > MaximumPackageBytes - file.Size || !paths.emplace(CaseFoldedPath(file.Path)).second)
                    throw std::runtime_error("Player support package file catalog is unsafe.");
                total += file.Size;
                (void)ParseDigest(file.Sha256);
            }
        }

        void WriteRegistry(const std::filesystem::path& versionRoot)
        {
            Json entries = Json::array();
            std::error_code error;
            if (std::filesystem::is_directory(versionRoot, error) && !error)
            {
                for (const auto& entry : std::filesystem::directory_iterator(versionRoot, error))
                {
                    if (error)
                        throw std::filesystem::filesystem_error("Could not enumerate Build Support.", versionRoot,
                                                                error);
                    if (!entry.is_directory() || entry.path().filename().string().starts_with('.'))
                        continue;
                    try
                    {
                        const auto manifest = LoadPlayerSupportManifest(entry.path() / "manifest.json");
                        entries.push_back({{"id", manifest.Id},
                                           {"platform", ToString(manifest.Platform)},
                                           {"architecture", ToString(manifest.Architecture)}});
                    }
                    catch (const std::exception&)
                    {
                    }
                }
            }
            WriteTextFileAtomically(versionRoot / "registry.json",
                                    Json{{"schemaVersion", 1}, {"installed", std::move(entries)}}.dump(2) + '\n');
        }

        [[nodiscard]] std::uint64_t InstalledBytes(const PlayerSupportManifest& manifest)
        {
            std::uint64_t result = 0;
            for (const auto& file : manifest.Files)
                result += file.Size;
            return result;
        }

        [[nodiscard]] std::filesystem::path ResolveStorageRoot(const std::filesystem::path& requested)
        {
            return requested.empty() ? PlayerSupportStorageRoot()
                                     : std::filesystem::absolute(requested).lexically_normal();
        }
    } // namespace

    PlayerSupportPackageResult CreatePlayerSupportPackage(PlayerSupportManifest manifest,
                                                          const std::filesystem::path& payloadRoot,
                                                          const std::filesystem::path& output,
                                                          const int compressionLevel)
    {
        const auto root = std::filesystem::absolute(payloadRoot).lexically_normal();
        if (!std::filesystem::is_directory(root) || output.empty())
            throw std::invalid_argument("Player support packaging requires a payload directory and output path.");
        manifest.Files.clear();
        std::set<std::string> executablePaths;
        for (const auto& variant : manifest.Variants)
            executablePaths.emplace(CaseFoldedPath((variant.Root / variant.Executable).lexically_normal()));
        std::uint64_t total = 0;
        for (std::filesystem::recursive_directory_iterator iterator(root), end; iterator != end; ++iterator)
        {
            const auto status = iterator->symlink_status();
            if (std::filesystem::is_symlink(status))
                throw std::runtime_error("Player support payloads may not contain symbolic links.");
            if (std::filesystem::is_directory(status))
                continue;
            if (!std::filesystem::is_regular_file(status))
                throw std::runtime_error("Player support payloads may contain only regular files.");
            const auto relative = iterator->path().lexically_relative(root).lexically_normal();
            if (!IsConfinedRelativePath(relative) || CaseFoldedPath(relative) == "manifest.json")
                throw std::runtime_error("Player support payload contains an unsafe or reserved path.");
            const auto size = iterator->file_size();
            if (size > MaximumFileBytes || total > MaximumPackageBytes - size || manifest.Files.size() >= MaximumFiles)
                throw std::runtime_error("Player support payload exceeds its size or file-count limit.");
            total += size;
            manifest.Files.push_back({.Path = relative,
                                      .Size = size,
                                      .Sha256 = DigestToString(Sha256File(iterator->path(), MaximumFileBytes)),
                                      .Mode = executablePaths.contains(CaseFoldedPath(relative)) ? 0755U : 0644U});
        }
        std::ranges::sort(manifest.Files, {}, [](const auto& file) { return PathToUtf8(file.Path); });
        ValidatePackageManifest(manifest, manifest.ModuleFingerprint);
        const auto encodedManifest = EncodePlayerSupportManifest(manifest);
        std::filesystem::create_directories(output.parent_path());
        const auto temporary = PathWithSuffix(output, ".tmp-" + AssetId::Generate().ToString());
        try
        {
            {
                CompressedWriter writer(temporary, compressionLevel);
                writer.Write(std::as_bytes(std::span(PackageMagic)));
                writer.Write(EncodeLittleEndian(PackageSchemaVersion));
                writer.Write(EncodeLittleEndian(static_cast<std::uint64_t>(encodedManifest.size())));
                writer.Write(encodedManifest);
                std::vector<std::byte> buffer(BufferBytes);
                for (const auto& file : manifest.Files)
                {
                    std::ifstream input(root / file.Path, std::ios::binary);
                    if (!input)
                        throw std::runtime_error("Could not read a player support payload file.");
                    std::uint64_t remaining = file.Size;
                    while (remaining != 0)
                    {
                        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
                        if (input.gcount() != static_cast<std::streamsize>(count))
                            throw std::runtime_error("Player support payload changed while packaging.");
                        writer.Write(std::span(buffer).first(count));
                        remaining -= count;
                    }
                }
                writer.Finish();
            }
            PublishFileAtomically(temporary, output);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
        return {.Manifest = std::move(manifest),
                .ArchiveSize = std::filesystem::file_size(output),
                .ArchiveSha256 = DigestToString(Sha256File(output, MaximumPackageBytes))};
    }

    PlayerSupportManifest ReadPlayerSupportPackageManifest(const std::filesystem::path& package)
    {
        if (std::filesystem::file_size(package) > MaximumPackageBytes)
            throw std::runtime_error("Player support package exceeds its archive size limit.");
        CompressedReader reader(package);
        auto manifest = ReadHeader(reader);
        ValidatePackageManifest(manifest, {});
        return manifest;
    }

    PlayerSupportPackageResult InstallPlayerSupportPackage(const std::filesystem::path& package,
                                                           const std::string_view expectedModuleFingerprint,
                                                           const PlayerSupportInstallCallbacks& callbacks,
                                                           const std::filesystem::path& storageRoot)
    {
        const auto archiveSize = std::filesystem::file_size(package);
        if (archiveSize > MaximumPackageBytes)
            throw std::runtime_error("Player support package exceeds its archive size limit.");
        Report(callbacks, 0.01F, "Reading Build Support manifest");
        CompressedReader reader(package);
        auto manifest = ReadHeader(reader);
        ValidatePackageManifest(manifest, expectedModuleFingerprint);
        ThrowIfCancelled(callbacks);

        const auto versionRoot = ResolveStorageRoot(storageRoot) / manifest.EngineVersion;
        const auto destination = versionRoot / PathFromUtf8(manifest.Id);
        const auto staging = versionRoot / (".install-" + AssetId::Generate().ToString());
        const auto backup = versionRoot / (".repair-" + AssetId::Generate().ToString());
        std::filesystem::create_directories(staging);
        std::uint64_t completed = 0;
        const auto total = InstalledBytes(manifest);
        try
        {
            std::vector<std::byte> buffer(BufferBytes);
            for (const auto& file : manifest.Files)
            {
                ThrowIfCancelled(callbacks);
                const auto target = staging / file.Path;
                std::filesystem::create_directories(NativeIoPath(target.parent_path()));
                const auto nativeTarget = NativeIoPath(target);
                std::ofstream output(nativeTarget, std::ios::binary | std::ios::trunc);
                if (!output)
                    throw std::runtime_error("Could not create a Build Support payload file.");
                std::uint64_t remaining = file.Size;
                while (remaining != 0)
                {
                    ThrowIfCancelled(callbacks);
                    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                    reader.Read(std::span(buffer).first(count));
                    output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(count));
                    if (!output)
                        throw std::runtime_error("Could not write a Build Support payload file.");
                    remaining -= count;
                    completed += count;
                    Report(callbacks,
                           0.05F +
                               (total == 0 ? 0.0F : 0.85F * static_cast<float>(completed) / static_cast<float>(total)),
                           "Extracting Build Support");
                }
                output.close();
                if (DigestToString(Sha256File(nativeTarget, MaximumFileBytes)) != file.Sha256)
                    throw std::runtime_error("Build Support payload hash verification failed.");
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
            reader.RequireEnd();
            WriteTextFileAtomically(staging / "manifest.json", EncodePlayerSupportManifest(manifest));
            ThrowIfCancelled(callbacks);
            Report(callbacks, 0.93F, "Publishing Build Support");
            const bool replacing = std::filesystem::exists(destination);
            if (replacing)
                std::filesystem::rename(destination, backup);
            try
            {
                std::filesystem::rename(staging, destination);
                WriteRegistry(versionRoot);
            }
            catch (...)
            {
                std::error_code ignored;
                std::filesystem::remove_all(destination, ignored);
                if (replacing && std::filesystem::exists(backup))
                    std::filesystem::rename(backup, destination);
                throw;
            }
            if (replacing)
            {
                std::error_code ignored;
                std::filesystem::remove_all(backup, ignored);
            }
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove_all(staging, ignored);
            if (std::filesystem::exists(backup) && !std::filesystem::exists(destination))
                std::filesystem::rename(backup, destination, ignored);
            throw;
        }
        Report(callbacks, 1.0F, "Build Support installed");
        return {.Manifest = std::move(manifest),
                .ArchiveSize = archiveSize,
                .ArchiveSha256 = DigestToString(Sha256File(package, MaximumPackageBytes))};
    }

    bool ValidateInstalledPlayerSupport(const std::filesystem::path& installation, std::string& diagnostic) noexcept
    {
        try
        {
            const auto manifest = LoadPlayerSupportManifest(installation / "manifest.json");
            ValidatePackageManifest(manifest, {});
            for (const auto& file : manifest.Files)
            {
                const auto path = installation / file.Path;
                const auto nativePath = NativeIoPath(path);
                if (!std::filesystem::is_regular_file(nativePath) ||
                    std::filesystem::file_size(nativePath) != file.Size ||
                    DigestToString(Sha256File(nativePath, MaximumFileBytes)) != file.Sha256)
                    throw std::runtime_error("Installed Build Support contains a missing or corrupt file: " +
                                             PathToUtf8(file.Path));
            }
            diagnostic.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
    }

    std::vector<PlayerSupportPackageResult> InstalledPlayerSupport(const std::filesystem::path& storageRoot)
    {
        std::vector<PlayerSupportPackageResult> result;
        const auto root = ResolveStorageRoot(storageRoot);
        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || error)
            return result;
        for (const auto& version : std::filesystem::directory_iterator(root, error))
        {
            if (error || !version.is_directory())
                continue;
            for (const auto& entry : std::filesystem::directory_iterator(version.path(), error))
            {
                if (error || !entry.is_directory() || entry.path().filename().string().starts_with('.'))
                    continue;
                try
                {
                    auto manifest = LoadPlayerSupportManifest(entry.path() / "manifest.json");
                    const auto bytes = InstalledBytes(manifest);
                    result.push_back({.Manifest = std::move(manifest), .ArchiveSize = bytes});
                }
                catch (const std::exception&)
                {
                }
            }
        }
        std::ranges::sort(result, {}, [](const auto& value) { return value.Manifest.Id; });
        return result;
    }

    void RemovePlayerSupport(const std::string_view engineVersion, const std::string_view packId,
                             const std::filesystem::path& storageRoot)
    {
        if (engineVersion.empty() || packId.empty() || packId.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Build Support removal requires safe engine-version and pack identifiers.");
        const auto versionRoot = ResolveStorageRoot(storageRoot) / PathFromUtf8(engineVersion);
        const auto installation = versionRoot / PathFromUtf8(packId);
        if (!std::filesystem::is_directory(installation))
            throw std::runtime_error("The requested Build Support module is not installed.");
        const auto removed = versionRoot / (".remove-" + AssetId::Generate().ToString());
        std::filesystem::rename(installation, removed);
        try
        {
            WriteRegistry(versionRoot);
        }
        catch (...)
        {
            std::filesystem::rename(removed, installation);
            throw;
        }
        std::error_code error;
        std::filesystem::remove_all(removed, error);
        if (error)
            throw std::filesystem::filesystem_error("Could not remove Build Support payload.", removed, error);
    }
} // namespace Keire::Detail
