#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <limits>

namespace Keire
{
    template <typename Tag> class StableHandle final
    {
      public:
        constexpr StableHandle() noexcept = default;

        [[nodiscard]] static constexpr StableHandle FromParts(const std::uint32_t index,
                                                              const std::uint32_t generation) noexcept
        {
            return StableHandle(index, generation);
        }

        [[nodiscard]] static constexpr StableHandle FromValue(const std::uint64_t value) noexcept
        {
            return StableHandle(static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(value >> 32U));
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_Index != std::numeric_limits<std::uint32_t>::max() && m_Generation != 0;
        }
        [[nodiscard]] constexpr std::uint32_t Index() const noexcept { return m_Index; }
        [[nodiscard]] constexpr std::uint32_t Generation() const noexcept { return m_Generation; }
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept
        {
            return static_cast<std::uint64_t>(m_Index) | (static_cast<std::uint64_t>(m_Generation) << 32U);
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
        [[nodiscard]] constexpr auto operator<=>(const StableHandle&) const noexcept = default;

      private:
        constexpr StableHandle(const std::uint32_t index, const std::uint32_t generation) noexcept
            : m_Index(index), m_Generation(generation)
        {
        }

        std::uint32_t m_Index = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t m_Generation = 0;
    };
} // namespace Keire

template <typename Tag> struct std::hash<Keire::StableHandle<Tag>>
{
    [[nodiscard]] std::size_t operator()(const Keire::StableHandle<Tag> value) const noexcept
    {
        return std::hash<std::uint64_t>{}(value.Value());
    }
};
