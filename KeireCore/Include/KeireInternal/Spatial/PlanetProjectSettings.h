#pragma once

#include "KeireInternal/Spatial/FrameRegistry.h"
#include "KeireInternal/Spatial/PlanetDefinition.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

namespace Keire::Detail
{
    enum class PlanetGeometryMode
    {
        Flat,
        Planetary
    };

    enum class PlanetGravityMode
    {
        Uniform,
        Radial,
        Combined
    };

    struct PlanetProjectSettings
    {
        PlanetGeometryMode Geometry = PlanetGeometryMode::Flat;
        PlanetGravityMode Gravity = PlanetGravityMode::Uniform;
        AssetId SelectedPlanet;
        std::vector<PlanetDefinition> Planets;

        bool operator==(const PlanetProjectSettings&) const noexcept = default;
    };

    inline void ValidatePlanetProjectSettings(const PlanetProjectSettings& settings)
    {
        constexpr std::size_t maximumPlanets = 64;
        if ((settings.Geometry != PlanetGeometryMode::Flat && settings.Geometry != PlanetGeometryMode::Planetary) ||
            (settings.Gravity != PlanetGravityMode::Uniform && settings.Gravity != PlanetGravityMode::Radial &&
             settings.Gravity != PlanetGravityMode::Combined) ||
            settings.Planets.size() > maximumPlanets)
        {
            throw std::invalid_argument("Planet project mode or planet count is unsupported.");
        }
        std::set<AssetId> identities;
        std::set<std::uint64_t> pending;
        FrameRegistry registry(1, 1);
        for (const auto& planet : settings.Planets)
        {
            ValidatePlanetDefinition(planet);
            if (planet.ParentCenter.Identity().World != 1 || planet.Frame == 1 ||
                !identities.insert(planet.Planet).second || !pending.insert(planet.Frame).second)
            {
                throw std::invalid_argument("Planet project contains foreign or duplicate identities.");
            }
        }
        if ((settings.SelectedPlanet && !identities.contains(settings.SelectedPlanet)) ||
            (settings.Geometry == PlanetGeometryMode::Planetary && !settings.SelectedPlanet) ||
            (settings.Gravity != PlanetGravityMode::Uniform && settings.Planets.empty()))
        {
            throw std::invalid_argument(
                "Planetary geometry needs a selected planet; radial gravity needs authored planets.");
        }
        std::set<std::uint64_t> created{1};
        while (!pending.empty())
        {
            bool progressed = false;
            for (const auto& planet : settings.Planets)
            {
                const auto parent = planet.ParentCenter.Identity().Frame;
                if (pending.contains(planet.Frame) && created.contains(parent))
                {
                    (void)registry.Create(planet.Frame, registry.Find(parent));
                    created.insert(planet.Frame);
                    pending.erase(planet.Frame);
                    progressed = true;
                }
            }
            if (!progressed)
            {
                throw std::invalid_argument("Planet project has a frame cycle or missing parent.");
            }
        }
    }
} // namespace Keire::Detail
