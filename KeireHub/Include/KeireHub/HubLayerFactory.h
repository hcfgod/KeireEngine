#pragma once

#include "Keire/Layer.h"

#include "KeireHub/HubInstance.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace KeireHub::Detail
{
    [[nodiscard]] std::unique_ptr<Keire::Layer>
    CreateHubLayer(std::filesystem::path executable, bool smoke,
                   std::optional<HubActivationRequest> pendingStartupActivation,
                   std::shared_ptr<HubInstanceCoordinator> instance);
} // namespace KeireHub::Detail
