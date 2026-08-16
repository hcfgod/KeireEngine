#include "KeireHub/HubUpdateIntegration.h"

#include "KeireHub/HubUpdatePlatform.h"

#include "KeireHubRuntime/HubUpdateWorkflow.h"

#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubResult<HubUpdateCandidate>
        RequireCandidate(const HubDistributionWorkflowSnapshot& distribution, const std::string_view currentVersion)
        {
            if (!distribution.Catalogs)
            {
                return HubResult<HubUpdateCandidate>::Failure(
                    {.Code = HubErrorCode::CatalogCacheInvalid,
                     .Message = "No verified Hub update catalog is available.",
                     .Retryable = true,
                     .AffectedItem = {},
                     .TechnicalDetails = {},
                     .LogReference = {}});
            }
            auto selected =
                SelectHubUpdate(*distribution.Catalogs, currentVersion, HubUpdateManager::HostPackageFormatIdentity());
            if (!selected)
                return HubResult<HubUpdateCandidate>::Failure(selected.Error());
            if (!selected.Value())
            {
                return HubResult<HubUpdateCandidate>::Failure({.Code = HubErrorCode::NotFound,
                                                               .Message = "No newer Hub version is available.",
                                                               .Retryable = false,
                                                               .AffectedItem = {},
                                                               .TechnicalDetails = {},
                                                               .LogReference = {}});
            }
            if (const auto status = ValidateHubUpdateCandidateForHost(*selected.Value()); !status)
                return HubResult<HubUpdateCandidate>::Failure(status.Error());
            return HubResult<HubUpdateCandidate>::Success(std::move(*selected.Value()));
        }

        [[nodiscard]] bool MayQueueDownload(const HubDistributionWorkflowSnapshot& distribution) noexcept
        {
            return !distribution.ServiceBaseUrl.empty() && distribution.Catalogs && !distribution.Catalogs->OfflineMode;
        }
    } // namespace

    void ApplyHubUpdateIntegrationSnapshot(const HubDistributionWorkflowSnapshot& distribution,
                                           const HubWorkerCoordinatorSnapshot& tasks, HubProductSnapshot& product,
                                           const HubUpdateHandoffState handoffState)
    {
        if (!product.HubUpdate)
            return;
        auto candidate = RequireCandidate(distribution, product.HubVersion);
        if (!candidate)
        {
            product.HubUpdate->ActionMessage = candidate.Error().Message;
            return;
        }
        auto state = InspectHubUpdateWorkflow(candidate.Value(), tasks, product.Settings.CacheRoot);
        product.HubUpdate->DownloadBytes = candidate.Value().Package.ArtifactSizeBytes;
        product.HubUpdate->CanDownload = MayQueueDownload(distribution);
        if (!state)
        {
            product.HubUpdate->ActionMessage = state.Error().Message;
            return;
        }
        auto& update = *product.HubUpdate;
        update.TaskId = state.Value().TaskId;
        update.VerifiedInstallerPath = state.Value().VerifiedInstallerPath;
        switch (state.Value().State)
        {
        case HubUpdateDownloadState::Available:
            update.ActionMessage = distribution.Catalogs && distribution.Catalogs->OfflineMode
                                       ? "Offline mode can use only an already cached installer."
                                       : "Download and verify the installer through the task center.";
            break;
        case HubUpdateDownloadState::Queued:
        case HubUpdateDownloadState::Downloading:
        case HubUpdateDownloadState::Verifying:
            update.DownloadActive = true;
            update.ActionMessage = "The signed Hub installer is being downloaded and verified.";
            break;
        case HubUpdateDownloadState::Paused:
            update.DownloadActive = true;
            update.DownloadPaused = true;
            update.ActionMessage = "The Hub update download is paused in the task center.";
            break;
        case HubUpdateDownloadState::Ready:
            update.ReadyToInstall = true;
            update.NativeHandoffAvailable = NativeHubUpdateHandoffAvailable();
            update.ActionMessage = update.NativeHandoffAvailable
                                       ? "The installer is verified and ready. Installation starts only when you "
                                         "choose Install update. The Hub closes after handing off to the native "
                                         "installer."
                                       : NativeHubUpdateHandoffUnavailableMessage();
            break;
        case HubUpdateDownloadState::Failed:
            update.DownloadFailed = true;
            update.ActionMessage =
                state.Value().Failure ? state.Value().Failure->Message : "The Hub update download failed.";
            break;
        case HubUpdateDownloadState::Cancelled:
            update.ActionMessage = "The Hub update download was cancelled.";
            break;
        }
        if (handoffState == HubUpdateHandoffState::Verifying || handoffState == HubUpdateHandoffState::Launched)
        {
            update.ReadyToInstall = false;
            update.ActionMessage = handoffState == HubUpdateHandoffState::Verifying
                                       ? "Rechecking the installer and its platform signature before handoff."
                                       : "The native installer is open and the Hub is closing.";
        }
    }

    HubResult<std::string> QueueAvailableHubUpdate(const HubDistributionWorkflowSnapshot& distribution,
                                                   const std::string_view currentVersion, HubPackageTaskWorkflow& tasks)
    {
        auto candidate = RequireCandidate(distribution, currentVersion);
        if (!candidate)
            return HubResult<std::string>::Failure(candidate.Error());
        if (!MayQueueDownload(distribution))
        {
            return HubResult<std::string>::Failure(
                {.Code = HubErrorCode::DownloadUnavailable,
                 .Message = "No verified Hub update task is ready and online downloads are unavailable.",
                 .Retryable = true,
                 .AffectedItem = candidate.Value().Package.Id,
                 .TechnicalDetails = {},
                 .LogReference = {}});
        }
        return tasks.QueueHubUpdate(candidate.Value(), distribution.ServiceBaseUrl,
                                    distribution.AllowInsecureLoopbackDevelopment);
    }

    HubResult<HubUpdateRequest> CreateAvailableHubUpdateHandoffRequest(
        const HubDistributionWorkflowSnapshot& distribution, const std::string_view currentVersion,
        const HubWorkerCoordinatorSnapshot& tasks, const std::filesystem::path& hubExecutable,
        const HubSettings& settings, const std::uint64_t nowUnixSeconds)
    {
        auto candidate = RequireCandidate(distribution, currentVersion);
        if (!candidate)
            return HubResult<HubUpdateRequest>::Failure(candidate.Error());
        auto state = InspectHubUpdateWorkflow(candidate.Value(), tasks, settings.CacheRoot);
        if (!state)
            return HubResult<HubUpdateRequest>::Failure(state.Error());
        return CreateHubUpdateHandoffRequest(
            candidate.Value(), state.Value(), settings.CacheRoot,
            std::filesystem::absolute(hubExecutable).lexically_normal().parent_path().parent_path(),
            std::string(currentVersion), HubCurrentProcessId(), nowUnixSeconds,
            NativeHubUpdateRequiresPlatformSignature());
    }

    HubStatus StartAvailableHubUpdateHandoff(const HubDistributionWorkflowSnapshot& distribution,
                                             const std::string_view currentVersion,
                                             const HubWorkerCoordinatorSnapshot& tasks, HubUpdateManager& manager,
                                             HubUpdateHandoffWorkflow& workflow,
                                             const std::filesystem::path& hubExecutable, const HubSettings& settings,
                                             const std::uint64_t nowUnixSeconds)
    {
        if (!NativeHubUpdateHandoffAvailable())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = NativeHubUpdateHandoffUnavailableMessage(),
                                       .AffectedItem = hubExecutable.filename().string()});
        }
        auto request = CreateAvailableHubUpdateHandoffRequest(distribution, currentVersion, tasks, hubExecutable,
                                                              settings, nowUnixSeconds);
        if (!request)
            return HubStatus::Failure(request.Error());
        return workflow.Start(manager, std::move(request).Value(), VerifyNativeHubInstallerSignature,
                              LaunchNativeHubInstaller);
    }
} // namespace KeireHub
