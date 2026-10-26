#pragma once

#include "Keire/Math/Math.h"
#include "KeireInternal/Environment/BloomEvaluation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace Keire::RenderBackend
{
    [[nodiscard]] constexpr std::uint32_t BloomHalfExtent(const std::uint32_t extent) noexcept
    {
        // Match the frame graph's round-up scaling without overflowing at UINT32_MAX.
        return extent / 2U + extent % 2U;
    }

    enum class BloomPass : std::uint32_t
    {
        Extract = 0,
        Horizontal = 1,
        Vertical = 2
    };

    struct alignas(16) BloomUniforms final
    {
        /// Inverse source width/height, exposed HDR threshold, absolute soft-knee width.
        Vector4 Parameters;
        /// Source-texel blur direction, pass mode, enabled flag.
        Vector4 Direction;
        std::array<Vector4, 7> Weights;
    };

    static_assert(sizeof(BloomUniforms) == 144);
    static_assert(alignof(BloomUniforms) == 16);
    static_assert(offsetof(BloomUniforms, Direction) == 16);
    static_assert(offsetof(BloomUniforms, Weights) == 32);

    [[nodiscard]] inline BloomUniforms BuildBloomUniforms(const BloomSettings& settings,
                                                          const std::uint32_t sourceWidth,
                                                          const std::uint32_t sourceHeight, const BloomPass pass)
    {
        Detail::ValidateBloomSettings(settings);
        if (sourceWidth == 0 || sourceHeight == 0 || pass > BloomPass::Vertical)
        {
            throw std::invalid_argument("Bloom requires nonempty source dimensions and a supported pass.");
        }
        const auto weights = Detail::BuildBloomGaussianKernel(settings);
        const float inverseWidth = 1.0F / static_cast<float>(sourceWidth);
        const float inverseHeight = 1.0F / static_cast<float>(sourceHeight);
        BloomUniforms result{};
        result.Parameters = {inverseWidth, inverseHeight, static_cast<float>(settings.Threshold),
                             static_cast<float>(settings.Threshold * settings.SoftKnee)};
        result.Direction = {pass == BloomPass::Horizontal ? inverseWidth : 0.0F,
                            pass == BloomPass::Vertical ? inverseHeight : 0.0F, static_cast<float>(pass),
                            settings.Enabled ? 1.0F : 0.0F};
        for (std::size_t block = 0; block < result.Weights.size(); ++block)
        {
            const auto weight = [&weights, block](const std::size_t component)
            {
                const std::size_t index = block * 4 + component;
                return index < weights.size() ? static_cast<float>(weights[index]) : 0.0F;
            };
            result.Weights[block] = {weight(0), weight(1), weight(2), weight(3)};
        }
        return result;
    }
} // namespace Keire::RenderBackend
