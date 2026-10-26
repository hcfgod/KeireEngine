#pragma once

#include "KeireInternal/Environment/VolumetricCloudValidation.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace Keire::Detail
{
    [[nodiscard]] inline nlohmann::json EncodeVolumetricCloudSettings(const VolumetricCloudSettings& settings)
    {
        ValidateVolumetricCloudSettings(settings);
        return {{"version", 1},
                {"enabled", settings.Enabled},
                {"baseHeightMeters", settings.BaseHeightMeters},
                {"thicknessMeters", settings.ThicknessMeters},
                {"patternSizeMeters", settings.PatternSizeMeters},
                {"coverage", settings.Coverage},
                {"extinctionPerMeter", settings.ExtinctionPerMeter},
                {"detailAmount", settings.DetailAmount},
                {"anisotropy", settings.Anisotropy},
                {"seed", settings.Seed},
                {"windMetersPerSecond", settings.WindMetersPerSecond},
                {"scatteringAlbedo", settings.ScatteringAlbedo},
                {"viewSteps", settings.ViewSteps},
                {"shadowSteps", settings.ShadowSteps},
                {"maximumDistanceMeters", settings.MaximumDistanceMeters}};
    }

    [[nodiscard]] inline VolumetricCloudSettings DecodeVolumetricCloudSettings(const nlohmann::json& record)
    {
        try
        {
            if (!record.is_object() || record.size() != 15 || !record.at("version").is_number_integer() ||
                record.at("version") != 1 || !record.at("enabled").is_boolean())
            {
                throw std::invalid_argument("Malformed or unsupported volumetric cloud record.");
            }
            const auto scalar = [&](const char* name)
            {
                if (!record.at(name).is_number())
                {
                    throw std::invalid_argument("Volumetric cloud scalar must be numeric.");
                }
                return record.at(name).get<double>();
            };
            const auto integer = [&](const char* name)
            {
                const auto& value = record.at(name);
                if (!value.is_number_integer() || value < 0 || value > 0xffffffffULL)
                {
                    throw std::invalid_argument("Volumetric cloud integer must fit uint32.");
                }
                return value.get<std::uint32_t>();
            };
            const auto vector = [&](const char* name, auto& output)
            {
                const auto& input = record.at(name);
                if (!input.is_array() || input.size() != output.size())
                {
                    throw std::invalid_argument("Invalid volumetric cloud vector length.");
                }
                for (std::size_t index = 0; index < output.size(); ++index)
                {
                    if (!input.at(index).is_number())
                    {
                        throw std::invalid_argument("Volumetric cloud vector must contain numbers.");
                    }
                    output[index] = input.at(index).get<double>();
                }
            };

            VolumetricCloudSettings result;
            result.Enabled = record.at("enabled").get<bool>();
            result.BaseHeightMeters = scalar("baseHeightMeters");
            result.ThicknessMeters = scalar("thicknessMeters");
            result.PatternSizeMeters = scalar("patternSizeMeters");
            result.Coverage = scalar("coverage");
            result.ExtinctionPerMeter = scalar("extinctionPerMeter");
            result.DetailAmount = scalar("detailAmount");
            result.Anisotropy = scalar("anisotropy");
            result.Seed = integer("seed");
            vector("windMetersPerSecond", result.WindMetersPerSecond);
            vector("scatteringAlbedo", result.ScatteringAlbedo);
            result.ViewSteps = integer("viewSteps");
            result.ShadowSteps = integer("shadowSteps");
            result.MaximumDistanceMeters = scalar("maximumDistanceMeters");
            ValidateVolumetricCloudSettings(result);
            return result;
        }
        catch (const nlohmann::json::exception&)
        {
            throw std::invalid_argument("Malformed volumetric cloud fields.");
        }
    }
} // namespace Keire::Detail
