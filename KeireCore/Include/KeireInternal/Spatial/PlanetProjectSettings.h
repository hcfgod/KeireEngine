#pragma once

#include "KeireInternal/Environment/AtmosphereValidation.h"
#include "KeireInternal/Environment/CloudEvaluation.h"
#include "KeireInternal/Environment/FogDensity.h"
#include "KeireInternal/Spatial/FrameRegistry.h"
#include "KeireInternal/Spatial/PlanetDefinition.h"

#include <algorithm>
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

    enum class PlanetSelectionMode
    {
        Manual,
        NearestSurface
    };

    struct PlanetAtmosphereOverride
    {
        AssetId Planet;
        AtmosphereSettings Atmosphere;

        bool operator==(const PlanetAtmosphereOverride&) const noexcept = default;
    };

    struct PlanetCloudOverride
    {
        AssetId Planet;
        CloudSettings Clouds;

        bool operator==(const PlanetCloudOverride&) const noexcept = default;
    };

    struct PlanetFogOverride
    {
        AssetId Planet;
        FogSettings Fog;

        bool operator==(const PlanetFogOverride&) const noexcept = default;
    };

    struct PlanetProjectSettings
    {
        PlanetGeometryMode Geometry = PlanetGeometryMode::Flat;
        PlanetGravityMode Gravity = PlanetGravityMode::Uniform;
        AssetId SelectedPlanet;
        std::vector<PlanetDefinition> Planets;
        PlanetSelectionMode Selection = PlanetSelectionMode::Manual;
        // Missing entries inherit the global atmosphere; an explicit disabled entry is airless.
        std::vector<PlanetAtmosphereOverride> AtmosphereOverrides;
        // Missing weather entries inherit global settings; explicit disabled profiles remain disabled.
        std::vector<PlanetCloudOverride> CloudOverrides;
        std::vector<PlanetFogOverride> FogOverrides;

        bool operator==(const PlanetProjectSettings&) const noexcept = default;
    };

    inline void ValidatePlanetProjectSettings(const PlanetProjectSettings& settings)
    {
        constexpr std::size_t maximumPlanets = 64;
        if ((settings.Geometry != PlanetGeometryMode::Flat && settings.Geometry != PlanetGeometryMode::Planetary) ||
            (settings.Gravity != PlanetGravityMode::Uniform && settings.Gravity != PlanetGravityMode::Radial &&
             settings.Gravity != PlanetGravityMode::Combined) ||
            (settings.Selection != PlanetSelectionMode::Manual &&
             settings.Selection != PlanetSelectionMode::NearestSurface) ||
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
        std::set<AssetId> overridden;
        for (const auto& profile : settings.AtmosphereOverrides)
        {
            if (!identities.contains(profile.Planet) || !overridden.insert(profile.Planet).second)
            {
                throw std::invalid_argument("Planet atmosphere override has a missing or duplicate planet.");
            }
            const auto planet = std::ranges::find(settings.Planets, profile.Planet, &PlanetDefinition::Planet);
            ValidateAtmosphere(profile.Atmosphere, planet->RadiusMetres);
        }
        overridden.clear();
        for (const auto& profile : settings.CloudOverrides)
        {
            if (!identities.contains(profile.Planet) || !overridden.insert(profile.Planet).second)
            {
                throw std::invalid_argument("Planet cloud override has a missing or duplicate planet.");
            }
            ValidateCloudSettings(profile.Clouds);
        }
        overridden.clear();
        for (const auto& profile : settings.FogOverrides)
        {
            if (!identities.contains(profile.Planet) || !overridden.insert(profile.Planet).second)
            {
                throw std::invalid_argument("Planet fog override has a missing or duplicate planet.");
            }
            ValidateFogDensity(profile.Fog);
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
