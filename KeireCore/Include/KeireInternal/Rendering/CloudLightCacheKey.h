#pragma once

#include "KeireInternal/Rendering/CloudVolumeViewUniforms.h"

#include <array>
#include <cstdint>

namespace Keire::RenderBackend
{
    /// Inputs to the three cached transmission orders, excluding view-dependent phase and radiance.
    /// Resource allocation/validity is separate: matching inputs never make an unbuilt buffer reusable.
    struct CloudLightCacheKey final
    {
        std::array<float, 4> Geometry{};
        std::array<float, 3> Density{};
        std::array<float, 4> PatternOriginAndOcclusion{};
        std::array<float, 4> Shape{};
        std::array<float, 4> HeightProfile{};
        std::array<float, 4> Weather{};
        std::array<float, 3> RegionalCoverageAndErosion{};
        std::array<float, 3> SunDirection{};
        std::array<float, 3> GridOrigin{};
        std::array<float, 3> CellSize{};
        std::array<std::uint32_t, 3> GridDimensions{};
        std::uint32_t Enabled = 0;
        std::uint32_t Seed = 0;
        std::uint32_t ShadowSteps = 0;
        bool AmbientOcclusionEnabled = false;
        bool SunlightEnabled = false;

        bool operator==(const CloudLightCacheKey&) const noexcept = default;
    };

    [[nodiscard]] inline CloudLightCacheKey
    MakeCloudLightCacheKey(const CloudVolumeViewUniforms& view, const std::array<float, 3>& gridOrigin,
                           const std::array<float, 3>& cellSize,
                           const std::array<std::uint32_t, 3>& gridDimensions) noexcept
    {
        const bool sunlightEnabled =
            view.SunRadiance.X > 0.0F || view.SunRadiance.Y > 0.0F || view.SunRadiance.Z > 0.0F;
        return {view.Volume.Geometry,
                {view.Volume.Density[0], view.Volume.Density[1], view.Volume.Density[2]},
                view.Volume.PatternOrigin,
                {view.ShapeControls.X, view.ShapeControls.Y, view.ShapeControls.Z, view.ShapeControls.W},
                {view.HeightProfile.X, view.HeightProfile.Y, view.HeightProfile.Z, view.HeightProfile.W},
                {view.WeatherControls.X, view.WeatherControls.Y, view.WeatherControls.Z, view.WeatherControls.W},
                {view.RegionalControls.X, view.RegionalControls.Y, view.RegionalControls.W},
                sunlightEnabled ? std::array<float, 3>{view.SunDirection.X, view.SunDirection.Y, view.SunDirection.Z}
                                : std::array<float, 3>{},
                gridOrigin,
                cellSize,
                gridDimensions,
                view.Volume.Quality[0],
                view.Volume.Quality[1],
                view.Volume.Quality[3],
                view.AmbientRadiance.W > 0.0F,
                sunlightEnabled};
    }
} // namespace Keire::RenderBackend
