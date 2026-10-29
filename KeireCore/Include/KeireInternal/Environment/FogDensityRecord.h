#pragma once

#include "KeireInternal/Environment/FogDensity.h"

#include <nlohmann/json.hpp>

namespace Keire::Detail
{
    [[nodiscard]] inline nlohmann::json EncodeFogDensity(const FogSettings& settings)
    {
        ValidateFogDensity(settings);
        auto volumes = nlohmann::json::array();
        for (const auto& volume : settings.LocalVolumes)
        {
            volumes.push_back({{"center", volume.Center},
                               {"halfExtent", volume.HalfExtent},
                               {"extinction", volume.ExtinctionPerMeter},
                               {"edgeFraction", volume.EdgeFraction},
                               {"enabled", volume.Enabled}});
        }
        auto exclusions = nlohmann::json::array();
        for (const auto& region : settings.ExclusionRegions)
        {
            exclusions.push_back({{"center", region.Center},
                                  {"halfExtent", region.HalfExtent},
                                  {"edgeFraction", region.EdgeFraction},
                                  {"strength", region.Strength},
                                  {"enabled", region.Enabled}});
        }
        return {{"enabled", settings.Enabled},
                {"baseHeight", settings.BaseHeightMeters},
                {"heightFalloff", settings.HeightFalloffPerMeter},
                {"extinction", settings.ExtinctionPerMeter},
                {"maximumDistance", settings.MaximumDistanceMeters},
                {"albedo", settings.ScatteringAlbedo},
                {"anisotropy", settings.Anisotropy},
                {"viewSteps", settings.ViewSteps},
                {"localVolumes", volumes},
                {"exclusionRegions", exclusions},
                {"planetaryHeight", settings.PlanetaryHeight}};
    }

    [[nodiscard]] inline FogSettings DecodeFogDensity(const nlohmann::json& record)
    {
        try
        {
            const bool hasExclusions = record.contains("exclusionRegions");
            const bool hasPlanetaryHeight = record.contains("planetaryHeight");
            if (!record.is_object() || record.size() != (hasExclusions ? 10U : 9U) + (hasPlanetaryHeight ? 1U : 0U) ||
                (hasPlanetaryHeight && !record.at("planetaryHeight").is_boolean()) ||
                !record.at("enabled").is_boolean())
            {
                throw std::invalid_argument("Malformed fog record.");
            }
            const auto scalar = [](const nlohmann::json& value)
            {
                if (!value.is_number())
                {
                    throw std::invalid_argument("Fog scalar must be numeric.");
                }
                return value.get<double>();
            };
            const auto vector = [&](const nlohmann::json& value)
            {
                if (!value.is_array() || value.size() != 3)
                {
                    throw std::invalid_argument("Fog vector must contain three numbers.");
                }
                return std::array{scalar(value.at(0)), scalar(value.at(1)), scalar(value.at(2))};
            };
            const auto& steps = record.at("viewSteps");
            const auto& volumes = record.at("localVolumes");
            if (!steps.is_number_integer() || steps < 1 || steps > 128 || !volumes.is_array() ||
                volumes.size() > MaximumLocalFogVolumes)
            {
                throw std::invalid_argument("Fog quality or local volume count exceeds its budget.");
            }
            FogSettings settings;
            settings.Enabled = record.at("enabled").get<bool>();
            settings.PlanetaryHeight = hasPlanetaryHeight ? record.at("planetaryHeight").get<bool>() : false;
            settings.BaseHeightMeters = scalar(record.at("baseHeight"));
            settings.HeightFalloffPerMeter = scalar(record.at("heightFalloff"));
            settings.ExtinctionPerMeter = scalar(record.at("extinction"));
            settings.MaximumDistanceMeters = scalar(record.at("maximumDistance"));
            settings.ScatteringAlbedo = vector(record.at("albedo"));
            settings.Anisotropy = scalar(record.at("anisotropy"));
            settings.ViewSteps = steps.get<std::uint32_t>();
            for (const auto& volume : volumes)
            {
                const bool hasEnabled = volume.contains("enabled");
                if (!volume.is_object() || volume.size() != (hasEnabled ? 5U : 4U) ||
                    (hasEnabled && !volume.at("enabled").is_boolean()))
                {
                    throw std::invalid_argument("Malformed local fog volume.");
                }
                settings.LocalVolumes.push_back({vector(volume.at("center")), vector(volume.at("halfExtent")),
                                                 scalar(volume.at("extinction")), scalar(volume.at("edgeFraction")),
                                                 hasEnabled ? volume.at("enabled").get<bool>() : true});
            }
            if (hasExclusions)
            {
                const auto& exclusions = record.at("exclusionRegions");
                if (!exclusions.is_array() || exclusions.size() > MaximumFogExclusionRegions)
                {
                    throw std::invalid_argument("Fog exclusion count exceeds its budget.");
                }
                for (const auto& region : exclusions)
                {
                    if (!region.is_object() || region.size() != 5 || !region.at("enabled").is_boolean())
                    {
                        throw std::invalid_argument("Malformed fog exclusion region.");
                    }
                    settings.ExclusionRegions.push_back({vector(region.at("center")), vector(region.at("halfExtent")),
                                                         scalar(region.at("edgeFraction")),
                                                         scalar(region.at("strength")),
                                                         region.at("enabled").get<bool>()});
                }
            }
            ValidateFogDensity(settings);
            return settings;
        }
        catch (const nlohmann::json::exception&)
        {
            throw std::invalid_argument("Malformed fog record.");
        }
    }
} // namespace Keire::Detail
