#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/HubTaskStore.h"

#include <filesystem>
#include <span>

namespace KeireHub
{
    [[nodiscard]] HubStatus ValidateVerifiedPackageCacheClear(std::span<const HubTask> tasks);
    [[nodiscard]] HubStatus ClearVerifiedPackageCache(const std::filesystem::path& cacheRoot);
} // namespace KeireHub
