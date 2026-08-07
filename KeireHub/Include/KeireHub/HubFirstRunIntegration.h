#pragma once

#include "KeireHub/HubFirstRunWorkflow.h"
#include "KeireHub/HubProductUi.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace KeireHub
{
    [[nodiscard]] HubFirstRunDiscoveryRequest BuildHubFirstRunDiscoveryRequest(const HubSettings& settings,
                                                                               const std::filesystem::path& executable,
                                                                               std::string platform,
                                                                               std::string architecture);
    void ApplyHubFirstRunSnapshot(const HubFirstRunWorkflowSnapshot& discovery, HubProductSnapshot& product);
    [[nodiscard]] HubStatus ImportHubFirstRunSnapshot(const HubFirstRunWorkflowSnapshot& discovery,
                                                      HubController& controller);
} // namespace KeireHub
