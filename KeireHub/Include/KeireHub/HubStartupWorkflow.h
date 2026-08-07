#pragma once

#include "KeireHubRuntime/HubController.h"

#include <cstdint>
#include <string_view>

namespace KeireHub
{
    [[nodiscard]] HubStatus PrepareHubStartupRuntime(HubController& controller, std::string_view hubVersion,
                                                     std::string_view configuredLogLevel, std::uint64_t nowUnixSeconds);
} // namespace KeireHub
