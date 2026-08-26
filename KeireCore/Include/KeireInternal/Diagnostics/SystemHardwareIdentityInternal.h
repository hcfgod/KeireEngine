#pragma once

#include <cstdint>
#include <string>

namespace Keire::Internal
{
    struct SystemHardwareIdentity final
    {
        std::string OperatingSystemDescription;
        std::string OperatingSystemVersion;
        std::string CpuModel;
        std::uint32_t LogicalProcessorCount = 1;
        std::uint64_t PhysicalMemoryBytes = 0;
    };

    [[nodiscard]] SystemHardwareIdentity QuerySystemHardwareIdentity();
} // namespace Keire::Internal
