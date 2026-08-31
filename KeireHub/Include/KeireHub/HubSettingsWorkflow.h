#pragma once

#include "KeireHubRuntime/HubController.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace KeireHub
{
    [[nodiscard]] std::filesystem::path DefaultHubProjectLocation();
    [[nodiscard]] HubResult<HubSettings> ResetHubSettings(HubController& controller);
    [[nodiscard]] HubResult<HubSettings> ResetHubSettings(HubController& controller,
                                                          const std::filesystem::path& defaultProjectRoot);
    [[nodiscard]] HubStatus ClearHubVerifiedCache(HubController& controller);
    [[nodiscard]] HubStatus ClearHubVerifiedCache(HubController& controller, std::span<const HubTask> tasks);
    [[nodiscard]] HubStatus SaveHubProjectPreferences(HubController& controller, bool cards, std::uint8_t sortIndex);
    [[nodiscard]] HubStatus EnsureHubSettingsDirectory(const std::filesystem::path& path, std::string_view label);
} // namespace KeireHub
