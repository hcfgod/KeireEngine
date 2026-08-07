#pragma once

#include "KeireHubRuntime/HubSettingsStore.h"

#include <filesystem>

namespace KeireHub
{
    [[nodiscard]] std::filesystem::path RepairLegacyHubPath(const std::filesystem::path& path);
    [[nodiscard]] HubStatus MigrateLegacyHubPreferenceRoot(const std::filesystem::path& canonicalRoot);
    [[nodiscard]] HubStatus RepairLegacyHubStorageRoots(HubSettingsStore& settingsStore);
} // namespace KeireHub
