#pragma once

#include <cmath>

namespace Keire::Detail
{
    // Double precision retains sub-millimetre horizontal offsets through this domain.
    // Horizontal coordinates are reduced modulo the noise period before conversion to float.
    inline constexpr double MaximumCloudHorizontalDisplacementMetres = 1.0e12;
    inline constexpr double MaximumCloudVerticalDisplacementMetres = 1.0e9;

    [[nodiscard]] inline bool ValidCloudHorizontalCoordinate(const double value) noexcept
    {
        return std::isfinite(value) && std::abs(value) <= MaximumCloudHorizontalDisplacementMetres;
    }

    [[nodiscard]] inline bool ValidCloudVerticalCoordinate(const double value) noexcept
    {
        return std::isfinite(value) && std::abs(value) <= MaximumCloudVerticalDisplacementMetres;
    }
} // namespace Keire::Detail
