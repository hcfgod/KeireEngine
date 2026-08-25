#include "KeireInternal/Rendering/ForwardPlusInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Keire::RenderBackend
{
    ForwardPlusTileGrid BuildForwardPlusCpuTiles(const std::uint32_t width, const std::uint32_t height,
                                                 const Matrix4& projection, const float nearPlane,
                                                 const std::span<const ForwardPlusLightBounds> lights)
    {
        if (width == 0 || height == 0 || width > 16384 || height > 16384 || !Math::IsFinite(projection) ||
            !std::isfinite(nearPlane) || nearPlane <= 0.0F)
            throw std::invalid_argument("Forward+ tile dimensions, projection, or near plane are invalid.");
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
        struct AxisBounds final
        {
            double Minimum = 0.0;
            double Maximum = 0.0;
        };

        constexpr double projectionEpsilon = 0.000001;
        const auto approximatelyZero = [](const double value) { return std::abs(value) <= projectionEpsilon; };
        const bool axisAligned = approximatelyZero(projection.Elements[1]) &&
                                 approximatelyZero(projection.Elements[3]) &&
                                 approximatelyZero(projection.Elements[4]) && approximatelyZero(projection.Elements[7]);
        const bool perspectiveProjection =
            axisAligned && projection.Elements[11] > projectionEpsilon && approximatelyZero(projection.Elements[15]) &&
            approximatelyZero(projection.Elements[12]) && approximatelyZero(projection.Elements[13]) &&
            !approximatelyZero(projection.Elements[0]) && !approximatelyZero(projection.Elements[5]);
        const bool orthographicProjection =
            axisAligned && approximatelyZero(projection.Elements[11]) && projection.Elements[15] > projectionEpsilon &&
            approximatelyZero(projection.Elements[8]) && approximatelyZero(projection.Elements[9]) &&
            !approximatelyZero(projection.Elements[0]) && !approximatelyZero(projection.Elements[5]);
        const double gridWidth = static_cast<double>(width);
        const double gridHeight = static_cast<double>(height);
        const double minimumDepth = static_cast<double>(nearPlane);

        for (std::uint32_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex)
        {
            const auto& light = lights[lightIndex];
            if (!Math::IsFinite(light.ViewPosition) || !std::isfinite(light.Range) || light.Range <= 0.0F)
                throw std::invalid_argument("Forward+ light bounds are invalid.");
            const double depth = static_cast<double>(light.ViewPosition.Z);
            const double range = static_cast<double>(light.Range);
            if (depth + range <= minimumDepth)
                continue;
            const auto coverAllTiles = [&]
            { projectedLights.push_back({lightIndex, 0, result.Columns - 1U, 0, result.Rows - 1U}); };
            if (!perspectiveProjection && !orthographicProjection)
            {
                coverAllTiles();
                continue;
            }

            AxisBounds ndcX;
            AxisBounds ndcY;
            if (orthographicProjection)
            {
                const double inverseW = 1.0 / static_cast<double>(projection.Elements[15]);
                const double centerX =
                    (static_cast<double>(projection.Elements[0]) * light.ViewPosition.X + projection.Elements[12]) *
                    inverseW;
                const double centerY =
                    (static_cast<double>(projection.Elements[5]) * light.ViewPosition.Y + projection.Elements[13]) *
                    inverseW;
                const double radiusX = std::abs(static_cast<double>(projection.Elements[0]) * inverseW) * range;
                const double radiusY = std::abs(static_cast<double>(projection.Elements[5]) * inverseW) * range;
                ndcX = {centerX - radiusX, centerX + radiusX};
                ndcY = {centerY - radiusY, centerY + radiusY};
            }
            else
            {
                const auto viewAxisBounds = [&](const double center)
                {
                    if (depth - range >= minimumDepth)
                    {
                        const double denominator = (depth - range) * (depth + range);
                        const double tangentLength = std::sqrt(std::max(center * center + denominator, 0.0));
                        return AxisBounds{(center * depth - range * tangentLength) / denominator,
                                          (center * depth + range * tangentLength) / denominator};
                    }
                    const double maximumDepth = depth + range;
                    const double minimumAxis = center - range;
                    const double maximumAxis = center + range;
                    return AxisBounds{std::min({minimumAxis / minimumDepth, minimumAxis / maximumDepth,
                                                maximumAxis / minimumDepth, maximumAxis / maximumDepth}),
                                      std::max({minimumAxis / minimumDepth, minimumAxis / maximumDepth,
                                                maximumAxis / minimumDepth, maximumAxis / maximumDepth})};
                };
                const auto projectAxis = [](const AxisBounds bounds, const double scale, const double offset)
                {
                    const double first = bounds.Minimum * scale + offset;
                    const double second = bounds.Maximum * scale + offset;
                    return AxisBounds{std::min(first, second), std::max(first, second)};
                };
                const double inverseWScale = 1.0 / static_cast<double>(projection.Elements[11]);
                ndcX = projectAxis(viewAxisBounds(light.ViewPosition.X),
                                   static_cast<double>(projection.Elements[0]) * inverseWScale,
                                   static_cast<double>(projection.Elements[8]) * inverseWScale);
                ndcY = projectAxis(viewAxisBounds(light.ViewPosition.Y),
                                   static_cast<double>(projection.Elements[5]) * inverseWScale,
                                   static_cast<double>(projection.Elements[9]) * inverseWScale);
            }

            const double minimumPixelX = (ndcX.Minimum * 0.5 + 0.5) * gridWidth;
            const double maximumPixelX = (ndcX.Maximum * 0.5 + 0.5) * gridWidth;
            const double minimumPixelY = (-ndcY.Maximum * 0.5 + 0.5) * gridHeight;
            const double maximumPixelY = (-ndcY.Minimum * 0.5 + 0.5) * gridHeight;
            if (!std::isfinite(minimumPixelX) || !std::isfinite(maximumPixelX) || !std::isfinite(minimumPixelY) ||
                !std::isfinite(maximumPixelY))
            {
                coverAllTiles();
                continue;
            }
            if (maximumPixelX < 0.0 || maximumPixelY < 0.0 || minimumPixelX >= gridWidth || minimumPixelY >= gridHeight)
                continue;
            const auto clampTile = [](const double pixel, const std::uint32_t maximum)
            {
                const double tile = std::floor(pixel / static_cast<double>(ForwardPlusTileGrid::TileSize));
                return static_cast<std::uint32_t>(std::clamp(tile, 0.0, static_cast<double>(maximum - 1U)));
            };
            const auto minimumX = clampTile(minimumPixelX, result.Columns);
            const auto maximumX = clampTile(maximumPixelX, result.Columns);
            const auto minimumY = clampTile(minimumPixelY, result.Rows);
            const auto maximumY = clampTile(maximumPixelY, result.Rows);
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
