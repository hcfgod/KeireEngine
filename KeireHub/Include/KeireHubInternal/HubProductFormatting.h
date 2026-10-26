#pragma once

#include <cstdint>
#include <string>

namespace KeireHub
{
    struct HubTaskUiRecord;

    namespace Detail
    {
        [[nodiscard]] std::string FormatHubBytes(std::uint64_t bytes);
        [[nodiscard]] std::string TaskActivityDetails(const HubTaskUiRecord& task);
    } // namespace Detail
} // namespace KeireHub
