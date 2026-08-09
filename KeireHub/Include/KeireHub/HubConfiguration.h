#pragma once

#include <filesystem>

namespace KeireHub
{
    [[nodiscard]] std::filesystem::path ResolveHubExecutablePath(const std::filesystem::path& executable);
    [[nodiscard]] std::filesystem::path ResolveHubConfigurationPath(const std::filesystem::path& executable,
                                                                    const std::filesystem::path& filename);
} // namespace KeireHub
