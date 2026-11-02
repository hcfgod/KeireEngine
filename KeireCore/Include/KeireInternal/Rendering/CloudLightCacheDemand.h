#pragma once

#include "KeireInternal/Rendering/CloudVolumeViewUniforms.h"

namespace Keire::RenderBackend
{
    [[nodiscard]] inline bool CloudLightCacheHasDemand(const CloudVolumeViewUniforms& view) noexcept
    {
        // A single-profile cache cannot represent shadows from other planetary media.
        if (view.PlanetWeather.Controls[0] > 1U)
        {
            return false;
        }
        if (view.Volume.Quality[0] == 0U || view.Volume.Density[0] <= 0.0F || view.Volume.Density[1] <= 0.0F)
        {
            return false;
        }
        // Match the shader's query predicate. With neither source active, every cache value is one
        // and the volume shader bypasses the lookup, including its direct-light fallback.
        return view.SunRadiance.X > 0.0F || view.SunRadiance.Y > 0.0F || view.SunRadiance.Z > 0.0F ||
               view.AmbientRadiance.W > 0.0F;
    }
} // namespace Keire::RenderBackend
