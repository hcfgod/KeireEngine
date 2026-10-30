#pragma once

#include "KeireInternal/Spatial/PlanetDefinitionRecord.h"
#include "KeireInternal/Spatial/PlanetProjectSettings.h"

#include <nlohmann/json.hpp>

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
            return {{"schemaVersion", 2},
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
                      (record.at("schemaVersion") == 2 && record.size() == 6)) ||
                    !record.at("planets").is_array() || record.at("planets").size() > 64)
                {
                    throw std::invalid_argument("Unsupported planet project settings record.");
                }
                PlanetProjectSettings settings;
                if (record.at("schemaVersion") == 2)
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
