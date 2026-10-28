#pragma once

#include "KeireInternal/Rendering/VolumetricCloudUniforms.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace Keire::RenderBackend
{
    struct CloudLightCacheGrid final
    {
        std::array<float, 3> Origin{};
        std::array<float, 3> CellSize{};
        std::array<std::uint32_t, 3> Dimensions{129U, 33U, 129U};
    };

    // Build in a horizontal cell-aligned frame with the cloud base at zero. Lookup stays camera-relative.
    // Requires the validated grid returned by MakeCloudLightCacheGrid.
    inline void AlignCloudLightCacheBuild(VolumetricCloudUniforms& volume, CloudLightCacheGrid& grid,
                                          const std::optional<float> cloudBasePatternPhase = std::nullopt) noexcept
    {
        const double patternSize = volume.Geometry[2];
        for (const std::size_t axis : {0U, 2U})
        {
            const double cellSize = grid.CellSize[axis];
            const double phaseMeters = static_cast<double>(volume.PatternOrigin[axis]) * patternSize;
            const double cell = std::floor(phaseMeters / cellSize);
            volume.PatternOrigin[axis] = static_cast<float>(cell * cellSize / patternSize);
            grid.Origin[axis] = -static_cast<float>((grid.Dimensions[axis] - 1U) / 2U) * grid.CellSize[axis];
        }
        const double baseHeight = volume.Geometry[0];
        double verticalPhase = std::fmod(volume.PatternOrigin[1] + baseHeight / patternSize, 256.0);
        if (verticalPhase < 0.0)
        {
            verticalPhase += 256.0;
        }
        volume.PatternOrigin[1] = static_cast<float>(verticalPhase);
        // Production capture supplies the phase evaluated at the cloud base in double precision.
        // Reconstructing it from two camera-relative floats makes vertical motion invalidate the cache.
        if (cloudBasePatternPhase)
        {
            volume.PatternOrigin[1] = *cloudBasePatternPhase;
        }
        if (volume.PatternOrigin[1] >= 256.0F)
        {
            volume.PatternOrigin[1] = 0.0F;
        }
        grid.Origin[1] = static_cast<float>(grid.Origin[1] - baseHeight);
        volume.Geometry[0] = 0.0F;
    }

    [[nodiscard]] inline std::optional<CloudLightCacheGrid>
    MakeCloudLightCacheGrid(const VolumetricCloudUniforms& volume, const float baseHeightVariation = 0.0F) noexcept
    {
        if (!std::isfinite(baseHeightVariation) || baseHeightVariation < 0.0F || baseHeightVariation > 100000.0F)
        {
            return std::nullopt;
        }
        const double patternSize = volume.Geometry[2];
        const double thickness = static_cast<double>(volume.Geometry[1]) + 2.0 * baseHeightVariation;
        if (!std::isfinite(patternSize) || patternSize <= 0.0 || !std::isfinite(thickness) || thickness <= 0.0 ||
            !std::isfinite(volume.Geometry[0]) || volume.Geometry[1] <= 0.0F)
        {
            return std::nullopt;
        }

        CloudLightCacheGrid grid;
        grid.CellSize = {static_cast<float>(patternSize / 32.0), static_cast<float>(thickness / 32.0),
                         static_cast<float>(patternSize / 32.0)};
        grid.Origin[1] = volume.Geometry[0] - baseHeightVariation;
        for (const std::size_t axis : {0U, 2U})
        {
            const double phase = volume.PatternOrigin[axis];
            if (!std::isfinite(phase) || grid.CellSize[axis] <= 0.0F)
            {
                return std::nullopt;
            }

            // Snap in advected cloud coordinates, not view coordinates. The 256-pattern phase wrap is
            // an integer number of cells, so crossing that wrap preserves the lattice alignment.
            const double cellSize = grid.CellSize[axis];
            double offset = std::fmod(phase * patternSize, cellSize);
            if (offset < 0.0)
            {
                offset += cellSize;
            }
            grid.Origin[axis] = static_cast<float>(-64.0 * cellSize - offset);
            if (!std::isfinite(grid.Origin[axis]))
            {
                return std::nullopt;
            }
        }

        if (grid.CellSize[1] <= 0.0F)
        {
            return std::nullopt;
        }
        return grid;
    }
} // namespace Keire::RenderBackend
