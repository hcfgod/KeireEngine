#include "KeireHub/HubConfiguration.h"

#include <stdexcept>

namespace KeireHub
{
    std::filesystem::path ResolveHubExecutablePath(const std::filesystem::path& executable)
    {
        if (executable.empty())
            throw std::invalid_argument("Hub executable resolution requires a path.");
        return std::filesystem::absolute(executable).lexically_normal();
    }

    std::filesystem::path ResolveHubConfigurationPath(const std::filesystem::path& executable,
                                                      const std::filesystem::path& filename)
    {
        if (filename.empty() || filename.is_absolute() || filename.has_parent_path())
            throw std::invalid_argument("Hub configuration resolution requires a filename.");

        const auto packaged = executable.parent_path().parent_path() / "Config" / filename;
        for (auto directory = executable.parent_path(); !directory.empty(); directory = directory.parent_path())
        {
            const auto candidate = directory / "Config" / filename;
            if (std::filesystem::is_regular_file(candidate))
                return candidate;
            if (directory == directory.root_path())
                break;
        }

        const auto development = std::filesystem::current_path() / "Config" / filename;
        return std::filesystem::is_regular_file(development) ? development : packaged;
    }
} // namespace KeireHub
