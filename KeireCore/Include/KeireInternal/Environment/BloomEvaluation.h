#pragma once

#include "Keire/Rendering/Bloom.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace Keire::Detail
{
    inline constexpr double BloomMaximumRadiance = 65504.0;
    inline constexpr std::size_t BloomKernelHalfWidth = 12;
    using BloomKernel = std::array<double, BloomKernelHalfWidth * 2 + 1>;

    inline void ValidateBloomSettings(const BloomSettings& settings)
    {
        const auto bounded = [](const double value, const double minimum, const double maximum)
        { return std::isfinite(value) && value >= minimum && value <= maximum; };
        if (!bounded(settings.Intensity, 0.0, 16.0) || !bounded(settings.Threshold, 0.0, BloomMaximumRadiance) ||
            !bounded(settings.SoftKnee, 0.0, 1.0) || !bounded(settings.Radius, 0.5, 4.0))
        {
            throw std::invalid_argument("Invalid bloom intensity, HDR threshold, soft knee or radius.");
        }
    }

    /// Peak-channel threshold preserves hue. Intensity belongs to the later composition, not extraction.
    /// Invalid HDR is rejected even when disabled; finite negative/oversized inputs are clipped to half-float range.
    [[nodiscard]] inline std::array<double, 3> ExtractBloom(const BloomSettings& settings,
                                                            const std::array<double, 3>& exposedHdr)
    {
        ValidateBloomSettings(settings);
        std::array<double, 3> color{};
        for (std::size_t channel = 0; channel < color.size(); ++channel)
        {
            if (!std::isfinite(exposedHdr[channel]))
            {
                throw std::invalid_argument("Bloom input radiance must be finite.");
            }
            color[channel] = std::clamp(exposedHdr[channel], 0.0, BloomMaximumRadiance);
        }
        if (!settings.Enabled)
        {
            return {};
        }
        const double brightness = *std::max_element(color.begin(), color.end());
        if (brightness == 0.0)
        {
            return {};
        }
        const double knee = settings.Threshold * settings.SoftKnee;
        double contribution = std::max(brightness - settings.Threshold, 0.0);
        if (knee > 0.0)
        {
            const double transition = std::clamp(brightness - settings.Threshold + knee, 0.0, 2.0 * knee);
            contribution = std::max(contribution, transition * (transition / knee) * 0.25);
        }
        const double weight = std::clamp(contribution / brightness, 0.0, 1.0);
        for (double& channel : color)
        {
            channel *= weight;
        }
        return color;
    }

    /// Symmetric 25-tap separable Gaussian; radius 4 retains a fixed three-sigma support at the widest setting.
    [[nodiscard]] inline BloomKernel BuildBloomGaussianKernel(const BloomSettings& settings)
    {
        ValidateBloomSettings(settings);
        BloomKernel result{};
        double total = 0.0;
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            const double distance = static_cast<double>(index) - static_cast<double>(BloomKernelHalfWidth);
            const double normalized = distance / settings.Radius;
            result[index] = std::exp(-0.5 * normalized * normalized);
            total += result[index];
        }
        for (double& weight : result)
        {
            weight /= total;
        }
        return result;
    }
} // namespace Keire::Detail
