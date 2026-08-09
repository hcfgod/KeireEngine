#include "KeireHubRuntime/ManagedEditorRemoval.h"

#include "KeireHubRuntime/PackageArchive.h"
#include "KeireHubRuntime/PackageReceipt.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/EditorInstallationManifest.h>
#include <KeireHubRuntimeInternal/PackageArchiveOutput.h>
#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumJournalBytes = std::size_t{64U} * 1024U;
        constexpr std::uint32_t JournalSchemaVersion = 1;

        struct RemovalPaths final
        {
            std::filesystem::path AllowedParent;
            std::filesystem::path Root;
            std::filesystem::path Tombstone;
            std::filesystem::path Journal;
        };

        struct RemovalJournal final
        {
            std::uint32_t SchemaVersion = JournalSchemaVersion;
            std::string OperationId;
            EditorManagedOperationPlan Plan;
            RemovalPaths Paths;
            ManagedEditorRemovalPhase Phase = ManagedEditorRemovalPhase::Prepared;
        };

        [[nodiscard]] HubError RemovalError(const HubErrorCode code, std::string message,
                                            const EditorManagedOperationPlan& plan, std::string details = {},
                                            const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = plan.InstallationId,
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

        [[nodiscard]] bool IsMissing(const std::filesystem::path& path) noexcept
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            return (!error && status.type() == std::filesystem::file_type::not_found) ||
                   error == std::errc::no_such_file_or_directory;
        }

        [[nodiscard]] bool IsReparsePoint(const std::filesystem::path& path) noexcept
        {
#if defined(_WIN32)
            const auto attributes = GetFileAttributesW(NativeIoPath(path).c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
            std::error_code error;
            return std::filesystem::is_symlink(std::filesystem::symlink_status(NativeIoPath(path), error)) && !error;
#endif
        }

        [[nodiscard]] bool HasUnsafeLinkAncestor(const std::filesystem::path& path) noexcept
        {
            auto current = path.lexically_normal();
            while (!current.empty())
            {
                std::error_code error;
                const auto status = std::filesystem::symlink_status(NativeIoPath(current), error);
                if ((!error && (std::filesystem::is_symlink(status) || IsReparsePoint(current))) ||
                    (error && error != std::errc::no_such_file_or_directory))
                {
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

        [[nodiscard]] bool IsSafeDirectory(const std::filesystem::path& path) noexcept
        {
            if (path.empty() || !path.is_absolute() || path == path.root_path() || HasUnsafeLinkAncestor(path))
                return false;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            return !error && status.type() == std::filesystem::file_type::directory && !IsReparsePoint(path);
        }

        [[nodiscard]] std::string PhaseText(const ManagedEditorRemovalPhase phase)
        {
            switch (phase)
            {
            case ManagedEditorRemovalPhase::Prepared:
                return "prepared";
            case ManagedEditorRemovalPhase::RootRenamed:
                return "rootRenamed";
            case ManagedEditorRemovalPhase::Purging:
                return "purging";
            case ManagedEditorRemovalPhase::RemovingAnchors:
                return "removingAnchors";
            }
            return {};
        }

        [[nodiscard]] std::optional<ManagedEditorRemovalPhase> ParsePhase(const std::string_view value) noexcept
        {
            if (value == "prepared")
                return ManagedEditorRemovalPhase::Prepared;
            if (value == "rootRenamed")
                return ManagedEditorRemovalPhase::RootRenamed;
            if (value == "purging")
                return ManagedEditorRemovalPhase::Purging;
            if (value == "removingAnchors")
                return ManagedEditorRemovalPhase::RemovingAnchors;
            return std::nullopt;
        }

        [[nodiscard]] bool SamePlan(const EditorManagedOperationPlan& left,
                                    const EditorManagedOperationPlan& right) noexcept
        {
            return left.Operation == right.Operation && left.InstallationId == right.InstallationId &&
                   left.Root.lexically_normal() == right.Root.lexically_normal() &&
                   left.ManifestFingerprint == right.ManifestFingerprint &&
                   left.PackageTreeIdentity == right.PackageTreeIdentity &&
                   left.PackageReceiptSha256 == right.PackageReceiptSha256 && left.MarkerNonce == right.MarkerNonce;
        }

        [[nodiscard]] bool SamePaths(const RemovalPaths& left, const RemovalPaths& right) noexcept
        {
            return left.AllowedParent.lexically_normal() == right.AllowedParent.lexically_normal() &&
                   left.Root.lexically_normal() == right.Root.lexically_normal() &&
                   left.Tombstone.lexically_normal() == right.Tombstone.lexically_normal() &&
                   left.Journal.lexically_normal() == right.Journal.lexically_normal();
        }

        [[nodiscard]] HubResult<RemovalPaths> PlanPaths(const EditorManagedOperationPlan& plan,
                                                        const std::string_view operationId)
        {
            const auto root = plan.Root.lexically_normal();
            const auto parent = root.parent_path();
            if (plan.Operation != EditorManagedOperation::Remove || !Detail::IsBoundedIdentifier(operationId) ||
                !Detail::IsBoundedIdentifier(plan.InstallationId) || root.empty() || !root.is_absolute() ||
                root == root.root_path() || parent.empty() || root.filename().empty() ||
                !Detail::IsSha256(plan.ManifestFingerprint) || !Detail::IsSha256(plan.PackageTreeIdentity) ||
                !Detail::IsSha256(plan.PackageReceiptSha256) || plan.MarkerNonce.size() < 32 ||
                plan.MarkerNonce.size() > 256 ||
                !std::ranges::all_of(plan.MarkerNonce, [](const unsigned char value) { return std::isxdigit(value); }))
            {
                return HubResult<RemovalPaths>::Failure(RemovalError(
                    HubErrorCode::InvalidArgument, "The managed editor removal authorization is invalid.", plan));
            }
            const auto keyText = Detail::NormalizedEditorPathKey(root);
            const auto key = Detail::Sha256Hex(std::as_bytes(std::span(keyText)));
            RemovalPaths result{.AllowedParent = parent,
                                .Root = root,
                                .Tombstone = parent / (".keire-remove-tombstone-" + std::string(operationId)),
                                .Journal = parent / (".keire-remove-" + key + ".json")};
            if (result.Tombstone.parent_path() != parent || result.Journal.parent_path() != parent ||
                result.Tombstone == root || result.Journal == root || HasUnsafeLinkAncestor(parent))
            {
                return HubResult<RemovalPaths>::Failure(
                    RemovalError(HubErrorCode::UnsafeInstallRoot,
                                 "The managed editor removal escaped its installation boundary.", plan));
            }
            return HubResult<RemovalPaths>::Success(std::move(result));
        }

        [[nodiscard]] Detail::Json EncodeJournal(const RemovalJournal& journal)
        {
            return {{"schemaVersion", journal.SchemaVersion},
                    {"operationId", journal.OperationId},
                    {"phase", PhaseText(journal.Phase)},
                    {"installationId", journal.Plan.InstallationId},
                    {"manifestFingerprint", journal.Plan.ManifestFingerprint},
                    {"packageTreeIdentity", journal.Plan.PackageTreeIdentity},
                    {"packageReceiptSha256", journal.Plan.PackageReceiptSha256},
                    {"markerNonce", journal.Plan.MarkerNonce},
                    {"paths",
                     {{"allowedParent", Detail::PathToUtf8(journal.Paths.AllowedParent)},
                      {"root", Detail::PathToUtf8(journal.Paths.Root)},
                      {"tombstone", Detail::PathToUtf8(journal.Paths.Tombstone)},
                      {"journal", Detail::PathToUtf8(journal.Paths.Journal)}}}};
        }

        [[nodiscard]] HubStatus StoreJournal(const RemovalJournal& journal, const bool replace)
        {
            const auto text = EncodeJournal(journal).dump(2) + '\n';
            if (replace)
                return Detail::WriteTextFileAtomically(journal.Paths.Journal, text);
            auto output = Detail::ExclusivePackageOutput::Create(journal.Paths.Journal, journal.OperationId);
            if (!output)
                return HubStatus::Failure(output.Error());
            if (const auto status = output.Value()->Write(std::span(text)); !status)
                return status;
            if (const auto status = output.Value()->Finish(); !status)
                return status;
            return output.Value()->Publish(journal.Paths.Journal);
        }

        [[nodiscard]] HubResult<RemovalJournal> LoadJournal(const std::filesystem::path& journalPath,
                                                            const EditorManagedOperationPlan& plan)
        {
            auto document = Detail::ReadJsonFile(journalPath, MaximumJournalBytes);
            if (!document)
                return HubResult<RemovalJournal>::Failure(document.Error());
            try
            {
                const auto phase = ParsePhase(document.Value().at("phase").get<std::string>());
                const auto& paths = document.Value().at("paths");
                RemovalJournal journal{
                    .SchemaVersion = document.Value().at("schemaVersion").get<std::uint32_t>(),
                    .OperationId = document.Value().at("operationId").get<std::string>(),
                    .Plan = {.Operation = EditorManagedOperation::Remove,
                             .InstallationId = document.Value().at("installationId").get<std::string>(),
                             .Root = Detail::PathFromUtf8(paths.at("root").get<std::string>()),
                             .ManifestFingerprint = document.Value().at("manifestFingerprint").get<std::string>(),
                             .PackageTreeIdentity = document.Value().at("packageTreeIdentity").get<std::string>(),
                             .PackageReceiptSha256 = document.Value().at("packageReceiptSha256").get<std::string>(),
                             .MarkerNonce = document.Value().at("markerNonce").get<std::string>()},
                    .Paths = {.AllowedParent = Detail::PathFromUtf8(paths.at("allowedParent").get<std::string>()),
                              .Root = Detail::PathFromUtf8(paths.at("root").get<std::string>()),
                              .Tombstone = Detail::PathFromUtf8(paths.at("tombstone").get<std::string>()),
                              .Journal = Detail::PathFromUtf8(paths.at("journal").get<std::string>())},
                    .Phase = phase.value_or(ManagedEditorRemovalPhase::Prepared)};
                auto expected = PlanPaths(plan, journal.OperationId);
                if (journal.SchemaVersion != JournalSchemaVersion || !phase || !expected ||
                    !SamePlan(journal.Plan, plan) || !SamePaths(journal.Paths, expected.Value()) ||
                    journal.Paths.Journal.lexically_normal() != journalPath.lexically_normal())
                {
                    throw std::invalid_argument("The removal journal does not match this authorization.");
                }
                return HubResult<RemovalJournal>::Success(std::move(journal));
            }
            catch (const std::exception& error)
            {
                return HubResult<RemovalJournal>::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot, "Another or invalid removal operation owns this editor location.",
                    plan, error.what(), true));
            }
        }

        [[nodiscard]] HubStatus PrepareForCommit(const ManagedEditorRemovalCallbacks& callbacks,
                                                 const EditorManagedOperationPlan& plan)
        {
            if (!callbacks.PrepareForCommit)
                return HubStatus::Success();
            try
            {
                callbacks.PrepareForCommit();
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(RemovalError(HubErrorCode::InstallationBusy,
                                                       "Background editor services could not be stopped before "
                                                       "uninstall. Close the editor and retry.",
                                                       plan, error.what(), true));
            }
            catch (...)
            {
                return HubStatus::Failure(RemovalError(HubErrorCode::InstallationBusy,
                                                       "Background editor services could not be stopped before "
                                                       "uninstall. Close the editor and retry.",
                                                       plan, {}, true));
            }
        }

        [[nodiscard]] HubStatus VerifyAnchorIdentity(const std::filesystem::path& root,
                                                     const EditorManagedOperationPlan& plan)
        {
            auto marker = EditorInstallationRegistry::ReadManagedMarker(root);
            if (!marker || marker.Value().InstallationId != plan.InstallationId ||
                marker.Value().ManifestFingerprint != plan.ManifestFingerprint ||
                marker.Value().Nonce != plan.MarkerNonce || marker.Value().ReceiptSha256 != plan.PackageReceiptSha256)
            {
                return HubStatus::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot,
                    "The managed editor marker changed. Verify or repair the installation before removing it.", plan,
                    marker ? "Marker fields do not match the authorized installation."
                           : marker.Error().TechnicalDetails));
            }
            auto receipt = ReadPackageInstallReceipt(root);
            if (!receipt || receipt.Value().DocumentSha256 != plan.PackageReceiptSha256 ||
                receipt.Value().AggregateIdentitySha256 != plan.PackageTreeIdentity)
            {
                return HubStatus::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot,
                    "The managed editor receipt changed. Verify or repair the installation before removing it.", plan,
                    receipt ? "Receipt identity does not match the authorized installation."
                            : receipt.Error().TechnicalDetails));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubResult<PackageFile> MetadataFile(const std::filesystem::path& root,
                                                          const std::filesystem::path& relative,
                                                          const EditorManagedOperationPlan& plan)
        {
            const auto path = root / relative;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            if (error || status.type() != std::filesystem::file_type::regular || IsReparsePoint(path))
            {
                return HubResult<PackageFile>::Failure(
                    RemovalError(HubErrorCode::UnsafeInstallRoot,
                                 "Managed editor ownership metadata is missing or unsafe. Repair the installation "
                                 "before removing it.",
                                 plan, error.message()));
            }
            const auto size = std::filesystem::file_size(NativeIoPath(path), error);
            if (error || size == 0 || size > PackageArchiveLimits::MaximumManifestBytes)
            {
                return HubResult<PackageFile>::Failure(RemovalError(HubErrorCode::UnsafeInstallRoot,
                                                                    "Managed editor ownership metadata has an invalid "
                                                                    "size. Repair the installation before removing it.",
                                                                    plan, error.message()));
            }
            auto digest = Detail::Sha256File(path, PackageArchiveLimits::MaximumManifestBytes);
            if (!digest)
                return HubResult<PackageFile>::Failure(digest.Error());
            return HubResult<PackageFile>::Success(
                {.Path = relative, .SizeBytes = size, .Sha256 = std::move(digest).Value(), .Mode = 0644U});
        }

        [[nodiscard]] HubStatus VerifyCompleteTree(const std::filesystem::path& root,
                                                   const EditorManagedOperationPlan& plan)
        {
            if (!IsSafeDirectory(root))
            {
                return HubStatus::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot,
                    "The managed editor root is missing or unsafe. Locate or repair it before removing it.", plan));
            }
            if (const auto anchors = VerifyAnchorIdentity(root, plan); !anchors)
                return anchors;
            auto editorManifest = Detail::ReadEditorPackageManifest(root);
            if (!editorManifest || editorManifest.Value().Fingerprint != plan.ManifestFingerprint)
            {
                return HubStatus::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot,
                    "The editor manifest changed. Verify or repair the installation before removing it.", plan,
                    editorManifest ? "Manifest fingerprint does not match the authorized installation."
                                   : editorManifest.Error().TechnicalDetails));
            }
            auto receipt = ReadPackageInstallReceipt(root);
            if (!receipt)
                return HubStatus::Failure(receipt.Error());

            std::vector<PackageFile> files;
            std::set<std::string, std::less<>> declared;
            for (const auto& package : receipt.Value().Packages)
            {
                for (const auto& file : package.Files)
                {
                    const auto key = Detail::NormalizedEditorPathKey(file.Path);
                    if (!declared.insert(key).second)
                    {
                        return HubStatus::Failure(RemovalError(HubErrorCode::UnsafeInstallRoot,
                                                               "The package receipt contains a colliding file "
                                                               "inventory. Repair the installation before removing it.",
                                                               plan, Detail::PathToUtf8(file.Path)));
                    }
                    files.push_back(file);
                }
            }
            for (const auto& relative : {std::filesystem::path(PackageInstallReceiptFileName),
                                         std::filesystem::path(EditorInstallationRegistry::MarkerFileName)})
            {
                auto file = MetadataFile(root, relative, plan);
                if (!file)
                    return HubStatus::Failure(file.Error());
                if (!declared.insert(Detail::NormalizedEditorPathKey(relative)).second)
                {
                    return HubStatus::Failure(RemovalError(HubErrorCode::UnsafeInstallRoot,
                                                           "The package receipt overlaps Hub ownership metadata. "
                                                           "Repair the installation before removing it.",
                                                           plan, Detail::PathToUtf8(relative)));
                }
                files.push_back(std::move(file).Value());
            }

            std::uint64_t installedSize = 0;
            for (const auto& file : files)
            {
                if (installedSize > std::numeric_limits<std::uint64_t>::max() - file.SizeBytes)
                {
                    return HubStatus::Failure(RemovalError(HubErrorCode::UnsafeInstallRoot,
                                                           "The managed editor inventory size is invalid.", plan));
                }
                installedSize += file.SizeBytes;
            }
            auto version = SemanticVersion::Parse("1.0.0");
            if (!version)
                return HubStatus::Failure(version.Error());
            PackageManifest manifest{.Id = "keire.managed-editor-removal",
                                     .Version = std::move(version).Value(),
                                     .Kind = PackageKind::Toolchain,
                                     .DisplayName = "Managed editor removal inventory",
                                     .Channel = "internal",
                                     .Platform = "any",
                                     .Architecture = "any",
                                     .ArtifactSizeBytes = installedSize,
                                     .ArtifactSha256 = plan.PackageTreeIdentity,
                                     .InstalledSizeBytes = installedSize,
                                     .Files = std::move(files),
                                     .SignatureKeyId = "keire.managed-editor-removal"};
            if (const auto verified = ValidatePackageTree(root, manifest); !verified)
            {
                return HubStatus::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot,
                    "The managed editor tree is damaged or contains undeclared files. Repair it before removing it.",
                    plan, verified.Error().Message + " " + verified.Error().TechnicalDetails));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus RenameNoReplace(const std::filesystem::path& source,
                                                const std::filesystem::path& destination,
                                                const EditorManagedOperationPlan& plan)
        {
            std::error_code error;
#if defined(_WIN32)
            if (!MoveFileExW(NativeIoPath(source).c_str(), NativeIoPath(destination).c_str(), MOVEFILE_WRITE_THROUGH))
                error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#elif defined(__linux__) && defined(SYS_renameat2)
            if (::syscall(SYS_renameat2, AT_FDCWD, NativeIoPath(source).c_str(), AT_FDCWD,
                          NativeIoPath(destination).c_str(), 1U) != 0)
            {
                error = std::error_code(errno, std::generic_category());
            }
#elif defined(__APPLE__)
            if (::renamex_np(NativeIoPath(source).c_str(), NativeIoPath(destination).c_str(), RENAME_EXCL) != 0)
                error = std::error_code(errno, std::generic_category());
#else
            error = std::make_error_code(std::errc::operation_not_supported);
#endif
            if (error)
            {
                const bool busy = error == std::errc::permission_denied || error == std::errc::device_or_resource_busy;
                return HubStatus::Failure(RemovalError(
                    error == std::errc::file_exists || error == std::errc::directory_not_empty || busy
                        ? HubErrorCode::InstallationBusy
                        : HubErrorCode::IoWrite,
                    busy ? "The editor installation is still in use. Close the editor and background build tools, "
                           "then retry uninstall."
                         : "The managed editor could not be moved into its removal tombstone.",
                    plan, error.message(), true));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemoveRegularFile(const std::filesystem::path& path,
                                                  const EditorManagedOperationPlan& plan)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            if (error || status.type() != std::filesystem::file_type::regular || IsReparsePoint(path) ||
                !std::filesystem::remove(NativeIoPath(path), error) || error)
            {
                return HubStatus::Failure(RemovalError(
                    HubErrorCode::IoWrite, "A managed editor file could not be removed.", plan, error.message(), true));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus PurgeEntry(const std::filesystem::path& path, const EditorManagedOperationPlan& plan)
        {
            if (HasUnsafeLinkAncestor(path) || IsReparsePoint(path))
            {
                return HubStatus::Failure(RemovalError(HubErrorCode::UnsafeInstallRoot,
                                                       "The removal tombstone contains a link or reparse point.", plan,
                                                       Detail::PathToUtf8(path.filename())));
            }
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            if (error)
                return HubStatus::Failure(RemovalError(HubErrorCode::IoRead,
                                                       "The removal tombstone could not be inspected.", plan,
                                                       error.message(), true));
            if (status.type() == std::filesystem::file_type::regular)
                return RemoveRegularFile(path, plan);
            if (status.type() != std::filesystem::file_type::directory)
            {
                return HubStatus::Failure(RemovalError(HubErrorCode::UnsafeInstallRoot,
                                                       "The removal tombstone contains an unsupported object.", plan,
                                                       Detail::PathToUtf8(path.filename())));
            }
            std::vector<std::filesystem::path> children;
            for (std::filesystem::directory_iterator iterator(NativeIoPath(path), error), end;
                 !error && iterator != end; iterator.increment(error))
            {
                // Windows directory iteration preserves the extended-length prefix passed to it. Keep that prefix at
                // the I/O boundary so lexical comparisons and ancestor checks continue to use the authorized path.
                children.push_back(path / iterator->path().filename());
            }
            if (error)
                return HubStatus::Failure(RemovalError(HubErrorCode::IoRead,
                                                       "The removal tombstone could not be enumerated.", plan,
                                                       error.message(), true));
            for (const auto& child : children)
            {
                if (const auto removed = PurgeEntry(child, plan); !removed)
                    return removed;
            }
            if (!std::filesystem::remove(NativeIoPath(path), error) || error)
                return HubStatus::Failure(RemovalError(HubErrorCode::IoWrite,
                                                       "An empty managed editor directory could not be removed.", plan,
                                                       error.message(), true));
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus PurgePayload(const RemovalPaths& paths, const EditorManagedOperationPlan& plan)
        {
            if (!IsSafeDirectory(paths.Tombstone))
            {
                return HubStatus::Failure(RemovalError(HubErrorCode::UnsafeInstallRoot,
                                                       "The managed editor removal tombstone is unsafe.", plan));
            }
            const std::array anchors{paths.Tombstone / PackageInstallReceiptFileName,
                                     paths.Tombstone / EditorInstallationRegistry::MarkerFileName};
            std::vector<std::filesystem::path> children;
            std::error_code error;
            for (std::filesystem::directory_iterator iterator(NativeIoPath(paths.Tombstone), error), end;
                 !error && iterator != end; iterator.increment(error))
            {
                const auto child = (paths.Tombstone / iterator->path().filename()).lexically_normal();
                if (std::ranges::find(anchors, child) == anchors.end())
                    children.push_back(child);
            }
            if (error)
                return HubStatus::Failure(RemovalError(HubErrorCode::IoRead,
                                                       "The managed editor tombstone could not be enumerated.", plan,
                                                       error.message(), true));
            for (const auto& child : children)
            {
                if (const auto removed = PurgeEntry(child, plan); !removed)
                    return removed;
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemoveAnchorsAndTombstone(const RemovalPaths& paths,
                                                          const EditorManagedOperationPlan& plan)
        {
            const auto receiptPath = (paths.Tombstone / PackageInstallReceiptFileName).lexically_normal();
            const auto markerPath = (paths.Tombstone / EditorInstallationRegistry::MarkerFileName).lexically_normal();
            if (!IsMissing(receiptPath))
            {
                if (const auto anchors = VerifyAnchorIdentity(paths.Tombstone, plan); !anchors)
                    return anchors;
            }
            else if (!IsMissing(markerPath))
            {
                auto marker = EditorInstallationRegistry::ReadManagedMarker(paths.Tombstone);
                if (!marker || marker.Value().InstallationId != plan.InstallationId ||
                    marker.Value().ManifestFingerprint != plan.ManifestFingerprint ||
                    marker.Value().Nonce != plan.MarkerNonce ||
                    marker.Value().ReceiptSha256 != plan.PackageReceiptSha256)
                {
                    return HubStatus::Failure(RemovalError(
                        HubErrorCode::UnsafeInstallRoot,
                        "The removal tombstone marker changed while recovery was removing its final anchors.", plan));
                }
            }
            std::error_code error;
            std::vector<std::filesystem::path> remaining;
            for (std::filesystem::directory_iterator iterator(NativeIoPath(paths.Tombstone), error), end;
                 !error && iterator != end; iterator.increment(error))
            {
                remaining.push_back((paths.Tombstone / iterator->path().filename()).lexically_normal());
            }
            const std::array expected{receiptPath, markerPath};
            if (error || std::ranges::any_of(remaining, [&](const auto& path)
                                             { return std::ranges::find(expected, path) == expected.end(); }))
            {
                return HubStatus::Failure(
                    RemovalError(HubErrorCode::UnsafeInstallRoot,
                                 "The removal tombstone changed before its ownership metadata could be removed.", plan,
                                 error.message(), true));
            }
            if (!IsMissing(receiptPath))
                if (const auto receipt = RemoveRegularFile(receiptPath, plan); !receipt)
                    return receipt;
            if (!IsMissing(markerPath))
                if (const auto marker = RemoveRegularFile(markerPath, plan); !marker)
                    return marker;
            if (!std::filesystem::remove(NativeIoPath(paths.Tombstone), error) || error)
                return HubStatus::Failure(RemovalError(HubErrorCode::IoWrite,
                                                       "The managed editor tombstone could not be removed.", plan,
                                                       error.message(), true));
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemoveJournal(const RemovalJournal& journal)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(journal.Paths.Journal), error);
            if (error || status.type() != std::filesystem::file_type::regular ||
                IsReparsePoint(journal.Paths.Journal) ||
                !std::filesystem::remove(NativeIoPath(journal.Paths.Journal), error) || error)
            {
                return HubStatus::Failure(RemovalError(HubErrorCode::IoWrite,
                                                       "The completed editor removal journal could not be cleared.",
                                                       journal.Plan, error.message(), true));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus Checkpoint(const RemovalJournal& journal,
                                           const ManagedEditorRemovalCallbacks& callbacks)
        {
            if (!callbacks.ContinueAfterPhase)
                return HubStatus::Success();
            try
            {
                if (callbacks.ContinueAfterPhase(journal.Phase))
                    return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(RemovalError(HubErrorCode::WorkerInterrupted,
                                                       "The editor removal was interrupted and can be resumed.",
                                                       journal.Plan, error.what(), true));
            }
            return HubStatus::Failure(RemovalError(HubErrorCode::WorkerInterrupted,
                                                   "The editor removal was interrupted and can be resumed.",
                                                   journal.Plan, PhaseText(journal.Phase), true));
        }

        [[nodiscard]] bool CancelledBeforeCommit(const ManagedEditorRemovalCallbacks& callbacks) noexcept
        {
            if (!callbacks.CancelBeforeCommit)
                return false;
            try
            {
                return callbacks.CancelBeforeCommit();
            }
            catch (...)
            {
                return true;
            }
        }
    } // namespace

    HubResult<ManagedEditorRemovalResult>
    RemoveManagedEditorInstallation(const EditorManagedOperationPlan& plan, std::string operationId,
                                    const ManagedEditorRemovalCallbacks& callbacks)
    {
        auto planned = PlanPaths(plan, operationId);
        if (!planned)
            return HubResult<ManagedEditorRemovalResult>::Failure(planned.Error());
        auto paths = std::move(planned).Value();
        if (!IsSafeDirectory(paths.AllowedParent))
        {
            return HubResult<ManagedEditorRemovalResult>::Failure(
                RemovalError(HubErrorCode::UnsafeInstallRoot,
                             "The managed editor parent directory is unavailable or unsafe.", plan));
        }

        std::optional<RemovalJournal> journal;
        if (!IsMissing(paths.Journal))
        {
            if (HasUnsafeLinkAncestor(paths.Journal))
            {
                return HubResult<ManagedEditorRemovalResult>::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot, "The managed editor removal journal is unsafe.", plan));
            }
            auto loaded = LoadJournal(paths.Journal, plan);
            if (!loaded)
                return HubResult<ManagedEditorRemovalResult>::Failure(loaded.Error());
            journal = std::move(loaded).Value();
            paths = journal->Paths;
        }
        else if (IsMissing(paths.Root))
        {
            if (!IsMissing(paths.Tombstone))
            {
                return HubResult<ManagedEditorRemovalResult>::Failure(
                    RemovalError(HubErrorCode::UnsafeInstallRoot,
                                 "An unowned editor removal tombstone blocks this location.", plan));
            }
            return HubResult<ManagedEditorRemovalResult>::Success({.Completed = true});
        }
        else
        {
            if (!IsMissing(paths.Tombstone))
            {
                return HubResult<ManagedEditorRemovalResult>::Failure(RemovalError(
                    HubErrorCode::InstallationBusy, "Another editor removal owns this location.", plan, {}, true));
            }
            if (const auto verified = VerifyCompleteTree(paths.Root, plan); !verified)
                return HubResult<ManagedEditorRemovalResult>::Failure(verified.Error());
            journal = RemovalJournal{.OperationId = std::move(operationId), .Plan = plan, .Paths = paths};
            if (const auto stored = StoreJournal(*journal, false); !stored)
            {
                if (!IsMissing(paths.Journal))
                {
                    auto existing = LoadJournal(paths.Journal, plan);
                    if (existing)
                    {
                        journal = std::move(existing).Value();
                        paths = journal->Paths;
                    }
                    else
                        return HubResult<ManagedEditorRemovalResult>::Failure(existing.Error());
                }
                else
                    return HubResult<ManagedEditorRemovalResult>::Failure(stored.Error());
            }
            if (const auto checkpoint = Checkpoint(*journal, callbacks); !checkpoint)
                return HubResult<ManagedEditorRemovalResult>::Failure(checkpoint.Error());
        }

        if (journal->Phase == ManagedEditorRemovalPhase::Prepared)
        {
            if (CancelledBeforeCommit(callbacks))
            {
                if (const auto removed = RemoveJournal(*journal); !removed)
                    return HubResult<ManagedEditorRemovalResult>::Failure(removed.Error());
                return HubResult<ManagedEditorRemovalResult>::Success({.CancelledBeforeCommit = true});
            }
            if (IsMissing(paths.Root) && IsMissing(paths.Tombstone))
            {
                if (const auto removed = RemoveJournal(*journal); !removed)
                    return HubResult<ManagedEditorRemovalResult>::Failure(removed.Error());
                return HubResult<ManagedEditorRemovalResult>::Success({.Completed = true});
            }
            if (!IsMissing(paths.Root) && !IsMissing(paths.Tombstone))
            {
                return HubResult<ManagedEditorRemovalResult>::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot, "Both the editor root and its removal tombstone exist.", plan));
            }
            if (!IsMissing(paths.Root))
            {
                // Verification is repeated after the preparation callback so the commit still relies on a fresh,
                // complete tree proof even when preparation had to stop processes owned by this installation.
                if (const auto verified = VerifyCompleteTree(paths.Root, plan); !verified)
                    return HubResult<ManagedEditorRemovalResult>::Failure(verified.Error());
                if (const auto prepared = PrepareForCommit(callbacks, plan); !prepared)
                    return HubResult<ManagedEditorRemovalResult>::Failure(prepared.Error());
                if (const auto verified = VerifyCompleteTree(paths.Root, plan); !verified)
                    return HubResult<ManagedEditorRemovalResult>::Failure(verified.Error());
                if (const auto renamed = RenameNoReplace(paths.Root, paths.Tombstone, plan); !renamed)
                    return HubResult<ManagedEditorRemovalResult>::Failure(renamed.Error());
            }
            if (const auto verified = VerifyCompleteTree(paths.Tombstone, plan); !verified)
                return HubResult<ManagedEditorRemovalResult>::Failure(verified.Error());
            journal->Phase = ManagedEditorRemovalPhase::RootRenamed;
            if (const auto stored = StoreJournal(*journal, true); !stored)
                return HubResult<ManagedEditorRemovalResult>::Failure(stored.Error());
            if (const auto checkpoint = Checkpoint(*journal, callbacks); !checkpoint)
                return HubResult<ManagedEditorRemovalResult>::Failure(checkpoint.Error());
        }

        if (journal->Phase == ManagedEditorRemovalPhase::RootRenamed)
        {
            if (!IsMissing(paths.Root) || !IsSafeDirectory(paths.Tombstone))
            {
                return HubResult<ManagedEditorRemovalResult>::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot, "The editor removal commit boundary is inconsistent.", plan));
            }
            if (const auto verified = VerifyCompleteTree(paths.Tombstone, plan); !verified)
                return HubResult<ManagedEditorRemovalResult>::Failure(verified.Error());
            journal->Phase = ManagedEditorRemovalPhase::Purging;
            if (const auto stored = StoreJournal(*journal, true); !stored)
                return HubResult<ManagedEditorRemovalResult>::Failure(stored.Error());
            if (const auto checkpoint = Checkpoint(*journal, callbacks); !checkpoint)
                return HubResult<ManagedEditorRemovalResult>::Failure(checkpoint.Error());
        }

        if (journal->Phase == ManagedEditorRemovalPhase::Purging)
        {
            if (!IsMissing(paths.Root) || !IsSafeDirectory(paths.Tombstone))
            {
                return HubResult<ManagedEditorRemovalResult>::Failure(RemovalError(
                    HubErrorCode::UnsafeInstallRoot, "The editor removal tombstone is unavailable or unsafe.", plan));
            }
            if (const auto anchors = VerifyAnchorIdentity(paths.Tombstone, plan); !anchors)
                return HubResult<ManagedEditorRemovalResult>::Failure(anchors.Error());
            if (const auto purged = PurgePayload(paths, plan); !purged)
                return HubResult<ManagedEditorRemovalResult>::Failure(purged.Error());
            journal->Phase = ManagedEditorRemovalPhase::RemovingAnchors;
            if (const auto stored = StoreJournal(*journal, true); !stored)
                return HubResult<ManagedEditorRemovalResult>::Failure(stored.Error());
            if (const auto checkpoint = Checkpoint(*journal, callbacks); !checkpoint)
                return HubResult<ManagedEditorRemovalResult>::Failure(checkpoint.Error());
        }

        if (!IsMissing(paths.Tombstone))
        {
            if (!IsSafeDirectory(paths.Tombstone))
            {
                return HubResult<ManagedEditorRemovalResult>::Failure(
                    RemovalError(HubErrorCode::UnsafeInstallRoot, "The editor removal tombstone is unsafe.", plan));
            }
            if (const auto removed = RemoveAnchorsAndTombstone(paths, plan); !removed)
                return HubResult<ManagedEditorRemovalResult>::Failure(removed.Error());
        }
        if (!IsMissing(paths.Root) || !IsMissing(paths.Tombstone))
        {
            return HubResult<ManagedEditorRemovalResult>::Failure(
                RemovalError(HubErrorCode::WorkerInterrupted, "The managed editor removal did not reach a clean state.",
                             plan, {}, true));
        }
        if (const auto removed = RemoveJournal(*journal); !removed)
            return HubResult<ManagedEditorRemovalResult>::Failure(removed.Error());
        return HubResult<ManagedEditorRemovalResult>::Success({.Completed = true});
    }
} // namespace KeireHub
