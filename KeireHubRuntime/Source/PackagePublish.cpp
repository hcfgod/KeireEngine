#include "KeireHubRuntime/PackagePublish.h"

#include "KeireHubRuntime/PackageArchive.h"

#include "DistributionEncoding.h"
#include "PackageArchiveOutput.h"
#include "Persistence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <ranges>
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
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif
#include <unistd.h>
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumJournalBytes = 64U * 1024U;
        constexpr std::size_t MaximumJsonDepth = 16;

        [[nodiscard]] HubError PublishError(const HubErrorCode code, std::string message, std::string item = {},
                                            std::string details = {}, const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
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

        [[nodiscard]] HubStatus SyncDirectory(const std::filesystem::path& directory,
                                              const std::string_view operationId)
        {
#if defined(_WIN32)
            static_cast<void>(directory);
            static_cast<void>(operationId);
            return HubStatus::Success();
#else
            int flags = O_RDONLY;
#if defined(O_CLOEXEC)
            flags |= O_CLOEXEC;
#endif
#if defined(O_DIRECTORY)
            flags |= O_DIRECTORY;
#endif
            const int descriptor = ::open(NativeIoPath(directory).c_str(), flags);
            if (descriptor < 0)
            {
                return HubStatus::Failure(PublishError(
                    HubErrorCode::IoWrite, "A package publication directory could not be synchronized.",
                    std::string(operationId), std::error_code(errno, std::generic_category()).message(), true));
            }
            const int result = ::fsync(descriptor);
            const int failure = result == 0 ? 0 : errno;
            ::close(descriptor);
            if (failure != 0)
            {
                return HubStatus::Failure(PublishError(
                    HubErrorCode::IoWrite, "A package publication directory could not be synchronized.",
                    std::string(operationId), std::error_code(failure, std::generic_category()).message(), true));
            }
            return HubStatus::Success();
#endif
        }

        [[nodiscard]] HubStatus SyncJournal(const std::filesystem::path& journal, const std::string_view operationId)
        {
#if defined(_WIN32)
            const HANDLE handle = CreateFileW(NativeIoPath(journal).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return HubStatus::Failure(PublishError(
                    HubErrorCode::IoWrite, "The package publication journal could not be synchronized.",
                    std::string(operationId),
                    std::error_code(static_cast<int>(GetLastError()), std::system_category()).message(), true));
            }
            const bool flushed = FlushFileBuffers(handle) != 0;
            const auto failure = flushed ? ERROR_SUCCESS : GetLastError();
            CloseHandle(handle);
            if (!flushed)
            {
                return HubStatus::Failure(
                    PublishError(HubErrorCode::IoWrite, "The package publication journal could not be synchronized.",
                                 std::string(operationId),
                                 std::error_code(static_cast<int>(failure), std::system_category()).message(), true));
            }
#else
            int flags = O_RDONLY;
#if defined(O_CLOEXEC)
            flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
            flags |= O_NOFOLLOW;
#endif
            const int descriptor = ::open(NativeIoPath(journal).c_str(), flags);
            if (descriptor < 0)
            {
                return HubStatus::Failure(PublishError(
                    HubErrorCode::IoWrite, "The package publication journal could not be synchronized.",
                    std::string(operationId), std::error_code(errno, std::generic_category()).message(), true));
            }
            const int result = ::fsync(descriptor);
            const int failure = result == 0 ? 0 : errno;
            ::close(descriptor);
            if (failure != 0)
            {
                return HubStatus::Failure(PublishError(
                    HubErrorCode::IoWrite, "The package publication journal could not be synchronized.",
                    std::string(operationId), std::error_code(failure, std::generic_category()).message(), true));
            }
#endif
            return SyncDirectory(journal.parent_path(), operationId);
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

        [[nodiscard]] bool HasUnsafeLinkAncestor(const std::filesystem::path& path)
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
                std::error_code ancestorError;
                const auto ancestor = std::filesystem::symlink_status(NativeIoPath(current), ancestorError);
                if (!ancestorError && std::filesystem::is_symlink(ancestor))
                    return true;
                if (ancestorError && ancestorError != std::errc::no_such_file_or_directory)
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
            if (HasUnsafeLinkAncestor(path))
                return false;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            return !error && status.type() == std::filesystem::file_type::directory;
        }

        [[nodiscard]] bool IsRegularFileWithoutLinks(const std::filesystem::path& path)
        {
            if (HasUnsafeLinkAncestor(path))
                return false;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            return !error && status.type() == std::filesystem::file_type::regular;
        }

        [[nodiscard]] bool IsMissing(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            return (!error && status.type() == std::filesystem::file_type::not_found) ||
                   (error == std::errc::no_such_file_or_directory);
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
                    std::filesystem::remove_all(m_Path, ignored);
                }
            }

            void Release() noexcept { m_Released = true; }

          private:
            std::filesystem::path m_Path;
            bool m_Released = false;
        };

        [[nodiscard]] std::string_view PhaseText(const PackagePublishPhase phase) noexcept
        {
            switch (phase)
            {
            case PackagePublishPhase::Prepared:
                return "prepared";
            case PackagePublishPhase::BackupMoved:
                return "backupMoved";
            case PackagePublishPhase::Published:
                return "published";
            }
            return {};
        }

        [[nodiscard]] std::optional<PackagePublishPhase> ParsePhase(const std::string_view value) noexcept
        {
            if (value == "prepared")
                return PackagePublishPhase::Prepared;
            if (value == "backupMoved")
                return PackagePublishPhase::BackupMoved;
            if (value == "published")
                return PackagePublishPhase::Published;
            return std::nullopt;
        }

        [[nodiscard]] bool SamePaths(const PackagePublishPaths& left, const PackagePublishPaths& right)
        {
            return left.AllowedParent.lexically_normal() == right.AllowedParent.lexically_normal() &&
                   left.StagingRoot.lexically_normal() == right.StagingRoot.lexically_normal() &&
                   left.Destination.lexically_normal() == right.Destination.lexically_normal() &&
                   left.BackupRoot.lexically_normal() == right.BackupRoot.lexically_normal() &&
                   left.LockRoot.lexically_normal() == right.LockRoot.lexically_normal() &&
                   left.Journal.lexically_normal() == right.Journal.lexically_normal();
        }

        [[nodiscard]] std::string FoldedName(const std::filesystem::path& path)
        {
            auto value = Detail::PathToUtf8(path.filename());
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] HubResult<std::string> ManifestIdentity(const PackageManifest& manifest)
        {
            auto encoded = EncodePackageArchiveManifest(manifest);
            if (!encoded)
                return HubResult<std::string>::Failure(encoded.Error());
            constexpr std::string_view domain = "KEIRE-PUBLISH-MANIFEST-1";
            std::vector<std::byte> bytes;
            bytes.reserve(domain.size() + encoded.Value().size() + sizeof(std::uint64_t) +
                          manifest.ArtifactSha256.size());
            const auto append = [&](const auto values) { bytes.insert(bytes.end(), values.begin(), values.end()); };
            append(std::as_bytes(std::span(domain)));
            append(std::span<const std::byte>(encoded.Value()));
            for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index)
            {
                bytes.push_back(static_cast<std::byte>((manifest.ArtifactSizeBytes >> (index * 8U)) & 0xffU));
            }
            append(std::as_bytes(std::span(manifest.ArtifactSha256)));
            return HubResult<std::string>::Success(Detail::Sha256Hex(bytes));
        }

        [[nodiscard]] HubStatus ValidateManifestBinding(const PackagePublishJournal& journal,
                                                        const PackageManifest& manifest)
        {
            auto identity = ManifestIdentity(manifest);
            if (!identity)
                return HubStatus::Failure(identity.Error());
            if (identity.Value() != journal.ManifestSha256)
            {
                return HubStatus::Failure(PublishError(HubErrorCode::PackageManifestInvalid,
                                                       "The package publication manifest changed.",
                                                       journal.OperationId));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidatePaths(const PackagePublishPaths& paths, const std::string_view operationId)
        {
            if (!Detail::IsBoundedIdentifier(operationId) || !IsAbsoluteBoundedPath(paths.AllowedParent) ||
                !IsAbsoluteBoundedPath(paths.Destination))
            {
                return HubStatus::Failure(PublishError(HubErrorCode::InvalidArgument,
                                                       "The package publication identity is invalid.",
                                                       std::string(operationId)));
            }
            const auto parent = paths.AllowedParent.lexically_normal();
            const PackagePublishPaths expected{.AllowedParent = parent,
                                               .StagingRoot = parent / (".keire-stage-" + std::string(operationId)),
                                               .Destination = paths.Destination.lexically_normal(),
                                               .BackupRoot = parent / (".keire-backup-" + std::string(operationId)),
                                               .LockRoot = parent / ".keire-publish.lock",
                                               .Journal = parent / ".keire-publish.lock/journal.json"};
            const auto destinationName = FoldedName(expected.Destination);
            if (expected.Destination.parent_path() != parent || !SamePaths(paths, expected) ||
                !IsAbsoluteBoundedPath(paths.StagingRoot) || !IsAbsoluteBoundedPath(paths.BackupRoot) ||
                !IsAbsoluteBoundedPath(paths.LockRoot) || !IsAbsoluteBoundedPath(paths.Journal) ||
                destinationName.starts_with(".keire-stage-") || destinationName.starts_with(".keire-backup-") ||
                destinationName.starts_with(".keire-publish-") || destinationName.starts_with(".keire-publish-lock-") ||
                destinationName == ".keire-publish.lock" || destinationName == FoldedName(expected.StagingRoot) ||
                destinationName == FoldedName(expected.BackupRoot) || destinationName == FoldedName(expected.LockRoot))
            {
                return HubStatus::Failure(
                    PublishError(HubErrorCode::UnsafeInstallRoot,
                                 "The package publication paths do not match their confined operation identity.",
                                 std::string(operationId)));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] Detail::Json EncodeJournal(const PackagePublishJournal& journal)
        {
            return {{"schemaVersion", journal.SchemaVersion},
                    {"operationId", journal.OperationId},
                    {"manifestSha256", journal.ManifestSha256},
                    {"phase", PhaseText(journal.Phase)},
                    {"replacesExisting", journal.ReplacesExisting},
                    {"paths",
                     {{"allowedParent", Detail::PathToUtf8(journal.Paths.AllowedParent)},
                      {"staging", Detail::PathToUtf8(journal.Paths.StagingRoot)},
                      {"destination", Detail::PathToUtf8(journal.Paths.Destination)},
                      {"backup", Detail::PathToUtf8(journal.Paths.BackupRoot)},
                      {"lock", Detail::PathToUtf8(journal.Paths.LockRoot)},
                      {"journal", Detail::PathToUtf8(journal.Paths.Journal)}}}};
        }

        [[nodiscard]] HubStatus WriteJournalFile(const std::filesystem::path& path,
                                                 const PackagePublishJournal& journal, const bool replaceExisting)
        {
            const auto text = EncodeJournal(journal).dump(2) + '\n';
            auto output = Detail::ExclusivePackageOutput::Create(path, journal.OperationId);
            if (!output)
                return HubStatus::Failure(output.Error());
            if (const auto status = output.Value()->Write(std::span(text)); !status)
                return status;
            if (const auto status = output.Value()->Finish(); !status)
                return status;
            return output.Value()->Publish(path, replaceExisting);
        }

        [[nodiscard]] HubStatus StoreJournal(const PackagePublishJournal& journal)
        {
            if (journal.SchemaVersion != PackagePublishJournal::CurrentSchemaVersion ||
                !Detail::IsSha256(journal.ManifestSha256) || journal.Phase < PackagePublishPhase::Prepared ||
                journal.Phase > PackagePublishPhase::Published)
            {
                return HubStatus::Failure(PublishError(
                    HubErrorCode::InvalidArgument, "The package publication journal is invalid.", journal.OperationId));
            }
            if (const auto status = ValidatePaths(journal.Paths, journal.OperationId); !status)
                return status;
            if (!IsDirectoryWithoutLinks(journal.Paths.LockRoot))
            {
                return HubStatus::Failure(PublishError(HubErrorCode::UnsafeInstallRoot,
                                                       "The package publication lock is unsafe.", journal.OperationId));
            }
            if (const auto status = WriteJournalFile(journal.Paths.Journal, journal, true); !status)
            {
                return status;
            }
            return SyncJournal(journal.Paths.Journal, journal.OperationId);
        }

        [[nodiscard]] HubStatus RenameDirectory(const std::filesystem::path& source,
                                                const std::filesystem::path& destination,
                                                const std::string& operationId)
        {
            std::error_code error;
#if defined(_WIN32)
            if (!MoveFileExW(NativeIoPath(source).c_str(), NativeIoPath(destination).c_str(), MOVEFILE_WRITE_THROUGH))
                error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#elif defined(__linux__)
#if defined(SYS_renameat2)
            if (::syscall(SYS_renameat2, AT_FDCWD, NativeIoPath(source).c_str(), AT_FDCWD,
                          NativeIoPath(destination).c_str(), RENAME_NOREPLACE) != 0)
            {
                error = std::error_code(errno, std::generic_category());
            }
#else
            error = std::make_error_code(std::errc::operation_not_supported);
#endif
#elif defined(__APPLE__)
            if (::renamex_np(NativeIoPath(source).c_str(), NativeIoPath(destination).c_str(), RENAME_EXCL) != 0)
                error = std::error_code(errno, std::generic_category());
#else
            error = std::make_error_code(std::errc::operation_not_supported);
#endif
            if (error)
            {
                const auto code = error == std::errc::file_exists || error == std::errc::directory_not_empty
                                      ? HubErrorCode::DestinationConflict
                                      : HubErrorCode::IoWrite;
                return HubStatus::Failure(PublishError(code, "A package publication directory could not be moved.",
                                                       operationId, error.message(), true));
            }
            return SyncDirectory(destination.parent_path(), operationId);
        }

        [[nodiscard]] bool IsJournalTemporary(const std::filesystem::path& path)
        {
            const auto name = Detail::PathToUtf8(path.filename());
            constexpr std::string_view prefix = "journal.json.tmp-";
            return name.starts_with(prefix) && name.size() > prefix.size();
        }

        [[nodiscard]] HubStatus RemoveJournalTemporaries(const std::filesystem::path& lockRoot,
                                                         const std::string& operationId)
        {
            std::error_code error;
            bool removed = false;
            for (std::filesystem::directory_iterator iterator(lockRoot, error), end; !error && iterator != end;
                 iterator.increment(error))
            {
                if (!IsJournalTemporary(iterator->path()))
                    continue;
                if (!IsRegularFileWithoutLinks(iterator->path()) || !std::filesystem::remove(iterator->path(), error) ||
                    error)
                {
                    return HubStatus::Failure(
                        PublishError(HubErrorCode::UnsafeInstallRoot,
                                     "An interrupted package journal staging file could not be removed.", operationId,
                                     error.message(), true));
                }
                removed = true;
            }
            if (error)
            {
                return HubStatus::Failure(PublishError(HubErrorCode::IoRead,
                                                       "The package publication lock could not be inspected.",
                                                       operationId, error.message(), true));
            }
            return removed ? SyncDirectory(lockRoot, operationId) : HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemoveJournal(const PackagePublishJournal& journal)
        {
            if (const auto status = RemoveJournalTemporaries(journal.Paths.LockRoot, journal.OperationId); !status)
                return status;
            std::error_code error;
            if (!std::filesystem::remove(journal.Paths.Journal, error) || error)
            {
                return HubStatus::Failure(PublishError(
                    HubErrorCode::IoWrite, "The completed package publication journal could not be removed.",
                    journal.OperationId, error.message(), true));
            }
            if (const auto status = SyncDirectory(journal.Paths.LockRoot, journal.OperationId); !status)
                return status;
            if (!std::filesystem::remove(journal.Paths.LockRoot, error) || error)
            {
                return HubStatus::Failure(PublishError(HubErrorCode::IoWrite,
                                                       "The completed package publication lock could not be removed.",
                                                       journal.OperationId, error.message(), true));
            }
            return SyncDirectory(journal.Paths.AllowedParent, journal.OperationId);
        }

        [[nodiscard]] HubStatus FinishPublished(PackagePublishJournal& journal, const PackageManifest& manifest)
        {
            if (!IsDirectoryWithoutLinks(journal.Paths.Destination) || !IsMissing(journal.Paths.StagingRoot))
            {
                return HubStatus::Failure(PublishError(HubErrorCode::InvalidData,
                                                       "The published package filesystem state is inconsistent.",
                                                       journal.OperationId));
            }
            if (const auto status = ValidatePackageTree(journal.Paths.Destination, manifest); !status)
            {
                if (!journal.ReplacesExisting || !IsDirectoryWithoutLinks(journal.Paths.BackupRoot) ||
                    !IsMissing(journal.Paths.StagingRoot))
                {
                    return status;
                }
                if (const auto moved =
                        RenameDirectory(journal.Paths.Destination, journal.Paths.StagingRoot, journal.OperationId);
                    !moved)
                {
                    auto error = status.Error();
                    error.TechnicalDetails += (error.TechnicalDetails.empty() ? "" : "; ") +
                                              std::string("rollback quarantine failed: ") + moved.Error().Message;
                    return HubStatus::Failure(std::move(error));
                }
                if (const auto restored =
                        RenameDirectory(journal.Paths.BackupRoot, journal.Paths.Destination, journal.OperationId);
                    !restored)
                {
                    const auto ignored =
                        RenameDirectory(journal.Paths.StagingRoot, journal.Paths.Destination, journal.OperationId);
                    static_cast<void>(ignored);
                    auto error = status.Error();
                    error.TechnicalDetails += (error.TechnicalDetails.empty() ? "" : "; ") +
                                              std::string("rollback restore failed: ") + restored.Error().Message;
                    return HubStatus::Failure(std::move(error));
                }
                journal.Phase = PackagePublishPhase::Prepared;
                if (const auto stored = StoreJournal(journal); !stored)
                {
                    auto error = status.Error();
                    error.TechnicalDetails += (error.TechnicalDetails.empty() ? "" : "; ") +
                                              std::string("rollback journal failed: ") + stored.Error().Message;
                    return HubStatus::Failure(std::move(error));
                }
                return status;
            }
            if (journal.ReplacesExisting && !IsMissing(journal.Paths.BackupRoot))
            {
                if (!IsDirectoryWithoutLinks(journal.Paths.BackupRoot))
                {
                    return HubStatus::Failure(PublishError(HubErrorCode::UnsafeInstallRoot,
                                                           "The package publication backup is not a regular directory.",
                                                           journal.OperationId));
                }
                std::error_code error;
                std::filesystem::remove_all(journal.Paths.BackupRoot, error);
                if (error)
                {
                    return HubStatus::Failure(PublishError(HubErrorCode::IoWrite,
                                                           "The previous package backup could not be removed.",
                                                           journal.OperationId, error.message(), true));
                }
                if (const auto status = SyncDirectory(journal.Paths.AllowedParent, journal.OperationId); !status)
                    return status;
            }
            else if (!journal.ReplacesExisting && !IsMissing(journal.Paths.BackupRoot))
            {
                return HubStatus::Failure(PublishError(HubErrorCode::InvalidData,
                                                       "An unexpected package publication backup exists.",
                                                       journal.OperationId));
            }
            return RemoveJournal(journal);
        }

        [[nodiscard]] bool SameJournal(const PackagePublishJournal& left, const PackagePublishJournal& right)
        {
            return left.SchemaVersion == right.SchemaVersion && left.OperationId == right.OperationId &&
                   left.ManifestSha256 == right.ManifestSha256 && SamePaths(left.Paths, right.Paths) &&
                   left.ReplacesExisting == right.ReplacesExisting && left.Phase == right.Phase;
        }

        [[nodiscard]] HubStatus ValidateDestinationPolicy(const bool replacesExisting,
                                                          const PackagePublishOptions& options,
                                                          const std::string_view operationId)
        {
            if (options.DestinationPolicy == PackagePublishDestinationPolicy::RequireAbsent && replacesExisting)
            {
                return HubStatus::Failure(PublishError(HubErrorCode::DestinationConflict,
                                                       "A new package install cannot replace an existing destination.",
                                                       std::string(operationId)));
            }
            if (options.DestinationPolicy == PackagePublishDestinationPolicy::RequireExisting && !replacesExisting)
            {
                return HubStatus::Failure(PublishError(HubErrorCode::UnsafeInstallRoot,
                                                       "A package repair requires its authorized destination.",
                                                       std::string(operationId)));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus AuthorizeMutation(const PackagePublishJournal& journal,
                                                  const PackagePublishOptions& options)
        {
            return options.AuthorizeMutation ? options.AuthorizeMutation(journal) : HubStatus::Success();
        }
    } // namespace

    HubResult<PackagePublishPaths> PlanPackagePublish(const std::filesystem::path& allowedParent,
                                                      const std::filesystem::path& destination, std::string operationId)
    {
        const auto normalizedParent = allowedParent.lexically_normal();
        const auto normalized = destination.lexically_normal();
        if (!Detail::IsBoundedIdentifier(operationId) || !IsAbsoluteBoundedPath(normalizedParent) ||
            !IsAbsoluteBoundedPath(normalized) || normalized.parent_path() != normalizedParent)
        {
            return HubResult<PackagePublishPaths>::Failure(PublishError(
                HubErrorCode::InvalidArgument, "The package publication request is invalid.", std::move(operationId)));
        }
        PackagePublishPaths paths{.AllowedParent = normalizedParent,
                                  .StagingRoot = normalizedParent / (".keire-stage-" + operationId),
                                  .Destination = normalized,
                                  .BackupRoot = normalizedParent / (".keire-backup-" + operationId),
                                  .LockRoot = normalizedParent / ".keire-publish.lock",
                                  .Journal = normalizedParent / ".keire-publish.lock/journal.json"};
        if (const auto status = ValidatePaths(paths, operationId); !status)
            return HubResult<PackagePublishPaths>::Failure(status.Error());
        return HubResult<PackagePublishPaths>::Success(std::move(paths));
    }

    HubResult<PackagePublishJournal> PreparePackagePublish(const PackagePublishPaths& paths,
                                                           const PackageManifest& manifest, std::string operationId,
                                                           const PackagePublishOptions& options)
    {
        if (const auto status = ValidatePaths(paths, operationId); !status)
            return HubResult<PackagePublishJournal>::Failure(status.Error());
        if (!IsDirectoryWithoutLinks(paths.AllowedParent) || !IsDirectoryWithoutLinks(paths.StagingRoot))
        {
            return HubResult<PackagePublishJournal>::Failure(
                PublishError(HubErrorCode::InvalidArgument,
                             "Package publication requires an extracted staging directory.", operationId));
        }
        if (!IsMissing(paths.BackupRoot) || !IsMissing(paths.LockRoot))
        {
            return HubResult<PackagePublishJournal>::Failure(
                PublishError(HubErrorCode::DestinationConflict, "Another package publication or backup already exists.",
                             operationId));
        }
        const bool replaces = !IsMissing(paths.Destination);
        if (const auto status = ValidateDestinationPolicy(replaces, options, operationId); !status)
            return HubResult<PackagePublishJournal>::Failure(status.Error());
        if (replaces && !IsDirectoryWithoutLinks(paths.Destination))
        {
            return HubResult<PackagePublishJournal>::Failure(
                PublishError(HubErrorCode::UnsafeInstallRoot,
                             "The existing package destination is not a regular directory.", operationId));
        }
        const auto lockStaging = paths.AllowedParent / (".keire-publish-lock-" + operationId);
        if (!IsMissing(lockStaging))
        {
            return HubResult<PackagePublishJournal>::Failure(
                PublishError(HubErrorCode::DestinationConflict,
                             "The package publication lock staging path is occupied.", operationId));
        }
        std::error_code error;
        if (!std::filesystem::create_directory(lockStaging, error) || error)
        {
            return HubResult<PackagePublishJournal>::Failure(
                PublishError(HubErrorCode::DestinationConflict, "The package publication lock could not be prepared.",
                             operationId, error.message(), true));
        }
        OwnedDirectoryGuard lockCleanup(lockStaging);
        auto manifestIdentity = ManifestIdentity(manifest);
        if (!manifestIdentity)
            return HubResult<PackagePublishJournal>::Failure(manifestIdentity.Error());
        if (const auto status = ValidatePackageTree(paths.StagingRoot, manifest); !status)
            return HubResult<PackagePublishJournal>::Failure(status.Error());
        PackagePublishJournal journal{.OperationId = std::move(operationId),
                                      .ManifestSha256 = std::move(manifestIdentity).Value(),
                                      .Paths = paths,
                                      .ReplacesExisting = replaces};
        const auto stagedJournal = lockStaging / "journal.json";
        if (const auto status = WriteJournalFile(stagedJournal, journal, false); !status)
            return HubResult<PackagePublishJournal>::Failure(status.Error());
        if (const auto status = SyncJournal(stagedJournal, journal.OperationId); !status)
            return HubResult<PackagePublishJournal>::Failure(status.Error());
        if (const auto status = RenameDirectory(lockStaging, paths.LockRoot, journal.OperationId); !status)
            return HubResult<PackagePublishJournal>::Failure(status.Error());
        lockCleanup.Release();
        return HubResult<PackagePublishJournal>::Success(std::move(journal));
    }

    static HubResult<PackagePublishJournal>
    LoadPackagePublishJournalFromStorage(const std::filesystem::path& allowedParent,
                                         const std::filesystem::path& storageRoot, const std::filesystem::path& journal)
    {
        try
        {
            const auto normalizedParent = allowedParent.lexically_normal();
            const auto normalizedStorage = storageRoot.lexically_normal();
            const auto normalizedJournal = journal.lexically_normal();
            const auto expectedLock = normalizedParent / ".keire-publish.lock";
            const auto expectedJournal = expectedLock / "journal.json";
            if (!IsAbsoluteBoundedPath(normalizedParent) || !IsAbsoluteBoundedPath(normalizedStorage) ||
                !IsAbsoluteBoundedPath(normalizedJournal) || normalizedStorage.parent_path() != normalizedParent ||
                normalizedJournal != normalizedStorage / "journal.json" || !IsDirectoryWithoutLinks(normalizedParent) ||
                !IsDirectoryWithoutLinks(normalizedStorage))
            {
                return HubResult<PackagePublishJournal>::Failure(
                    PublishError(HubErrorCode::InvalidArgument, "The package publication journal path is invalid.",
                                 Detail::PathToUtf8(journal.filename())));
            }
            if (!IsRegularFileWithoutLinks(normalizedJournal))
            {
                return HubResult<PackagePublishJournal>::Failure(
                    PublishError(HubErrorCode::IoRead, "The package publication journal is unavailable.",
                                 Detail::PathToUtf8(journal.filename()), {}, true));
            }
            auto text = Detail::ReadTextFile(normalizedJournal, MaximumJournalBytes);
            if (!text)
                return HubResult<PackagePublishJournal>::Failure(text.Error());
            auto parsed = Detail::ParseStrictJson(text.Value(), MaximumJsonDepth, HubErrorCode::InvalidData,
                                                  "The package publication journal is malformed.",
                                                  Detail::PathToUtf8(journal.filename()));
            if (!parsed)
                return HubResult<PackagePublishJournal>::Failure(parsed.Error());
            constexpr std::array rootKeys{std::string_view{"schemaVersion"},    std::string_view{"operationId"},
                                          std::string_view{"manifestSha256"},   std::string_view{"phase"},
                                          std::string_view{"replacesExisting"}, std::string_view{"paths"}};
            constexpr std::array pathKeys{std::string_view{"allowedParent"}, std::string_view{"staging"},
                                          std::string_view{"destination"},   std::string_view{"backup"},
                                          std::string_view{"lock"},          std::string_view{"journal"}};
            if (!parsed.Value().is_object() || parsed.Value().size() != rootKeys.size() ||
                !std::ranges::all_of(rootKeys,
                                     [&](const auto key) { return parsed.Value().contains(std::string(key)); }) ||
                !parsed.Value().at("paths").is_object() || parsed.Value().at("paths").size() != pathKeys.size() ||
                !std::ranges::all_of(pathKeys, [&](const auto key)
                                     { return parsed.Value().at("paths").contains(std::string(key)); }))
            {
                throw std::invalid_argument("The journal has an unexpected schema.");
            }
            const auto phase = ParsePhase(parsed.Value().at("phase").get<std::string>());
            if (!phase)
                throw std::invalid_argument("The journal phase is invalid.");
            const auto& encodedPaths = parsed.Value().at("paths");
            PackagePublishJournal result{
                .SchemaVersion = parsed.Value().at("schemaVersion").get<std::uint32_t>(),
                .OperationId = parsed.Value().at("operationId").get<std::string>(),
                .ManifestSha256 = parsed.Value().at("manifestSha256").get<std::string>(),
                .Paths = {.AllowedParent = Detail::PathFromUtf8(encodedPaths.at("allowedParent").get<std::string>()),
                          .StagingRoot = Detail::PathFromUtf8(encodedPaths.at("staging").get<std::string>()),
                          .Destination = Detail::PathFromUtf8(encodedPaths.at("destination").get<std::string>()),
                          .BackupRoot = Detail::PathFromUtf8(encodedPaths.at("backup").get<std::string>()),
                          .LockRoot = Detail::PathFromUtf8(encodedPaths.at("lock").get<std::string>()),
                          .Journal = Detail::PathFromUtf8(encodedPaths.at("journal").get<std::string>())},
                .ReplacesExisting = parsed.Value().at("replacesExisting").get<bool>(),
                .Phase = *phase};
            if (result.SchemaVersion != PackagePublishJournal::CurrentSchemaVersion ||
                !Detail::IsSha256(result.ManifestSha256) ||
                result.Paths.AllowedParent.lexically_normal() != normalizedParent ||
                result.Paths.Journal.lexically_normal() != expectedJournal)
            {
                return HubResult<PackagePublishJournal>::Failure(
                    PublishError(HubErrorCode::UnsupportedSchema, "The package publication journal is incompatible.",
                                 result.OperationId));
            }
            if (const auto validation = ValidatePaths(result.Paths, result.OperationId); !validation)
                return HubResult<PackagePublishJournal>::Failure(validation.Error());
            return HubResult<PackagePublishJournal>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<PackagePublishJournal>::Failure(
                PublishError(HubErrorCode::InvalidData, "The package publication journal is invalid.",
                             Detail::PathToUtf8(journal.filename()), error.what()));
        }
    }

    HubResult<PackagePublishJournal> LoadPackagePublishJournal(const std::filesystem::path& allowedParent,
                                                               const std::filesystem::path& journal)
    {
        return LoadPackagePublishJournalFromStorage(allowedParent,
                                                    allowedParent.lexically_normal() / ".keire-publish.lock", journal);
    }

    HubStatus ContinuePackagePublish(PackagePublishJournal journal, const PackageManifest& manifest,
                                     const PackagePublishOptions& options)
    {
        if (const auto status = ValidatePaths(journal.Paths, journal.OperationId); !status)
            return status;
        if (const auto status = ValidateManifestBinding(journal, manifest); !status)
            return status;
        if (const auto status = ValidateDestinationPolicy(journal.ReplacesExisting, options, journal.OperationId);
            !status)
        {
            return status;
        }
        if (!IsDirectoryWithoutLinks(journal.Paths.AllowedParent) || !IsDirectoryWithoutLinks(journal.Paths.LockRoot))
        {
            return HubStatus::Failure(PublishError(HubErrorCode::UnsafeInstallRoot,
                                                   "The package publication boundary is unsafe.", journal.OperationId));
        }
        auto persisted = LoadPackagePublishJournal(journal.Paths.AllowedParent, journal.Paths.Journal);
        if (!persisted)
            return HubStatus::Failure(persisted.Error());
        if (!SameJournal(journal, persisted.Value()))
        {
            return HubStatus::Failure(PublishError(HubErrorCode::InvalidTransition,
                                                   "The package publication journal changed before it could continue.",
                                                   journal.OperationId));
        }

        const auto rollbackReplacement = [&](const HubStatus& primary)
        {
            if (!journal.ReplacesExisting)
                return HubStatus::Failure(primary.Error());
            const auto rollback =
                RenameDirectory(journal.Paths.BackupRoot, journal.Paths.Destination, journal.OperationId);
            if (!rollback)
            {
                auto error = primary.Error();
                error.TechnicalDetails += (error.TechnicalDetails.empty() ? "" : "; ") +
                                          std::string("rollback failed: ") + rollback.Error().Message + " " +
                                          rollback.Error().TechnicalDetails;
                error.Retryable = true;
                return HubStatus::Failure(std::move(error));
            }
            journal.Phase = PackagePublishPhase::Prepared;
            if (const auto stored = StoreJournal(journal); !stored)
            {
                auto error = primary.Error();
                error.TechnicalDetails += (error.TechnicalDetails.empty() ? "" : "; ") +
                                          std::string("rollback journal failed: ") + stored.Error().Message + " " +
                                          stored.Error().TechnicalDetails;
                error.Retryable = true;
                return HubStatus::Failure(std::move(error));
            }
            return HubStatus::Failure(primary.Error());
        };

        if (journal.Phase == PackagePublishPhase::Prepared)
        {
            if (!IsDirectoryWithoutLinks(journal.Paths.StagingRoot))
            {
                return HubStatus::Failure(PublishError(HubErrorCode::InvalidData,
                                                       "The prepared package staging directory is unavailable.",
                                                       journal.OperationId));
            }
            if (const auto status = SealPackageTreeForPublish(journal.Paths.StagingRoot, manifest); !status)
                return status;
            if (journal.ReplacesExisting)
            {
                if (!IsDirectoryWithoutLinks(journal.Paths.Destination) || !IsMissing(journal.Paths.BackupRoot))
                {
                    return HubStatus::Failure(PublishError(HubErrorCode::UnsafeInstallRoot,
                                                           "The package replacement destination is unsafe.",
                                                           journal.OperationId));
                }
                if (const auto status = AuthorizeMutation(journal, options); !status)
                    return status;
                if (const auto status =
                        RenameDirectory(journal.Paths.Destination, journal.Paths.BackupRoot, journal.OperationId);
                    !status)
                {
                    return status;
                }
                if (const auto status = AuthorizeMutation(journal, options); !status)
                    return rollbackReplacement(status);
            }
            else if (!IsMissing(journal.Paths.Destination) || !IsMissing(journal.Paths.BackupRoot))
            {
                return HubStatus::Failure(PublishError(HubErrorCode::DestinationConflict,
                                                       "A package publication path became occupied before commit.",
                                                       journal.OperationId));
            }
            journal.Phase = PackagePublishPhase::BackupMoved;
            if (const auto status = StoreJournal(journal); !status)
                return rollbackReplacement(status);
        }

        if (journal.Phase == PackagePublishPhase::BackupMoved)
        {
            if (!IsDirectoryWithoutLinks(journal.Paths.StagingRoot) || !IsMissing(journal.Paths.Destination) ||
                (journal.ReplacesExisting ? !IsDirectoryWithoutLinks(journal.Paths.BackupRoot)
                                          : !IsMissing(journal.Paths.BackupRoot)))
            {
                return HubStatus::Failure(PublishError(HubErrorCode::InvalidData,
                                                       "The package publication filesystem state is inconsistent.",
                                                       journal.OperationId));
            }
            if (const auto status = AuthorizeMutation(journal, options); !status)
                return status;
            if (const auto status = ValidatePackageTree(journal.Paths.StagingRoot, manifest); !status)
                return rollbackReplacement(status);
            if (const auto status =
                    RenameDirectory(journal.Paths.StagingRoot, journal.Paths.Destination, journal.OperationId);
                !status)
            {
                return rollbackReplacement(status);
            }
            journal.Phase = PackagePublishPhase::Published;
            if (const auto status = StoreJournal(journal); !status)
                return status;
        }

        return FinishPublished(journal, manifest);
    }

    HubStatus PublishStagedPackage(const PackagePublishPaths& paths, const PackageManifest& manifest,
                                   std::string operationId, const PackagePublishOptions& options)
    {
        auto prepared = PreparePackagePublish(paths, manifest, std::move(operationId), options);
        if (!prepared)
            return HubStatus::Failure(prepared.Error());
        return ContinuePackagePublish(std::move(prepared).Value(), manifest, options);
    }

    HubStatus RecoverPackagePublish(const std::filesystem::path& allowedParent,
                                    const std::filesystem::path& journalPath, const PackageManifest& manifest,
                                    std::string operationId, const PackagePublishOptions& options)
    {
        const auto normalizedParent = allowedParent.lexically_normal();
        const auto lock = normalizedParent / ".keire-publish.lock";
        const auto normalizedJournal = journalPath.lexically_normal();
        if (!Detail::IsBoundedIdentifier(operationId) || !IsAbsoluteBoundedPath(normalizedParent) ||
            normalizedJournal != lock / "journal.json" || !IsDirectoryWithoutLinks(normalizedParent))
        {
            return HubStatus::Failure(PublishError(HubErrorCode::InvalidArgument,
                                                   "The package recovery authority or operation identity is invalid.",
                                                   std::move(operationId)));
        }
        const auto lockStaging = normalizedParent / (".keire-publish-lock-" + operationId);
        if (IsMissing(lock) && IsDirectoryWithoutLinks(lockStaging))
        {
            std::error_code error;
            bool committedJournal = false;
            bool unexpectedEntry = false;
            for (std::filesystem::directory_iterator iterator(lockStaging, error), end; !error && iterator != end;
                 iterator.increment(error))
            {
                if (iterator->path().filename() == "journal.json" && IsRegularFileWithoutLinks(iterator->path()))
                    committedJournal = true;
                else if (!IsJournalTemporary(iterator->path()) || !IsRegularFileWithoutLinks(iterator->path()))
                    unexpectedEntry = true;
            }
            if (error)
            {
                return HubStatus::Failure(PublishError(HubErrorCode::IoRead,
                                                       "The interrupted package lock staging could not be inspected.",
                                                       operationId, error.message(), true));
            }
            if (committedJournal && !unexpectedEntry)
            {
                auto staged =
                    LoadPackagePublishJournalFromStorage(normalizedParent, lockStaging, lockStaging / "journal.json");
                if (!staged)
                    return HubStatus::Failure(staged.Error());
                if (staged.Value().OperationId != operationId)
                {
                    return HubStatus::Failure(PublishError(
                        HubErrorCode::InvalidTransition,
                        "The package lock staging belongs to a different recovery operation.", operationId));
                }
                if (const auto status = ValidateManifestBinding(staged.Value(), manifest); !status)
                    return status;
                if (const auto status = RemoveJournalTemporaries(lockStaging, operationId); !status)
                    return status;
                if (const auto status = RenameDirectory(lockStaging, lock, operationId); !status)
                    return status;
            }
            else
            {
                std::filesystem::remove_all(lockStaging, error);
                if (error)
                {
                    return HubStatus::Failure(PublishError(HubErrorCode::IoWrite,
                                                           "The interrupted package lock staging could not be removed.",
                                                           operationId, error.message(), true));
                }
                if (const auto status = SyncDirectory(normalizedParent, operationId); !status)
                    return status;
                return HubStatus::Failure(
                    PublishError(HubErrorCode::WorkerInterrupted,
                                 "The interrupted package publication discarded its incomplete lock staging.",
                                 operationId, {}, true));
            }
        }
        else if (!IsMissing(lockStaging) && !IsDirectoryWithoutLinks(lockStaging))
        {
            return HubStatus::Failure(PublishError(HubErrorCode::UnsafeInstallRoot,
                                                   "The package recovery lock staging is unsafe.", operationId));
        }
        if (journalPath.lexically_normal() == lock / "journal.json" && IsDirectoryWithoutLinks(lock) &&
            IsMissing(journalPath))
        {
            std::error_code error;
            const bool empty = std::filesystem::is_empty(lock, error);
            if (!error && empty && std::filesystem::remove(lock, error) && !error)
            {
                if (const auto status = SyncDirectory(normalizedParent, Detail::PathToUtf8(lock.filename())); !status)
                    return status;
                return HubStatus::Failure(
                    PublishError(HubErrorCode::WorkerInterrupted,
                                 "An interrupted package publication released its empty installation lock.",
                                 Detail::PathToUtf8(lock.filename()), {}, true));
            }
        }
        auto loaded = LoadPackagePublishJournal(allowedParent, journalPath);
        if (!loaded)
            return HubStatus::Failure(loaded.Error());
        auto journal = std::move(loaded).Value();
        if (journal.OperationId != operationId)
        {
            return HubStatus::Failure(PublishError(HubErrorCode::InvalidTransition,
                                                   "The package recovery operation does not own the active lock.",
                                                   operationId));
        }
        if (const auto status = ValidateManifestBinding(journal, manifest); !status)
            return status;
        if (const auto status = ValidateDestinationPolicy(journal.ReplacesExisting, options, journal.OperationId);
            !status)
        {
            return status;
        }
        if (const auto status = RemoveJournalTemporaries(journal.Paths.LockRoot, operationId); !status)
            return status;
        if (IsDirectoryWithoutLinks(lockStaging))
        {
            std::error_code error;
            std::filesystem::remove_all(lockStaging, error);
            if (error)
            {
                return HubStatus::Failure(PublishError(HubErrorCode::IoWrite,
                                                       "The obsolete package lock staging could not be removed.",
                                                       operationId, error.message(), true));
            }
            if (const auto status = SyncDirectory(normalizedParent, operationId); !status)
                return status;
        }
        const bool staging = IsDirectoryWithoutLinks(journal.Paths.StagingRoot);
        const bool destination = IsDirectoryWithoutLinks(journal.Paths.Destination);
        const bool backup = IsDirectoryWithoutLinks(journal.Paths.BackupRoot);
        const bool stagingMissing = IsMissing(journal.Paths.StagingRoot);
        const bool destinationMissing = IsMissing(journal.Paths.Destination);
        const bool backupMissing = IsMissing(journal.Paths.BackupRoot);
        if ((!staging && !stagingMissing) || (!destination && !destinationMissing) || (!backup && !backupMissing))
        {
            return HubStatus::Failure(PublishError(HubErrorCode::UnsafeInstallRoot,
                                                   "Package publication recovery found an unsafe filesystem object.",
                                                   journal.OperationId));
        }
        if (const auto status = AuthorizeMutation(journal, options); !status)
            return status;

        const auto interrupted = [&](const std::string_view message)
        {
            if (const auto status = RemoveJournal(journal); !status)
                return status;
            return HubStatus::Failure(
                PublishError(HubErrorCode::WorkerInterrupted, std::string(message), journal.OperationId, {}, true));
        };
        const auto restorePrevious = [&]()
        {
            if (const auto status =
                    RenameDirectory(journal.Paths.BackupRoot, journal.Paths.Destination, journal.OperationId);
                !status)
            {
                return status;
            }
            if (const auto status = RemoveJournal(journal); !status)
                return status;
            return HubStatus::Failure(
                PublishError(HubErrorCode::WorkerInterrupted,
                             "The interrupted package publication restored its previous installation.",
                             journal.OperationId, {}, true));
        };

        bool phaseChanged = false;
        switch (journal.Phase)
        {
        case PackagePublishPhase::Prepared:
            if (staging && journal.ReplacesExisting && destination && backupMissing)
            {
                // No filesystem mutation has occurred; continue from the persisted phase.
            }
            else if (staging && journal.ReplacesExisting && destinationMissing && backup)
            {
                journal.Phase = PackagePublishPhase::BackupMoved;
                phaseChanged = true;
            }
            else if (staging && !journal.ReplacesExisting && destinationMissing && backupMissing)
            {
                // A new install remains ready to publish.
            }
            else if (stagingMissing && journal.ReplacesExisting && destination && backupMissing)
            {
                return interrupted("The interrupted package publication left its previous installation unchanged.");
            }
            else if (stagingMissing && journal.ReplacesExisting && destinationMissing && backup)
            {
                return restorePrevious();
            }
            else if (stagingMissing && !journal.ReplacesExisting && destinationMissing && backupMissing)
            {
                return interrupted("The interrupted package publication lost its staging directory.");
            }
            else
            {
                return HubStatus::Failure(PublishError(
                    HubErrorCode::InvalidData, "Package publication recovery found an ambiguous prepared state.",
                    journal.OperationId));
            }
            break;
        case PackagePublishPhase::BackupMoved:
            if (staging && destinationMissing &&
                ((journal.ReplacesExisting && backup) || (!journal.ReplacesExisting && backupMissing)))
            {
                // The payload is ready for the final atomic rename.
            }
            else if (stagingMissing && destination &&
                     ((journal.ReplacesExisting && backup) || (!journal.ReplacesExisting && backupMissing)))
            {
                journal.Phase = PackagePublishPhase::Published;
                phaseChanged = true;
            }
            else if (staging && destination && journal.ReplacesExisting && backupMissing)
            {
                journal.Phase = PackagePublishPhase::Prepared;
                phaseChanged = true;
            }
            else if (stagingMissing && destination && journal.ReplacesExisting && backupMissing)
            {
                return interrupted(
                    "The interrupted package publication had already restored its previous installation.");
            }
            else if (stagingMissing && destinationMissing && journal.ReplacesExisting && backup)
            {
                return restorePrevious();
            }
            else if (stagingMissing && destinationMissing && !journal.ReplacesExisting && backupMissing)
            {
                return interrupted("The interrupted package publication lost its staging directory.");
            }
            else
            {
                return HubStatus::Failure(PublishError(HubErrorCode::InvalidData,
                                                       "Package publication recovery found an ambiguous backup state.",
                                                       journal.OperationId));
            }
            break;
        case PackagePublishPhase::Published:
            if (staging && destination && journal.ReplacesExisting && backupMissing)
            {
                journal.Phase = PackagePublishPhase::Prepared;
                phaseChanged = true;
                break;
            }
            if (!stagingMissing || !destination ||
                (journal.ReplacesExisting ? (!backup && !backupMissing) : !backupMissing))
            {
                return HubStatus::Failure(PublishError(
                    HubErrorCode::InvalidData, "Package publication recovery found an ambiguous published state.",
                    journal.OperationId));
            }
            break;
        }
        if (phaseChanged)
            if (const auto status = StoreJournal(journal); !status)
                return status;
        return ContinuePackagePublish(std::move(journal), manifest, options);
    }
} // namespace KeireHub
