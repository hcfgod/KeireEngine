#pragma once

#include <cstdint>

namespace KeireHub
{
    inline constexpr std::uint32_t MinimumProjectWorkflowSchemaVersion = 3;
    inline constexpr std::uint32_t CurrentProjectSchemaVersion = 4;

    [[nodiscard]] constexpr bool IsProjectWorkflowSchemaSupported(const std::uint32_t version) noexcept
    {
        return version >= MinimumProjectWorkflowSchemaVersion && version <= CurrentProjectSchemaVersion;
    }
} // namespace KeireHub
