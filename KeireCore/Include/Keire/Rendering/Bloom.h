#pragma once

#include <compare>

namespace Keire
{
    /// Bloom consumes exposed, linear HDR before tone mapping; threshold is in that same exposure domain.
    struct BloomSettings
    {
        bool Enabled = false;
        double Intensity = 0.1;
        double Threshold = 1.0;
        /// Fraction of threshold used as the symmetric soft transition width; zero selects a hard threshold.
        double SoftKnee = 0.5;
        /// Gaussian standard deviation in pixels of the bloom buffer, not final display pixels.
        double Radius = 2.0;

        auto operator<=>(const BloomSettings&) const noexcept = default;
    };
} // namespace Keire
