#pragma once

#include "Keire/Math/Math.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Keire::RenderBackend
{
    struct ForwardPlusLightBounds final
    {
        Vector3 ViewPosition;
        float Range = 0.0F;
    };

    struct ForwardPlusTileGrid final
    {
        static constexpr std::uint32_t TileSize = 16;
        static constexpr std::uint32_t MaximumLightsPerTile = 128;

        std::uint32_t Columns = 0;
        std::uint32_t Rows = 0;
        std::vector<std::uint32_t> Offsets;
        std::vector<std::uint16_t> Counts;
        std::vector<std::uint32_t> LightIndices;
        std::uint32_t OverflowedTiles = 0;
    };

    [[nodiscard]] ForwardPlusTileGrid BuildForwardPlusCpuTiles(std::uint32_t width, std::uint32_t height,
                                                               const Matrix4& projection,
                                                               std::span<const ForwardPlusLightBounds> lights);
} // namespace Keire::RenderBackend
