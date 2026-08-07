#pragma once

#include "KeireHub/HubMaintenanceWorkflow.h"

#include "KeireHubRuntime/HubController.h"

#include <memory>
#include <string>

namespace KeireHub
{
    class HubPackageTaskWorkflow;

    [[nodiscard]] HubStatus BeginHubVerifiedCacheClear(HubMaintenanceWorkflow& maintenance, HubController& controller,
                                                       std::unique_ptr<HubPackageTaskWorkflow>& packageTasks,
                                                       bool& packageTaskRefreshPending);
    void PollHubMaintenance(HubMaintenanceWorkflow& maintenance, bool& packageTaskRefreshPending, std::string& notice,
                            bool& noticeError);
} // namespace KeireHub
