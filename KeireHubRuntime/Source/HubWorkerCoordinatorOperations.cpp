#include <KeireHubRuntimeInternal/HubWorkerCoordinatorOperations.h>

#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <exception>
#include <ranges>
#include <system_error>
#include <utility>

namespace KeireHub::Detail
{
    namespace
    {
        [[nodiscard]] DownloadRequest CreateDownloadRequest(const CatalogPackageDownloadRequest& request)
        {
            return {.PackageId = request.Package.Id,
                    .Url = request.PackageUrl,
                    .Sha256 = request.Package.ArtifactSha256,
                    .SizeBytes = request.Package.ArtifactSizeBytes,
                    .CacheRoot = request.CacheRoot,
                    .Retry = request.Retry,
                    .AllowInsecureLoopbackDevelopment = request.AllowInsecureLoopbackDevelopment,
                    .CustomProxyUrl = request.CustomProxyUrl,
                    .BandwidthLimitBytesPerSecond = request.BandwidthLimitBytesPerSecond};
        }
    } // namespace

    OperationPaths PathsFor(const std::filesystem::path& root, const std::string& taskId)
    {
        const auto directory = root / taskId;
        return {.Directory = directory,
                .Request = directory / "request.json",
                .Status = directory / "status.json",
                .Result = directory / "result.json",
                .Control = directory / "control.json"};
    }

    bool Exists(const std::filesystem::path& path) noexcept
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        return exists && !error;
    }

    void RemoveKnownFile(const std::filesystem::path& path) noexcept
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        auto temporary = path;
        temporary += ".tmp";
        std::filesystem::remove(temporary, ignored);
    }

    HubStatus PrepareOperationRoot(const std::filesystem::path& root)
    {
        try
        {
            std::filesystem::create_directories(root);
            std::error_code error;
            const auto status = std::filesystem::symlink_status(root, error);
            if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
            {
                return HubStatus::Failure({.Code = HubErrorCode::WorkerProtocolInvalid,
                                           .Message = "The Hub worker operation directory is unsafe.",
                                           .AffectedItem = PathToUtf8(root.filename())});
            }
            return HubStatus::Success();
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                       .Message = "The Hub could not prepare its package task directory.",
                                       .Retryable = true,
                                       .AffectedItem = PathToUtf8(root.filename()),
                                       .TechnicalDetails = error.what()});
        }
    }

    HubStatus PrepareManagedEditorRoot(const std::filesystem::path& root, const std::string& taskId)
    {
        std::error_code error;
        auto status = std::filesystem::symlink_status(root, error);
        if (error && error.default_error_condition() != std::errc::no_such_file_or_directory)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoRead,
                                       .Message = "The editor installation root could not be inspected.",
                                       .Retryable = true,
                                       .AffectedItem = taskId,
                                       .TechnicalDetails = error.message()});
        }
        if (!error && status.type() != std::filesystem::file_type::not_found)
        {
            if (std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status))
                return HubStatus::Success();
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The editor installation root is occupied by an unsafe object.",
                                       .AffectedItem = taskId});
        }

        error.clear();
        const auto parentStatus = std::filesystem::symlink_status(root.parent_path(), error);
        if (error || !std::filesystem::is_directory(parentStatus) || std::filesystem::is_symlink(parentStatus))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The editor installation root parent is unavailable or unsafe.",
                                       .AffectedItem = taskId,
                                       .TechnicalDetails = error ? error.message() : std::string{}});
        }
        if (!std::filesystem::create_directory(root, error) && error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                       .Message = "The editor installation root could not be created.",
                                       .Retryable = true,
                                       .AffectedItem = taskId,
                                       .TechnicalDetails = error.message()});
        }
        status = std::filesystem::symlink_status(root, error);
        if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The created editor installation root is unsafe.",
                                       .AffectedItem = taskId,
                                       .TechnicalDetails = error ? error.message() : std::string{}});
        }
        return HubStatus::Success();
    }

    HubStatus PrepareOperationDirectory(const std::filesystem::path& root, const OperationPaths& paths)
    {
        try
        {
            std::error_code error;
            if (std::filesystem::exists(paths.Directory, error))
            {
                const auto status = std::filesystem::symlink_status(paths.Directory, error);
                if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
                {
                    return HubStatus::Failure({.Code = HubErrorCode::WorkerProtocolInvalid,
                                               .Message = "The package task directory is unsafe.",
                                               .AffectedItem = PathToUtf8(paths.Directory.filename())});
                }
            }
            else if (error || !std::filesystem::create_directory(paths.Directory))
            {
                return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                           .Message = "The Hub could not create the package task directory.",
                                           .Retryable = true,
                                           .AffectedItem = PathToUtf8(paths.Directory.filename()),
                                           .TechnicalDetails = error.message()});
            }

            const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
            if (error)
                throw std::filesystem::filesystem_error("Could not resolve operation root.", root, error);
            const auto canonicalDirectory = std::filesystem::weakly_canonical(paths.Directory, error);
            if (error || canonicalDirectory.parent_path() != canonicalRoot)
            {
                return HubStatus::Failure({.Code = HubErrorCode::WorkerProtocolInvalid,
                                           .Message = "The package task directory escaped its allowed root.",
                                           .AffectedItem = PathToUtf8(paths.Directory.filename())});
            }

            for (const auto& file : {paths.Request, paths.Status, paths.Result, paths.Control})
            {
                auto temporary = file;
                temporary += ".tmp";
                for (const auto& candidate : {file, temporary})
                {
                    error.clear();
                    const auto status = std::filesystem::symlink_status(candidate, error);
                    if (error)
                    {
                        if (error == std::errc::no_such_file_or_directory)
                        {
                            error.clear();
                            continue;
                        }
                        throw std::filesystem::filesystem_error("Could not inspect operation file.", candidate, error);
                    }
                    if (status.type() == std::filesystem::file_type::not_found)
                        continue;
                    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
                    {
                        return HubStatus::Failure({.Code = HubErrorCode::WorkerProtocolInvalid,
                                                   .Message = "A package task journal is unsafe.",
                                                   .AffectedItem = PathToUtf8(candidate.filename())});
                    }
                }
            }
            return HubStatus::Success();
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                       .Message = "The Hub could not prepare a package task.",
                                       .Retryable = true,
                                       .AffectedItem = PathToUtf8(paths.Directory.filename()),
                                       .TechnicalDetails = error.what()});
        }
    }

    HubError ProtocolFailure(const std::string& taskId, const std::string_view details)
    {
        return {.Code = HubErrorCode::WorkerProtocolInvalid,
                .Message = "The package worker returned invalid operation data.",
                .Retryable = true,
                .AffectedItem = taskId,
                .TechnicalDetails = std::string(details)};
    }

    DownloadRequest CreateWorkerDownloadRequest(const CatalogPackageDownloadRequest& request)
    {
        return CreateDownloadRequest(request);
    }

    HubStatus ValidateCatalogDownload(const CatalogPackageDownloadRequest& request)
    {
        if (!IsBoundedIdentifier(request.TaskId))
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The package download task identity is invalid.",
                                       .AffectedItem = request.TaskId});
        }
        if (const auto status = ValidatePackageManifest(request.Package); !status)
            return status;
        return DownloadManager::Validate(CreateDownloadRequest(request));
    }

    HubWorkerRequest CreateEditorInstallWorkerRequest(const CatalogEditorInstallRequest& request)
    {
        std::vector<HubWorkerInstallPackageRequest> packageSteps;
        packageSteps.reserve(request.AdditionalDownloads.size() + 1);
        packageSteps.push_back(
            {.Package = request.Download.Package, .Download = CreateDownloadRequest(request.Download)});
        for (const auto& additional : request.AdditionalDownloads)
            packageSteps.push_back({.Package = additional.Package, .Download = CreateDownloadRequest(additional)});
        return {.TaskId = request.Download.TaskId,
                .Download = packageSteps.front().Download,
                .EditorInstall = HubWorkerEditorInstallRequest{.Package = request.EditorPackage,
                                                               .PackageSteps = std::move(packageSteps),
                                                               .RequestedPackageIds = request.RequestedPackageIds,
                                                               .AllowedInstallRoot = request.AllowedInstallRoot,
                                                               .Destination = request.Destination,
                                                               .InstallationId = request.InstallationId,
                                                               .MarkerNonce = request.MarkerNonce,
                                                               .HostPlatform = request.HostPlatform,
                                                               .HostArchitecture = request.HostArchitecture,
                                                               .VerifiedUnixSeconds = request.VerifiedUnixSeconds}};
    }

    HubWorkerRequest CreateEditorRepairWorkerRequest(const CatalogEditorRepairRequest& request)
    {
        auto result = CreateEditorInstallWorkerRequest(request.Install);
        result.EditorInstall->Mode = HubWorkerEditorInstallMode::Repair;
        result.EditorInstall->RepairAuthorization = {.ManifestFingerprint = request.ManifestFingerprint,
                                                     .PackageTreeIdentity = request.PackageTreeIdentity,
                                                     .PackageReceiptSha256 = request.PackageReceiptSha256,
                                                     .EditorEntrypoint = request.EditorEntrypoint};
        return result;
    }

    HubWorkerRequest CreateEditorRemovalWorkerRequest(const CatalogEditorRemovalRequest& request)
    {
        return {.TaskId = request.TaskId,
                .EditorRemoval = HubWorkerEditorRemovalRequest{.AllowedInstallRoot = request.AllowedInstallRoot,
                                                               .Root = request.Root,
                                                               .InstallationId = request.InstallationId,
                                                               .ManifestFingerprint = request.ManifestFingerprint,
                                                               .PackageTreeIdentity = request.PackageTreeIdentity,
                                                               .PackageReceiptSha256 = request.PackageReceiptSha256,
                                                               .MarkerNonce = request.MarkerNonce}};
    }

    std::vector<std::string> WorkerRequestPackageIds(const HubWorkerRequest& request)
    {
        std::vector<std::string> result;
        if (request.EditorInstall)
        {
            result.reserve(request.EditorInstall->PackageSteps.size());
            for (const auto& step : request.EditorInstall->PackageSteps)
                result.push_back(step.Package.Id);
        }
        else if (!request.EditorRemoval)
        {
            result.push_back(request.Download.PackageId);
        }
        return result;
    }

    std::uint64_t WorkerRequestDownloadBytes(const HubWorkerRequest& request) noexcept
    {
        if (request.EditorRemoval)
            return 0;
        if (!request.EditorInstall)
            return request.Download.SizeBytes;
        std::uint64_t result = 0;
        for (const auto& step : request.EditorInstall->PackageSteps)
            result += step.Download.SizeBytes;
        return result;
    }

    bool WorkerRequestMatchesTaskKind(const HubWorkerRequest& request, const HubTaskKind kind) noexcept
    {
        if (request.EditorInstall)
        {
            return (request.EditorInstall->Mode == HubWorkerEditorInstallMode::Install &&
                    kind == HubTaskKind::Install) ||
                   (request.EditorInstall->Mode == HubWorkerEditorInstallMode::Repair && kind == HubTaskKind::Repair);
        }
        if (request.EditorRemoval)
            return kind == HubTaskKind::Remove;
        return kind == HubTaskKind::Download || kind == HubTaskKind::HubUpdate;
    }

    bool HasPendingWorkerResult(const std::shared_ptr<const std::vector<HubTask>>& tasks,
                                const std::filesystem::path& operationRoot, const std::uint64_t processId) noexcept
    {
        if (!tasks)
            return false;
        return std::ranges::any_of(*tasks,
                                   [&](const HubTask& task)
                                   {
                                       std::error_code error;
                                       return !IsTerminal(task.State) && task.WorkerProcessId == processId &&
                                              std::filesystem::exists(operationRoot / task.Id / "result.json", error) &&
                                              !error;
                                   });
    }

    HubCompletedEditorRemoval CreateCompletedEditorRemoval(std::string taskId,
                                                           const HubWorkerEditorRemovalRequest& removal)
    {
        return {.TaskId = std::move(taskId),
                .Proof = {.InstallationId = removal.InstallationId,
                          .Root = removal.Root,
                          .ManifestFingerprint = removal.ManifestFingerprint,
                          .PackageTreeIdentity = removal.PackageTreeIdentity,
                          .PackageReceiptSha256 = removal.PackageReceiptSha256,
                          .MarkerNonce = removal.MarkerNonce}};
    }

    HubCompletedEditorInstall CreateCompletedEditorInstall(std::string taskId,
                                                           const HubWorkerEditorInstallRequest& install)
    {
        HubCompletedEditorInstall result{.TaskId = std::move(taskId),
                                         .InstallationId = install.InstallationId,
                                         .PackageId = install.Package.Id,
                                         .Root = install.Destination,
                                         .RepairsExisting = install.Mode == HubWorkerEditorInstallMode::Repair,
                                         .MarkerNonce = install.MarkerNonce};
        if (install.RepairAuthorization)
        {
            result.ManifestFingerprint = install.RepairAuthorization->ManifestFingerprint;
            result.PackageTreeIdentity = install.RepairAuthorization->PackageTreeIdentity;
            result.PackageReceiptSha256 = install.RepairAuthorization->PackageReceiptSha256;
        }
        return result;
    }
} // namespace KeireHub::Detail
