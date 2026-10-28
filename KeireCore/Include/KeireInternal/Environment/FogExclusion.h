#pragma once

#include "Keire/Rendering/Fog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace Keire::Detail
{
    inline void ValidateFogExclusion(const FogExclusionRegion& region)
    {
        if (!std::isfinite(region.EdgeFraction) || region.EdgeFraction < 0.0 || region.EdgeFraction > 1.0 ||
            !std::isfinite(region.Strength) || region.Strength < 0.0 || region.Strength > 1.0)
        {
            throw std::invalid_argument("Fog exclusion softness and strength must be within [0,1].");
        }
        for (std::size_t axis = 0; axis < region.Center.size(); ++axis)
        {
            if (!std::isfinite(region.Center[axis]) || !std::isfinite(region.HalfExtent[axis]) ||
                region.HalfExtent[axis] < 0.01 || region.HalfExtent[axis] > 1'000'000.0)
            {
                throw std::invalid_argument("Fog exclusion requires finite bounds with supported positive extents.");
            }
        }
    }

    // Validate regions once before sampling. Use the strongest exclusion, so duplicates do not deepen the cutout
    // and overlap is independent of authoring order. The result multiplies fog density, never cloud density.
    [[nodiscard]] inline double SampleFogExclusionRetention(std::span<const FogExclusionRegion> regions,
                                                            const std::array<double, 3>& position)
    {
        for (const double coordinate : position)
        {
            if (!std::isfinite(coordinate))
            {
                throw std::invalid_argument("Fog exclusion sample position must be finite.");
            }
        }
        double exclusion = 0.0;
        for (const auto& region : regions)
        {
            if (!region.Enabled || region.Strength == 0.0)
            {
                continue;
            }
            double distance = 0.0;
            for (std::size_t axis = 0; axis < position.size(); ++axis)
            {
                distance = std::max(distance, std::abs(position[axis] - region.Center[axis]) / region.HalfExtent[axis]);
            }
            if (distance >= 1.0)
            {
                continue;
            }
            const double edge =
                region.EdgeFraction == 0.0 ? 1.0 : std::clamp((1.0 - distance) / region.EdgeFraction, 0.0, 1.0);
            exclusion = std::max(exclusion, region.Strength * edge * edge * (3.0 - 2.0 * edge));
            if (exclusion == 1.0)
            {
                return 0.0;
            }
        }
        return 1.0 - exclusion;
    }
} // namespace Keire::Detail
