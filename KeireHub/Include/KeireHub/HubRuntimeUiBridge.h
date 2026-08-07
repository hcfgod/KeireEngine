#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/HubController.h"

#include <span>

namespace KeireHub
{
    [[nodiscard]] std::uint64_t HubNowUnixSeconds();
    void ApplyHubTaskSnapshot(std::span<const HubTask> tasks, HubProductSnapshot& product);
    void ApplyRuntimeSnapshot(const HubControllerSnapshot& runtime, HubProductSnapshot& product);
    [[nodiscard]] HubStatus ExecuteRuntimeUiCommand(HubController& controller, const HubUiCommand& command,
                                                    std::uint64_t nowUnixSeconds);
} // namespace KeireHub
