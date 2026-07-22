#include "KeireInternal/Rendering/ForwardPlusInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Keire::RenderBackend
{
    ForwardPlusTileGrid BuildForwardPlusCpuTiles(const std::uint32_t width, const std::uint32_t height,
                                                 const Matrix4& projection,
                                                 const std::span<const ForwardPlusLightBounds> lights)
    {
        if (width == 0 || height == 0 || width > 16384 || height > 16384 || !Math::IsFinite(projection))
            throw std::invalid_argument("Forward+ tile dimensions or projection are invalid.");
        if (lights.size() > 4096)
            throw std::invalid_argument("Forward+ supports at most 4096 visible local lights.");

        ForwardPlusTileGrid result;
        result.Columns = (width + ForwardPlusTileGrid::TileSize - 1U) / ForwardPlusTileGrid::TileSize;
        result.Rows = (height + ForwardPlusTileGrid::TileSize - 1U) / ForwardPlusTileGrid::TileSize;
        const auto tileCount = static_cast<std::size_t>(result.Columns) * result.Rows;
        std::vector<std::vector<std::uint32_t>> tiles(tileCount);
        std::vector<bool> overflowed(tileCount);
        const float projectionX = std::abs(projection.Elements[0]);
        const float projectionY = std::abs(projection.Elements[5]);

        for (std::uint32_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex)
        {
            const auto& light = lights[lightIndex];
            if (!Math::IsFinite(light.ViewPosition) || !std::isfinite(light.Range) || light.Range <= 0.0F)
                throw std::invalid_argument("Forward+ light bounds are invalid.");
            if (light.ViewPosition.Z + light.Range <= 0.0F)
                continue;
            const float depth = std::max(light.ViewPosition.Z, 0.0001F);
            const float centerX = (light.ViewPosition.X * projectionX / depth * 0.5F + 0.5F) * width;
            const float centerY = (-light.ViewPosition.Y * projectionY / depth * 0.5F + 0.5F) * height;
            const float radiusX = light.Range * projectionX / depth * width * 0.5F;
            const float radiusY = light.Range * projectionY / depth * height * 0.5F;
            const auto clampTile = [](const float value, const std::uint32_t maximum)
            { return static_cast<std::uint32_t>(std::clamp(value, 0.0F, static_cast<float>(maximum - 1U))); };
            const auto minimumX =
                clampTile(std::floor((centerX - radiusX) / ForwardPlusTileGrid::TileSize), result.Columns);
            const auto maximumX =
                clampTile(std::floor((centerX + radiusX) / ForwardPlusTileGrid::TileSize), result.Columns);
            const auto minimumY =
                clampTile(std::floor((centerY - radiusY) / ForwardPlusTileGrid::TileSize), result.Rows);
            const auto maximumY =
                clampTile(std::floor((centerY + radiusY) / ForwardPlusTileGrid::TileSize), result.Rows);
            if (centerX + radiusX < 0.0F || centerY + radiusY < 0.0F || centerX - radiusX >= width ||
                centerY - radiusY >= height)
                continue;
            for (auto y = minimumY; y <= maximumY; ++y)
            {
                for (auto x = minimumX; x <= maximumX; ++x)
                {
                    const auto tileIndex = static_cast<std::size_t>(y) * result.Columns + x;
                    if (tiles[tileIndex].size() < ForwardPlusTileGrid::MaximumLightsPerTile)
                        tiles[tileIndex].push_back(lightIndex);
                    else
                        overflowed[tileIndex] = true;
                }
            }
        }

        result.Offsets.reserve(tileCount);
        result.Counts.reserve(tileCount);
        for (std::size_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
        {
            result.Offsets.push_back(static_cast<std::uint32_t>(result.LightIndices.size()));
            result.Counts.push_back(static_cast<std::uint16_t>(tiles[tileIndex].size()));
            result.LightIndices.insert(result.LightIndices.end(), tiles[tileIndex].begin(), tiles[tileIndex].end());
            result.OverflowedTiles += overflowed[tileIndex] ? 1U : 0U;
        }
        return result;
    }
} // namespace Keire::RenderBackend
