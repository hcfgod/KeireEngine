#include "KeireHubRuntime/HubUpdateWorkflow.h"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] bool MatchesCatalogIdentity(const HubUpdateCandidate& candidate) noexcept
        {
            const auto& package = candidate.Package;
            const auto& identity = candidate.CatalogIdentity;
            return package.Channel == identity.Channel && package.Platform == identity.Platform &&
                   package.Architecture == identity.Architecture && package.SignatureKeyId == identity.KeyId;
        }

        [[nodiscard]] bool TrustedSource(const DistributionCatalogSourceState source) noexcept
        {
            return source == DistributionCatalogSourceState::Online ||
                   source == DistributionCatalogSourceState::LastKnownGood ||
                   source == DistributionCatalogSourceState::OfflineLastKnownGood;
        }

        [[nodiscard]] HubUpdateDownloadState ToWorkflowState(const HubTaskState state) noexcept
        {
            switch (state)
            {
            case HubTaskState::Queued:
                return HubUpdateDownloadState::Queued;
            case HubTaskState::Downloading:
                return HubUpdateDownloadState::Downloading;
            case HubTaskState::Paused:
                return HubUpdateDownloadState::Paused;
            case HubTaskState::Verifying:
                return HubUpdateDownloadState::Verifying;
            case HubTaskState::Completed:
                return HubUpdateDownloadState::Ready;
            case HubTaskState::Failed:
                return HubUpdateDownloadState::Failed;
            case HubTaskState::Cancelled:
                return HubUpdateDownloadState::Cancelled;
            case HubTaskState::Extracting:
            case HubTaskState::Installing:
            case HubTaskState::Configuring:
            case HubTaskState::Removing:
            case HubTaskState::Cancelling:
                return HubUpdateDownloadState::Downloading;
            }
            return HubUpdateDownloadState::Failed;
        }

        [[nodiscard]] HubError InvalidCompletion(const HubUpdateCandidate& candidate, std::string details)
        {
            return {.Code = HubErrorCode::WorkerProtocolInvalid,
                    .Message = "The verified Hub installer record is incomplete.",
                    .Retryable = true,
                    .AffectedItem = candidate.Package.Id,
                    .TechnicalDetails = std::move(details),
                    .LogReference = {}};
        }

        [[nodiscard]] DownloadCacheKind NativeInstallerCacheKind(const PackageManifest& package) noexcept
        {
            if (package.Platform == "windows")
                return DownloadCacheKind::WindowsExecutable;
            if (package.Platform == "macos")
                return DownloadCacheKind::MacDiskImage;
            if (package.Platform == "linux" && package.PackageFormat == "rpm")
                return DownloadCacheKind::RpmPackage;
            if (package.Platform == "linux")
                return DownloadCacheKind::DebianPackage;
            return DownloadCacheKind::Package;
        }
    } // namespace

    std::string HubUpdateTaskPrefix(const HubUpdateCandidate& candidate)
    {
        return "hub-update-" + candidate.Package.ArtifactSha256 + "-";
    }

    HubStatus ValidateHubUpdateCandidateForHost(const HubUpdateCandidate& candidate)
    {
        const auto nativePackageFormat = HubUpdateManager::HostPackageFormatIdentity();
        const auto effectivePackageFormat =
            candidate.Package.PackageFormat.empty() && candidate.Package.Platform == "linux"
                ? std::string_view{"deb"}
                : std::string_view{candidate.Package.PackageFormat};
        if (!TrustedSource(candidate.Source) || candidate.Package.Kind != PackageKind::HubInstaller ||
            candidate.CatalogIdentity.Sequence == 0 || !MatchesCatalogIdentity(candidate) ||
            candidate.Package.Platform != HubUpdateManager::HostPlatformIdentity() ||
            candidate.Package.Architecture != HubUpdateManager::HostArchitectureIdentity() ||
            (!effectivePackageFormat.empty() && effectivePackageFormat != nativePackageFormat))
        {
            return HubStatus::Failure({.Code = HubErrorCode::CatalogIdentityMismatch,
                                       .Message = "The signed Hub installer does not match this computer.",
                                       .AffectedItem = candidate.Package.Id,
                                       .TechnicalDetails = {},
                                       .LogReference = {}});
        }
        return ValidatePackageManifest(candidate.Package);
    }

    HubResult<CatalogPackageDownloadRequest> CreateHubUpdateDownloadRequest(const HubUpdateCandidate& candidate,
                                                                            HubUpdateDownloadOptions options)
    {
        if (const auto status = ValidateHubUpdateCandidateForHost(candidate); !status)
            return HubResult<CatalogPackageDownloadRequest>::Failure(status.Error());
        if (!options.TaskId.starts_with(HubUpdateTaskPrefix(candidate)))
        {
            return HubResult<CatalogPackageDownloadRequest>::Failure(
                {.Code = HubErrorCode::InvalidArgument,
                 .Message = "The Hub update task identity is invalid.",
                 .AffectedItem = options.TaskId,
                 .TechnicalDetails = {},
                 .LogReference = {}});
        }
        while (options.ServiceBaseUrl.ends_with('/'))
            options.ServiceBaseUrl.pop_back();
        if (options.ServiceBaseUrl.empty())
        {
            return HubResult<CatalogPackageDownloadRequest>::Failure(
                {.Code = HubErrorCode::DistributionConfigurationInvalid,
                 .Message = "No trusted Hub update service is configured.",
                 .Retryable = true,
                 .AffectedItem = candidate.Package.Id,
                 .TechnicalDetails = {},
                 .LogReference = {}});
        }

        CatalogPackageDownloadRequest request{
            .TaskId = std::move(options.TaskId),
            .Package = candidate.Package,
            .PackageUrl = options.ServiceBaseUrl + "/v1/packages/" + candidate.Package.ArtifactSha256,
            .CacheRoot = std::move(options.CacheRoot),
            .Retry = {},
            .AllowInsecureLoopbackDevelopment = options.AllowInsecureLoopbackDevelopment,
            .CustomProxyUrl = std::move(options.CustomProxyUrl),
            .BandwidthLimitBytesPerSecond = options.BandwidthLimitBytesPerSecond,
            .CacheKind = NativeInstallerCacheKind(candidate.Package)};
        if (const auto status =
                DownloadManager::Validate({.PackageId = request.Package.Id,
                                           .Url = request.PackageUrl,
                                           .Sha256 = request.Package.ArtifactSha256,
                                           .SizeBytes = request.Package.ArtifactSizeBytes,
                                           .CacheRoot = request.CacheRoot,
                                           .Retry = request.Retry,
                                           .AllowInsecureLoopbackDevelopment = request.AllowInsecureLoopbackDevelopment,
                                           .CustomProxyUrl = request.CustomProxyUrl,
                                           .BandwidthLimitBytesPerSecond = request.BandwidthLimitBytesPerSecond,
                                           .CacheKind = request.CacheKind});
            !status)
        {
            return HubResult<CatalogPackageDownloadRequest>::Failure(status.Error());
        }
        return HubResult<CatalogPackageDownloadRequest>::Success(std::move(request));
    }

    HubResult<HubUpdateWorkflowState> InspectHubUpdateWorkflow(const HubUpdateCandidate& candidate,
                                                               const HubWorkerCoordinatorSnapshot& tasks,
                                                               const std::filesystem::path& cacheRoot)
    {
        if (const auto status = ValidateHubUpdateCandidateForHost(candidate); !status)
            return HubResult<HubUpdateWorkflowState>::Failure(status.Error());
        if (cacheRoot.empty() || !cacheRoot.is_absolute())
        {
            return HubResult<HubUpdateWorkflowState>::Failure({.Code = HubErrorCode::InvalidArgument,
                                                               .Message = "The verified package cache path is invalid.",
                                                               .AffectedItem = candidate.Package.Id,
                                                               .TechnicalDetails = {},
                                                               .LogReference = {}});
        }
        if (!tasks.Tasks)
            return HubResult<HubUpdateWorkflowState>::Success({});

        const auto prefix = HubUpdateTaskPrefix(candidate);
        const HubTask* selected = nullptr;
        for (const auto& task : *tasks.Tasks)
        {
            if (task.Kind != HubTaskKind::HubUpdate || !task.Id.starts_with(prefix) ||
                task.PackageIds != std::vector<std::string>{candidate.Package.Id})
            {
                continue;
            }
            if (!selected || task.CreatedUnixSeconds > selected->CreatedUnixSeconds ||
                (task.CreatedUnixSeconds == selected->CreatedUnixSeconds && task.Id > selected->Id))
            {
                selected = &task;
            }
        }
        if (!selected)
            return HubResult<HubUpdateWorkflowState>::Success({});

        HubUpdateWorkflowState result{.State = ToWorkflowState(selected->State),
                                      .TaskId = selected->Id,
                                      .VerifiedInstallerPath = {},
                                      .Failure = selected->Failure};
        if (selected->State != HubTaskState::Completed)
            return HubResult<HubUpdateWorkflowState>::Success(std::move(result));
        if (!tasks.VerifiedDownloads)
            return HubResult<HubUpdateWorkflowState>::Failure(
                InvalidCompletion(candidate, "The coordinator published no verified download snapshot."));
        const auto verified =
            std::ranges::find(*tasks.VerifiedDownloads, selected->Id, &HubVerifiedPackageDownload::TaskId);
        if (verified == tasks.VerifiedDownloads->end() || verified->PackageId != candidate.Package.Id ||
            verified->Sha256 != candidate.Package.ArtifactSha256 ||
            verified->SizeBytes != candidate.Package.ArtifactSizeBytes)
        {
            return HubResult<HubUpdateWorkflowState>::Failure(
                InvalidCompletion(candidate, "The task and verified download identities disagree."));
        }
        const DownloadRequest expected{.PackageId = candidate.Package.Id,
                                       .Url = "https://verified.invalid/",
                                       .Sha256 = candidate.Package.ArtifactSha256,
                                       .SizeBytes = candidate.Package.ArtifactSizeBytes,
                                       .CacheRoot = cacheRoot,
                                       .Retry = {},
                                       .AllowInsecureLoopbackDevelopment = false,
                                       .CustomProxyUrl = std::nullopt,
                                       .BandwidthLimitBytesPerSecond = 0,
                                       .CacheKind = NativeInstallerCacheKind(candidate.Package)};
        if (verified->CachePath.lexically_normal() != DownloadManager::CachePath(expected).lexically_normal())
        {
            return HubResult<HubUpdateWorkflowState>::Failure(
                InvalidCompletion(candidate, "The verified installer is outside its content-addressed cache path."));
        }
        std::error_code error;
        const auto status = std::filesystem::symlink_status(verified->CachePath, error);
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
        {
            result.State = HubUpdateDownloadState::Available;
            result.TaskId.clear();
            return HubResult<HubUpdateWorkflowState>::Success(std::move(result));
        }
        result.VerifiedInstallerPath = verified->CachePath;
        return HubResult<HubUpdateWorkflowState>::Success(std::move(result));
    }

    HubResult<HubUpdateRequest>
    CreateHubUpdateHandoffRequest(const HubUpdateCandidate& candidate, const HubUpdateWorkflowState& state,
                                  const std::filesystem::path& cacheRoot, const std::filesystem::path& installRoot,
                                  std::string currentVersion, const std::uint64_t currentProcessId,
                                  const std::uint64_t startedUnixSeconds, const bool requirePlatformSignature)
    {
        if (const auto status = ValidateHubUpdateCandidateForHost(candidate); !status)
            return HubResult<HubUpdateRequest>::Failure(status.Error());
        if (state.State != HubUpdateDownloadState::Ready || state.VerifiedInstallerPath.empty())
        {
            return HubResult<HubUpdateRequest>::Failure(
                {.Code = HubErrorCode::InvalidTransition,
                 .Message = "Download and verify the Hub installer before starting the update.",
                 .AffectedItem = candidate.Package.Id,
                 .TechnicalDetails = {},
                 .LogReference = {}});
        }
        return HubResult<HubUpdateRequest>::Success({.InstallerPath = state.VerifiedInstallerPath,
                                                     .VerifiedCacheRoot = cacheRoot,
                                                     .HubInstallRoot = installRoot,
                                                     .PackageId = candidate.Package.Id,
                                                     .Sha256 = candidate.Package.ArtifactSha256,
                                                     .CurrentVersion = std::move(currentVersion),
                                                     .TargetVersion = candidate.Package.Version.ToString(),
                                                     .Platform = candidate.Package.Platform,
                                                     .Architecture = candidate.Package.Architecture,
                                                     .SignatureKeyId = candidate.Package.SignatureKeyId,
                                                     .CatalogSequence = candidate.CatalogIdentity.Sequence,
                                                     .CurrentProcessId = currentProcessId,
                                                     .StartedUnixSeconds = startedUnixSeconds,
                                                     .RequirePlatformSignature = requirePlatformSignature});
    }
} // namespace KeireHub
