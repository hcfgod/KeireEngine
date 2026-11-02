#pragma once

#include "KeireInternal/Rendering/AtmosphereUniforms.h"
#include "KeireInternal/Spatial/PlanetFrameResolution.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline AtmosphereUniforms MakePlanetAtmosphereUniforms(const AtmosphereSettings& settings,
                                                                         const Vector3 lightDirection,
                                                                         const Detail::ResolvedPlanetFrame& planet)
    {
        // The shared frame resolver already cancelled sector positions. Rotate this relative vector before
        // narrowing so atmospheric rays, lighting and cloud shells all use the observer's rendering axes.
        const auto relative = planet.PlanetToObserver.Apply(planet.ObserverFromCenterPlanetMetres);
        // Shader sphere intersections square kilometre-valued coordinates. A finite float conversion alone
        // does not guarantee finite dot products; leave headroom for vector sums and the radial discriminant.
        constexpr double metresPerKilometre = 1000.0;
        const double maximumMetres =
            std::sqrt(static_cast<double>(std::numeric_limits<float>::max())) * 0.125 * metresPerKilometre;
        const double observerMetres = std::hypot(relative[0], relative[1], relative[2]);
        const double outerRadiusMetres = planet.RadiusMetres + settings.Height;
        if (!std::isfinite(planet.RadiusMetres) || planet.RadiusMetres <= 0.0 || !std::isfinite(settings.Height) ||
            settings.Height <= 0.0 || !std::isfinite(observerMetres) || observerMetres > maximumMetres ||
            !std::isfinite(outerRadiusMetres) || outerRadiusMetres > maximumMetres)
        {
            throw std::out_of_range("Planet atmosphere placement exceeds the shader intersection coordinate range.");
        }
        auto resolvedSettings = settings;
        resolvedSettings.PlanetCenter = {};
        resolvedSettings.PlanetRadius = planet.RadiusMetres;
        const auto result = MakeAtmosphereUniforms(resolvedSettings, {}, lightDirection,
                                                   Vector3d{relative[0], relative[1], relative[2]});
        if (result.ObserverRadius.W <= 0.0F || result.SunHeight.W <= 0.0F)
        {
            throw std::out_of_range("Planet atmosphere radius or height underflows the shader coordinate range.");
        }
        return result;
    }
} // namespace Keire::RenderBackend
