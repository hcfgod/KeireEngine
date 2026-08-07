#pragma once

#include "KeireHubRuntime/HubError.h"

#include <filesystem>

namespace KeireHub
{
    // These probes perform bounded filesystem work and are intended for Hub workers, never per-frame UI code.
    [[nodiscard]] HubResult<bool> ProbeProjectLock(const std::filesystem::path& root);
    [[nodiscard]] HubResult<bool> ProbeProjectRecovery(const std::filesystem::path& root);
} // namespace KeireHub
