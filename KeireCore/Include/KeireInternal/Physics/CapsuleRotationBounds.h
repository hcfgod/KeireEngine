#pragma once

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace Keire::Detail
{
    /// Convert an envelope dimension without shrinking it at the float physics boundary.
    [[nodiscard]] inline float RoundCapsuleBoundOutward(const double bound)
    {
        if (!std::isfinite(bound) || bound < 0.0)
        {
            throw std::invalid_argument("Capsule bounds require a finite nonnegative dimension.");
        }
        if (bound > static_cast<double>(std::numeric_limits<float>::max()))
        {
            throw std::overflow_error("Capsule bound exceeds the physics coordinate range.");
        }

        const float rounded = static_cast<float>(bound);
        if (static_cast<double>(rounded) < bound)
        {
            return std::nextafter(rounded, std::numeric_limits<float>::max());
        }
        return rounded;
    }

    /// Covers a capsule's centerline throughout an interval around a sampled midpoint pose.
    /// centerExcursion bounds center motion from that midpoint, including any off-center pivot arc.
    /// angularExcursion is the maximum orientation deviation from the midpoint, in radians.
    [[nodiscard]] inline double CapsuleRotationInflation(const double halfCylinderLength, const double angularExcursion,
                                                         const double centerExcursion)
    {
        if (!std::isfinite(halfCylinderLength) || halfCylinderLength < 0.0 || !std::isfinite(angularExcursion) ||
            angularExcursion < 0.0 || angularExcursion > std::numbers::pi || !std::isfinite(centerExcursion) ||
            centerExcursion < 0.0)
        {
            throw std::invalid_argument("Capsule rotation bounds require finite nonnegative dimensions and angle.");
        }

        // A centerline point at radius L moves at most 2 L sin(theta / 2). The spherical caps are
        // rotationally invariant; only the segment and center motion require additional inflation.
        const double angularScale = 2.0 * std::sin(angularExcursion * 0.5);
        const double inflation = centerExcursion + halfCylinderLength * angularScale;
        if (!std::isfinite(inflation))
        {
            throw std::overflow_error("Capsule rotation inflation exceeds finite range.");
        }
        return inflation;
    }
} // namespace Keire::Detail
