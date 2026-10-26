#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace Keire::Detail
{
    // Internal numerical foundation; not yet a scene, frame, or public Transform contract.
    class SectorCoordinate final
    {
      public:
        static constexpr double SectorSize = 65536.0;
        static constexpr double HalfSector = SectorSize / 2.0;

        [[nodiscard]] static SectorCoordinate Normalize(std::int64_t sector, const double offset)
        {
            if (!std::isfinite(offset))
            {
                throw std::invalid_argument("Sector offset must be finite.");
            }
            const double quotient = std::trunc(offset / SectorSize);
            // The positive bound is exclusive: INT64_MAX rounds to 2^63 as a double.
            constexpr double integerLimit = 9223372036854775808.0;
            if (quotient < -integerLimit || quotient >= integerLimit)
            {
                throw std::out_of_range("Sector offset carry exceeds the supported integer range.");
            }
            auto carry = static_cast<std::int64_t>(quotient);
            double remainder = std::fmod(offset, SectorSize);
            if (remainder >= HalfSector)
            {
                carry = CheckedAdd(carry, 1);
                remainder -= SectorSize;
            }
            else if (remainder < -HalfSector)
            {
                carry = CheckedAdd(carry, -1);
                remainder += SectorSize;
            }
            sector = CheckedAdd(sector, carry);
            return SectorCoordinate(sector, remainder == 0.0 ? 0.0 : remainder);
        }

        [[nodiscard]] std::int64_t Sector() const noexcept { return m_Sector; }

        [[nodiscard]] double Offset() const noexcept { return m_Offset; }

        [[nodiscard]] SectorCoordinate Translated(const double displacement) const
        {
            // Split movement before addition: adding a huge displacement directly would erase the local offset.
            const auto movement = Normalize(0, displacement);
            const auto local = Normalize(0, m_Offset + movement.m_Offset);
            const auto carry = CheckedAdd(movement.m_Sector, local.m_Sector);
            return SectorCoordinate(CheckedAdd(m_Sector, carry), local.m_Offset);
        }

        // Subtract integer sectors before conversion so adjacent sectors remain precise even near INT64 limits.
        // A distant result is still a double displacement, with the usual magnitude-dependent precision limit.
        [[nodiscard]] double RelativeTo(const SectorCoordinate& origin) const noexcept
        {
            const bool negative = m_Sector < origin.m_Sector;
            const auto magnitude =
                negative ? static_cast<std::uint64_t>(origin.m_Sector) - static_cast<std::uint64_t>(m_Sector)
                         : static_cast<std::uint64_t>(m_Sector) - static_cast<std::uint64_t>(origin.m_Sector);
            const double sectorDifference = static_cast<double>(magnitude) * (negative ? -SectorSize : SectorSize);
            return sectorDifference + (m_Offset - origin.m_Offset);
        }

        bool operator==(const SectorCoordinate&) const noexcept = default;

      private:
        SectorCoordinate(const std::int64_t sector, const double offset) : m_Sector(sector), m_Offset(offset) {}

        [[nodiscard]] static std::int64_t CheckedAdd(const std::int64_t value, const std::int64_t delta)
        {
            if ((delta > 0 && value > std::numeric_limits<std::int64_t>::max() - delta) ||
                (delta < 0 && value < std::numeric_limits<std::int64_t>::min() - delta))
            {
                throw std::out_of_range("Sector address overflow.");
            }
            return value + delta;
        }

        std::int64_t m_Sector;
        double m_Offset;
    };
} // namespace Keire::Detail
