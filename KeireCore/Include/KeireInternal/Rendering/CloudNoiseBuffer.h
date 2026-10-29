#pragma once

#include "KeireInternal/Rendering/CloudNoiseVolume.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace Keire::RenderBackend
{
    inline constexpr std::uint32_t CloudNoiseBufferHeaderWords = 4;
    inline constexpr std::uint32_t CloudNoiseBufferMipWords = 4;

    // uint storage-buffer ABI: base dimension, mip count, two reserved words, then one uint4 per mip.
    // Mip descriptors contain dimension, absolute texel-word offset and two reserved words.
    // Texels retain the texture's RGBA8 quantization, with R in the least significant byte.
    [[nodiscard]] inline std::vector<std::uint32_t> PackCloudNoiseBuffer(const CloudNoiseVolume& volume)
    {
        if (volume.Mips.empty())
        {
            throw std::invalid_argument("Cloud noise buffer requires a complete mip chain.");
        }
        const auto baseDimension = volume.Mips.front().Dimension;
        if (baseDimension < 4 || baseDimension > 64 || (baseDimension & (baseDimension - 1U)) != 0)
        {
            throw std::invalid_argument("Cloud noise buffer dimensions must be powers of two from 4 through 64.");
        }

        std::uint32_t dimension = baseDimension;
        std::size_t texelCount = 0;
        for (const auto& mip : volume.Mips)
        {
            const auto mipTexels = static_cast<std::size_t>(dimension) * dimension * dimension;
            if (dimension == 0 || mip.Dimension != dimension || mip.Rgba.size() != mipTexels * 4U)
            {
                throw std::invalid_argument("Cloud noise buffer contains an invalid mip level.");
            }
            texelCount += mipTexels;
            dimension /= 2U;
        }
        if (dimension != 0)
        {
            throw std::invalid_argument("Cloud noise buffer requires every mip down to one texel.");
        }

        const auto mipCount = static_cast<std::uint32_t>(volume.Mips.size());
        auto offset = CloudNoiseBufferHeaderWords + CloudNoiseBufferMipWords * mipCount;
        std::vector<std::uint32_t> words(offset + texelCount, 0U);
        words[0] = baseDimension;
        words[1] = mipCount;
        for (std::uint32_t level = 0; level < mipCount; ++level)
        {
            const auto& mip = volume.Mips[level];
            const auto descriptor = CloudNoiseBufferHeaderWords + level * CloudNoiseBufferMipWords;
            words[descriptor] = mip.Dimension;
            words[descriptor + 1U] = offset;
            for (std::size_t byte = 0; byte < mip.Rgba.size(); byte += 4U)
            {
                words[offset++] = static_cast<std::uint32_t>(mip.Rgba[byte]) |
                                  (static_cast<std::uint32_t>(mip.Rgba[byte + 1U]) << 8U) |
                                  (static_cast<std::uint32_t>(mip.Rgba[byte + 2U]) << 16U) |
                                  (static_cast<std::uint32_t>(mip.Rgba[byte + 3U]) << 24U);
            }
        }
        return words;
    }
} // namespace Keire::RenderBackend
