#include "KeireInternal/Build/PlayerSupportPackage.h"

#include "Keire/BuildInfo.h"
#include "Keire/Log.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
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
        constexpr std::size_t BufferBytes = 256ULL * 1024U;
        constexpr std::size_t MaximumRemovalJournalBytes = 16ULL * 1024U;
        constexpr std::size_t MaximumRemovalJournals = 128;

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

        [[nodiscard]] std::string LogicalPathKey(const std::filesystem::path& path, const PlayerPlatform platform)
        {
            return platform == PlayerPlatform::Windows ? CaseFoldedPath(path) : PathToUtf8(path.lexically_normal());
        }

        [[nodiscard]] bool HasExecutableMode(const std::filesystem::file_status status) noexcept
        {
#if defined(_WIN32)
            (void)status;
            return false;
#else
            constexpr auto executable = std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                        std::filesystem::perms::others_exec;
            return status.permissions() != std::filesystem::perms::unknown &&
                   (status.permissions() & executable) != std::filesystem::perms::none;
#endif
        }

        [[nodiscard]] bool IsCreateDump(const std::filesystem::path& path, const PlayerPlatform platform)
        {
            const auto filename =
                platform == PlayerPlatform::Windows ? CaseFoldedPath(path.filename()) : PathToUtf8(path.filename());
            return filename == (platform == PlayerPlatform::Windows ? "createdump.exe" : "createdump");
        }

        [[nodiscard]] bool HasExpectedExecutableMode(const std::filesystem::file_status status,
                                                     const std::uint32_t mode) noexcept
        {
#if defined(_WIN32)
            (void)status;
            (void)mode;
            return true;
#else
            constexpr auto mask = std::filesystem::perms::owner_all | std::filesystem::perms::group_all |
                                  std::filesystem::perms::others_all;
            const auto expected = mode == 0755U
                                      ? std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                            std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                            std::filesystem::perms::others_exec
                                      : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                            std::filesystem::perms::group_read | std::filesystem::perms::others_read;
            return status.permissions() != std::filesystem::perms::unknown && (status.permissions() & mask) == expected;
#endif
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

        void ValidatePackageManifestStructure(const PlayerSupportManifest& manifest)
        {
            ValidatePlayerSupportManifest(manifest);
            if (manifest.Files.empty() || manifest.Files.size() > MaximumFiles)
                throw std::runtime_error("Player support package file catalog is empty or oversized.");
            std::uint64_t total = 0;
            std::set<std::string> paths;
            for (const auto& file : manifest.Files)
            {
                if (!IsConfinedRelativePath(file.Path) || file.Size > MaximumFileBytes ||
                    total > MaximumPackageBytes - file.Size ||
                    !paths.emplace(LogicalPathKey(file.Path, manifest.Platform)).second)
                    throw std::runtime_error("Player support package file catalog is unsafe.");
                total += file.Size;
                (void)ParseDigest(file.Sha256);
            }
        }

        void ValidatePackageManifest(const PlayerSupportManifest& manifest,
                                     const std::string_view expectedModuleFingerprint)
        {
            ValidatePackageManifestStructure(manifest);
            if (manifest.EngineVersion != GetBuildInfo().Version || manifest.PlayerAbi != PlayerBuildAbiVersion)
                throw std::runtime_error("Player support package targets a different engine or player ABI.");
            if (!expectedModuleFingerprint.empty() && manifest.ModuleFingerprint != expectedModuleFingerprint)
                throw std::runtime_error("Player support package source-module catalog is incompatible.");
        }

        [[nodiscard]] std::string ReadAnchoredText(const AnchoredFileSystem& fileSystem,
                                                   const std::filesystem::path& relative,
                                                   const std::size_t maximumBytes)
        {
            const auto bytes = fileSystem.Read(relative, maximumBytes);
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }

        [[nodiscard]] PlayerSupportManifest LoadAnchoredManifest(const AnchoredFileSystem& fileSystem,
                                                                 const std::filesystem::path& relative)
        {
            return DecodePlayerSupportManifest(ReadAnchoredText(fileSystem, relative, MaximumManifestBytes));
        }

        void WriteRegistry(const AnchoredFileSystem& fileSystem, const std::filesystem::path& versionRelative)
        {
            const auto versionRoot = fileSystem.Root() / versionRelative;
            Json entries = Json::array();
            std::error_code error;
            if (std::filesystem::is_directory(versionRoot, error) && !error)
            {
                for (const auto& entry : std::filesystem::directory_iterator(versionRoot, error))
                {
                    if (error)
                        throw std::filesystem::filesystem_error("Could not enumerate Build Support.", versionRoot,
                                                                error);
                    const auto status = entry.symlink_status(error);
                    if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status) ||
                        entry.path().filename().string().starts_with('.'))
                        continue;
                    try
                    {
                        const auto relative = entry.path().lexically_relative(fileSystem.Root()).lexically_normal();
                        (void)fileSystem.Exists(relative);
                        if (!fileSystem.IsRegularFile(relative / "manifest.json"))
                            throw std::runtime_error("Build Support manifest is not a regular file.");
                        const auto manifest = LoadAnchoredManifest(fileSystem, relative / "manifest.json");
                        entries.push_back({{"id", manifest.Id},
                                           {"platform", ToString(manifest.Platform)},
                                           {"architecture", ToString(manifest.Architecture)}});
                    }
                    catch (const std::exception&)
                    {
                    }
                }
            }
            const auto contents = Json{{"schemaVersion", 1}, {"installed", std::move(entries)}}.dump(2) + '\n';
            fileSystem.WriteFileAtomically(versionRelative / "registry.json",
                                           std::as_bytes(std::span(contents.data(), contents.size())));
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

        [[nodiscard]] bool IsSafeRemovalComponent(const std::string_view value,
                                                  const bool allowInternal = false) noexcept
        {
            if (value.empty() || value == "." || value == ".." || value.size() > 128 || value.back() == '.' ||
                value.back() == ' ' || (!allowInternal && value.front() == '.'))
            {
                return false;
            }
            if (std::ranges::any_of(value,
                                    [](const unsigned char character)
                                    {
                                        return character < 0x20U || character == 0x7fU || character == '/' ||
                                               character == '\\' || character == '<' || character == '>' ||
                                               character == ':' || character == '"' || character == '|' ||
                                               character == '?' || character == '*';
                                    }))
            {
                return false;
            }
            const auto path = PathFromUtf8(value);
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory() || path.filename() != path)
                return false;
            auto stem = std::string(value.substr(0, value.find('.')));
            std::ranges::transform(stem, stem.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::toupper(character)); });
            static constexpr std::array reservedNames{"CON",  "PRN",  "AUX",  "NUL",  "CLOCK$", "COM1", "COM2", "COM3",
                                                      "COM4", "COM5", "COM6", "COM7", "COM8",   "COM9", "LPT1", "LPT2",
                                                      "LPT3", "LPT4", "LPT5", "LPT6", "LPT7",   "LPT8", "LPT9"};
            return std::ranges::find(reservedNames, stem) == reservedNames.end();
        }

        void RequireUnredirectedRoot(const std::filesystem::path& root)
        {
            const auto parent = root.parent_path();
            if (parent.empty() || root.filename().empty())
                throw std::invalid_argument("Build Support storage requires a named directory root.");
            (void)ResolveConfinedPath(parent, root.filename());
        }

        void RemoveAnchoredTree(const AnchoredFileSystem& fileSystem, const std::filesystem::path& relative)
        {
            if (!fileSystem.Exists(relative))
                return;
            std::vector<std::filesystem::path> entries;
            const auto nativeRoot = NativeIoPath(fileSystem.Root());
            const auto nativeTree = nativeRoot / relative;
            for (std::filesystem::recursive_directory_iterator iterator(nativeTree), end; iterator != end; ++iterator)
            {
                const auto entry = iterator->path().lexically_relative(nativeRoot).lexically_normal();
                (void)fileSystem.Exists(entry);
                entries.push_back(entry);
            }
            std::ranges::sort(entries, [](const auto& left, const auto& right)
                              { return std::ranges::distance(left) > std::ranges::distance(right); });
            for (const auto& entry : entries)
                fileSystem.Remove(entry);
            fileSystem.Remove(relative);
        }

        void TryRemoveAnchoredTree(const AnchoredFileSystem& fileSystem, const std::filesystem::path& relative) noexcept
        {
            try
            {
                RemoveAnchoredTree(fileSystem, relative);
            }
            catch (...)
            {
            }
        }

        void ValidateTreeHasNoRedirects(const AnchoredFileSystem& fileSystem)
        {
            const auto nativeRoot = NativeIoPath(fileSystem.Root());
            for (std::filesystem::recursive_directory_iterator iterator(nativeRoot), end; iterator != end; ++iterator)
            {
                const auto relative = iterator->path().lexically_relative(nativeRoot).lexically_normal();
                (void)fileSystem.Exists(relative);
            }
        }

        void ValidateInstalledFiles(const AnchoredFileSystem& fileSystem, const PlayerSupportManifest& manifest)
        {
            ValidateTreeHasNoRedirects(fileSystem);
            for (const auto& file : manifest.Files)
            {
                AnchoredFileMetadata metadata;
                const auto digest = DigestToString(Sha256File(fileSystem, file.Path, MaximumFileBytes, metadata));
                const auto status =
                    std::filesystem::file_status(std::filesystem::file_type::regular, metadata.Permissions);
                if (metadata.Size != file.Size || digest != file.Sha256 ||
                    !HasExpectedExecutableMode(status, file.Mode))
                {
                    throw std::runtime_error("Installed Build Support contains a missing or corrupt file: " +
                                             PathToUtf8(file.Path));
                }
            }
        }

        void RecoverInterruptedRemovals(const std::filesystem::path& storageRoot)
        {
            std::error_code error;
            if (!std::filesystem::is_directory(storageRoot, error) || error)
                return;
            RequireUnredirectedRoot(storageRoot);
            const AnchoredFileSystem fileSystem(storageRoot);
            for (const auto& version : std::filesystem::directory_iterator(storageRoot))
            {
                const auto versionStatus = version.symlink_status();
                if (!std::filesystem::is_directory(versionStatus) || std::filesystem::is_symlink(versionStatus))
                    continue;
                const auto versionRelative = version.path().lexically_relative(storageRoot).lexically_normal();
                (void)fileSystem.Exists(versionRelative);
                std::vector<std::filesystem::path> journals;
                for (const auto& entry : std::filesystem::directory_iterator(version.path()))
                {
                    const auto filename = PathToUtf8(entry.path().filename());
                    const auto entryStatus = entry.symlink_status();
                    if (std::filesystem::is_regular_file(entryStatus) && filename.starts_with(".remove-") &&
                        filename.ends_with(".json"))
                    {
                        if (journals.size() >= MaximumRemovalJournals)
                            throw std::runtime_error("Build Support removal recovery exceeds its journal limit.");
                        journals.push_back(entry.path());
                    }
                }
                std::ranges::sort(journals);
                for (const auto& journal : journals)
                {
                    const auto journalRelative = journal.lexically_relative(storageRoot).lexically_normal();
                    const auto document =
                        Json::parse(ReadAnchoredText(fileSystem, journalRelative, MaximumRemovalJournalBytes));
                    if (!document.is_object() || document.value("schemaVersion", 0U) != 1U)
                        throw std::runtime_error("Build Support removal journal schema is invalid.");
                    const auto engineVersion = document.at("engineVersion").get<std::string>();
                    const auto packId = document.at("packId").get<std::string>();
                    const auto tombstoneName = document.at("tombstone").get<std::string>();
                    if (!IsSafeRemovalComponent(engineVersion) || !IsSafeRemovalComponent(packId) ||
                        !IsSafeRemovalComponent(tombstoneName, true) || !tombstoneName.starts_with(".remove-") ||
                        version.path().filename() != PathFromUtf8(engineVersion))
                    {
                        throw std::runtime_error("Build Support removal journal identity is invalid.");
                    }
                    const auto tombstone = version.path() / PathFromUtf8(tombstoneName);
                    const auto tombstoneStatus = std::filesystem::symlink_status(tombstone, error);
                    const bool tombstoneExists =
                        !error && tombstoneStatus.type() != std::filesystem::file_type::not_found;
                    if (error && error.default_error_condition() != std::errc::no_such_file_or_directory)
                    {
                        throw std::filesystem::filesystem_error("Could not inspect Build Support removal tombstone.",
                                                                tombstone, error);
                    }
                    error.clear();
                    if (tombstoneExists && (!std::filesystem::is_directory(tombstoneStatus) ||
                                            std::filesystem::is_symlink(tombstoneStatus)))
                    {
                        throw std::runtime_error("Build Support removal tombstone is not a directory.");
                    }
                    if (tombstoneExists)
                        (void)fileSystem.Exists(versionRelative / PathFromUtf8(tombstoneName));

                    WriteRegistry(fileSystem, versionRelative);
                    if (tombstoneExists)
                        RemoveAnchoredTree(fileSystem, versionRelative / PathFromUtf8(tombstoneName));
                    fileSystem.Remove(journalRelative);
                }
            }
        }
    } // namespace

    PlayerSupportFailureDescription DescribePlayerSupportFailure(const PlayerSupportFailureKind kind) noexcept
    {
        switch (kind)
        {
        case PlayerSupportFailureKind::InstalledInventoryInvalid:
            return {.Code = "build_support.inventory_invalid",
                    .Message = "Installed Build Support files are missing or corrupt."};
        case PlayerSupportFailureKind::InstallationFailed:
            return {.Code = "build_support.install_failed",
                    .Message = "Build Support could not be installed. Verify the package and try again."};
        case PlayerSupportFailureKind::CatalogUnavailable:
            return {.Code = "build_support.catalog_unavailable",
                    .Message = "The Build Support catalog could not be downloaded. Check the network connection and "
                               "try again."};
        case PlayerSupportFailureKind::DownloadAndInstallationFailed:
            return {.Code = "build_support.download_install_failed",
                    .Message = "Build Support could not be downloaded and installed. Check the connection and package, "
                               "then try again."};
        }
        return {.Code = "build_support.operation_failed", .Message = "The Build Support operation failed."};
    }

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
            executablePaths.emplace(
                LogicalPathKey((variant.Root / variant.Executable).lexically_normal(), manifest.Platform));
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
            if (!IsConfinedRelativePath(relative) ||
                LogicalPathKey(relative, manifest.Platform) == LogicalPathKey("manifest.json", manifest.Platform))
                throw std::runtime_error("Player support payload contains an unsafe or reserved path.");
            const auto size = iterator->file_size();
            if (size > MaximumFileBytes || total > MaximumPackageBytes - size || manifest.Files.size() >= MaximumFiles)
                throw std::runtime_error("Player support payload exceeds its size or file-count limit.");
            total += size;
            manifest.Files.push_back({.Path = relative,
                                      .Size = size,
                                      .Sha256 = DigestToString(Sha256File(iterator->path(), MaximumFileBytes)),
                                      .Mode = executablePaths.contains(LogicalPathKey(relative, manifest.Platform)) ||
                                                      IsCreateDump(relative, manifest.Platform) ||
                                                      HasExecutableMode(status)
                                                  ? 0755U
                                                  : 0644U});
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

        const auto root = ResolveStorageRoot(storageRoot);
        std::filesystem::create_directories(root);
        RequireUnredirectedRoot(root);
        const AnchoredFileSystem fileSystem(root);
        const auto version = PathFromUtf8(manifest.EngineVersion);
        const auto destination = version / PathFromUtf8(manifest.Id);
        const auto staging = version / (".install-" + AssetId::Generate().ToString());
        const auto backup = version / (".repair-" + AssetId::Generate().ToString());
        fileSystem.CreateDirectories(staging);
        std::uint64_t completed = 0;
        const auto total = InstalledBytes(manifest);
        try
        {
            for (const auto& file : manifest.Files)
            {
                ThrowIfCancelled(callbacks);
                const auto target = staging / file.Path;
                const auto permissions =
                    file.Mode == 0755U ? std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                             std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                             std::filesystem::perms::others_exec
                                       : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                             std::filesystem::perms::group_read | std::filesystem::perms::others_read;
                fileSystem.WriteFileAtomically(
                    target, file.Size,
                    [&](const std::span<std::byte> chunk)
                    {
                        ThrowIfCancelled(callbacks);
                        reader.Read(chunk);
                        completed += chunk.size();
                        Report(callbacks,
                               0.05F + (total == 0 ? 0.0F
                                                   : 0.85F * static_cast<float>(completed) / static_cast<float>(total)),
                               "Extracting Build Support");
                    },
                    permissions, false);
                AnchoredFileMetadata metadata;
                if (DigestToString(Sha256File(fileSystem, target, MaximumFileBytes, metadata)) != file.Sha256 ||
                    metadata.Size != file.Size)
                    throw std::runtime_error("Build Support payload hash verification failed.");
            }
            reader.RequireEnd();
            const auto encodedManifest = EncodePlayerSupportManifest(manifest);
            fileSystem.WriteFileAtomically(staging / "manifest.json",
                                           std::as_bytes(std::span(encodedManifest.data(), encodedManifest.size())),
                                           false);
            ThrowIfCancelled(callbacks);
            Report(callbacks, 0.93F, "Publishing Build Support");
            const bool replacing = fileSystem.Exists(destination);
            if (replacing)
                fileSystem.Rename(destination, backup);
            try
            {
                fileSystem.Rename(staging, destination);
                WriteRegistry(fileSystem, version);
            }
            catch (...)
            {
                const auto original = std::current_exception();
                TryRemoveAnchoredTree(fileSystem, destination);
                try
                {
                    if (replacing && fileSystem.Exists(backup) && !fileSystem.Exists(destination))
                        fileSystem.Rename(backup, destination);
                }
                catch (...)
                {
                }
                std::rethrow_exception(original);
            }
            if (replacing)
                TryRemoveAnchoredTree(fileSystem, backup);
        }
        catch (...)
        {
            const auto original = std::current_exception();
            TryRemoveAnchoredTree(fileSystem, staging);
            try
            {
                if (fileSystem.Exists(backup) && !fileSystem.Exists(destination))
                    fileSystem.Rename(backup, destination);
            }
            catch (...)
            {
            }
            std::rethrow_exception(original);
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
            RequireUnredirectedRoot(installation);
            const AnchoredFileSystem fileSystem(installation);
            if (!fileSystem.IsRegularFile("manifest.json"))
                throw std::runtime_error("Installed Build Support manifest is not a regular file.");
            const auto manifest = LoadAnchoredManifest(fileSystem, "manifest.json");
            ValidatePackageManifest(manifest, {});
            ValidateInstalledFiles(fileSystem, manifest);
            diagnostic.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            try
            {
                KEIRE_CORE_ERROR("Installed Build Support validation failed: {}", error.what());
            }
            catch (...)
            {
            }
            diagnostic = DescribePlayerSupportFailure(PlayerSupportFailureKind::InstalledInventoryInvalid).Message;
            return false;
        }
    }

    bool ValidateInstalledPlayerSupportInventory(const std::filesystem::path& installation,
                                                 const std::string_view expectedEngineVersion,
                                                 std::string& diagnostic) noexcept
    {
        try
        {
            RequireUnredirectedRoot(installation);
            const AnchoredFileSystem fileSystem(installation);
            if (!fileSystem.IsRegularFile("manifest.json"))
                throw std::runtime_error("Installed Build Support manifest is not a regular file.");
            const auto manifest = LoadAnchoredManifest(fileSystem, "manifest.json");
            ValidatePackageManifestStructure(manifest);
            if (expectedEngineVersion.empty() || manifest.EngineVersion != expectedEngineVersion)
                throw std::runtime_error("Installed Build Support is stored under the wrong editor version.");
            ValidateInstalledFiles(fileSystem, manifest);
            diagnostic.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            try
            {
                KEIRE_CORE_ERROR("Installed Build Support inventory validation failed: {}", error.what());
            }
            catch (...)
            {
            }
            diagnostic = DescribePlayerSupportFailure(PlayerSupportFailureKind::InstalledInventoryInvalid).Message;
            return false;
        }
    }

    std::vector<PlayerSupportPackageResult> InstalledPlayerSupport(const std::filesystem::path& storageRoot)
    {
        std::vector<PlayerSupportPackageResult> result;
        const auto root = ResolveStorageRoot(storageRoot);
        RecoverInterruptedRemovals(root);
        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || error)
            return result;
        RequireUnredirectedRoot(root);
        const AnchoredFileSystem fileSystem(root);
        for (const auto& version : std::filesystem::directory_iterator(root, error))
        {
            const auto versionStatus = version.symlink_status(error);
            if (error || !std::filesystem::is_directory(versionStatus) || std::filesystem::is_symlink(versionStatus))
                continue;
            const auto versionRelative = version.path().lexically_relative(root).lexically_normal();
            (void)fileSystem.Exists(versionRelative);
            for (const auto& entry : std::filesystem::directory_iterator(version.path(), error))
            {
                const auto entryStatus = entry.symlink_status(error);
                if (error || !std::filesystem::is_directory(entryStatus) || std::filesystem::is_symlink(entryStatus) ||
                    entry.path().filename().string().starts_with('.'))
                    continue;
                try
                {
                    const auto relative = entry.path().lexically_relative(root).lexically_normal();
                    (void)fileSystem.Exists(relative);
                    if (!fileSystem.IsRegularFile(relative / "manifest.json"))
                        throw std::runtime_error("Build Support manifest is not a regular file.");
                    auto manifest = LoadAnchoredManifest(fileSystem, relative / "manifest.json");
                    const auto bytes = InstalledBytes(manifest);
                    result.push_back({.Manifest = std::move(manifest), .ArchiveSize = bytes});
                }
                catch (const std::exception& exception)
                {
                    KEIRE_CORE_ERROR("Installed Build Support entry could not be inspected: {}", exception.what());
                }
            }
        }
        std::ranges::sort(result, {}, [](const auto& value) { return value.Manifest.Id; });
        return result;
    }

    void RemovePlayerSupport(const std::string_view engineVersion, const std::string_view packId,
                             const std::filesystem::path& storageRoot)
    {
        if (!IsSafeRemovalComponent(engineVersion) || !IsSafeRemovalComponent(packId))
            throw std::invalid_argument("Build Support removal requires safe engine-version and pack identifiers.");
        const auto root = ResolveStorageRoot(storageRoot);
        RecoverInterruptedRemovals(root);
        RequireUnredirectedRoot(root);
        const AnchoredFileSystem fileSystem(root);
        const auto version = PathFromUtf8(engineVersion);
        const auto installation = version / PathFromUtf8(packId);
        const auto versionRoot = root / version;
        (void)fileSystem.Exists(version);
        (void)fileSystem.Exists(installation);
        const auto versionStatus = std::filesystem::symlink_status(versionRoot);
        const auto installationStatus = std::filesystem::symlink_status(root / installation);
        if (!std::filesystem::is_directory(versionStatus) || std::filesystem::is_symlink(versionStatus) ||
            !std::filesystem::is_directory(installationStatus) || std::filesystem::is_symlink(installationStatus))
        {
            throw std::runtime_error("The requested Build Support module is not installed.");
        }
        const auto operationId = ".remove-" + AssetId::Generate().ToString();
        const auto removed = version / operationId;
        const auto journal = version / (operationId + ".json");
        const auto journalContents = Json{{"schemaVersion", 1},
                                          {"engineVersion", std::string(engineVersion)},
                                          {"packId", std::string(packId)},
                                          {"tombstone", operationId}}
                                         .dump(2) +
                                     '\n';
        fileSystem.WriteFileAtomically(journal,
                                       std::as_bytes(std::span(journalContents.data(), journalContents.size())), false);
        fileSystem.Rename(installation, removed);
        try
        {
            WriteRegistry(fileSystem, version);
        }
        catch (...)
        {
            const auto original = std::current_exception();
            try
            {
                fileSystem.Rename(removed, installation);
                fileSystem.Remove(journal);
            }
            catch (...)
            {
            }
            std::rethrow_exception(original);
        }
        RemoveAnchoredTree(fileSystem, removed);
        fileSystem.Remove(journal);
    }
} // namespace Keire::Detail
