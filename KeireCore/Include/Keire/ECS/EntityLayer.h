#pragma once

#include <cstddef>
#include <cstdint>

namespace Keire
{
    inline constexpr std::size_t EntityLayerCount = 32;

    [[nodiscard]] constexpr bool IsValidEntityLayer(const std::uint32_t layer) noexcept
    {
        return layer < EntityLayerCount;
    }

    [[nodiscard]] constexpr std::uint32_t EntityLayerBit(const std::uint32_t layer) noexcept
    {
        return IsValidEntityLayer(layer) ? (1U << layer) : 0U;
    }
} // namespace Keire
