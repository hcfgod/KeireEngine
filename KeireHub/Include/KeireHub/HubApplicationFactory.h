#pragma once

#include "KeireHub/HubInstance.h"

#include "Keire/Application.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace KeireHub
{
    [[nodiscard]] constexpr bool ShouldHideHubAfterEditorLaunch(const bool keepRunning,
                                                                const bool trayAvailable) noexcept
    {
        return keepRunning && trayAvailable;
    }

    [[nodiscard]] std::unique_ptr<Keire::Application>
    CreateHubApplication(Keire::ApplicationSpecification specification, std::filesystem::path executable, bool smoke,
                         std::optional<HubActivationRequest> pendingStartupActivation,
                         std::shared_ptr<HubInstanceCoordinator> instance);
} // namespace KeireHub
