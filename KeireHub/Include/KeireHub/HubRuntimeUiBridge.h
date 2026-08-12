#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/HubController.h"

#include <span>
#include <string_view>

namespace KeireHub
{
    [[nodiscard]] std::uint64_t HubNowUnixSeconds();
    [[nodiscard]] std::string_view HubTaskPhaseLabel(HubTaskKind kind, HubTaskState state) noexcept;
    void ApplyHubTaskSnapshot(std::span<const HubTask> tasks, HubProductSnapshot& product);
    void ApplyRuntimeSnapshot(const HubControllerSnapshot& runtime, HubProductSnapshot& product);
    [[nodiscard]] HubStatus ExecuteRuntimeUiCommand(HubController& controller, const HubUiCommand& command,
                                                    std::uint64_t nowUnixSeconds);
} // namespace KeireHub
