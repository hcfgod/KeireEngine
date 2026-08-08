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
        struct ProjectedLight
        {
            std::uint32_t Index = 0;
            std::uint32_t MinimumX = 0;
            std::uint32_t MaximumX = 0;
            std::uint32_t MinimumY = 0;
            std::uint32_t MaximumY = 0;
        };

        std::vector<ProjectedLight> projectedLights;
        projectedLights.reserve(lights.size());
        const float projectionX = std::abs(projection.Elements[0]);
        const float projectionY = std::abs(projection.Elements[5]);
        const float gridWidth = static_cast<float>(width);
        const float gridHeight = static_cast<float>(height);

        for (std::uint32_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex)
        {
            const auto& light = lights[lightIndex];
            if (!Math::IsFinite(light.ViewPosition) || !std::isfinite(light.Range) || light.Range <= 0.0F)
                throw std::invalid_argument("Forward+ light bounds are invalid.");
            if (light.ViewPosition.Z + light.Range <= 0.0F)
                continue;
            const float depth = std::max(light.ViewPosition.Z, 0.0001F);
            const float centerX = (light.ViewPosition.X * projectionX / depth * 0.5F + 0.5F) * gridWidth;
            const float centerY = (-light.ViewPosition.Y * projectionY / depth * 0.5F + 0.5F) * gridHeight;
            const float radiusX = light.Range * projectionX / depth * gridWidth * 0.5F;
            const float radiusY = light.Range * projectionY / depth * gridHeight * 0.5F;
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
            if (centerX + radiusX < 0.0F || centerY + radiusY < 0.0F || centerX - radiusX >= gridWidth ||
                centerY - radiusY >= gridHeight)
                continue;
            projectedLights.push_back({lightIndex, minimumX, maximumX, minimumY, maximumY});
        }

        if (projectedLights.empty())
        {
            result.Offsets.assign(tileCount, 0);
            result.Counts.assign(tileCount, 0);
            return result;
        }
        if (projectedLights.size() == 1)
        {
            const auto& light = projectedLights.front();
            const auto coveredColumns =
                static_cast<std::size_t>(light.MaximumX) - static_cast<std::size_t>(light.MinimumX) + 1U;
            const auto coveredRows =
                static_cast<std::size_t>(light.MaximumY) - static_cast<std::size_t>(light.MinimumY) + 1U;
            result.Offsets.resize(tileCount);
            result.Counts.assign(tileCount, 0);
            result.LightIndices.assign(coveredColumns * coveredRows, light.Index);
            std::uint32_t lightIndexOffset = 0;
            for (std::uint32_t y = 0; y < result.Rows; ++y)
            {
                for (std::uint32_t x = 0; x < result.Columns; ++x)
                {
                    const auto tileIndex = static_cast<std::size_t>(y) * result.Columns + x;
                    result.Offsets[tileIndex] = lightIndexOffset;
                    if (x < light.MinimumX || x > light.MaximumX || y < light.MinimumY || y > light.MaximumY)
                        continue;
                    result.Counts[tileIndex] = 1;
                    ++lightIndexOffset;
                }
            }
            return result;
        }

        result.Counts.assign(tileCount, 0);
        std::vector<bool> overflowed(tileCount);
        for (const auto& light : projectedLights)
        {
            for (auto y = light.MinimumY; y <= light.MaximumY; ++y)
            {
                for (auto x = light.MinimumX; x <= light.MaximumX; ++x)
                {
                    const auto tileIndex = static_cast<std::size_t>(y) * result.Columns + x;
                    if (result.Counts[tileIndex] < ForwardPlusTileGrid::MaximumLightsPerTile)
                        ++result.Counts[tileIndex];
                    else
                        overflowed[tileIndex] = true;
                }
            }
        }

        result.Offsets.resize(tileCount);
        std::size_t lightIndexCount = 0;
        for (std::size_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
        {
            if (lightIndexCount > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Forward+ light index storage exceeds the supported range.");
            result.Offsets[tileIndex] = static_cast<std::uint32_t>(lightIndexCount);
            lightIndexCount += result.Counts[tileIndex];
            result.OverflowedTiles += overflowed[tileIndex] ? 1U : 0U;
        }
        if (lightIndexCount > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Forward+ light index storage exceeds the supported range.");

        result.LightIndices.resize(lightIndexCount);
        std::vector<std::uint16_t> writeOffsets(tileCount, 0);
        for (const auto& light : projectedLights)
        {
            for (auto y = light.MinimumY; y <= light.MaximumY; ++y)
            {
                for (auto x = light.MinimumX; x <= light.MaximumX; ++x)
                {
                    const auto tileIndex = static_cast<std::size_t>(y) * result.Columns + x;
                    auto& writeOffset = writeOffsets[tileIndex];
                    if (writeOffset >= result.Counts[tileIndex])
                        continue;
                    result.LightIndices[result.Offsets[tileIndex] + writeOffset] = light.Index;
                    ++writeOffset;
                }
            }
        }
        return result;
    }
} // namespace Keire::RenderBackend
