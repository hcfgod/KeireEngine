#pragma once

#include "KeireInternal/Environment/AtmosphereRecord.h"
#include "KeireInternal/Spatial/PlanetDefinitionRecord.h"
#include "KeireInternal/Spatial/PlanetProjectSettings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire::Detail
{
    class PlanetProjectSettingsRecord final
    {
      public:
        [[nodiscard]] static nlohmann::json Encode(const PlanetProjectSettings& settings)
        {
            ValidatePlanetProjectSettings(settings);
            auto planets = nlohmann::json::array();
            for (const auto& planet : settings.Planets)
            {
                planets.push_back(PlanetDefinitionRecord::Encode(planet));
            }
            auto overrides = nlohmann::json::array();
            for (const auto& profile : settings.AtmosphereOverrides)
            {
                const auto planet = std::ranges::find(settings.Planets, profile.Planet, &PlanetDefinition::Planet);
                overrides.push_back({{"planet", profile.Planet.ToString()},
                                     {"atmosphere", EncodeAtmosphere(profile.Atmosphere, planet->RadiusMetres)}});
            }
            return {{"schemaVersion", 3},
                    {"atmosphereOverrides", std::move(overrides)},
                    {"selection", settings.Selection == PlanetSelectionMode::Manual ? "manual" : "nearest-surface"},
                    {"geometry", settings.Geometry == PlanetGeometryMode::Flat ? "flat" : "planetary"},
                    {"gravity", settings.Gravity == PlanetGravityMode::Uniform  ? "uniform"
                                : settings.Gravity == PlanetGravityMode::Radial ? "radial"
                                                                                : "combined"},
                    {"selectedPlanet", settings.SelectedPlanet ? nlohmann::json(settings.SelectedPlanet.ToString())
                                                               : nlohmann::json(nullptr)},
                    {"planets", std::move(planets)}};
        }

        [[nodiscard]] static PlanetProjectSettings Decode(const nlohmann::json& record)
        {
            try
            {
                if (!record.is_object() || !record.at("schemaVersion").is_number_integer() ||
                    !((record.at("schemaVersion") == 1 && record.size() == 5) ||
                      (record.at("schemaVersion") == 2 && record.size() == 6) ||
                      (record.at("schemaVersion") == 3 && record.size() == 7)) ||
                    !record.at("planets").is_array() || record.at("planets").size() > 64)
                {
                    throw std::invalid_argument("Unsupported planet project settings record.");
                }
                PlanetProjectSettings settings;
                if (record.at("schemaVersion") >= 2)
                {
                    const auto selection = record.at("selection").get<std::string>();
                    if (selection != "manual" && selection != "nearest-surface")
                    {
                        throw std::invalid_argument("Unknown planet selection mode.");
                    }
                    settings.Selection =
                        selection == "manual" ? PlanetSelectionMode::Manual : PlanetSelectionMode::NearestSurface;
                }
                const auto geometry = record.at("geometry").get<std::string>();
                const auto gravity = record.at("gravity").get<std::string>();
                if ((geometry != "flat" && geometry != "planetary") ||
                    (gravity != "uniform" && gravity != "radial" && gravity != "combined"))
                {
                    throw std::invalid_argument("Unknown planet geometry or gravity mode.");
                }
                settings.Geometry = geometry == "flat" ? PlanetGeometryMode::Flat : PlanetGeometryMode::Planetary;
                settings.Gravity = gravity == "uniform"  ? PlanetGravityMode::Uniform
                                   : gravity == "radial" ? PlanetGravityMode::Radial
                                                         : PlanetGravityMode::Combined;
                if (!record.at("selectedPlanet").is_null())
                {
                    settings.SelectedPlanet = AssetId::Parse(record.at("selectedPlanet").get<std::string>());
                    if (!settings.SelectedPlanet)
                    {
                        throw std::invalid_argument("An unselected planet must be represented by null.");
                    }
                }
                for (const auto& planet : record.at("planets"))
                {
                    settings.Planets.push_back(PlanetDefinitionRecord::Decode(planet));
                }
                if (record.at("schemaVersion") >= 3)
                {
                    const auto& overrides = record.at("atmosphereOverrides");
                    if (!overrides.is_array() || overrides.size() > settings.Planets.size())
                    {
                        throw std::invalid_argument("Planet atmosphere override collection is invalid.");
                    }
                    for (const auto& profile : overrides)
                    {
                        if (!profile.is_object() || profile.size() != 2)
                        {
                            throw std::invalid_argument("Planet atmosphere override record is invalid.");
                        }
                        const auto identity = AssetId::Parse(profile.at("planet").get<std::string>());
                        const auto planet = std::ranges::find(settings.Planets, identity, &PlanetDefinition::Planet);
                        if (planet == settings.Planets.end())
                        {
                            throw std::invalid_argument("Planet atmosphere override references a missing planet.");
                        }
                        settings.AtmosphereOverrides.push_back(
                            {identity, DecodeAtmosphere(profile.at("atmosphere"), planet->RadiusMetres)});
                    }
                }
                ValidatePlanetProjectSettings(settings);
                return settings;
            }
            catch (const nlohmann::json::exception& error)
            {
                throw std::invalid_argument(std::string("Malformed planet project settings: ") + error.what());
            }
        }
    };
} // namespace Keire::Detail
