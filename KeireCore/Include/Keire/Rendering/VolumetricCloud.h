#pragma once

#include <array>
#include <compare>
#include <cstdint>

namespace Keire
{
    /// Local-frame volumetric cloud authoring values. Lengths are metres; extinction is inverse metres.
    /// Separate from artistic cloud sheets. Disabled by default for existing projects.
    struct VolumetricCloudSettings
    {
        bool Enabled = false;
        double BaseHeightMeters = 1500.0;
        double ThicknessMeters = 1200.0;
        double PatternSizeMeters = 3000.0;
        double Coverage = 0.6;
        double ExtinctionPerMeter = 0.004;
        double DetailAmount = 0.2;
        double Anisotropy = 0.65;
        std::uint32_t Seed = 1;
        std::array<double, 3> WindMetersPerSecond{5.0, 0.0, 0.0};
        std::array<double, 3> ScatteringAlbedo{0.95, 0.95, 0.95};
        std::uint32_t ViewSteps = 48;
        std::uint32_t ShadowSteps = 6;
        double MaximumDistanceMeters = 100000.0;

        auto operator<=>(const VolumetricCloudSettings&) const noexcept = default;
    };
} // namespace Keire
