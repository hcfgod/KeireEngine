#pragma once

#include "KeireInternal/Rendering/CloudNoiseVolume.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace KeireTest
{
    using CloudSamplePoint = std::array<double, 3>;

    inline double SampleCloudChannel(const Keire::RenderBackend::CloudNoiseMip& mip, const CloudSamplePoint& position,
                                     const std::size_t channel = 0)
    {
        std::array<int, 3> cell{};
        CloudSamplePoint fraction{};
        const int dimension = static_cast<int>(mip.Dimension);
        for (std::size_t axis = 0; axis < position.size(); ++axis)
        {
            const double coordinate = (position[axis] - std::floor(position[axis])) * dimension - 0.5;
            cell[axis] = static_cast<int>(std::floor(coordinate));
            fraction[axis] = coordinate - cell[axis];
        }
        double result = 0.0;
        for (int corner = 0; corner < 8; ++corner)
        {
            std::array<int, 3> texel{};
            double weight = 1.0;
            for (std::size_t axis = 0; axis < texel.size(); ++axis)
            {
                const int offset = (corner >> axis) & 1;
                texel[axis] = (cell[axis] + offset + dimension) % dimension;
                weight *= offset == 0 ? 1.0 - fraction[axis] : fraction[axis];
            }
            const auto index = static_cast<std::size_t>((texel[2] * dimension + texel[1]) * dimension + texel[0]);
            result += weight * mip.Rgba[index * 4 + channel] / 255.0;
        }
        return result;
    }
} // namespace KeireTest
