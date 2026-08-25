#pragma once

#include "KeireHubRuntime/HubUpdateCatalog.h"
#include "KeireHubRuntime/HubUpdateManager.h"
#include "KeireHubRuntime/HubWorkerCoordinator.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace KeireHub
{
    struct HubUpdateDownloadOptions final
    {
        std::string TaskId;
        std::string ServiceBaseUrl;
        std::filesystem::path CacheRoot;
        bool AllowInsecureLoopbackDevelopment = false;
        std::optional<std::string> CustomProxyUrl;
        std::uint64_t BandwidthLimitBytesPerSecond = 0;
    };

    enum class HubUpdateDownloadState
    {
        Available,
        Queued,
        Downloading,
        Paused,
        Verifying,
        Ready,
        Failed,
        Cancelled
    };

    struct HubUpdateWorkflowState final
    {
        HubUpdateDownloadState State = HubUpdateDownloadState::Available;
        std::string TaskId;
        std::filesystem::path VerifiedInstallerPath;
        std::optional<HubError> Failure;
    };

    [[nodiscard]] std::string HubUpdateTaskPrefix(const HubUpdateCandidate& candidate);
    [[nodiscard]] HubStatus ValidateHubUpdateCandidateForHost(const HubUpdateCandidate& candidate);
    [[nodiscard]] HubResult<CatalogPackageDownloadRequest>
    CreateHubUpdateDownloadRequest(const HubUpdateCandidate& candidate, HubUpdateDownloadOptions options);
    [[nodiscard]] HubResult<HubUpdateWorkflowState> InspectHubUpdateWorkflow(const HubUpdateCandidate& candidate,
                                                                             const HubWorkerCoordinatorSnapshot& tasks,
                                                                             const std::filesystem::path& cacheRoot);
    [[nodiscard]] HubResult<HubUpdateRequest>
    CreateHubUpdateHandoffRequest(const HubUpdateCandidate& candidate, const HubUpdateWorkflowState& state,
                                  const std::filesystem::path& cacheRoot, const std::filesystem::path& installRoot,
                                  std::string currentVersion, std::uint64_t currentProcessId,
                                  std::uint64_t startedUnixSeconds,
                                  HubUpdatePlatformSignaturePolicy platformSignaturePolicy);
} // namespace KeireHub
