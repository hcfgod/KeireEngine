#pragma once

#include "Keire/Rendering/StarField.h"

namespace Keire::Detail
{
    /// A visible artistic starting point, not a calibrated astronomical exposure.
    /// Preserve placement and visibility timing so a look change does not relocate the authored sky.
    [[nodiscard]] inline StarFieldSettings MakeBrightNightStarAppearance(const StarFieldSettings& current) noexcept
    {
        auto result = current;
        result.Density = 0.35;
        result.Intensity = 21.0;
        result.Tint = {1.0, 1.0, 1.0};
        result.ColorVariation = 0.5;
        result.AngularRadius = 0.00015;
        result.TwinkleAmplitude = 0.0;
        return result;
    }
} // namespace Keire::Detail
