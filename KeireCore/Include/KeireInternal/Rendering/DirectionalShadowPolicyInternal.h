#pragma once

#include "Keire/Rendering/Lighting.h"

#include <algorithm>
#include <cstdint>

namespace Keire::RenderBackend
{
    [[nodiscard]] constexpr std::uint32_t DirectionalShadowResolutionForHint(const std::uint32_t projectResolution,
                                                                             const ShadowResolutionHint hint) noexcept
    {
        const auto baseResolution = std::clamp(projectResolution, 256U, 8192U);
        switch (hint)
        {
        case ShadowResolutionHint::Low:
            return std::max(baseResolution / 4U, 256U);
        case ShadowResolutionHint::Medium:
            return std::max(baseResolution / 2U, 256U);
        case ShadowResolutionHint::VeryHigh:
            return std::min(baseResolution * 2U, 8192U);
        case ShadowResolutionHint::High:
        default:
            return baseResolution;
        }
    }
} // namespace Keire::RenderBackend
