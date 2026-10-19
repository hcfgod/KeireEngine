#pragma once

#include <string_view>

namespace KeireHub::Detail
{
    [[nodiscard]] constexpr std::string_view EditorCatalogEmptyMessage(const bool refreshing, const bool failed,
                                                                       const bool filtered) noexcept
    {
        if (refreshing)
        {
            return "Loading verified editor catalogs...";
        }
        if (failed)
        {
            return "Editor releases could not be loaded. Review the catalog error "
                   "above. "
                   "Installed editors and local projects remain available.";
        }
        if (filtered)
        {
            return "No verified editor release matches these filters. Clear the search "
                   "or select All channels.";
        }
        return "No verified editor releases are available for this platform.";
    }
} // namespace KeireHub::Detail
