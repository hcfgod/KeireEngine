#pragma once

#include "KeireInternal/Spatial/SectorCoordinate.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace Keire::Detail
{
    // Logical coordinate-space keys, not Entity::World() scene pointers or live ownership handles.
    // A world service must resolve these keys and validate its runtime incarnation before use.
    struct SpatialIdentity final
    {
        std::uint64_t World = 0;
        std::uint64_t Frame = 0;

        bool operator==(const SpatialIdentity&) const noexcept = default;
    };

    // Value-only foundation. Frame lookup, lifetime and transformations remain owned by the future world service.
    class SectorPosition final
    {
      public:
        SectorPosition(const SpatialIdentity identity, const std::array<std::int64_t, 3>& sectors,
                       const std::array<double, 3>& offsets)
            : m_Identity(identity), m_Axes{SectorCoordinate::Normalize(sectors[0], offsets[0]),
                                           SectorCoordinate::Normalize(sectors[1], offsets[1]),
                                           SectorCoordinate::Normalize(sectors[2], offsets[2])}
        {
            if (identity.World == 0 || identity.Frame == 0)
            {
                throw std::invalid_argument("Spatial position requires nonzero world and frame identities.");
            }
        }

        [[nodiscard]] SpatialIdentity Identity() const noexcept { return m_Identity; }

        [[nodiscard]] const std::array<SectorCoordinate, 3>& Axes() const noexcept { return m_Axes; }

        [[nodiscard]] SectorPosition Translated(const std::array<double, 3>& displacement) const
        {
            auto result = *this;
            for (std::size_t axis = 0; axis < m_Axes.size(); ++axis)
            {
                result.m_Axes[axis] = m_Axes[axis].Translated(displacement[axis]);
            }
            return result;
        }

        [[nodiscard]] std::array<double, 3> RelativeTo(const SectorPosition& origin) const
        {
            if (m_Identity != origin.m_Identity)
            {
                throw std::invalid_argument("Positions require an explicit conversion between worlds or frames.");
            }
            return {m_Axes[0].RelativeTo(origin.m_Axes[0]), m_Axes[1].RelativeTo(origin.m_Axes[1]),
                    m_Axes[2].RelativeTo(origin.m_Axes[2])};
        }

        bool operator==(const SectorPosition&) const noexcept = default;

      private:
        SpatialIdentity m_Identity;
        std::array<SectorCoordinate, 3> m_Axes;
    };
} // namespace Keire::Detail
