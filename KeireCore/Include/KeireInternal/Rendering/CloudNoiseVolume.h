#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Keire::RenderBackend
{
    struct CloudNoiseMip final
    {
        std::uint32_t Dimension = 0;
        // X varies fastest, then Y, then Z. Linear UNORM: combined shape, then three erosion frequencies.
        std::vector<std::uint8_t> Rgba;
    };

    struct CloudNoiseVolume final
    {
        std::vector<CloudNoiseMip> Mips;
    };

    /// Period-one noise, including negative coordinates. Repeatable on one floating-point implementation.
    [[nodiscard]] std::array<float, 4> SampleCloudNoise(std::array<double, 3> position, std::uint32_t seed);

    /// Bounded power-of-two dimensions from 4 through 64; includes all mips down to one texel.
    /// CPU generation is initialization work, never per-view or per-frame work.
    [[nodiscard]] CloudNoiseVolume BuildCloudNoiseVolume(std::uint32_t dimension, std::uint32_t seed);
} // namespace Keire::RenderBackend
