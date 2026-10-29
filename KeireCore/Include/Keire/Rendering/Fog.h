#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Keire
{
    inline constexpr std::size_t MaximumLocalFogVolumes = 16;
    inline constexpr std::size_t MaximumFogExclusionRegions = 16;

    /// Removes fog inside a box. Overlaps use the strongest exclusion rather than accumulating strength.
    struct FogExclusionRegion
    {
        std::array<double, 3> Center{};
        std::array<double, 3> HalfExtent{10.0, 10.0, 10.0};
        double EdgeFraction = 0.2;
        double Strength = 1.0;
        bool Enabled = true;

        auto operator<=>(const FogExclusionRegion&) const noexcept = default;
    };

    /// Axis-aligned box in the environment's selected local metre frame.
    struct LocalFogVolume
    {
        std::array<double, 3> Center{};
        std::array<double, 3> HalfExtent{10.0, 10.0, 10.0};
        double ExtinctionPerMeter = 0.02;
        double EdgeFraction = 0.2;
        bool Enabled = true;

        auto operator<=>(const LocalFogVolume&) const noexcept = default;
    };

    /// Authored height and local fog, shared by Environment controls and the runtime volume renderer.
    struct FogSettings
    {
        bool Enabled = false;
        double BaseHeightMeters = 0.0;
        double HeightFalloffPerMeter = 0.01;
        double ExtinctionPerMeter = 0.002;
        double MaximumDistanceMeters = 10'000.0;
        std::array<double, 3> ScatteringAlbedo{0.9, 0.9, 0.9};
        double Anisotropy = 0.2;
        std::uint32_t ViewSteps = 64;
        std::vector<LocalFogVolume> LocalVolumes;
        std::vector<FogExclusionRegion> ExclusionRegions;
        /// Measure height above the environment atmosphere's planet instead of the flat world-Y plane.
        bool PlanetaryHeight = false;

        auto operator<=>(const FogSettings&) const noexcept = default;
    };
} // namespace Keire
