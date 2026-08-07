#pragma once

#include "KeireHub/HubDistributionWorkflow.h"
#include "KeireHub/HubPackageTaskWorkflow.h"
#include "KeireHub/HubProductUi.h"
#include "KeireHub/HubUpdateHandoffWorkflow.h"

#include <filesystem>
#include <string_view>

namespace KeireHub
{
    void ApplyHubUpdateIntegrationSnapshot(const HubDistributionWorkflowSnapshot& distribution,
                                           const HubWorkerCoordinatorSnapshot& tasks, HubProductSnapshot& product,
                                           HubUpdateHandoffState handoffState);
    [[nodiscard]] HubResult<std::string> QueueAvailableHubUpdate(const HubDistributionWorkflowSnapshot& distribution,
                                                                 std::string_view currentVersion,
                                                                 HubPackageTaskWorkflow& tasks);
    [[nodiscard]] HubResult<HubUpdateRequest>
    CreateAvailableHubUpdateHandoffRequest(const HubDistributionWorkflowSnapshot& distribution,
                                           std::string_view currentVersion, const HubWorkerCoordinatorSnapshot& tasks,
                                           const std::filesystem::path& hubExecutable, const HubSettings& settings,
                                           std::uint64_t nowUnixSeconds);
    [[nodiscard]] HubStatus
    StartAvailableHubUpdateHandoff(const HubDistributionWorkflowSnapshot& distribution, std::string_view currentVersion,
                                   const HubWorkerCoordinatorSnapshot& tasks, HubUpdateManager& manager,
                                   HubUpdateHandoffWorkflow& workflow, const std::filesystem::path& hubExecutable,
                                   const HubSettings& settings, std::uint64_t nowUnixSeconds);
} // namespace KeireHub
