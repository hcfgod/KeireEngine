#include "KeireInternal/Rendering/CloudNoiseVolume.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>

TEST_CASE("Fixed cloud atlas displacement stays within the shader filtering bound")
{
    const auto volume = Keire::RenderBackend::BuildCloudNoiseVolume(64, 1);
    std::array<double, 9> maximumDerivatives{};
    for (const auto& mip : volume.Mips)
    {
        const std::size_t size = mip.Dimension;
        const auto offset = [size](const std::array<std::size_t, 3>& point)
        { return ((point[2] * size + point[1]) * size + point[0]) * 4; };
        for (std::size_t z = 0; z < size; ++z)
        {
            for (std::size_t y = 0; y < size; ++y)
            {
                for (std::size_t x = 0; x < size; ++x)
                {
                    const std::array<std::size_t, 3> point{x, y, z};
                    for (std::size_t axis = 0; axis < point.size(); ++axis)
                    {
                        auto neighbor = point;
                        neighbor[axis] = (neighbor[axis] + 1) % size;
                        for (std::size_t component = 0; component < point.size(); ++component)
                        {
                            const double difference =
                                std::abs(static_cast<double>(mip.Rgba[offset(point) + component + 1]) -
                                         mip.Rgba[offset(neighbor) + component + 1]) /
                                255.0;
                            const double derivative = difference * static_cast<double>(size) * 0.75 / 64.0;
                            auto& maximum = maximumDerivatives[component * 3 + axis];
                            maximum = std::max(maximum, derivative);
                        }
                    }
                }
            }
        }
    }
    // Trilinear derivatives are convex combinations of edge differences. Fractional mip blending retains
    // these bounds. The Frobenius norm bounds the displacement Jacobian's spectral norm in any ray direction.
    double squaredBound = 0.0;
    for (const double derivative : maximumDerivatives)
    {
        squaredBound += derivative * derivative;
    }
    const double stretchBound = 1.0 + std::sqrt(squaredBound);
    INFO("Conservative displacement stretch bound: ", stretchBound);
    CHECK(stretchBound < 1.4);
}

TEST_CASE("Cloud noise repeats across positive and negative tile seams")
{
    using Keire::RenderBackend::SampleCloudNoise;
    for (const std::uint32_t seed : {0U, 1U, 4294967295U})
    {
        for (const double coordinate : {-1.25, -0.125, 0.0, 0.375, 0.875})
        {
            const std::array<double, 3> position{coordinate, 0.25, 0.625};
            const auto sample = SampleCloudNoise(position, seed);
            CHECK(sample == SampleCloudNoise(position, seed));
            for (std::size_t axis = 0; axis < position.size(); ++axis)
            {
                auto shifted = position;
                shifted[axis] += 3.0;
                CHECK(sample == SampleCloudNoise(shifted, seed));
            }
            for (const float value : sample)
            {
                CHECK(std::isfinite(value));
                CHECK(value >= 0.0F);
                CHECK(value <= 1.0F);
            }
        }
    }
    const auto before = SampleCloudNoise({-0.000001, 0.31, 0.57}, 7);
    const auto after = SampleCloudNoise({0.000001, 0.31, 0.57}, 7);
    for (std::size_t channel = 0; channel < before.size(); ++channel)
    {
        CHECK(std::abs(before[channel] - after[channel]) < 0.0002F);
    }
    CHECK(SampleCloudNoise({0.31, 0.57, 0.83}, 1) != SampleCloudNoise({0.31, 0.57, 0.83}, 2));
}

TEST_CASE("Cloud noise volume has a bounded deterministic complete mip chain")
{
    using Keire::RenderBackend::BuildCloudNoiseVolume;
    const auto volume = BuildCloudNoiseVolume(8, 71);
    const auto repeated = BuildCloudNoiseVolume(8, 71);
    const auto different = BuildCloudNoiseVolume(8, 72);
    REQUIRE(volume.Mips.size() == 4);
    CHECK(volume.Mips.front().Rgba != different.Mips.front().Rgba);
    std::uint32_t expectedDimension = 8;
    for (std::size_t level = 0; level < volume.Mips.size(); ++level)
    {
        const auto& mip = volume.Mips[level];
        CHECK(mip.Dimension == expectedDimension);
        CHECK(mip.Rgba.size() ==
              static_cast<std::size_t>(expectedDimension) * expectedDimension * expectedDimension * 4);
        CHECK(mip.Rgba == repeated.Mips[level].Rgba);
        if (level > 0)
        {
            const auto& previous = volume.Mips[level - 1];
            for (std::size_t index = 0; index < mip.Rgba.size(); ++index)
            {
                const std::size_t channel = index % 4;
                const std::size_t voxel = index / 4;
                const std::size_t originX = (voxel % mip.Dimension) * 2;
                const std::size_t originY = ((voxel / mip.Dimension) % mip.Dimension) * 2;
                const std::size_t originZ = (voxel / (mip.Dimension * mip.Dimension)) * 2;
                unsigned sum = 0;
                for (std::size_t z = 0; z < 2; ++z)
                {
                    for (std::size_t y = 0; y < 2; ++y)
                    {
                        for (std::size_t x = 0; x < 2; ++x)
                        {
                            sum +=
                                previous.Rgba[(((originZ + z) * previous.Dimension + originY + y) * previous.Dimension +
                                               originX + x) *
                                                  4 +
                                              channel];
                        }
                    }
                }
                CHECK(mip.Rgba[index] == (sum + 4U) / 8U);
            }
        }
        expectedDimension /= 2;
    }
}

TEST_CASE("Cloud noise volume rejects invalid dimensions before allocation and nonfinite positions")
{
    for (const std::uint32_t dimension : {0U, 1U, 3U, 5U, 63U, 65U, 128U, 4294967295U})
    {
        CHECK_THROWS_AS((void)Keire::RenderBackend::BuildCloudNoiseVolume(dimension, 1), std::invalid_argument);
    }
    for (const double invalid : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()})
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            std::array<double, 3> position{};
            position[axis] = invalid;
            CHECK_THROWS_AS((void)Keire::RenderBackend::SampleCloudNoise(position, 0), std::invalid_argument);
        }
    }
}

TEST_CASE("Cloud noise default coverage leaves both empty space and cloud interiors")
{
    for (const std::uint32_t seed : {1U, 71U, 301U})
    {
        const auto volume = Keire::RenderBackend::BuildCloudNoiseVolume(32, seed);
        const auto& pixels = volume.Mips.front().Rgba;
        std::size_t occupied = 0;
        for (std::size_t index = 0; index < pixels.size(); index += 4)
        {
            const float shape = pixels[index] / 255.0F;
            const float erosion =
                1.0F - (pixels[index + 1] * 0.625F + pixels[index + 2] * 0.25F + pixels[index + 3] * 0.125F) / 255.0F;
            // Interior envelope is one. Match the authored default coverage .6 and erosion .2.
            const float body = std::clamp((shape - 0.4F) / 0.6F, 0.0F, 1.0F);
            occupied += body > erosion * 0.2F;
        }
        const double fraction = static_cast<double>(occupied) / static_cast<double>(pixels.size() / 4);
        // Wide bounds catch the previous near-solid field without prescribing one seed's appearance.
        CHECK(fraction > 0.1);
        CHECK(fraction < 0.9);
    }
}
