#pragma once

#include <string>
#include <string_view>

namespace KeireHub
{
    [[nodiscard]] inline std::string PackageVersionLabel(std::string_view name, std::string_view version)
    {
        if (version.empty() || name == version ||
            (name.size() > version.size() && name.ends_with(version) && name[name.size() - version.size() - 1] == ' '))
        {
            return std::string(name);
        }
        if (name.empty())
        {
            return std::string(version);
        }
        return std::string(name) + " " + std::string(version);
    }
} // namespace KeireHub
