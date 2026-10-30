#pragma once

#include "KeireInternal/Spatial/PlanetFrameResolution.h"
#include "KeireInternal/Spatial/PlanetProjectSettings.h"

#include <cmath>
#include <limits>
#include <optional>

namespace Keire::Detail
{
    // Stateless per-view selection. Stable identity breaks equal-distance ties independently of authoring order.
    template <typename Resolver>
    [[nodiscard]] std::optional<ResolvedPlanetFrame> SelectPlanetEnvironment(const PlanetProjectSettings& settings,
                                                                             Resolver&& resolve)
    {
        if (settings.Geometry == PlanetGeometryMode::Flat)
        {
            return std::nullopt;
        }
        if (settings.Selection == PlanetSelectionMode::Manual)
        {
            return resolve(settings.SelectedPlanet);
        }

        std::optional<ResolvedPlanetFrame> selected;
        double nearest = std::numeric_limits<double>::infinity();
        for (const auto& planet : settings.Planets)
        {
            auto candidate = resolve(planet.Planet);
            const auto& position = candidate.ObserverFromCenterPlanetMetres;
            const double distance =
                std::abs(std::hypot(position[0], position[1], position[2]) - candidate.RadiusMetres);
            if (!selected || distance < nearest || (distance == nearest && candidate.Planet < selected->Planet))
            {
                nearest = distance;
                selected = candidate;
            }
        }
        return selected;
    }
} // namespace Keire::Detail
