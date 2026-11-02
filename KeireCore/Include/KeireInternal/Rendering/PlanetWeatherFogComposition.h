#pragma once

#include "KeireInternal/Rendering/PlanetWeatherCapture.h"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    struct PlanetWeatherFogComposition
    {
        FogSettings Common;
        // Same stable ordering as captured planet weather; never deduplicate authored overrides by value.
        std::vector<FogSettings> Planets;
    };

    [[nodiscard]] inline PlanetWeatherFogComposition
    ComposePlanetWeatherFog(const FogSettings& inherited, const std::span<const CapturedPlanetWeather> weather)
    {
        ValidateFogDensity(inherited);
        PlanetWeatherFogComposition result;
        result.Common = inherited;
        const bool hasInherited = std::ranges::any_of(weather, &CapturedPlanetWeather::InheritsFog);
        // One planet keeps its complete fog profile and the established cached volume path.
        if (weather.size() == 1 || (!weather.empty() && !hasInherited))
        {
            result.Common.Enabled = false;
        }
        else if (!weather.empty() && inherited.PlanetaryHeight)
        {
            // Local boxes use the environment anchor, even when height density is planet-relative.
            result.Common.ExtinctionPerMeter = 0.0;
            result.Common.PlanetaryHeight = false;
        }
        result.Planets.reserve(weather.size());
        for (const auto& planet : weather)
        {
            ValidateFogDensity(planet.Fog);
            auto fog = planet.Fog;
            if (planet.InheritsFog && weather.size() > 1)
            {
                // Global flat height and inherited boxes already appear in Common exactly once.
                fog.LocalVolumes.clear();
                if (!fog.PlanetaryHeight)
                {
                    fog.Enabled = false;
                }
                // Exclusions still mask each body's radial height field; they add no density.
            }
            result.Planets.push_back(std::move(fog));
        }
        return result;
    }
} // namespace Keire::Detail
