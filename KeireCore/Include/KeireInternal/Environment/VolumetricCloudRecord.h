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
        return {{"version", 9},
                {"baseHeightVariationMeters", settings.BaseHeightVariationMeters},
                {"independentWeather", settings.IndependentWeather},
                {"cloudType", settings.CloudType},
                {"typeVariation", settings.TypeVariation},
                {"heightProfile", settings.HeightProfile},
                {"ambientOcclusionStrength", settings.AmbientOcclusionStrength},
                {"enabled", settings.Enabled},
                {"baseHeightMeters", settings.BaseHeightMeters},
                {"thicknessMeters", settings.ThicknessMeters},
                {"patternSizeMeters", settings.PatternSizeMeters},
                {"coverage", settings.Coverage},
                {"extinctionPerMeter", settings.ExtinctionPerMeter},
                {"detailAmount", settings.DetailAmount},
                {"erosionAmount", settings.ErosionAmount},
                {"detailFrequency", settings.DetailFrequency},
                {"variationStrength", settings.VariationStrength},
                {"variationPeriod", settings.VariationPeriod},
                {"densityContrast", settings.DensityContrast},
                {"regionalCoverageStrength", settings.RegionalCoverageStrength},
                {"regionalCoveragePeriod", settings.RegionalCoveragePeriod},
                {"anisotropy", settings.Anisotropy},
                {"seed", settings.Seed},
                {"windMetersPerSecond", settings.WindMetersPerSecond},
                {"scatteringAlbedo", settings.ScatteringAlbedo},
                {"viewSteps", settings.ViewSteps},
                {"shadowSteps", settings.ShadowSteps},
                {"maximumDistanceMeters", settings.MaximumDistanceMeters},
                {"multipleScatteringContribution", settings.MultipleScatteringContribution},
                {"multipleScatteringOcclusion", settings.MultipleScatteringOcclusion}};
    }

    [[nodiscard]] inline VolumetricCloudSettings DecodeVolumetricCloudSettings(const nlohmann::json& record)
    {
        try
        {
            if (!record.is_object() || !record.at("version").is_number_integer() ||
                (record.at("version") != 1 && record.at("version") != 2 && record.at("version") != 3 &&
                 record.at("version") != 4 && record.at("version") != 5 && record.at("version") != 6 &&
                 record.at("version") != 7 && record.at("version") != 8 && record.at("version") != 9) ||
                record.size() != (record.at("version") == 1   ? 15U
                                  : record.at("version") == 2 ? 17U
                                  : record.at("version") == 3 ? 21U
                                  : record.at("version") == 4 ? 23U
                                  : record.at("version") == 5 ? 24U
                                  : record.at("version") == 6 ? 25U
                                  : record.at("version") == 7 ? 26U
                                  : record.at("version") == 8 ? 29U
                                                              : 30U) ||
                !record.at("enabled").is_boolean())
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
            result.BaseHeightVariationMeters = record.at("version") >= 9 ? scalar("baseHeightVariationMeters") : 0.0;
            result.AmbientOcclusionStrength = record.at("version") >= 6 ? scalar("ambientOcclusionStrength") : 0.0;
            if (record.at("version") >= 7)
            {
                vector("heightProfile", result.HeightProfile);
            }
            if (record.at("version") >= 8)
            {
                if (!record.at("independentWeather").is_boolean())
                {
                    throw std::invalid_argument("Independent weather selection must be boolean.");
                }
                result.IndependentWeather = record.at("independentWeather").get<bool>();
                result.CloudType = scalar("cloudType");
                result.TypeVariation = scalar("typeVariation");
            }
            result.Enabled = record.at("enabled").get<bool>();
            result.BaseHeightMeters = scalar("baseHeightMeters");
            result.ThicknessMeters = scalar("thicknessMeters");
            result.PatternSizeMeters = scalar("patternSizeMeters");
            result.Coverage = scalar("coverage");
            result.ExtinctionPerMeter = scalar("extinctionPerMeter");
            result.DetailAmount = scalar("detailAmount");
            // Earlier records used the same control for shape detail and erosion.
            result.ErosionAmount = record.at("version") >= 5 ? scalar("erosionAmount") : result.DetailAmount;
            if (record.at("version") >= 3)
            {
                result.DetailFrequency = integer("detailFrequency");
                result.VariationStrength = scalar("variationStrength");
                result.VariationPeriod = integer("variationPeriod");
                result.DensityContrast = scalar("densityContrast");
            }
            if (record.at("version") >= 4)
            {
                result.RegionalCoverageStrength = scalar("regionalCoverageStrength");
                result.RegionalCoveragePeriod = integer("regionalCoveragePeriod");
            }
            result.Anisotropy = scalar("anisotropy");
            result.Seed = integer("seed");
            vector("windMetersPerSecond", result.WindMetersPerSecond);
            vector("scatteringAlbedo", result.ScatteringAlbedo);
            result.ViewSteps = integer("viewSteps");
            result.ShadowSteps = integer("shadowSteps");
            result.MaximumDistanceMeters = scalar("maximumDistanceMeters");
            // Existing authored volumes retain their single-scattering appearance.
            result.MultipleScatteringContribution =
                record.at("version") == 1 ? 0.0 : scalar("multipleScatteringContribution");
            result.MultipleScatteringOcclusion =
                record.at("version") == 1 ? 0.5 : scalar("multipleScatteringOcclusion");
            ValidateVolumetricCloudSettings(result);
            return result;
        }
        catch (const nlohmann::json::exception&)
        {
            throw std::invalid_argument("Malformed volumetric cloud fields.");
        }
    }
} // namespace Keire::Detail
