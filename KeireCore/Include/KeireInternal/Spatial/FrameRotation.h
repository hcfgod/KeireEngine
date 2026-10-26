#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    // Double frame orientation; separate from float local mesh/solver rotations.
    class FrameRotation final
    {
      public:
        FrameRotation() = default;

        [[nodiscard]] static FrameRotation Normalize(const double x, const double y, const double z, const double w)
        {
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w))
            {
                throw std::invalid_argument("Frame rotation must be finite.");
            }
            const double scale = std::max({std::abs(x), std::abs(y), std::abs(z), std::abs(w)});
            if (scale == 0.0)
            {
                throw std::invalid_argument("Frame rotation must be nonzero.");
            }
            // Scaling first handles both near-maximum doubles and subnormal nonzero inputs.
            const double sx = x / scale;
            const double sy = y / scale;
            const double sz = z / scale;
            const double sw = w / scale;
            const double length = std::sqrt(sx * sx + sy * sy + sz * sz + sw * sw);
            return FrameRotation({sx / length, sy / length, sz / length, sw / length});
        }

        [[nodiscard]] const std::array<double, 4>& Components() const noexcept { return m_Value; }

        [[nodiscard]] FrameRotation Inverse() const noexcept
        {
            return FrameRotation({-m_Value[0], -m_Value[1], -m_Value[2], m_Value[3]});
        }

        // Apply child first, then this orientation (active right-handed rotations).
        [[nodiscard]] FrameRotation Compose(const FrameRotation& child) const
        {
            const auto& a = m_Value;
            const auto& b = child.m_Value;
            return Normalize(a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                             a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                             a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                             a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]);
        }

        [[nodiscard]] std::array<double, 3> Apply(const std::array<double, 3>& displacement) const
        {
            if (!std::isfinite(displacement[0]) || !std::isfinite(displacement[1]) || !std::isfinite(displacement[2]))
            {
                throw std::invalid_argument("Frame displacement must be finite.");
            }
            const double x = m_Value[0];
            const double y = m_Value[1];
            const double z = m_Value[2];
            const double w = m_Value[3];
            const std::array result{
                (1.0 - 2.0 * (y * y + z * z)) * displacement[0] + 2.0 * (x * y - z * w) * displacement[1] +
                    2.0 * (x * z + y * w) * displacement[2],
                2.0 * (x * y + z * w) * displacement[0] + (1.0 - 2.0 * (x * x + z * z)) * displacement[1] +
                    2.0 * (y * z - x * w) * displacement[2],
                2.0 * (x * z - y * w) * displacement[0] + 2.0 * (y * z + x * w) * displacement[1] +
                    (1.0 - 2.0 * (x * x + y * y)) * displacement[2]};
            for (const auto value : result)
            {
                if (!std::isfinite(value))
                {
                    throw std::out_of_range("Rotated frame displacement exceeds double range.");
                }
            }
            return result;
        }

      private:
        explicit FrameRotation(const std::array<double, 4>& value) : m_Value(value) {}

        std::array<double, 4> m_Value{0.0, 0.0, 0.0, 1.0};
    };
} // namespace Keire::Detail
