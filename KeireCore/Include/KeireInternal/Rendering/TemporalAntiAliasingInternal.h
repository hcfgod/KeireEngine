#pragma once

#include "Keire/Math/Math.h"

#include <cstddef>
#include <cstdint>

namespace Keire::RenderBackend::TemporalAntiAliasing
{
    [[nodiscard]] inline float HaltonSample(std::uint32_t index, const std::uint32_t base) noexcept
    {
        float result = 0.0F;
        float fraction = 1.0F;
        while (index != 0U)
        {
            fraction /= static_cast<float>(base);
            result += fraction * static_cast<float>(index % base);
            index /= base;
        }
        return result;
    }

    [[nodiscard]] inline Vector2 TemporalJitterPixels(const std::uint64_t frameId) noexcept
    {
        // An eight-position Halton cycle gives useful sub-pixel coverage without a long warm-up after a camera cut.
        const auto sample = static_cast<std::uint32_t>(frameId % 8U) + 1U;
        return {HaltonSample(sample, 2U) - 0.5F, HaltonSample(sample, 3U) - 0.5F};
    }

    [[nodiscard]] inline Vector2 TemporalJitterNdc(const std::uint64_t frameId, const std::uint32_t width,
                                                   const std::uint32_t height) noexcept
    {
        if (width == 0U || height == 0U)
            return {};
        const auto pixels = TemporalJitterPixels(frameId);
        return {pixels.X * 2.0F / static_cast<float>(width), -pixels.Y * 2.0F / static_cast<float>(height)};
    }

    [[nodiscard]] inline Matrix4 ApplyTemporalProjectionJitter(Matrix4 projection, const Vector2 jitterNdc) noexcept
    {
        // Matrices are column-major. Adding jitter * clip.w to the two clip-space rows works for perspective and
        // orthographic projections without making assumptions about their depth convention.
        for (std::size_t column = 0; column < 4U; ++column)
        {
            projection.Elements[column * 4U] += jitterNdc.X * projection.Elements[column * 4U + 3U];
            projection.Elements[column * 4U + 1U] += jitterNdc.Y * projection.Elements[column * 4U + 3U];
        }
        return projection;
    }
} // namespace Keire::RenderBackend::TemporalAntiAliasing
