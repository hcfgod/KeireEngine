#pragma once

#include "Keire/Math/Math.h"

#include <cmath>
#include <compare>
#include <limits>
#include <stdexcept>

namespace Keire
{
    // Frame-local coordinates; this value carries no world/frame identity.
    struct Vector3d
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;

        auto operator<=>(const Vector3d&) const noexcept = default;
    };

    namespace Math
    {
        [[nodiscard]] inline bool IsFinite(const Vector3d value) noexcept
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

        [[nodiscard]] inline Vector3d Widen(const Vector3 value) noexcept { return {value.X, value.Y, value.Z}; }

        // Subtract in double precision before converting to a bounded renderer/solver coordinate.
        // Both inputs must already belong to the same frame. The caller chooses the permitted error in metres.
        [[nodiscard]] inline Vector3 RelativeFloat(const Vector3d position, const Vector3d origin,
                                                   const double maximumError)
        {
            if (!IsFinite(position) || !IsFinite(origin) || !std::isfinite(maximumError) || maximumError < 0.0)
            {
                throw std::invalid_argument(
                    "Relative coordinates require finite positions and a nonnegative error budget.");
            }
            const auto convert = [maximumError](const double value)
            {
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                {
                    throw std::out_of_range("Relative coordinate exceeds the float range.");
                }
                const auto narrowed = static_cast<float>(value);
                if (std::abs(static_cast<double>(narrowed) - value) > maximumError)
                {
                    throw std::out_of_range("Relative coordinate exceeds its precision budget.");
                }
                return narrowed;
            };
            return {convert(position.X - origin.X), convert(position.Y - origin.Y), convert(position.Z - origin.Z)};
        }
    } // namespace Math
} // namespace Keire
