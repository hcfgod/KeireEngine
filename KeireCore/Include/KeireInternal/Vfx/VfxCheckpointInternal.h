#pragma once

#include "Keire/Assets/Asset.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace Keire::Detail
{
    inline constexpr std::array<char, 8> VfxCheckpointMagic{'K', 'R', 'V', 'F', 'X', 'C', 'P', '\0'};
    inline constexpr std::uint32_t VfxCheckpointVersion = 1;

    class VfxCheckpointWriter final
    {
      public:
        template <typename T> void Unsigned(const T value)
        {
            static_assert(std::is_unsigned_v<T>);
            for (std::size_t index = 0; index < sizeof(T); ++index)
                Bytes.push_back(static_cast<std::byte>(value >> (index * 8U)));
        }

        void Boolean(const bool value) { Unsigned<std::uint8_t>(value ? 1U : 0U); }
        void Float(const float value) { Unsigned(std::bit_cast<std::uint32_t>(value)); }
        void Double(const double value) { Unsigned(std::bit_cast<std::uint64_t>(value)); }
        void Id(const AssetId value)
        {
            Unsigned(value.High());
            Unsigned(value.Low());
        }

        std::vector<std::byte> Bytes;
    };

    class VfxCheckpointReader final
    {
      public:
        explicit VfxCheckpointReader(const std::span<const std::byte> bytes) : m_Bytes(bytes) {}

        template <typename T> [[nodiscard]] T Unsigned()
        {
            static_assert(std::is_unsigned_v<T>);
            if (m_Offset > m_Bytes.size() || sizeof(T) > m_Bytes.size() - m_Offset)
                throw std::runtime_error("VFX checkpoint is truncated.");
            std::uintmax_t result = 0;
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                result |= static_cast<std::uintmax_t>(std::to_integer<std::uint8_t>(m_Bytes[m_Offset++]))
                          << (index * 8U);
            }
            return static_cast<T>(result);
        }

        [[nodiscard]] bool Boolean()
        {
            const auto value = Unsigned<std::uint8_t>();
            if (value > 1U)
                throw std::runtime_error("VFX checkpoint contains an invalid Boolean.");
            return value != 0U;
        }
        [[nodiscard]] float Float() { return std::bit_cast<float>(Unsigned<std::uint32_t>()); }
        [[nodiscard]] double Double() { return std::bit_cast<double>(Unsigned<std::uint64_t>()); }
        [[nodiscard]] AssetId Id() { return {Unsigned<std::uint64_t>(), Unsigned<std::uint64_t>()}; }
        [[nodiscard]] bool Complete() const noexcept { return m_Offset == m_Bytes.size(); }

      private:
        std::span<const std::byte> m_Bytes;
        std::size_t m_Offset = 0;
    };
} // namespace Keire::Detail
