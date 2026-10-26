#pragma once

#include "KeireInternal/Spatial/SectorPosition.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace Keire::Detail
{
    // Internal format prototype. No native struct dumps, host endianness, or scene-schema changes.
    class SectorPositionCodec final
    {
      public:
        static constexpr std::size_t EncodedSize = 80;
        using Bytes = std::array<std::byte, EncodedSize>;

        [[nodiscard]] static Bytes Encode(const SectorPosition& position)
        {
            Bytes bytes{};
            Write(bytes, 0, FormatTag);
            Write(bytes, 8, std::bit_cast<std::uint64_t>(SectorCoordinate::SectorSize));
            Write(bytes, 16, position.Identity().World);
            Write(bytes, 24, position.Identity().Frame);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                Write(bytes, 32 + axis * 16, std::bit_cast<std::uint64_t>(position.Axes()[axis].Sector()));
                Write(bytes, 40 + axis * 16, std::bit_cast<std::uint64_t>(position.Axes()[axis].Offset()));
            }
            return bytes;
        }

        [[nodiscard]] static SectorPosition Decode(const std::span<const std::byte> bytes)
        {
            if (bytes.size() != EncodedSize)
            {
                throw std::invalid_argument("Spatial position record has an invalid size.");
            }
            if (Read(bytes, 0) != FormatTag ||
                Read(bytes, 8) != std::bit_cast<std::uint64_t>(SectorCoordinate::SectorSize))
            {
                throw std::invalid_argument("Unsupported spatial position format or sector size.");
            }
            std::array<std::int64_t, 3> sectors{};
            std::array<double, 3> offsets{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                sectors[axis] = std::bit_cast<std::int64_t>(Read(bytes, 32 + axis * 16));
                offsets[axis] = std::bit_cast<double>(Read(bytes, 40 + axis * 16));
                if (!std::isfinite(offsets[axis]) || offsets[axis] < -SectorCoordinate::HalfSector ||
                    offsets[axis] >= SectorCoordinate::HalfSector ||
                    (offsets[axis] == 0.0 && std::signbit(offsets[axis])))
                {
                    throw std::invalid_argument("Spatial position record contains a noncanonical offset.");
                }
            }
            return SectorPosition({Read(bytes, 16), Read(bytes, 24)}, sectors, offsets);
        }

      private:
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
        // ASCII KSP followed by version 1, with reserved upper bytes required to be zero.
        static constexpr std::uint64_t FormatTag = 0x0150534B;

        static void Write(Bytes& bytes, const std::size_t start, const std::uint64_t value)
        {
            for (std::size_t byte = 0; byte < 8; ++byte)
            {
                bytes[start + byte] = static_cast<std::byte>((value >> (byte * 8)) & 0xFFU);
            }
        }

        [[nodiscard]] static std::uint64_t Read(const std::span<const std::byte> bytes, const std::size_t start)
        {
            std::uint64_t value = 0;
            for (std::size_t byte = 0; byte < 8; ++byte)
            {
                value |= std::to_integer<std::uint64_t>(bytes[start + byte]) << (byte * 8);
            }
            return value;
        }
    };
} // namespace Keire::Detail
