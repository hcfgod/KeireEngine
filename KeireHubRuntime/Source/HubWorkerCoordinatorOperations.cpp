#include "HubWorkerCoordinatorOperations.h"

#include "Persistence.h"

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
