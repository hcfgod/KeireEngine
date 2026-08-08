#include "KeireHubRuntime/EditorInstallationManager.h"

#include <KeireHubRuntimeInternal/EditorInstallationManifest.h>
#include <KeireHubRuntimeInternal/ExecutableProcessProbe.h>
#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <ranges>
#include <set>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace KeireHub
{
    EditorEntrypointActivity ProbeEditorEntrypointProcessActivity(const std::filesystem::path& executable) noexcept
    {
        return Detail::ProbeEditorEntrypointActivity(executable);
    }

    namespace
    {
        [[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            return Detail::NormalizedEditorPathKey(left) == Detail::NormalizedEditorPathKey(right);
        }

        [[nodiscard]] bool SameText(const std::string_view left, const std::string_view right)
        {
            if (left.size() != right.size())
                return false;
            return std::ranges::equal(left, right, [](const unsigned char a, const unsigned char b)
                                      { return std::tolower(a) == std::tolower(b); });
        }

        [[nodiscard]] std::string LowerText(std::string value)
        {
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

        [[nodiscard]] bool IsUnsafeLinkLike(const std::filesystem::path& path)
        {
#if defined(_WIN32)
            const auto attributes = GetFileAttributesW(NativeIoPath(path).c_str());
            return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            return error || std::filesystem::is_symlink(status);
#endif
        }

        [[nodiscard]] bool IsConfinedRegularFile(const std::filesystem::path& root,
                                                 const std::filesystem::path& relative)
        {
            auto current = root;
            if (IsUnsafeLinkLike(current))
                return false;
            for (const auto& component : relative)
            {
                current /= component;
                if (IsUnsafeLinkLike(current))
                    return false;
            }
            std::error_code error;
            return std::filesystem::is_regular_file(current, error) && !error;
        }

        void AddIssue(EditorInstallationHealthSnapshot& snapshot, const EditorInstallationIssueCode code,
                      std::filesystem::path relativePath, std::string message)
        {
            snapshot.Issues.push_back(
                {.Code = code, .RelativePath = std::move(relativePath), .Message = std::move(message)});
        }

        [[nodiscard]] std::set<std::string, std::less<>> PathSet(const std::vector<std::filesystem::path>& paths)
        {
            std::set<std::string, std::less<>> result;
            for (const auto& path : paths)
                result.insert(Detail::NormalizedEditorPathKey(path));
            return result;
        }

        [[nodiscard]] bool IsFileDamage(const EditorInstallationIssueCode code) noexcept
        {
            return code == EditorInstallationIssueCode::MissingFile ||
                   code == EditorInstallationIssueCode::UnsafeFile ||
                   code == EditorInstallationIssueCode::FileSizeMismatch ||
                   code == EditorInstallationIssueCode::FileDigestMismatch ||
                   code == EditorInstallationIssueCode::FileModeMismatch ||
                   code == EditorInstallationIssueCode::MissingEntrypoint;
        }

        [[nodiscard]] bool RequiresCompletePackage(const EditorInstallationIssueCode code) noexcept
        {
            return code == EditorInstallationIssueCode::ManifestMissing ||
                   code == EditorInstallationIssueCode::ManifestInvalid ||
                   code == EditorInstallationIssueCode::InventoryInvalid ||
                   code == EditorInstallationIssueCode::ManifestFingerprintMismatch ||
                   code == EditorInstallationIssueCode::ReceiptMissing ||
                   code == EditorInstallationIssueCode::ReceiptInvalid ||
                   code == EditorInstallationIssueCode::ReceiptMismatch ||
                   code == EditorInstallationIssueCode::RegistrationMismatch;
        }

        [[nodiscard]] EditorInstallationHealthSnapshot
        InspectInstallation(const EditorInstallation& installation, const EditorInstallationManagerSpecification& spec,
                            const EditorInstallationActivity activity)
        {
            EditorInstallationHealthSnapshot snapshot{.Installation = installation, .Activity = activity};
            std::error_code error;
            const auto rootStatus = std::filesystem::symlink_status(installation.Root, error);
            if (error || !std::filesystem::is_directory(rootStatus))
            {
                AddIssue(snapshot, EditorInstallationIssueCode::RootMissing, {},
                         "The editor installation folder is missing.");
                snapshot.Health = InstallationHealth::Missing;
                snapshot.Installation.Health = snapshot.Health;
                return snapshot;
            }
            if (std::filesystem::is_symlink(rootStatus) || IsUnsafeLinkLike(installation.Root))
            {
                AddIssue(snapshot, EditorInstallationIssueCode::UnsafeFile, {},
                         "The editor installation root is a symbolic link.");
                snapshot.Health = InstallationHealth::Damaged;
                snapshot.Installation.Health = snapshot.Health;
                return snapshot;
            }

            const auto manifestPath = installation.Root / "editor-package.json";
            if (!std::filesystem::exists(manifestPath))
            {
                AddIssue(snapshot, EditorInstallationIssueCode::ManifestMissing, "editor-package.json",
                         "The editor package manifest is missing.");
                if (installation.Ownership == InstallationOwnership::Managed &&
                    !EditorInstallationRegistry::ReadManagedMarker(installation.Root))
                {
                    AddIssue(snapshot, EditorInstallationIssueCode::MarkerMismatch,
                             EditorInstallationRegistry::MarkerFileName,
                             "The managed-install marker does not match this registration.");
                }
                snapshot.Health = InstallationHealth::Damaged;
                snapshot.Installation.Health = snapshot.Health;
                return snapshot;
            }

            auto manifest = Detail::ReadEditorPackageManifest(installation.Root);
            if (!manifest)
            {
                auto code = EditorInstallationIssueCode::ManifestInvalid;
                if (manifest.Error().Code == HubErrorCode::EditorInventoryInvalid)
                    code = EditorInstallationIssueCode::InventoryInvalid;
                else if (manifest.Error().AffectedItem == "manifestFingerprint")
                    code = EditorInstallationIssueCode::ManifestFingerprintMismatch;
                AddIssue(snapshot, code, manifest.Error().AffectedItem, manifest.Error().Message);
                if (installation.Ownership == InstallationOwnership::Managed)
                {
                    const auto marker = EditorInstallationRegistry::ReadManagedMarker(installation.Root);
                    if (!marker || marker.Value().InstallationId != installation.Id ||
                        marker.Value().ManifestFingerprint != installation.ManifestFingerprint ||
                        marker.Value().Nonce != installation.MarkerNonce ||
                        marker.Value().ReceiptSha256 != installation.PackageReceiptSha256)
                    {
                        AddIssue(snapshot, EditorInstallationIssueCode::MarkerMismatch,
                                 EditorInstallationRegistry::MarkerFileName,
                                 "The managed-install marker does not match this registration.");
                    }
                }
                snapshot.Health = InstallationHealth::Damaged;
                snapshot.Installation.Health = snapshot.Health;
                return snapshot;
            }

            const auto& package = manifest.Value();
            if (package.Fingerprint != installation.ManifestFingerprint || package.Version != installation.Version ||
                !SameText(package.Channel, installation.Channel) || package.Platform != installation.Platform ||
                package.Architecture != installation.Architecture ||
                package.MinimumProjectSchema != installation.MinimumProjectSchema ||
                package.MaximumProjectSchema != installation.MaximumProjectSchema ||
                (!installation.InstalledPackages.empty() &&
                 (installation.InstalledPackages.front().Id != package.PackageId ||
                  installation.InstalledPackages.front().Version.ToString() != package.Version)) ||
                (installation.InstalledPackages.empty() && installation.InstalledSizeBytes != 0 &&
                 package.InstalledSizeBytes != installation.InstalledSizeBytes) ||
                PathSet(package.Entrypoints) != PathSet(installation.Entrypoints) ||
                Detail::NormalizedEditorPathKey(package.EditorEntrypoint) !=
                    Detail::NormalizedEditorPathKey(ResolveEditorEntrypoint(installation)) ||
                Detail::NormalizedEditorPathKey(package.AssetToolEntrypoint) !=
                    Detail::NormalizedEditorPathKey(ResolveAssetToolEntrypoint(installation)))
            {
                AddIssue(snapshot, EditorInstallationIssueCode::RegistrationMismatch, "editor-package.json",
                         "The package manifest does not match its Hub registration.");
            }
            bool receiptVerified = false;
            if (!installation.PackageReceiptSha256.empty())
            {
                auto receipt = ReadPackageInstallReceipt(installation.Root);
                if (!receipt)
                {
                    AddIssue(snapshot,
                             receipt.Error().Code == HubErrorCode::NotFound
                                 ? EditorInstallationIssueCode::ReceiptMissing
                                 : EditorInstallationIssueCode::ReceiptInvalid,
                             PackageInstallReceiptFileName, receipt.Error().Message);
                }
                else
                {
                    PackageInstallReceipt registered{.DocumentSha256 = installation.PackageReceiptSha256,
                                                     .AggregateIdentitySha256 = installation.PackageTreeIdentity,
                                                     .AggregateInstalledSizeBytes = installation.InstalledSizeBytes,
                                                     .Packages = installation.InstalledPackages};
                    auto encodedReceipt = EncodePackageInstallReceipt(receipt.Value());
                    auto encodedRegistered = EncodePackageInstallReceipt(registered);
                    if (receipt.Value().DocumentSha256 != installation.PackageReceiptSha256 || !encodedReceipt ||
                        !encodedRegistered || encodedReceipt.Value() != encodedRegistered.Value())
                    {
                        AddIssue(snapshot, EditorInstallationIssueCode::ReceiptMismatch, PackageInstallReceiptFileName,
                                 "The installed-package receipt does not match this Hub registration.");
                    }
                    else
                    {
                        receiptVerified = true;
                        snapshot.Installation.PackageTreeIdentity = receipt.Value().AggregateIdentitySha256;
                        snapshot.Installation.PackageReceiptSha256 = receipt.Value().DocumentSha256;
                        snapshot.Installation.InstalledPackages = receipt.Value().Packages;
                        snapshot.Installation.InstalledSizeBytes = receipt.Value().AggregateInstalledSizeBytes;
                    }
                }
            }
            if (package.Platform != Detail::CanonicalEditorPlatform(spec.HostPlatform) ||
                package.Architecture != Detail::CanonicalEditorArchitecture(spec.HostArchitecture))
            {
                AddIssue(snapshot, EditorInstallationIssueCode::HostIncompatible, {},
                         "This editor package is not compatible with the current host.");
            }
            if (installation.Ownership == InstallationOwnership::Managed)
            {
                const auto marker = EditorInstallationRegistry::ReadManagedMarker(installation.Root);
                if (!marker || marker.Value().InstallationId != installation.Id ||
                    marker.Value().ManifestFingerprint != installation.ManifestFingerprint ||
                    marker.Value().Nonce != installation.MarkerNonce ||
                    marker.Value().ReceiptSha256 != installation.PackageReceiptSha256)
                {
                    AddIssue(snapshot, EditorInstallationIssueCode::MarkerMismatch,
                             EditorInstallationRegistry::MarkerFileName,
                             "The managed-install marker does not match this registration.");
                }
            }

            std::set<std::string, std::less<>> damagedPaths;
            const auto verifyFile = [&](const std::filesystem::path& relative, const std::uint64_t expectedSize,
                                        const std::string& expectedDigest,
                                        const std::optional<std::uint32_t> expectedMode)
            {
                const auto key = Detail::NormalizedEditorPathKey(relative);
                const auto path = installation.Root / relative;
                std::error_code fileError;
                const auto status = std::filesystem::symlink_status(path, fileError);
                if (fileError || status.type() == std::filesystem::file_type::not_found)
                {
                    AddIssue(snapshot, EditorInstallationIssueCode::MissingFile, relative,
                             "A declared installation file is missing.");
                    damagedPaths.insert(key);
                    return;
                }
                if (!IsConfinedRegularFile(installation.Root, relative))
                {
                    AddIssue(snapshot, EditorInstallationIssueCode::UnsafeFile, relative,
                             "A declared installation file is not a confined regular file.");
                    damagedPaths.insert(key);
                    return;
                }
                const auto size = std::filesystem::file_size(path, fileError);
                if (fileError || size != expectedSize)
                {
                    AddIssue(snapshot, EditorInstallationIssueCode::FileSizeMismatch, relative,
                             "A declared installation file has the wrong size.");
                    damagedPaths.insert(key);
                    return;
                }
                auto digest = Detail::Sha256File(path, expectedSize);
                if (!digest || digest.Value() != expectedDigest)
                {
                    AddIssue(snapshot, EditorInstallationIssueCode::FileDigestMismatch, relative,
                             "A declared installation file failed integrity verification.");
                    damagedPaths.insert(key);
                    return;
                }
#if !defined(_WIN32)
                if (expectedMode)
                {
                    const auto expectedPermissions =
                        *expectedMode == 0755U
                            ? std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                  std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                  std::filesystem::perms::others_exec
                            : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                  std::filesystem::perms::group_read | std::filesystem::perms::others_read;
                    if ((status.permissions() & std::filesystem::perms::mask) != expectedPermissions)
                    {
                        AddIssue(snapshot, EditorInstallationIssueCode::FileModeMismatch, relative,
                                 "A declared installation file has unsafe permissions.");
                        damagedPaths.insert(key);
                        return;
                    }
                }
#else
                static_cast<void>(expectedMode);
#endif
                ++snapshot.VerifiedFileCount;
                snapshot.VerifiedBytes += expectedSize;
            };
            if (receiptVerified)
            {
                for (const auto& installedPackage : snapshot.Installation.InstalledPackages)
                {
                    for (const auto& file : installedPackage.Files)
                        verifyFile(file.Path, file.SizeBytes, file.Sha256, file.Mode);
                }
            }
            else
            {
                for (const auto& file : package.Files)
                    verifyFile(file.Path, file.SizeBytes, file.Sha256, std::nullopt);
            }
            for (const auto& entrypoint : package.Entrypoints)
            {
                if (damagedPaths.contains(Detail::NormalizedEditorPathKey(entrypoint)) ||
                    !IsConfinedRegularFile(installation.Root, entrypoint))
                {
                    AddIssue(snapshot, EditorInstallationIssueCode::MissingEntrypoint, entrypoint,
                             "An editor entrypoint is missing or damaged.");
                }
            }

            const bool hasDamage =
                std::ranges::any_of(snapshot.Issues, [](const auto& issue)
                                    { return issue.Code != EditorInstallationIssueCode::HostIncompatible; });
            const bool incompatible =
                std::ranges::any_of(snapshot.Issues, [](const auto& issue)
                                    { return issue.Code == EditorInstallationIssueCode::HostIncompatible; });
            snapshot.Health = hasDamage      ? InstallationHealth::Damaged
                              : incompatible ? InstallationHealth::VerificationRequired
                                             : InstallationHealth::Healthy;
            snapshot.Installation.Health = snapshot.Health;
            return snapshot;
        }

        [[nodiscard]] HubStatus ValidateInspectionHost(const EditorInstallationManagerSpecification& specification)
        {
            const auto platform = Detail::CanonicalEditorPlatform(specification.HostPlatform);
            const auto architecture = Detail::CanonicalEditorArchitecture(specification.HostArchitecture);
            if ((platform == "windows" || platform == "linux" || platform == "macos") &&
                (architecture == "x86_64" || architecture == "arm64"))
            {
                return HubStatus::Success();
            }
            return HubStatus::Failure(
                {.Code = HubErrorCode::InvalidArgument,
                 .Message = "The editor-installation host identity is invalid.",
                 .AffectedItem = specification.HostPlatform + "/" + specification.HostArchitecture});
        }

        [[nodiscard]] EditorInstallationActivity
        ProbeSnapshotActivity(const EditorInstallation& installation,
                              const EditorInstallationManagerSpecification& specification)
        {
            EditorInstallationActivity activity;
            try
            {
                if (specification.ProbeActivity)
                    activity = specification.ProbeActivity(installation);
            }
            catch (...)
            {
                return {.Running = true, .HasActiveTask = true};
            }

            const auto relativeEntrypoint = ResolveEditorEntrypoint(installation);
            EditorEntrypointActivity entrypointActivity = EditorEntrypointActivity::Indeterminate;
            if (!relativeEntrypoint.empty() && Detail::IsSafeRelativePath(relativeEntrypoint) &&
                installation.Root.is_absolute())
            {
                try
                {
                    const auto executable = (installation.Root / relativeEntrypoint).lexically_normal();
                    entrypointActivity = specification.ProbeEntrypointActivity
                                             ? specification.ProbeEntrypointActivity(executable)
                                             : Detail::ProbeEditorEntrypointActivity(executable);
                }
                catch (...)
                {
                    entrypointActivity = EditorEntrypointActivity::Indeterminate;
                }
            }
            activity.Running = activity.Running || entrypointActivity != EditorEntrypointActivity::NotRunning;
            return activity;
        }

        [[nodiscard]] HubStatus GuardSnapshotInactive(const EditorInstallation& installation,
                                                      const EditorInstallationActivity activity)
        {
            if (activity.Running)
                return HubStatus::Failure({.Code = HubErrorCode::EditorRunning,
                                           .Message = "Close the editor before changing this installation.",
                                           .Retryable = true,
                                           .AffectedItem = installation.Id});
            if (activity.HasActiveTask)
                return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                           .Message = "Wait for the active installation task to finish.",
                                           .Retryable = true,
                                           .AffectedItem = installation.Id});
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidateManagedSnapshotMarker(const EditorInstallation& installation,
                                                              const std::filesystem::path& expectedRoot)
        {
            if (installation.Ownership != InstallationOwnership::Managed || !SamePath(installation.Root, expectedRoot))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The editor location is not a verified managed installation.",
                                           .AffectedItem = installation.Id});
            }
            auto marker = EditorInstallationRegistry::ReadManagedMarker(expectedRoot);
            if (!marker || marker.Value().InstallationId != installation.Id ||
                marker.Value().ManifestFingerprint != installation.ManifestFingerprint ||
                marker.Value().Nonce != installation.MarkerNonce ||
                marker.Value().ReceiptSha256 != installation.PackageReceiptSha256)
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The managed-install marker does not match this editor.",
                                           .AffectedItem = installation.Id,
                                           .TechnicalDetails = marker ? "Marker fields do not match the registration."
                                                                      : marker.Error().TechnicalDetails});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] bool SameManagedPlan(const EditorManagedOperationPlan& left,
                                           const EditorManagedOperationPlan& right)
        {
            return left.Operation == right.Operation && left.InstallationId == right.InstallationId &&
                   SamePath(left.Root, right.Root) && left.ManifestFingerprint == right.ManifestFingerprint &&
                   left.PackageTreeIdentity == right.PackageTreeIdentity &&
                   left.PackageReceiptSha256 == right.PackageReceiptSha256 && left.MarkerNonce == right.MarkerNonce &&
                   left.EditorEntrypoint == right.EditorEntrypoint && left.CurrentHealth == right.CurrentHealth &&
                   left.RequiresCompletePackage == right.RequiresCompletePackage &&
                   left.FilesToRestore == right.FilesToRestore;
        }
    } // namespace

    HubResult<EditorInstallationHealthSnapshot>
    InspectEditorInstallationSnapshot(const EditorInstallation& installation,
                                      const EditorInstallationManagerSpecification& specification)
    {
        if (const auto host = ValidateInspectionHost(specification); !host)
            return HubResult<EditorInstallationHealthSnapshot>::Failure(host.Error());
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            InspectInstallation(installation, specification, ProbeSnapshotActivity(installation, specification)));
    }

    HubResult<EditorInstallationHealthSnapshot>
    VerifyEditorInstallationSnapshot(const EditorInstallation& installation,
                                     const EditorInstallationManagerSpecification& specification)
    {
        if (const auto host = ValidateInspectionHost(specification); !host)
            return HubResult<EditorInstallationHealthSnapshot>::Failure(host.Error());
        const auto activity = ProbeSnapshotActivity(installation, specification);
        if (const auto inactive = GuardSnapshotInactive(installation, activity); !inactive)
            return HubResult<EditorInstallationHealthSnapshot>::Failure(inactive.Error());
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            InspectInstallation(installation, specification, activity));
    }

    HubResult<EditorManagedOperationPlan> PrepareManagedEditorOperationSnapshot(
        const EditorInstallation& installation, const std::filesystem::path& expectedRoot,
        const EditorManagedOperation operation, const EditorInstallationManagerSpecification& specification)
    {
        if (operation < EditorManagedOperation::Repair || operation > EditorManagedOperation::Remove)
        {
            return HubResult<EditorManagedOperationPlan>::Failure({.Code = HubErrorCode::InvalidArgument,
                                                                   .Message = "The editor operation is invalid.",
                                                                   .AffectedItem = installation.Id});
        }
        if (const auto host = ValidateInspectionHost(specification); !host)
            return HubResult<EditorManagedOperationPlan>::Failure(host.Error());
        if (const auto marker = ValidateManagedSnapshotMarker(installation, expectedRoot); !marker)
            return HubResult<EditorManagedOperationPlan>::Failure(marker.Error());
        if (const auto inactive =
                GuardSnapshotInactive(installation, ProbeSnapshotActivity(installation, specification));
            !inactive)
        {
            return HubResult<EditorManagedOperationPlan>::Failure(inactive.Error());
        }

        auto inspection =
            InspectInstallation(installation, specification, ProbeSnapshotActivity(installation, specification));
        if (const auto inactive = GuardSnapshotInactive(installation, inspection.Activity); !inactive)
            return HubResult<EditorManagedOperationPlan>::Failure(inactive.Error());
        if (operation == EditorManagedOperation::Remove && inspection.Health != InstallationHealth::Healthy)
        {
            return HubResult<EditorManagedOperationPlan>::Failure(
                {.Code = HubErrorCode::UnsafeInstallRoot,
                 .Message = "Verify or repair this managed editor before removing its files.",
                 .AffectedItem = installation.Id,
                 .TechnicalDetails = "Removal requires a complete marker-, receipt-, and inventory-verified tree."});
        }
        if (operation == EditorManagedOperation::Remove &&
            (installation.PackageTreeIdentity.empty() || installation.PackageReceiptSha256.empty() ||
             installation.InstalledPackages.empty()))
        {
            return HubResult<EditorManagedOperationPlan>::Failure(
                {.Code = HubErrorCode::UnsafeInstallRoot,
                 .Message = "This legacy managed editor cannot be uninstalled safely. Reinstall or repair it first.",
                 .AffectedItem = installation.Id,
                 .TechnicalDetails = "Removal requires a receipt-bound complete package inventory."});
        }
        if (operation == EditorManagedOperation::Repair &&
            std::ranges::any_of(inspection.Issues, [](const auto& issue)
                                { return issue.Code == EditorInstallationIssueCode::HostIncompatible; }))
        {
            return HubResult<EditorManagedOperationPlan>::Failure(
                {.Code = HubErrorCode::PackageHostIncompatible,
                 .Message = "This editor package cannot be repaired on the current host.",
                 .AffectedItem = installation.Id});
        }

        EditorManagedOperationPlan plan{.Operation = operation,
                                        .InstallationId = installation.Id,
                                        .Root = installation.Root,
                                        .ManifestFingerprint = installation.ManifestFingerprint,
                                        .PackageTreeIdentity = installation.PackageTreeIdentity,
                                        .PackageReceiptSha256 = installation.PackageReceiptSha256,
                                        .MarkerNonce = installation.MarkerNonce,
                                        .EditorEntrypoint = ResolveEditorEntrypoint(installation),
                                        .CurrentHealth = inspection.Health};
        if (operation == EditorManagedOperation::Repair)
        {
            std::set<std::string, std::less<>> paths;
            for (const auto& issue : inspection.Issues)
            {
                plan.RequiresCompletePackage |= RequiresCompletePackage(issue.Code);
                if (IsFileDamage(issue.Code) && !issue.RelativePath.empty() &&
                    paths.insert(Detail::NormalizedEditorPathKey(issue.RelativePath)).second)
                {
                    plan.FilesToRestore.push_back(issue.RelativePath);
                }
            }
            std::ranges::sort(plan.FilesToRestore, {},
                              [](const auto& path) { return Detail::NormalizedEditorPathKey(path); });
        }
        return HubResult<EditorManagedOperationPlan>::Success(std::move(plan));
    }

    HubStatus RevalidateManagedEditorOperationSnapshot(const EditorInstallation& installation,
                                                       const EditorManagedOperationPlan& plan,
                                                       const EditorInstallationManagerSpecification& specification)
    {
        if (plan.InstallationId != installation.Id)
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The editor operation plan is stale and must be prepared again.",
                                       .AffectedItem = plan.InstallationId});
        auto current = PrepareManagedEditorOperationSnapshot(installation, plan.Root, plan.Operation, specification);
        if (!current)
            return HubStatus::Failure(current.Error());
        if (!SameManagedPlan(current.Value(), plan))
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The editor operation plan is stale and must be prepared again.",
                                       .AffectedItem = plan.InstallationId});
        return HubStatus::Success();
    }

    HubResult<std::filesystem::path> ResolveExternalEditorPackageRoot(const std::filesystem::path& selected)
    {
        const auto failure = [&selected](std::string details)
        {
            return HubResult<std::filesystem::path>::Failure({.Code = HubErrorCode::InvalidData,
                                                              .Message = "The selected editor folder is invalid.",
                                                              .AffectedItem = selected.filename().string(),
                                                              .TechnicalDetails = std::move(details)});
        };
        std::error_code error;
        auto root = std::filesystem::weakly_canonical(selected, error);
        if (error || root.empty())
            return failure("The selected editor folder could not be resolved.");
        if (SameText(root.filename().string(), "bin"))
            root = root.parent_path();
        if (std::filesystem::is_regular_file(root / "editor-package.json", error) && !error)
            return HubResult<std::filesystem::path>::Success(std::move(root));

        error.clear();
        if (!SameText(root.extension().string(), ".app") || !std::filesystem::is_directory(root, error) || error)
            return failure("The selected folder has no editor-package.json manifest.");
        auto bundleRoot = std::filesystem::weakly_canonical(root / "Contents/Resources/Editor", error);
        if (error || bundleRoot.empty())
            return failure("The selected editor application has no packaged editor payload.");
        const auto relative = bundleRoot.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() ||
            std::ranges::any_of(relative, [](const auto& component) { return component == ".."; }))
        {
            return failure("The selected editor application payload escapes its bundle.");
        }
        if (!std::filesystem::is_regular_file(bundleRoot / "editor-package.json", error) || error)
            return failure("The selected editor application has no packaged editor manifest.");
        return HubResult<std::filesystem::path>::Success(std::move(bundleRoot));
    }

    HubResult<EditorInstallation> PrepareManagedEditorPackage(const ManagedEditorPackageRequest& request)
    {
        const auto platform = Detail::CanonicalEditorPlatform(request.HostPlatform);
        const auto architecture = Detail::CanonicalEditorArchitecture(request.HostArchitecture);
        if (request.PackageRoot.empty() || !request.PackageRoot.is_absolute() || request.InstallationRoot.empty() ||
            !request.InstallationRoot.is_absolute() || request.InstallationId.empty() || request.MarkerNonce.empty() ||
            request.VerifiedUnixSeconds == 0 || (platform != "windows" && platform != "linux" && platform != "macos") ||
            (architecture != "x86_64" && architecture != "arm64"))
        {
            return HubResult<EditorInstallation>::Failure(
                {.Code = HubErrorCode::InvalidArgument,
                 .Message = "The managed editor package registration request is invalid.",
                 .AffectedItem = request.InstallationId});
        }
        std::error_code error;
        const auto rootStatus = std::filesystem::symlink_status(request.PackageRoot, error);
        if (error || !std::filesystem::is_directory(rootStatus) || std::filesystem::is_symlink(rootStatus) ||
            IsUnsafeLinkLike(request.PackageRoot))
        {
            return HubResult<EditorInstallation>::Failure(
                {.Code = HubErrorCode::UnsafeInstallRoot,
                 .Message = "The managed editor package root is unavailable or unsafe.",
                 .AffectedItem = request.InstallationId,
                 .TechnicalDetails = error.message()});
        }
        std::optional<ManagedInstallMarker> existingMarker;
        if (request.PreserveExistingMarker)
        {
            auto marker = EditorInstallationRegistry::ReadManagedMarker(request.PackageRoot);
            if (!marker)
                return HubResult<EditorInstallation>::Failure(marker.Error());
            if (marker.Value().InstallationId != request.InstallationId || marker.Value().Nonce != request.MarkerNonce)
            {
                return HubResult<EditorInstallation>::Failure(
                    {.Code = HubErrorCode::UnsafeInstallRoot,
                     .Message = "The managed editor marker changed before registration.",
                     .AffectedItem = request.InstallationId});
            }
            existingMarker = std::move(marker).Value();
        }
        auto package = Detail::ReadEditorPackageManifest(request.PackageRoot);
        if (!package)
            return HubResult<EditorInstallation>::Failure(package.Error());
        if (package.Value().Platform != platform || package.Value().Architecture != architecture)
        {
            return HubResult<EditorInstallation>::Failure(
                {.Code = HubErrorCode::PackageHostIncompatible,
                 .Message = "The editor package is incompatible with this computer.",
                 .AffectedItem = request.InstallationId});
        }
        for (const auto& entrypoint : package.Value().Entrypoints)
        {
            if (!IsConfinedRegularFile(request.PackageRoot, entrypoint))
            {
                return HubResult<EditorInstallation>::Failure(
                    {.Code = HubErrorCode::EditorInventoryInvalid,
                     .Message = "A managed editor package entrypoint is missing or unsafe.",
                     .AffectedItem = Detail::PathToUtf8(entrypoint)});
            }
        }
        auto receipt = ReadPackageInstallReceipt(request.PackageRoot);
        if (!receipt)
        {
            std::error_code receiptError;
            const auto receiptStatus =
                std::filesystem::symlink_status(request.PackageRoot / PackageInstallReceiptFileName, receiptError);
            const bool missing = (!receiptError && receiptStatus.type() == std::filesystem::file_type::not_found) ||
                                 receiptError == std::errc::no_such_file_or_directory;
            if (request.RequirePackageReceipt || !missing)
                return HubResult<EditorInstallation>::Failure(receipt.Error());
        }
        else if (receipt.Value().Packages.front().Id != package.Value().PackageId ||
                 receipt.Value().Packages.front().Version.ToString() != package.Value().Version)
        {
            return HubResult<EditorInstallation>::Failure(
                {.Code = HubErrorCode::PackageManifestInvalid,
                 .Message = "The installed-package receipt does not identify this editor version.",
                 .AffectedItem = request.InstallationId});
        }
        ManagedInstallMarker marker{.InstallationId = request.InstallationId,
                                    .ManifestFingerprint = package.Value().Fingerprint,
                                    .Nonce = request.MarkerNonce,
                                    .ReceiptSha256 = receipt ? receipt.Value().DocumentSha256 : std::string{}};
        if (existingMarker && (existingMarker->ManifestFingerprint != marker.ManifestFingerprint ||
                               existingMarker->ReceiptSha256 != marker.ReceiptSha256))
        {
            return HubResult<EditorInstallation>::Failure(
                {.Code = HubErrorCode::UnsafeInstallRoot,
                 .Message = "The managed editor marker no longer matches its package contents.",
                 .AffectedItem = request.InstallationId});
        }
        if (!existingMarker)
        {
            if (const auto markerStatus = EditorInstallationRegistry::WriteManagedMarker(request.PackageRoot, marker);
                !markerStatus)
            {
                return HubResult<EditorInstallation>::Failure(markerStatus.Error());
            }
        }
        return HubResult<EditorInstallation>::Success(
            {.Id = request.InstallationId,
             .Version = package.Value().Version,
             .Channel = LowerText(package.Value().Channel),
             .Platform = package.Value().Platform,
             .Architecture = package.Value().Architecture,
             .Root = request.InstallationRoot.lexically_normal(),
             .Ownership = InstallationOwnership::Managed,
             .ManifestFingerprint = package.Value().Fingerprint,
             .PackageTreeIdentity = receipt ? receipt.Value().AggregateIdentitySha256 : std::string{},
             .PackageReceiptSha256 = receipt ? receipt.Value().DocumentSha256 : std::string{},
             .MarkerNonce = request.MarkerNonce,
             .InstalledPackages = receipt ? receipt.Value().Packages : std::vector<InstalledPackageRecord>{},
             .Entrypoints = package.Value().Entrypoints,
             .EditorEntrypoint = package.Value().EditorEntrypoint,
             .AssetToolEntrypoint = package.Value().AssetToolEntrypoint,
             .BundledDotnetSdk = package.Value().BundledDotnetSdk,
             .MinimumProjectSchema = package.Value().MinimumProjectSchema,
             .MaximumProjectSchema = package.Value().MaximumProjectSchema,
             .InstalledSizeBytes =
                 receipt ? receipt.Value().AggregateInstalledSizeBytes : package.Value().InstalledSizeBytes,
             .LastVerifiedUnixSeconds = request.VerifiedUnixSeconds,
             .Health = InstallationHealth::Healthy});
    }

    HubResult<EditorInstallation> RegisterManagedEditorPackage(EditorInstallationRegistry& registry,
                                                               const std::filesystem::path& root,
                                                               std::string installationId, std::string hostPlatform,
                                                               std::string hostArchitecture,
                                                               const std::uint64_t verifiedUnixSeconds)
    {
        auto marker = EditorInstallationRegistry::ReadManagedMarker(root);
        if (!marker)
            return HubResult<EditorInstallation>::Failure(marker.Error());
        if (marker.Value().InstallationId != installationId)
        {
            return HubResult<EditorInstallation>::Failure(
                {.Code = HubErrorCode::UnsafeInstallRoot,
                 .Message = "The managed editor marker belongs to another installation.",
                 .AffectedItem = installationId});
        }
        auto installation =
            PrepareManagedEditorPackage({.PackageRoot = root,
                                         .InstallationRoot = root,
                                         .InstallationId = std::move(installationId),
                                         .MarkerNonce = marker.Value().Nonce,
                                         .HostPlatform = std::move(hostPlatform),
                                         .HostArchitecture = std::move(hostArchitecture),
                                         .VerifiedUnixSeconds = verifiedUnixSeconds,
                                         .RequirePackageReceipt = !marker.Value().ReceiptSha256.empty()});
        if (!installation)
            return installation;
        if (installation.Value().ManifestFingerprint != marker.Value().ManifestFingerprint ||
            installation.Value().PackageReceiptSha256 != marker.Value().ReceiptSha256)
        {
            return HubResult<EditorInstallation>::Failure(
                {.Code = HubErrorCode::UnsafeInstallRoot,
                 .Message = "The managed editor marker does not match its package manifest.",
                 .AffectedItem = installation.Value().Id});
        }
        if (const auto status = registry.Upsert(installation.Value()); !status)
            return HubResult<EditorInstallation>::Failure(status.Error());
        return installation;
    }

    EditorInstallationManager::EditorInstallationManager(EditorInstallationRegistry& registry,
                                                         EditorInstallationManagerSpecification specification)
        : m_Registry(registry), m_Specification(std::move(specification)),
          m_Snapshot(std::make_shared<const std::vector<EditorInstallationHealthSnapshot>>())
    {
    }

    HubStatus EditorInstallationManager::Refresh()
    {
        const auto platform = Detail::CanonicalEditorPlatform(m_Specification.HostPlatform);
        const auto architecture = Detail::CanonicalEditorArchitecture(m_Specification.HostArchitecture);
        if ((platform != "windows" && platform != "linux" && platform != "macos") ||
            (architecture != "x86_64" && architecture != "arm64"))
        {
            return HubStatus::Failure(
                {.Code = HubErrorCode::InvalidArgument,
                 .Message = "The editor-installation host identity is invalid.",
                 .AffectedItem = m_Specification.HostPlatform + "/" + m_Specification.HostArchitecture});
        }
        std::vector<EditorInstallationHealthSnapshot> snapshots;
        const auto installations = m_Registry.Snapshot();
        snapshots.reserve(installations->size());
        for (const auto& installation : *installations)
            snapshots.push_back(InspectInstallation(installation, m_Specification, ProbeActivity(installation)));
        m_Snapshot = std::make_shared<const std::vector<EditorInstallationHealthSnapshot>>(std::move(snapshots));
        return HubStatus::Success();
    }

    HubResult<EditorInstallationHealthSnapshot>
    EditorInstallationManager::Inspect(const std::string& installationId) const
    {
        const auto installation = Find(installationId);
        if (!installation)
        {
            return HubResult<EditorInstallationHealthSnapshot>::Failure(
                {.Code = HubErrorCode::NotFound,
                 .Message = "The editor installation is no longer registered.",
                 .AffectedItem = installationId});
        }
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            InspectInstallation(*installation, m_Specification, ProbeActivity(*installation)));
    }

    HubResult<EditorInstallationHealthSnapshot>
    EditorInstallationManager::Verify(const std::string& installationId) const
    {
        const auto installation = Find(installationId);
        if (!installation)
        {
            return HubResult<EditorInstallationHealthSnapshot>::Failure(
                {.Code = HubErrorCode::NotFound,
                 .Message = "The editor installation is no longer registered.",
                 .AffectedItem = installationId});
        }
        const auto activity = ProbeActivity(*installation);
        if (const auto inactive = GuardInactive(*installation, activity); !inactive)
            return HubResult<EditorInstallationHealthSnapshot>::Failure(inactive.Error());
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            InspectInstallation(*installation, m_Specification, activity));
    }

    HubStatus EditorInstallationManager::RemoveExternalRegistration(const std::string& installationId,
                                                                    const std::filesystem::path& expectedRoot)
    {
        const auto installation = Find(installationId);
        if (!installation)
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The editor installation is no longer registered.",
                                       .AffectedItem = installationId});
        if (installation->Ownership != InstallationOwnership::External || !SamePath(installation->Root, expectedRoot))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "Only the exact external editor registration can be removed.",
                                       .AffectedItem = installationId});
        }
        if (const auto inactive = GuardInactive(*installation); !inactive)
            return inactive;
        if (const auto removed = m_Registry.RemoveExternal(installationId); !removed)
            return removed;
        return Refresh();
    }

    HubResult<EditorManagedOperationPlan>
    EditorInstallationManager::PrepareManagedRepair(const std::string& installationId,
                                                    const std::filesystem::path& expectedRoot) const
    {
        return PrepareManagedOperation(installationId, expectedRoot, EditorManagedOperation::Repair);
    }

    HubResult<EditorManagedOperationPlan>
    EditorInstallationManager::PrepareManagedRemoval(const std::string& installationId,
                                                     const std::filesystem::path& expectedRoot) const
    {
        return PrepareManagedOperation(installationId, expectedRoot, EditorManagedOperation::Remove);
    }

    HubStatus EditorInstallationManager::Revalidate(const EditorManagedOperationPlan& plan) const
    {
        if (plan.Operation < EditorManagedOperation::Repair || plan.Operation > EditorManagedOperation::Remove)
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The editor operation plan is invalid.",
                                       .AffectedItem = plan.InstallationId});
        auto current = PrepareManagedOperation(plan.InstallationId, plan.Root, plan.Operation);
        if (!current)
            return HubStatus::Failure(current.Error());
        const auto& expected = current.Value();
        if (expected.ManifestFingerprint != plan.ManifestFingerprint ||
            expected.PackageTreeIdentity != plan.PackageTreeIdentity ||
            expected.PackageReceiptSha256 != plan.PackageReceiptSha256 || expected.MarkerNonce != plan.MarkerNonce ||
            expected.EditorEntrypoint != plan.EditorEntrypoint || expected.CurrentHealth != plan.CurrentHealth ||
            expected.RequiresCompletePackage != plan.RequiresCompletePackage ||
            expected.FilesToRestore != plan.FilesToRestore)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The editor operation plan is stale and must be prepared again.",
                                       .AffectedItem = plan.InstallationId});
        }
        return HubStatus::Success();
    }

    std::shared_ptr<const std::vector<EditorInstallationHealthSnapshot>>
    EditorInstallationManager::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    std::optional<EditorInstallation> EditorInstallationManager::Find(const std::string& installationId) const
    {
        const auto installations = m_Registry.Snapshot();
        const auto found = std::ranges::find(*installations, installationId, &EditorInstallation::Id);
        return found == installations->end() ? std::nullopt : std::optional(*found);
    }

    EditorInstallationActivity EditorInstallationManager::ProbeActivity(const EditorInstallation& installation) const
    {
        EditorInstallationActivity activity;
        try
        {
            if (m_Specification.ProbeActivity)
                activity = m_Specification.ProbeActivity(installation);
        }
        catch (...)
        {
            return {.Running = true, .HasActiveTask = true};
        }

        const auto relativeEntrypoint = ResolveEditorEntrypoint(installation);
        EditorEntrypointActivity entrypointActivity = EditorEntrypointActivity::Indeterminate;
        if (!relativeEntrypoint.empty() && Detail::IsSafeRelativePath(relativeEntrypoint) &&
            installation.Root.is_absolute())
        {
            try
            {
                const auto executable = (installation.Root / relativeEntrypoint).lexically_normal();
                entrypointActivity = m_Specification.ProbeEntrypointActivity
                                         ? m_Specification.ProbeEntrypointActivity(executable)
                                         : Detail::ProbeEditorEntrypointActivity(executable);
            }
            catch (...)
            {
                entrypointActivity = EditorEntrypointActivity::Indeterminate;
            }
        }
        activity.Running = activity.Running || entrypointActivity != EditorEntrypointActivity::NotRunning;
        return activity;
    }

    HubStatus EditorInstallationManager::GuardInactive(const EditorInstallation& installation) const
    {
        return GuardInactive(installation, ProbeActivity(installation));
    }

    HubStatus EditorInstallationManager::GuardInactive(const EditorInstallation& installation,
                                                       const EditorInstallationActivity activity) const
    {
        if (activity.Running)
            return HubStatus::Failure({.Code = HubErrorCode::EditorRunning,
                                       .Message = "Close the editor before changing this installation.",
                                       .Retryable = true,
                                       .AffectedItem = installation.Id});
        if (activity.HasActiveTask)
            return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                       .Message = "Wait for the active installation task to finish.",
                                       .Retryable = true,
                                       .AffectedItem = installation.Id});
        return HubStatus::Success();
    }

    HubResult<EditorManagedOperationPlan>
    EditorInstallationManager::PrepareManagedOperation(const std::string& installationId,
                                                       const std::filesystem::path& expectedRoot,
                                                       const EditorManagedOperation operation) const
    {
        const auto installation = Find(installationId);
        if (!installation)
            return HubResult<EditorManagedOperationPlan>::Failure(
                {.Code = HubErrorCode::NotFound,
                 .Message = "The editor installation is no longer registered.",
                 .AffectedItem = installationId});
        if (installation->Ownership != InstallationOwnership::Managed || !SamePath(installation->Root, expectedRoot))
        {
            return HubResult<EditorManagedOperationPlan>::Failure(
                {.Code = HubErrorCode::UnsafeInstallRoot,
                 .Message = "The editor location is not the expected managed installation.",
                 .AffectedItem = installationId});
        }
        if (const auto marker = m_Registry.CanMutateManagedInstall(installationId, expectedRoot); !marker)
            return HubResult<EditorManagedOperationPlan>::Failure(marker.Error());
        if (const auto inactive = GuardInactive(*installation); !inactive)
            return HubResult<EditorManagedOperationPlan>::Failure(inactive.Error());

        auto inspection = Inspect(installationId);
        if (!inspection)
            return HubResult<EditorManagedOperationPlan>::Failure(inspection.Error());
        if (const auto inactive = GuardInactive(*installation, inspection.Value().Activity); !inactive)
            return HubResult<EditorManagedOperationPlan>::Failure(inactive.Error());
        if (operation == EditorManagedOperation::Remove && inspection.Value().Health != InstallationHealth::Healthy)
        {
            return HubResult<EditorManagedOperationPlan>::Failure(
                {.Code = HubErrorCode::UnsafeInstallRoot,
                 .Message = "Verify or repair this managed editor before removing its files.",
                 .AffectedItem = installationId,
                 .TechnicalDetails = "Removal requires a complete marker-, receipt-, and inventory-verified tree."});
        }
        if (operation == EditorManagedOperation::Remove &&
            (installation->PackageTreeIdentity.empty() || installation->PackageReceiptSha256.empty() ||
             installation->InstalledPackages.empty()))
        {
            return HubResult<EditorManagedOperationPlan>::Failure(
                {.Code = HubErrorCode::UnsafeInstallRoot,
                 .Message = "This legacy managed editor cannot be uninstalled safely. Reinstall or repair it first.",
                 .AffectedItem = installationId,
                 .TechnicalDetails = "Removal requires a receipt-bound complete package inventory."});
        }
        if (operation == EditorManagedOperation::Repair &&
            std::ranges::any_of(inspection.Value().Issues, [](const auto& issue)
                                { return issue.Code == EditorInstallationIssueCode::HostIncompatible; }))
        {
            return HubResult<EditorManagedOperationPlan>::Failure(
                {.Code = HubErrorCode::PackageHostIncompatible,
                 .Message = "This editor package cannot be repaired on the current host.",
                 .AffectedItem = installationId});
        }

        EditorManagedOperationPlan plan{.Operation = operation,
                                        .InstallationId = installation->Id,
                                        .Root = installation->Root,
                                        .ManifestFingerprint = installation->ManifestFingerprint,
                                        .PackageTreeIdentity = installation->PackageTreeIdentity,
                                        .PackageReceiptSha256 = installation->PackageReceiptSha256,
                                        .MarkerNonce = installation->MarkerNonce,
                                        .EditorEntrypoint = ResolveEditorEntrypoint(*installation),
                                        .CurrentHealth = inspection.Value().Health};
        if (operation == EditorManagedOperation::Repair)
        {
            std::set<std::string, std::less<>> paths;
            for (const auto& issue : inspection.Value().Issues)
            {
                plan.RequiresCompletePackage |= RequiresCompletePackage(issue.Code);
                if (IsFileDamage(issue.Code) && !issue.RelativePath.empty() &&
                    paths.insert(Detail::NormalizedEditorPathKey(issue.RelativePath)).second)
                {
                    plan.FilesToRestore.push_back(issue.RelativePath);
                }
            }
            std::ranges::sort(plan.FilesToRestore, {},
                              [](const auto& path) { return Detail::NormalizedEditorPathKey(path); });
        }
        return HubResult<EditorManagedOperationPlan>::Success(std::move(plan));
    }
} // namespace KeireHub
