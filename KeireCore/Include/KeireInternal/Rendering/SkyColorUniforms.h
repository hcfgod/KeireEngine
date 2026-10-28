#pragma once

#include "Keire/Rendering/EnvironmentLighting.h"

#include <array>
#include <optional>

namespace Keire::RenderBackend
{
    struct SkyColorUniforms
    {
        std::array<float, 4> HorizonWeight{};
        std::array<float, 4> ZenithExponent{};
    };

    static_assert(sizeof(SkyColorUniforms) == 32);

    [[nodiscard]] inline SkyColorUniforms CaptureSkyColors(const std::optional<EnvironmentLightingSnapshot>& snapshot)
    {
        if (!snapshot || !snapshot->SkyColors)
        {
            return {};
        }
        const auto& colors = *snapshot->SkyColors;
        return {{colors.Horizon[0], colors.Horizon[1], colors.Horizon[2], colors.BlendWeight},
                {colors.Zenith[0], colors.Zenith[1], colors.Zenith[2], colors.GradientExponent}};
    }
} // namespace Keire::RenderBackend
