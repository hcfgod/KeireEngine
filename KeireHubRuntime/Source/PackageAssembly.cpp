#include "KeireHubRuntime/PackageAssembly.h"

#include "DistributionEncoding.h"
#include "PackageArchiveOutput.h"
#include "Persistence.h"
#include "Sha256.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t BufferBytes = 256U * 1024U;
        constexpr std::size_t MaximumPackages = 256U;
        constexpr std::size_t MaximumSourceFiles = PackageArchiveLimits::MaximumFiles - 2U;
        constexpr std::string_view InstallationMarkerPath = ".keirehub-install.json";
        constexpr std::string_view PublicationSignatureKeyId = "package-tree-seal";

        class AssemblyFailure final
        {
          public:
            explicit AssemblyFailure(HubError error) : Error(std::move(error)) {}

            HubError Error;
        };

        [[nodiscard]] HubError AssemblyError(const HubErrorCode code, std::string message, std::string item = {},
                                             std::string details = {}, const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[noreturn]] void Fail(const HubErrorCode code, std::string message, std::string item = {},
                               std::string details = {}, const bool retryable = false)
        {
            throw AssemblyFailure(
                AssemblyError(code, std::move(message), std::move(item), std::move(details), retryable));
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

        [[nodiscard]] bool IsAbsoluteBoundedPath(const std::filesystem::path& path)
        {
            if (path.empty() || !path.is_absolute() || path == path.root_path() || path.filename().empty())
                return false;
            try
            {
                if (path.generic_u8string().size() > 4096U)
                    return false;
            }
            catch (...)
            {
                return false;
            }
            return std::ranges::none_of(path, [](const std::filesystem::path& component)
                                        { return component == "." || component == ".."; });
        }

        [[nodiscard]] bool HasSymlinkAncestor(const std::filesystem::path& path)
        {
            auto current = path.lexically_normal();
            while (!current.empty())
            {
#if defined(_WIN32)
                const auto attributes = GetFileAttributesW(NativeIoPath(current).c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    return true;
                if (attributes == INVALID_FILE_ATTRIBUTES)
                {
                    const auto failure = GetLastError();
                    if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND)
                        return true;
                }
#else
                std::error_code error;
                const auto status = std::filesystem::symlink_status(NativeIoPath(current), error);
                if (!error && std::filesystem::is_symlink(status))
                    return true;
                if (error && error != std::errc::no_such_file_or_directory)
                    return true;
#endif
                if (current == current.root_path())
                    break;
                const auto parent = current.parent_path();
                if (parent == current)
                    break;
                current = parent;
            }
            return false;
        }

        [[nodiscard]] bool IsDirectoryWithoutLinks(const std::filesystem::path& path)
        {
            if (HasSymlinkAncestor(path))
                return false;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            return !error && status.type() == std::filesystem::file_type::directory;
        }

        [[nodiscard]] bool IsMissing(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            return (!error && status.type() == std::filesystem::file_type::not_found) ||
                   error == std::errc::no_such_file_or_directory;
        }

        [[nodiscard]] bool IsLexicallySameOrWithin(const std::filesystem::path& root,
                                                   const std::filesystem::path& candidate)
        {
            const auto normalizedRoot = root.lexically_normal();
            const auto normalizedCandidate = candidate.lexically_normal();
            auto rootPart = normalizedRoot.begin();
            auto candidatePart = normalizedCandidate.begin();
            while (rootPart != normalizedRoot.end() && candidatePart != normalizedCandidate.end())
            {
                if (*rootPart != *candidatePart)
                    return false;
                ++rootPart;
                ++candidatePart;
            }
            return rootPart == normalizedRoot.end();
        }

        [[nodiscard]] bool IsFilesystemSameOrWithin(const std::filesystem::path& root,
                                                    const std::filesystem::path& candidate)
        {
            auto current = candidate.lexically_normal();
            while (!current.empty())
            {
                std::error_code existsError;
                const bool exists = std::filesystem::exists(NativeIoPath(current), existsError);
                if (existsError && existsError != std::errc::no_such_file_or_directory)
                    return true;
                if (exists)
                {
                    std::error_code equivalentError;
                    const bool equivalent =
                        std::filesystem::equivalent(NativeIoPath(root), NativeIoPath(current), equivalentError);
                    if (equivalentError)
                        return true;
                    if (equivalent)
                        return true;
                }
                if (current == current.root_path())
                    break;
                const auto parent = current.parent_path();
                if (parent == current)
                    break;
                current = parent;
            }
            return false;
        }

        [[nodiscard]] std::string PathText(const std::filesystem::path& path)
        {
            return Detail::PathToUtf8(path.lexically_normal());
        }

        [[nodiscard]] std::string Folded(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        template <typename Integer> void UpdateInteger(Detail::Sha256Builder& digest, Integer value)
        {
            std::array<std::byte, sizeof(Integer)> bytes{};
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                bytes[index] = static_cast<std::byte>(value & 0xffU);
                value >>= 8U;
            }
            digest.Update(bytes);
        }

        void UpdateBytes(Detail::Sha256Builder& digest, const std::span<const std::byte> bytes)
        {
            UpdateInteger(digest, static_cast<std::uint64_t>(bytes.size()));
            digest.Update(bytes);
        }

        [[nodiscard]] PackageManifest ValidationManifest(const PackageTreeSeal& seal)
        {
            auto version = SemanticVersion::Parse("1.0.0");
            if (!version)
                throw std::logic_error("The internal package-tree validation version is invalid.");
            std::vector<PackageFile> files(seal.Files().begin(), seal.Files().end());
            return {.Id = "keire.package-tree",
                    .Version = std::move(version).Value(),
                    .Kind = PackageKind::Toolchain,
                    .DisplayName = "Verified package tree",
                    .Channel = "internal",
                    .Platform = "any",
                    .Architecture = "any",
                    .ArtifactSizeBytes = 1,
                    .ArtifactSha256 = seal.IdentitySha256(),
                    .InstalledSizeBytes = seal.InstalledSizeBytes(),
                    .Files = std::move(files),
                    .SignatureKeyId = std::string(PublicationSignatureKeyId)};
        }

        [[nodiscard]] HubStatus RefreshPublicationIdentity(PackageManifest& manifest)
        {
            manifest.ArtifactSizeBytes = manifest.InstalledSizeBytes;
            manifest.ArtifactSha256.assign(64U, '0');
            auto encoded = EncodePackageManifest(manifest);
            if (!encoded)
                return HubStatus::Failure(encoded.Error());
            manifest.ArtifactSha256 = Detail::Sha256Hex(std::as_bytes(std::span(encoded.Value())));
            return ValidatePackageManifest(manifest);
        }

        [[nodiscard]] bool HasCurrentPublicationIdentity(const PackageManifest& manifest)
        {
            auto expected = manifest;
            if (const auto status = RefreshPublicationIdentity(expected); !status)
                return false;
            return manifest.ArtifactSizeBytes == expected.ArtifactSizeBytes &&
                   manifest.ArtifactSha256 == expected.ArtifactSha256;
        }

        [[nodiscard]] bool IsMarkerPath(const std::filesystem::path& path)
        {
            return Folded(PathText(path)) == Folded(std::string(InstallationMarkerPath));
        }

        [[nodiscard]] bool IsReceiptPath(const std::filesystem::path& path)
        {
            return Folded(PathText(path)) == Folded(PackageInstallReceiptFileName);
        }

        [[nodiscard]] HubResult<bool> SameManifest(const PackageManifest& left, const PackageManifest& right)
        {
            auto encodedLeft = EncodePackageManifest(left);
            if (!encodedLeft)
                return HubResult<bool>::Failure(encodedLeft.Error());
            auto encodedRight = EncodePackageManifest(right);
            if (!encodedRight)
                return HubResult<bool>::Failure(encodedRight.Error());
            return HubResult<bool>::Success(encodedLeft.Value() == encodedRight.Value());
        }

        [[nodiscard]] HubResult<PackageManifest> AddMetadataFile(PackageManifest manifest, PackageFile file)
        {
            if (file.SizeBytes > PackageArchiveLimits::MaximumPayloadBytes ||
                manifest.InstalledSizeBytes > PackageArchiveLimits::MaximumPayloadBytes - file.SizeBytes)
            {
                return HubResult<PackageManifest>::Failure(
                    AssemblyError(HubErrorCode::PackageManifestInvalid,
                                  "The finalized package publication exceeds its payload limit.", manifest.Id));
            }
            manifest.Files.push_back(std::move(file));
            manifest.InstalledSizeBytes += manifest.Files.back().SizeBytes;
            std::ranges::sort(manifest.Files, [](const PackageFile& left, const PackageFile& right)
                              { return PathText(left.Path) < PathText(right.Path); });
            if (const auto status = RefreshPublicationIdentity(manifest); !status)
                return HubResult<PackageManifest>::Failure(status.Error());
            return HubResult<PackageManifest>::Success(std::move(manifest));
        }

        [[nodiscard]] PackageInstallReceipt CreateReceipt(const std::span<const PackageManifest> manifests,
                                                          const PackageTreeSeal& seal)
        {
            PackageInstallReceipt receipt{.AggregateIdentitySha256 = seal.IdentitySha256(),
                                          .AggregateInstalledSizeBytes = seal.InstalledSizeBytes()};
            receipt.Packages.reserve(manifests.size());
            for (const auto& manifest : manifests)
            {
                receipt.Packages.push_back({.Id = manifest.Id,
                                            .Version = manifest.Version,
                                            .Kind = manifest.Kind,
                                            .ArtifactSizeBytes = manifest.ArtifactSizeBytes,
                                            .ArtifactSha256 = manifest.ArtifactSha256,
                                            .InstalledSizeBytes = manifest.InstalledSizeBytes,
                                            .Dependencies = manifest.Dependencies,
                                            .Files = manifest.Files,
                                            .LicenseReferences = manifest.LicenseReferences});
            }
            return receipt;
        }

        class OwnedDirectoryGuard final
        {
          public:
            explicit OwnedDirectoryGuard(std::filesystem::path path) : m_Path(std::move(path)) {}
            ~OwnedDirectoryGuard()
            {
                if (!m_Released)
                {
                    std::error_code ignored;
                    std::filesystem::remove_all(NativeIoPath(m_Path), ignored);
                }
            }

            void Release() noexcept { m_Released = true; }

          private:
            std::filesystem::path m_Path;
            bool m_Released = false;
        };

        class SecureInputFile final
        {
          public:
            SecureInputFile(const std::filesystem::path& path, std::string packageId, std::string relativePath,
                            const std::optional<std::uint64_t> expectedSize = std::nullopt)
                : m_Item(std::move(packageId)), m_Path(std::move(relativePath))
            {
                if (HasSymlinkAncestor(path))
                    Fail(HubErrorCode::UnsafeInstallRoot, "A package assembly source path is unsafe.", m_Item, m_Path);
#if defined(_WIN32)
                const HANDLE handle =
                    CreateFileW(NativeIoPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                    IoFailure();
                FILE_ATTRIBUTE_TAG_INFO attributes{};
                LARGE_INTEGER size{};
                if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
                    (attributes.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
                    !GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
                    (expectedSize && static_cast<std::uint64_t>(size.QuadPart) != *expectedSize))
                {
                    CloseHandle(handle);
                    Fail(HubErrorCode::InvalidData, "A package assembly source file changed.", m_Item, m_Path);
                }
                m_Handle = handle;
                m_Size = static_cast<std::uint64_t>(size.QuadPart);
#else
                int flags = O_RDONLY;
#if defined(O_CLOEXEC)
                flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
                flags |= O_NOFOLLOW;
#endif
                const int handle = ::open(NativeIoPath(path).c_str(), flags);
                if (handle < 0)
                    IoFailure();
                struct stat status{};
                if (::fstat(handle, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0 ||
                    (expectedSize && static_cast<std::uint64_t>(status.st_size) != *expectedSize))
                {
                    ::close(handle);
                    Fail(HubErrorCode::InvalidData, "A package assembly source file changed.", m_Item, m_Path);
                }
                m_Handle = handle;
                m_Size = static_cast<std::uint64_t>(status.st_size);
#endif
                m_Remaining = m_Size;
            }

            ~SecureInputFile()
            {
#if defined(_WIN32)
                if (m_Handle != INVALID_HANDLE_VALUE)
                    CloseHandle(m_Handle);
#else
                if (m_Handle >= 0)
                    ::close(m_Handle);
#endif
            }

            SecureInputFile(const SecureInputFile&) = delete;
            SecureInputFile& operator=(const SecureInputFile&) = delete;

            [[nodiscard]] std::uint64_t Size() const noexcept { return m_Size; }

            [[nodiscard]] std::size_t Read(const std::span<std::byte> destination)
            {
                if (m_Remaining == 0)
                    return 0;
                const auto requested = static_cast<std::size_t>(
                    std::min<std::uint64_t>(m_Remaining, static_cast<std::uint64_t>(destination.size())));
#if defined(_WIN32)
                DWORD read = 0;
                if (!ReadFile(m_Handle, destination.data(), static_cast<DWORD>(requested), &read, nullptr) ||
                    read != static_cast<DWORD>(requested))
                    IoFailure();
                const auto result = static_cast<std::size_t>(read);
#else
                std::size_t result = 0;
                while (result < requested)
                {
                    const auto read = ::read(m_Handle, destination.data() + result, requested - result);
                    if (read < 0 && errno == EINTR)
                        continue;
                    if (read <= 0)
                        IoFailure();
                    result += static_cast<std::size_t>(read);
                }
#endif
                m_Remaining -= result;
                return result;
            }

            void RequireEnd()
            {
                if (m_Remaining != 0)
                    Fail(HubErrorCode::IoRead, "A package assembly source file is truncated.", m_Item, m_Path);
                std::byte extra{};
#if defined(_WIN32)
                DWORD read = 0;
                if (!ReadFile(m_Handle, &extra, 1, &read, nullptr) || read != 0)
                    Fail(HubErrorCode::InvalidData, "A package assembly source file changed.", m_Item, m_Path);
#else
                for (;;)
                {
                    const auto read = ::read(m_Handle, &extra, 1);
                    if (read < 0 && errno == EINTR)
                        continue;
                    if (read < 0 || read != 0)
                        Fail(HubErrorCode::InvalidData, "A package assembly source file changed.", m_Item, m_Path);
                    break;
                }
#endif
            }

          private:
            [[noreturn]] void IoFailure() const
            {
#if defined(_WIN32)
                const std::error_code error(static_cast<int>(GetLastError()), std::system_category());
#else
                const std::error_code error(errno, std::generic_category());
#endif
                Fail(HubErrorCode::IoRead, "A package assembly source file could not be read.", m_Item,
                     m_Path + ": " + error.message(), true);
            }

            std::string m_Item;
            std::string m_Path;
            std::uint64_t m_Size = 0;
            std::uint64_t m_Remaining = 0;
#if defined(_WIN32)
            HANDLE m_Handle = INVALID_HANDLE_VALUE;
#else
            int m_Handle = -1;
#endif
        };

        void ThrowIfCancelled(const PackageArchiveCallbacks& callbacks, const std::string& item)
        {
            if (callbacks.Cancelled && callbacks.Cancelled())
                Fail(HubErrorCode::WorkerInterrupted, "Package assembly was cancelled.", item, {}, true);
        }

        void Report(const PackageArchiveCallbacks& callbacks, const std::uint64_t completed, const std::uint64_t total,
                    const std::string_view file, const std::string& item)
        {
            if (!callbacks.Progress)
                return;
            try
            {
                callbacks.Progress(
                    {.CompletedBytes = completed, .TotalBytes = total, .CurrentFile = std::string(file)});
            }
            catch (const std::exception& error)
            {
                Fail(HubErrorCode::WorkerProtocolInvalid, "The package assembly progress callback failed.", item,
                     error.what());
            }
            catch (...)
            {
                Fail(HubErrorCode::WorkerProtocolInvalid, "The package assembly progress callback failed.", item);
            }
        }

        void ApplyMode(const std::filesystem::path& path, const PackageFile& file, const std::string& item)
        {
#if !defined(_WIN32)
            const auto permissions = file.Mode == 0755U
                                         ? std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                               std::filesystem::perms::group_exec |
                                               std::filesystem::perms::others_read | std::filesystem::perms::others_exec
                                         : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                               std::filesystem::perms::group_read | std::filesystem::perms::others_read;
            std::error_code error;
            std::filesystem::permissions(NativeIoPath(path), permissions, std::filesystem::perm_options::replace,
                                         error);
            if (error)
                Fail(HubErrorCode::IoWrite, "An assembled package file mode could not be applied.", item,
                     error.message(), true);
#else
            static_cast<void>(path);
            static_cast<void>(file);
            static_cast<void>(item);
#endif
        }

        void NormalizeDirectoryModes(const std::filesystem::path& root, const std::string& item)
        {
#if !defined(_WIN32)
            constexpr auto permissions = std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                         std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                         std::filesystem::perms::others_exec;
            std::error_code error;
            std::filesystem::permissions(NativeIoPath(root), permissions, std::filesystem::perm_options::replace,
                                         error);
            if (error)
                Fail(HubErrorCode::IoWrite, "The package assembly root mode could not be normalized.", item,
                     error.message(), true);
            for (std::filesystem::recursive_directory_iterator iterator(NativeIoPath(root)), end; iterator != end;
                 ++iterator)
            {
                const auto status = iterator->symlink_status();
                if (std::filesystem::is_symlink(status))
                    Fail(HubErrorCode::InvalidData, "The package assembly contains a symbolic link.", item);
                if (!std::filesystem::is_directory(status))
                    continue;
                std::filesystem::permissions(iterator->path(), permissions, std::filesystem::perm_options::replace,
                                             error);
                if (error)
                    Fail(HubErrorCode::IoWrite, "A package assembly directory mode could not be normalized.", item,
                         error.message(), true);
            }
#else
            static_cast<void>(root);
            static_cast<void>(item);
#endif
        }
    } // namespace

    HubResult<PackageTreeSeal> CreatePackageTreeSeal(const std::span<const PackageManifest> manifests)
    {
        try
        {
            if (manifests.empty() || manifests.size() > MaximumPackages)
            {
                return HubResult<PackageTreeSeal>::Failure(
                    AssemblyError(HubErrorCode::InvalidArgument, "The package tree manifest set is invalid."));
            }
            struct Entry final
            {
                PackageFile File;
            };
            std::vector<Entry> entries;
            std::vector<std::pair<std::string, std::vector<std::byte>>> identities;
            std::set<std::string, std::less<>> packageIds;
            std::set<std::string, std::less<>> filePaths;
            std::map<std::string, std::string, std::less<>> directoryPaths;
            std::uint64_t installedSize = 0;
            for (const auto& manifest : manifests)
            {
                if (const auto status = ValidatePackageManifest(manifest); !status)
                    return HubResult<PackageTreeSeal>::Failure(status.Error());
                if (!packageIds.insert(manifest.Id).second || manifest.Files.size() > MaximumSourceFiles ||
                    entries.size() > MaximumSourceFiles - manifest.Files.size() ||
                    manifest.InstalledSizeBytes > PackageArchiveLimits::MaximumPayloadBytes ||
                    installedSize > PackageArchiveLimits::MaximumPayloadBytes - manifest.InstalledSizeBytes)
                {
                    return HubResult<PackageTreeSeal>::Failure(
                        AssemblyError(HubErrorCode::PackageManifestInvalid,
                                      "The package tree manifest set exceeds its limits.", manifest.Id));
                }
                installedSize += manifest.InstalledSizeBytes;
                auto encoded = EncodePackageManifest(manifest);
                if (!encoded)
                    return HubResult<PackageTreeSeal>::Failure(encoded.Error());
                identities.emplace_back(
                    manifest.Id + '@' + manifest.Version.ToString(),
                    std::vector<std::byte>(
                        reinterpret_cast<const std::byte*>(encoded.Value().data()),
                        reinterpret_cast<const std::byte*>(encoded.Value().data() + encoded.Value().size())));
                for (const auto& file : manifest.Files)
                {
                    std::string originalPrefix;
                    std::string foldedPrefix;
                    bool collision = false;
                    auto component = file.Path.begin();
                    while (component != file.Path.end())
                    {
                        const auto original = Detail::PathToUtf8(*component);
                        const auto folded = Folded(original);
                        if (!originalPrefix.empty())
                        {
                            originalPrefix += '/';
                            foldedPrefix += '/';
                        }
                        originalPrefix += original;
                        foldedPrefix += folded;
                        const bool final = std::next(component) == file.Path.end();
                        if (final)
                            collision = !filePaths.insert(foldedPrefix).second || directoryPaths.contains(foldedPrefix);
                        else
                        {
                            const auto [existing, inserted] = directoryPaths.emplace(foldedPrefix, originalPrefix);
                            collision =
                                filePaths.contains(foldedPrefix) || (!inserted && existing->second != originalPrefix);
                        }
                        if (collision)
                            break;
                        ++component;
                    }
                    if (collision)
                    {
                        return HubResult<PackageTreeSeal>::Failure(
                            AssemblyError(HubErrorCode::PackageConflict,
                                          "Package payload paths collide in the assembled tree.", PathText(file.Path)));
                    }
                    entries.push_back({.File = file});
                }
            }
            std::ranges::sort(entries, [](const Entry& left, const Entry& right)
                              { return PathText(left.File.Path) < PathText(right.File.Path); });
            std::ranges::sort(identities, [](const auto& left, const auto& right) { return left.first < right.first; });
            Detail::Sha256Builder digest;
            UpdateInteger(digest, static_cast<std::uint64_t>(identities.size()));
            PackageTreeSeal result;
            result.m_InstalledSizeBytes = installedSize;
            result.m_Files.reserve(entries.size());
            result.m_PackageIdentities.reserve(identities.size());
            for (const auto& [identity, bytes] : identities)
            {
                UpdateBytes(digest, std::as_bytes(std::span(identity)));
                UpdateBytes(digest, std::span<const std::byte>(bytes));
                result.m_PackageIdentities.push_back(identity);
            }
            for (auto& entry : entries)
                result.m_Files.push_back(std::move(entry.File));
            result.m_IdentitySha256 = Detail::DigestToString(digest.Finish());
            return HubResult<PackageTreeSeal>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<PackageTreeSeal>::Failure(AssemblyError(
                HubErrorCode::PackageManifestInvalid, "The package tree seal could not be created.", {}, error.what()));
        }
    }

    HubResult<PackageManifest> CreatePackagePublicationManifest(const std::span<const PackageManifest> manifests)
    {
        try
        {
            if (manifests.empty() || manifests.front().Kind != PackageKind::Editor ||
                std::ranges::any_of(manifests | std::views::drop(1), [](const PackageManifest& manifest)
                                    { return manifest.Kind == PackageKind::Editor; }))
            {
                return HubResult<PackageManifest>::Failure(
                    AssemblyError(HubErrorCode::PackageManifestInvalid,
                                  "A package publication must begin with exactly one editor package."));
            }
            auto seal = CreatePackageTreeSeal(manifests);
            if (!seal)
                return HubResult<PackageManifest>::Failure(seal.Error());
            if (std::ranges::any_of(seal.Value().Files(), [](const PackageFile& file)
                                    { return IsMarkerPath(file.Path) || IsReceiptPath(file.Path); }))
            {
                return HubResult<PackageManifest>::Failure(AssemblyError(
                    HubErrorCode::PackageConflict,
                    "A package payload cannot declare reserved Hub installation metadata.", manifests.front().Id));
            }

            auto result = manifests.front();
            result.Dependencies.clear();
            result.Conflicts.clear();
            result.Files.assign(seal.Value().Files().begin(), seal.Value().Files().end());
            result.InstalledSizeBytes = seal.Value().InstalledSizeBytes();
            result.SignatureKeyId = PublicationSignatureKeyId;

            std::set<std::string, std::less<>> licenses;
            for (const auto& manifest : manifests)
                licenses.insert(manifest.LicenseReferences.begin(), manifest.LicenseReferences.end());
            result.LicenseReferences.assign(licenses.begin(), licenses.end());

            if (const auto status = RefreshPublicationIdentity(result); !status)
                return HubResult<PackageManifest>::Failure(status.Error());
            return HubResult<PackageManifest>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<PackageManifest>::Failure(
                AssemblyError(HubErrorCode::PackageManifestInvalid,
                              "The package publication manifest could not be created.", {}, error.what()));
        }
    }

    HubStatus ValidatePackageTree(const std::filesystem::path& root, const PackageTreeSeal& seal)
    {
        try
        {
            return ValidatePackageTree(root, ValidationManifest(seal));
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure(AssemblyError(HubErrorCode::InvalidData,
                                                    "The package tree seal could not be validated.", {}, error.what()));
        }
    }

    HubStatus SealPackageTreeForPublish(const std::filesystem::path& root, const PackageTreeSeal& seal)
    {
        try
        {
            return SealPackageTreeForPublish(root, ValidationManifest(seal));
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure(AssemblyError(
                HubErrorCode::InvalidData, "The package tree seal could not be synchronized.", {}, error.what()));
        }
    }

    HubResult<PackageAssemblyResult> AssemblePackageTreesToStaging(const PackageAssemblyRequest& request)
    {
        const auto allowedParent = request.AllowedStagingParent.lexically_normal();
        const auto staging = request.StagingRoot.lexically_normal();
        try
        {
            if (request.Sources.empty() || !IsAbsoluteBoundedPath(allowedParent) || !IsAbsoluteBoundedPath(staging) ||
                staging.parent_path() != allowedParent ||
                !Detail::PathToUtf8(staging.filename()).starts_with(".keire-stage-") ||
                !IsDirectoryWithoutLinks(allowedParent) || !IsMissing(staging))
            {
                return HubResult<PackageAssemblyResult>::Failure(
                    AssemblyError(HubErrorCode::InvalidArgument, "The package assembly request is invalid."));
            }
            std::vector<PackageManifest> manifests;
            manifests.reserve(request.Sources.size());
            for (const auto& source : request.Sources)
            {
                const auto root = source.Root.lexically_normal();
                if (!IsAbsoluteBoundedPath(root) || !IsDirectoryWithoutLinks(root) ||
                    IsFilesystemSameOrWithin(root, staging) || IsLexicallySameOrWithin(staging, root))
                {
                    return HubResult<PackageAssemblyResult>::Failure(
                        AssemblyError(HubErrorCode::UnsafeInstallRoot, "A package assembly source root is unsafe.",
                                      source.Manifest.Id));
                }
                if (const auto status = ValidatePackageTree(root, source.Manifest); !status)
                    return HubResult<PackageAssemblyResult>::Failure(status.Error());
                manifests.push_back(source.Manifest);
            }
            auto seal = CreatePackageTreeSeal(manifests);
            if (!seal)
                return HubResult<PackageAssemblyResult>::Failure(seal.Error());
            auto publicationManifest = CreatePackagePublicationManifest(manifests);
            if (!publicationManifest)
                return HubResult<PackageAssemblyResult>::Failure(publicationManifest.Error());

            std::error_code error;
            if (!std::filesystem::create_directory(NativeIoPath(staging), error) || error)
            {
                return HubResult<PackageAssemblyResult>::Failure(
                    AssemblyError(HubErrorCode::IoWrite, "The package assembly staging root could not be created.",
                                  Detail::PathToUtf8(staging.filename()), error.message(), true));
            }
            OwnedDirectoryGuard cleanup(staging);
            if (!IsDirectoryWithoutLinks(staging))
                Fail(HubErrorCode::UnsafeInstallRoot, "The package assembly staging root is unsafe.");

            std::map<std::string, std::pair<const PackageAssemblySource*, const PackageFile*>, std::less<>> files;
            for (const auto& source : request.Sources)
                for (const auto& file : source.Manifest.Files)
                    files.emplace(PathText(file.Path), std::pair{&source, &file});

            std::uint64_t completed = 0;
            Report(request.Callbacks, 0, seal.Value().InstalledSizeBytes(), {}, "package-assembly");
            for (const auto& sealedFile : seal.Value().Files())
            {
                ThrowIfCancelled(request.Callbacks, "package-assembly");
                const auto key = PathText(sealedFile.Path);
                const auto declaration = files.find(key);
                if (declaration == files.end())
                    Fail(HubErrorCode::InvalidData, "The package assembly inventory changed.", key);
                const auto& [source, file] = declaration->second;
                const auto sourcePath = source->Root.lexically_normal() / file->Path;
                const auto target = staging / file->Path;
                std::filesystem::create_directories(NativeIoPath(target.parent_path()), error);
                if (error || !IsDirectoryWithoutLinks(target.parent_path()))
                    Fail(HubErrorCode::IoWrite, "A package assembly target directory could not be created.", key,
                         error.message(), true);

                SecureInputFile input(sourcePath, source->Manifest.Id, key, file->SizeBytes);
                auto output = Detail::ExclusivePackageOutput::Create(target, source->Manifest.Id);
                if (!output)
                    throw AssemblyFailure(output.Error());
                std::vector<std::byte> buffer(BufferBytes);
                Detail::Sha256Builder digest;
                while (const auto count = input.Read(std::span(buffer)))
                {
                    const auto bytes = std::span(buffer).first(count);
                    const auto characters = std::span(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    if (const auto status = output.Value()->Write(characters); !status)
                        throw AssemblyFailure(status.Error());
                    digest.Update(bytes);
                    completed += count;
                    Report(request.Callbacks, completed, seal.Value().InstalledSizeBytes(), key, source->Manifest.Id);
                    ThrowIfCancelled(request.Callbacks, source->Manifest.Id);
                }
                input.RequireEnd();
                if (Detail::DigestToString(digest.Finish()) != file->Sha256)
                    Fail(HubErrorCode::DownloadChecksumMismatch,
                         "An assembled package source file failed digest verification.", source->Manifest.Id, key);
                if (const auto status = output.Value()->Finish(); !status)
                    throw AssemblyFailure(status.Error());
                if (const auto status = output.Value()->Publish(target); !status)
                    throw AssemblyFailure(status.Error());
                ApplyMode(target, *file, source->Manifest.Id);
            }
            NormalizeDirectoryModes(staging, publicationManifest.Value().Id);
            if (const auto status = ValidatePackageTree(staging, seal.Value()); !status)
                return HubResult<PackageAssemblyResult>::Failure(status.Error());
            auto withReceipt = FinalizePackageAssemblyReceipt(staging, publicationManifest.Value(), manifests);
            if (!withReceipt)
                return HubResult<PackageAssemblyResult>::Failure(withReceipt.Error());
            Report(request.Callbacks, completed, seal.Value().InstalledSizeBytes(), {}, "package-assembly");
            auto result =
                HubResult<PackageAssemblyResult>::Success({.Seal = std::move(seal).Value(),
                                                           .PublicationManifest = std::move(withReceipt).Value(),
                                                           .StagingRoot = staging});
            cleanup.Release();
            return result;
        }
        catch (const AssemblyFailure& failure)
        {
            return HubResult<PackageAssemblyResult>::Failure(failure.Error);
        }
        catch (const std::exception& error)
        {
            return HubResult<PackageAssemblyResult>::Failure(
                AssemblyError(HubErrorCode::InvalidData, "The package trees could not be assembled.",
                              Detail::PathToUtf8(staging.filename()), error.what()));
        }
    }

    HubResult<PackageManifest> FinalizePackageAssemblyReceipt(const std::filesystem::path& stagingRoot,
                                                              const PackageManifest& publicationManifest,
                                                              const std::span<const PackageManifest> sourceManifests)
    {
        const auto staging = stagingRoot.lexically_normal();
        try
        {
            if (!IsAbsoluteBoundedPath(staging) || !IsDirectoryWithoutLinks(staging) ||
                publicationManifest.Kind != PackageKind::Editor ||
                publicationManifest.SignatureKeyId != PublicationSignatureKeyId ||
                !HasCurrentPublicationIdentity(publicationManifest))
            {
                return HubResult<PackageManifest>::Failure(
                    AssemblyError(HubErrorCode::PackageManifestInvalid,
                                  "The package receipt manifest or staging root is invalid.", publicationManifest.Id));
            }
            auto seal = CreatePackageTreeSeal(sourceManifests);
            if (!seal)
                return HubResult<PackageManifest>::Failure(seal.Error());
            auto base = CreatePackagePublicationManifest(sourceManifests);
            if (!base)
                return HubResult<PackageManifest>::Failure(base.Error());
            auto encodedReceipt = EncodePackageInstallReceipt(CreateReceipt(sourceManifests, seal.Value()));
            if (!encodedReceipt)
                return HubResult<PackageManifest>::Failure(encodedReceipt.Error());

            const auto declaredReceipt = std::ranges::find_if(publicationManifest.Files, [](const PackageFile& file)
                                                              { return IsReceiptPath(file.Path); });
            const bool alreadyFinalized = declaredReceipt != publicationManifest.Files.end();
            if (alreadyFinalized && PathText(declaredReceipt->Path) != PackageInstallReceiptFileName)
            {
                return HubResult<PackageManifest>::Failure(AssemblyError(HubErrorCode::PackageManifestInvalid,
                                                                         "The package receipt path is not canonical.",
                                                                         publicationManifest.Id));
            }
            if (!alreadyFinalized)
            {
                auto sameBase = SameManifest(publicationManifest, base.Value());
                if (!sameBase)
                    return HubResult<PackageManifest>::Failure(sameBase.Error());
                if (!sameBase.Value())
                {
                    return HubResult<PackageManifest>::Failure(
                        AssemblyError(HubErrorCode::PackageManifestInvalid,
                                      "The package receipt base manifest changed.", publicationManifest.Id));
                }
            }

            const auto receiptPath = staging / PackageInstallReceiptFileName;
            const auto markerPath = staging / Detail::PathFromUtf8(InstallationMarkerPath);
            if (IsMissing(receiptPath))
            {
                if (alreadyFinalized || !IsMissing(markerPath))
                {
                    return HubResult<PackageManifest>::Failure(AssemblyError(
                        HubErrorCode::InvalidData, "The installed-package receipt is unexpectedly missing.",
                        publicationManifest.Id));
                }
                auto output = Detail::ExclusivePackageOutput::Create(receiptPath, publicationManifest.Id);
                if (!output)
                    return HubResult<PackageManifest>::Failure(output.Error());
                if (const auto status = output.Value()->Write(std::span(encodedReceipt.Value())); !status)
                    return HubResult<PackageManifest>::Failure(status.Error());
                if (const auto status = output.Value()->Finish(); !status)
                    return HubResult<PackageManifest>::Failure(status.Error());
                if (const auto status = output.Value()->Publish(receiptPath); !status)
                    return HubResult<PackageManifest>::Failure(status.Error());
            }

            SecureInputFile receiptInput(receiptPath, publicationManifest.Id, PackageInstallReceiptFileName,
                                         encodedReceipt.Value().size());
            std::vector<std::byte> buffer(BufferBytes);
            Detail::Sha256Builder digest;
            std::size_t offset = 0;
            while (const auto count = receiptInput.Read(std::span(buffer)))
            {
                const auto bytes = std::span(buffer).first(count);
                const auto expected = std::as_bytes(std::span(encodedReceipt.Value())).subspan(offset, count);
                if (!std::ranges::equal(bytes, expected))
                {
                    return HubResult<PackageManifest>::Failure(AssemblyError(
                        HubErrorCode::InvalidData, "The installed-package receipt does not match the verified plan.",
                        publicationManifest.Id));
                }
                digest.Update(bytes);
                offset += count;
            }
            receiptInput.RequireEnd();
            const PackageFile receiptFile{.Path = PackageInstallReceiptFileName,
                                          .SizeBytes = receiptInput.Size(),
                                          .Sha256 = Detail::DigestToString(digest.Finish()),
                                          .Mode = 0644U};
            ApplyMode(receiptPath, receiptFile, publicationManifest.Id);
            auto finalized = AddMetadataFile(std::move(base).Value(), receiptFile);
            if (!finalized)
                return finalized;
            if (alreadyFinalized)
            {
                auto sameFinal = SameManifest(publicationManifest, finalized.Value());
                if (!sameFinal)
                    return HubResult<PackageManifest>::Failure(sameFinal.Error());
                if (!sameFinal.Value())
                {
                    return HubResult<PackageManifest>::Failure(
                        AssemblyError(HubErrorCode::InvalidData, "The finalized package receipt manifest changed.",
                                      publicationManifest.Id));
                }
            }
            if (IsMissing(markerPath))
            {
                if (const auto status = ValidatePackageTree(staging, finalized.Value()); !status)
                    return HubResult<PackageManifest>::Failure(status.Error());
            }
            else
            {
                auto complete = FinalizePackageAssemblyMarker(staging, finalized.Value());
                if (!complete)
                    return HubResult<PackageManifest>::Failure(complete.Error());
            }
            return finalized;
        }
        catch (const AssemblyFailure& failure)
        {
            return HubResult<PackageManifest>::Failure(failure.Error);
        }
        catch (const std::exception& error)
        {
            return HubResult<PackageManifest>::Failure(
                AssemblyError(HubErrorCode::InvalidData, "The installed-package receipt could not be finalized.",
                              publicationManifest.Id, error.what()));
        }
    }

    HubResult<PackageManifest> FinalizePackageAssemblyMarker(const std::filesystem::path& stagingRoot,
                                                             const PackageManifest& publicationManifest)
    {
        const auto staging = stagingRoot.lexically_normal();
        try
        {
            if (!IsAbsoluteBoundedPath(staging) || !IsDirectoryWithoutLinks(staging) ||
                publicationManifest.Kind != PackageKind::Editor ||
                publicationManifest.SignatureKeyId != PublicationSignatureKeyId ||
                !HasCurrentPublicationIdentity(publicationManifest))
            {
                return HubResult<PackageManifest>::Failure(AssemblyError(
                    HubErrorCode::PackageManifestInvalid,
                    "The package publication manifest or staging root is invalid.", publicationManifest.Id));
            }

            const auto markerPath = staging / Detail::PathFromUtf8(InstallationMarkerPath);
            SecureInputFile marker(markerPath, publicationManifest.Id, std::string(InstallationMarkerPath));
            if (marker.Size() == 0 || marker.Size() > PackageArchiveLimits::MaximumFileBytes)
            {
                return HubResult<PackageManifest>::Failure(
                    AssemblyError(HubErrorCode::InvalidData, "The managed-install marker has an invalid size.",
                                  publicationManifest.Id));
            }
            std::vector<std::byte> buffer(BufferBytes);
            Detail::Sha256Builder digest;
            while (const auto count = marker.Read(std::span(buffer)))
                digest.Update(std::span(buffer).first(count));
            marker.RequireEnd();
            const PackageFile markerFile{.Path = Detail::PathFromUtf8(InstallationMarkerPath),
                                         .SizeBytes = marker.Size(),
                                         .Sha256 = Detail::DigestToString(digest.Finish()),
                                         .Mode = 0644U};
            ApplyMode(markerPath, markerFile, publicationManifest.Id);

            auto result = publicationManifest;
            const auto existing =
                std::ranges::find_if(result.Files, [](const PackageFile& file) { return IsMarkerPath(file.Path); });
            if (existing != result.Files.end())
            {
                if (PathText(existing->Path) != InstallationMarkerPath || existing->SizeBytes != markerFile.SizeBytes ||
                    existing->Sha256 != markerFile.Sha256 || existing->Mode != markerFile.Mode)
                {
                    return HubResult<PackageManifest>::Failure(AssemblyError(
                        HubErrorCode::InvalidData, "The managed-install marker changed after it was finalized.",
                        publicationManifest.Id));
                }
            }
            else
            {
                if (result.InstalledSizeBytes > PackageArchiveLimits::MaximumPayloadBytes - markerFile.SizeBytes)
                {
                    return HubResult<PackageManifest>::Failure(AssemblyError(
                        HubErrorCode::PackageManifestInvalid,
                        "The finalized package publication exceeds its payload limit.", publicationManifest.Id));
                }
                result.Files.push_back(markerFile);
                result.InstalledSizeBytes += markerFile.SizeBytes;
                std::ranges::sort(result.Files, [](const PackageFile& left, const PackageFile& right)
                                  { return PathText(left.Path) < PathText(right.Path); });
                if (const auto status = RefreshPublicationIdentity(result); !status)
                    return HubResult<PackageManifest>::Failure(status.Error());
            }
            if (const auto status = ValidatePackageTree(staging, result); !status)
                return HubResult<PackageManifest>::Failure(status.Error());
            return HubResult<PackageManifest>::Success(std::move(result));
        }
        catch (const AssemblyFailure& failure)
        {
            return HubResult<PackageManifest>::Failure(failure.Error);
        }
        catch (const std::exception& error)
        {
            return HubResult<PackageManifest>::Failure(
                AssemblyError(HubErrorCode::InvalidData, "The managed-install marker could not be finalized.",
                              publicationManifest.Id, error.what()));
        }
    }
} // namespace KeireHub
