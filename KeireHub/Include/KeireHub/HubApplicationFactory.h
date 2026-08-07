#pragma once

#include "KeireHub/HubInstance.h"

#include "Keire/Application.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace KeireHub
{
    [[nodiscard]] std::unique_ptr<Keire::Application>
    CreateHubApplication(Keire::ApplicationSpecification specification, std::filesystem::path executable, bool smoke,
                         std::optional<HubActivationRequest> pendingStartupActivation,
                         std::shared_ptr<HubInstanceCoordinator> instance);
} // namespace KeireHub
