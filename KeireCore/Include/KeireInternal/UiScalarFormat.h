#pragma once

#include <cmath>

namespace Keire::Detail
{
    // Keep common physical coefficients and planet dimensions readable without
    // changing the stored value. Extreme magnitudes still need compact notation.
    [[nodiscard]] inline const char* ScalarDisplayFormat(const double value)
    {
        const double magnitude = std::abs(value);
        if (magnitude >= 1.0e6 && magnitude < 1.0e15)
        {
            return "%.3f";
        }
        if (magnitude >= 1.0e-9 && magnitude < 1.0e-4)
        {
            return "%.9f";
        }
        if (magnitude >= 1.0e-12 && magnitude < 1.0e-9)
        {
            return "%.12f";
        }
        return "%.6g";
    }
} // namespace Keire::Detail
