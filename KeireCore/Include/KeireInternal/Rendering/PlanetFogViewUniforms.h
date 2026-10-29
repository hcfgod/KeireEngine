#pragma once

#include "KeireInternal/Rendering/FogViewUniforms.h"
#include "KeireInternal/Spatial/PlanetFrameResolution.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline FogViewUniforms MakePlanetFogViewUniforms(const FogSettings& settings,
                                                                   const std::array<double, 3>& observerFromAnchor,
                                                                   const Detail::ResolvedPlanetFrame& planet)
    {
        // Local boxes remain in the environment frame. Only global height uses the selected planet.
        auto localSettings = settings;
        localSettings.PlanetaryHeight = false;
        auto result = MakeFogViewUniforms(localSettings, observerFromAnchor);
        if (!settings.Enabled || !settings.PlanetaryHeight)
        {
            return result;
        }
        const auto& relative = planet.ObserverFromCenterPlanetMetres;
        const double radius = std::hypot(relative[0], relative[1], relative[2]);
        if (!std::isfinite(radius) || !std::isfinite(planet.RadiusMetres) || planet.RadiusMetres <= 0.0)
        {
            throw std::invalid_argument("Planet-relative fog requires a finite observer and positive radius.");
        }
        const double altitude = radius - planet.RadiusMetres;
        result.HeightDensity[0] = static_cast<float>(settings.BaseHeightMeters - altitude);
        result.Controls[0] = 2U;
        return result;
    }
} // namespace Keire::RenderBackend
