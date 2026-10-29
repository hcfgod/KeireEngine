#pragma once

#include "KeireInternal/Rendering/AtmosphereUniforms.h"
#include "KeireInternal/Spatial/PlanetFrameResolution.h"

namespace Keire::RenderBackend
{
    [[nodiscard]] inline AtmosphereUniforms MakePlanetAtmosphereUniforms(const AtmosphereSettings& settings,
                                                                         const Vector3 lightDirection,
                                                                         const Detail::ResolvedPlanetFrame& planet)
    {
        // The shared frame resolver already cancelled sector positions. Rotate this relative vector before
        // narrowing so atmospheric rays, lighting and cloud shells all use the observer's rendering axes.
        const auto relative = planet.PlanetToObserver.Apply(planet.ObserverFromCenterPlanetMetres);
        auto resolvedSettings = settings;
        resolvedSettings.PlanetCenter = {};
        resolvedSettings.PlanetRadius = planet.RadiusMetres;
        return MakeAtmosphereUniforms(resolvedSettings, {}, lightDirection,
                                      Vector3d{relative[0], relative[1], relative[2]});
    }
} // namespace Keire::RenderBackend
