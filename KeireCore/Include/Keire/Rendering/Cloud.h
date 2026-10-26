#pragma once

#include "Keire/Rendering/VolumetricCloud.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace Keire
{
    inline constexpr std::size_t MaximumCloudLayers = 2;

    enum class CloudAppearance : std::uint8_t
    {
        LegacyUnlit = 0,
        NaturalLit = 1
    };

    /// Artistic horizontal sheets in one explicitly selected local east/up/north frame. Lengths are metres.
    struct CloudLayer
    {
        bool Enabled = false;
        double AltitudeMeters = 2000.0;
        double PatternSizeMeters = 12000.0;
        double Coverage = 0.5;
        double Opacity = 0.8;
        double EdgeSoftness = 0.15;
        double HorizonFade = 0.1;
        std::array<double, 2> WindMetersPerSecond{5.0, 0.0};
        std::array<double, 3> LinearColor{1.0, 1.0, 1.0};
        CloudAppearance Appearance = CloudAppearance::NaturalLit;
        std::uint32_t Seed = 1;

        auto operator<=>(const CloudLayer&) const noexcept = default;
    };

    struct CloudSettings
    {
        std::array<CloudLayer, MaximumCloudLayers> Layers;
        VolumetricCloudSettings Volume;

        auto operator<=>(const CloudSettings&) const noexcept = default;
    };
} // namespace Keire
