#include "KeireInternal/Rendering/CloudNoiseVolume.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace Keire::RenderBackend
{
    namespace
    {
        using Point = std::array<float, 3>;
        using Cell = std::array<int, 3>;

        [[nodiscard]] std::uint32_t Hash(const Cell& cell, const int period, const std::uint32_t seed) noexcept
        {
            std::array<std::uint32_t, 3> wrapped{};
            for (std::size_t axis = 0; axis < cell.size(); ++axis)
            {
                wrapped[axis] = static_cast<std::uint32_t>((cell[axis] % period + period) % period);
            }
            auto value = wrapped[0] * 0x8da6b343U ^ wrapped[1] * 0xd8163841U ^ wrapped[2] * 0xcb1ab31fU ^ seed;
            value ^= value >> 16U;
            value *= 0x7feb352dU;
            value ^= value >> 15U;
            value *= 0x846ca68bU;
            return value ^ (value >> 16U);
        }

        [[nodiscard]] float UnitHash(const Cell& cell, const int period, const std::uint32_t seed) noexcept
        {
            return static_cast<float>(Hash(cell, period, seed) >> 8U) / 16777216.0F;
        }

        [[nodiscard]] Point CellPosition(const Point& position, const int period) noexcept
        {
            return {position[0] * period, position[1] * period, position[2] * period};
        }

        [[nodiscard]] Cell FloorCell(const Point& position) noexcept
        {
            return {static_cast<int>(std::floor(position[0])), static_cast<int>(std::floor(position[1])),
                    static_cast<int>(std::floor(position[2]))};
        }

        [[nodiscard]] float GradientNoise(const Point& position, const int period, const std::uint32_t seed) noexcept
        {
            constexpr std::array<Point, 12> gradients{{{1, 1, 0},
                                                       {1, -1, 0},
                                                       {-1, 1, 0},
                                                       {-1, -1, 0},
                                                       {1, 0, 1},
                                                       {1, 0, -1},
                                                       {-1, 0, 1},
                                                       {-1, 0, -1},
                                                       {0, 1, 1},
                                                       {0, 1, -1},
                                                       {0, -1, 1},
                                                       {0, -1, -1}}};
            const auto scaled = CellPosition(position, period);
            const auto cell = FloorCell(scaled);
            Point fraction{};
            Point blend{};
            for (std::size_t axis = 0; axis < fraction.size(); ++axis)
            {
                const float value = scaled[axis] - static_cast<float>(cell[axis]);
                fraction[axis] = value;
                blend[axis] = value * value * value * (value * (value * 6.0F - 15.0F) + 10.0F);
            }
            float sum = 0.0F;
            for (int corner = 0; corner < 8; ++corner)
            {
                const Cell offset{corner & 1, (corner >> 1) & 1, (corner >> 2) & 1};
                const Cell neighbor{cell[0] + offset[0], cell[1] + offset[1], cell[2] + offset[2]};
                const auto& gradient = gradients[Hash(neighbor, period, seed) % gradients.size()];
                float dot = 0.0F;
                float weight = 1.0F;
                for (std::size_t axis = 0; axis < fraction.size(); ++axis)
                {
                    dot += gradient[axis] * (fraction[axis] - static_cast<float>(offset[axis]));
                    weight *= offset[axis] == 0 ? 1.0F - blend[axis] : blend[axis];
                }
                sum += dot * weight;
            }
            return std::clamp(0.5F + 0.5F * sum, 0.0F, 1.0F);
        }

        [[nodiscard]] float CellularNoise(const Point& position, const int period, const std::uint32_t seed) noexcept
        {
            const auto scaled = CellPosition(position, period);
            const auto cell = FloorCell(scaled);
            // Distances are capped at one cell. Features outside these neighbors cannot affect that result.
            float nearestSquared = 1.0F;
            for (int z = -1; z <= 1; ++z)
            {
                for (int y = -1; y <= 1; ++y)
                {
                    for (int x = -1; x <= 1; ++x)
                    {
                        const Cell neighbor{cell[0] + x, cell[1] + y, cell[2] + z};
                        float squared = 0.0F;
                        for (std::size_t axis = 0; axis < scaled.size(); ++axis)
                        {
                            const float feature =
                                static_cast<float>(neighbor[axis]) +
                                UnitHash(neighbor, period, seed + static_cast<std::uint32_t>(axis) * 71U);
                            const float distance = feature - scaled[axis];
                            squared += distance * distance;
                        }
                        nearestSquared = std::min(nearestSquared, squared);
                    }
                }
            }
            return 1.0F - std::sqrt(nearestSquared);
        }

        [[nodiscard]] std::array<float, 4> SampleWrapped(const Point& position, const std::uint32_t seed) noexcept
        {
            const float gradient = GradientNoise(position, 4, seed) * 0.625F +
                                   GradientNoise(position, 8, seed + 17U) * 0.25F +
                                   GradientNoise(position, 16, seed + 37U) * 0.125F;
            const float coarse = CellularNoise(position, 4, seed + 71U);
            const float medium = CellularNoise(position, 8, seed + 113U);
            const float fine = CellularNoise(position, 16, seed + 151U);
            const float billows = coarse * 0.625F + medium * 0.25F + fine * 0.125F;
            const float combined = gradient + (1.0F - gradient) * billows;
            // Combining two positive fields biases the shape upward. Restore a useful coverage range before
            // quantization and mip averaging; the uncorrected field fills almost every voxel at default coverage.
            constexpr float shapeFloor = 0.5F;
            const float shape = std::clamp((combined - shapeFloor) / (1.0F - shapeFloor), 0.0F, 1.0F);
            return {shape, coarse, medium, fine};
        }

        [[nodiscard]] std::size_t Offset(const std::uint32_t dimension, const std::uint32_t x, const std::uint32_t y,
                                         const std::uint32_t z) noexcept
        {
            return ((static_cast<std::size_t>(z) * dimension + y) * dimension + x) * 4;
        }

        [[nodiscard]] CloudNoiseMip Downsample(const CloudNoiseMip& input)
        {
            CloudNoiseMip output;
            output.Dimension = input.Dimension / 2;
            const auto size = output.Dimension;
            output.Rgba.resize(static_cast<std::size_t>(size) * size * size * 4);
            for (std::uint32_t z = 0; z < size; ++z)
            {
                for (std::uint32_t y = 0; y < size; ++y)
                {
                    for (std::uint32_t x = 0; x < size; ++x)
                    {
                        for (std::size_t channel = 0; channel < 4; ++channel)
                        {
                            unsigned sum = 0;
                            for (unsigned corner = 0; corner < 8; ++corner)
                            {
                                sum += input.Rgba[Offset(input.Dimension, x * 2 + (corner & 1U),
                                                         y * 2 + ((corner >> 1U) & 1U), z * 2 + ((corner >> 2U) & 1U)) +
                                                  channel];
                            }
                            output.Rgba[Offset(size, x, y, z) + channel] = static_cast<std::uint8_t>((sum + 4U) / 8U);
                        }
                    }
                }
            }
            return output;
        }
    } // namespace

    std::array<float, 4> SampleCloudNoise(const std::array<double, 3> position, const std::uint32_t seed)
    {
        Point wrapped{};
        for (std::size_t axis = 0; axis < position.size(); ++axis)
        {
            if (!std::isfinite(position[axis]))
            {
                throw std::invalid_argument("Cloud noise coordinates must be finite.");
            }
            wrapped[axis] = static_cast<float>(position[axis] - std::floor(position[axis]));
        }
        return SampleWrapped(wrapped, seed);
    }

    CloudNoiseVolume BuildCloudNoiseVolume(const std::uint32_t dimension, const std::uint32_t seed)
    {
        if (dimension < 4 || dimension > 64 || (dimension & (dimension - 1U)) != 0)
        {
            throw std::invalid_argument("Cloud noise dimension must be a power of two from 4 through 64.");
        }
        CloudNoiseVolume result;
        CloudNoiseMip base;
        base.Dimension = dimension;
        base.Rgba.resize(static_cast<std::size_t>(dimension) * dimension * dimension * 4);
        for (std::uint32_t z = 0; z < dimension; ++z)
        {
            for (std::uint32_t y = 0; y < dimension; ++y)
            {
                for (std::uint32_t x = 0; x < dimension; ++x)
                {
                    // Texel centers match normalized repeating linear texture sampling.
                    const Point position{(static_cast<float>(x) + 0.5F) / dimension,
                                         (static_cast<float>(y) + 0.5F) / dimension,
                                         (static_cast<float>(z) + 0.5F) / dimension};
                    const auto sample = SampleWrapped(position, seed);
                    for (std::size_t channel = 0; channel < sample.size(); ++channel)
                    {
                        base.Rgba[Offset(dimension, x, y, z) + channel] =
                            static_cast<std::uint8_t>(std::clamp(sample[channel], 0.0F, 1.0F) * 255.0F + 0.5F);
                    }
                }
            }
        }
        result.Mips.push_back(std::move(base));
        while (result.Mips.back().Dimension > 1)
        {
            result.Mips.push_back(Downsample(result.Mips.back()));
        }
        return result;
    }
} // namespace Keire::RenderBackend
