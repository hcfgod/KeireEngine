#pragma once

#include "KeireInternal/Rendering/FogViewUniforms.h"

#include <algorithm>
#include <cmath>

namespace Keire::RenderBackend
{
    // Bounds optical depth, not HDR radiance: arbitrary incident lighting can amplify tiny scattering.
    [[nodiscard]] inline bool FogViewHasNegligibleOpticalDepth(const FogViewUniforms& fog, bool orthographic) noexcept
    {
        if (orthographic || fog.Controls[0] == 0U || fog.Controls[0] > 2U || fog.Controls[2] != 0U)
        {
            return false;
        }
        const double heightAboveBase = -static_cast<double>(fog.HeightDensity[0]);
        const double falloff = fog.HeightDensity[1];
        const double extinction = fog.HeightDensity[2];
        const double distance = fog.HeightDensity[3];
        if (!std::isfinite(heightAboveBase) || !std::isfinite(falloff) || !std::isfinite(extinction) ||
            !std::isfinite(distance) || falloff < 0.0 || falloff > 10.0 || extinction < 0.0 || extinction > 10.0 ||
            distance < 1.0 || distance > 1'000'000.0)
        {
            return false;
        }
        if (extinction == 0.0)
        {
            return true;
        }
        // Flat and radial altitude change by at most the ray length. Use the already packed altitude,
        // with a generous metre-domain allowance for float shader evaluation near the cutoff.
        const double heightGuard = 1.0 + distance * 0.0001;
        const double minimumHeight = std::max(0.0, heightAboveBase - distance - heightGuard);
        // Log space avoids underflow. A factor-of-two reserve keeps borderline cases on the full path.
        constexpr double maximumOpticalDepth = 1.0e-7;
        const double logBound = std::log(distance) + std::log(extinction) - falloff * minimumHeight;
        return logBound < std::log(maximumOpticalDepth * 0.5);
    }
} // namespace Keire::RenderBackend
