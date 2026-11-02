#pragma once

#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Rendering/CloudTemporalResources.h"

#include <cstddef>
#include <stdexcept>

namespace Keire::RenderBackend
{
    // Numeric values match the existing RegionalControls.z shader contract.
    enum class CloudHistoryCompositeMode : unsigned char
    {
        Radiance = 0,
        Attenuation = 1,
        Composite = 2
    };

    inline constexpr std::size_t CloudTemporalPipelineCount = 9;

    [[nodiscard]] inline std::size_t CloudTemporalPipelineIndex(const CloudTemporalPass pass,
                                                                const RenderSampleCount samples,
                                                                const CloudHistoryCompositeMode mode)
    {
        if (pass != CloudTemporalPass::Trace && pass != CloudTemporalPass::Resolve &&
            pass != CloudTemporalPass::Composite)
        {
            throw std::invalid_argument("Invalid cloud history pass.");
        }
        if (samples == RenderSampleCount::One)
        {
            if (mode != CloudHistoryCompositeMode::Composite)
            {
                throw std::invalid_argument("Single-sample cloud history uses direct composition.");
            }
            return static_cast<std::size_t>(pass);
        }
        if (pass != CloudTemporalPass::Composite ||
            (mode != CloudHistoryCompositeMode::Radiance && mode != CloudHistoryCompositeMode::Attenuation))
        {
            throw std::invalid_argument("Only cloud history blends can target multisample color.");
        }
        std::size_t sampleIndex = 0;
        switch (samples)
        {
        case RenderSampleCount::Two:
            sampleIndex = 0;
            break;
        case RenderSampleCount::Four:
            sampleIndex = 1;
            break;
        case RenderSampleCount::Eight:
            sampleIndex = 2;
            break;
        default:
            throw std::invalid_argument("Unsupported cloud history sample count.");
        }
        return 3 + sampleIndex * 2 + static_cast<std::size_t>(mode);
    }
} // namespace Keire::RenderBackend
