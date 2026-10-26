#pragma once

#include "KeireInternal/Environment/CloudEvaluation.h"
#include "KeireInternal/Environment/VolumetricCloudRecord.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace Keire::Detail
{
    /// Internal versioned authoring record. Runtime phase and preview state are deliberately not persisted.
    [[nodiscard]] inline nlohmann::json EncodeCloudSettings(const CloudSettings& settings)
    {
        ValidateCloudSettings(settings);
        auto layers = nlohmann::json::array();
        for (const auto& layer : settings.Layers)
        {
            layers.push_back(
                {{"enabled", layer.Enabled},
                 {"altitudeMeters", layer.AltitudeMeters},
                 {"patternSizeMeters", layer.PatternSizeMeters},
                 {"coverage", layer.Coverage},
                 {"opacity", layer.Opacity},
                 {"edgeSoftness", layer.EdgeSoftness},
                 {"horizonFade", layer.HorizonFade},
                 {"windMetersPerSecond", layer.WindMetersPerSecond},
                 {"linearColor", layer.LinearColor},
                 {"appearance", layer.Appearance == CloudAppearance::NaturalLit ? "natural-lit" : "legacy-unlit"},
                 {"seed", layer.Seed}});
        }
        // Retain the existing sheet-only format when there is no authored volume state to preserve.
        if (settings.Volume == VolumetricCloudSettings{})
        {
            return {{"version", 2}, {"layers", std::move(layers)}};
        }
        return {
            {"version", 3}, {"layers", std::move(layers)}, {"volume", EncodeVolumetricCloudSettings(settings.Volume)}};
    }

    [[nodiscard]] inline CloudSettings DecodeCloudSettings(const nlohmann::json& record)
    {
        try
        {
            if (!record.is_object() || !record.at("version").is_number_integer() ||
                (record.at("version") != 1 && record.at("version") != 2 && record.at("version") != 3) ||
                record.size() != (record.at("version") == 3 ? 3U : 2U) || !record.at("layers").is_array() ||
                record.at("layers").size() != MaximumCloudLayers)
            {
                throw std::invalid_argument("Malformed or unsupported cloud settings record.");
            }
            CloudSettings result;
            if (record.at("version") == 3)
            {
                result.Volume = DecodeVolumetricCloudSettings(record.at("volume"));
            }
            const bool legacy = record.at("version") == 1;
            for (std::size_t index = 0; index < MaximumCloudLayers; ++index)
            {
                const auto& input = record.at("layers").at(index);
                if (!input.is_object() || input.size() != (legacy ? 9U : 11U) || !input.at("enabled").is_boolean())
                {
                    throw std::invalid_argument("Malformed cloud layer fields.");
                }
                const auto scalar = [&](const char* name)
                {
                    if (!input.at(name).is_number())
                    {
                        throw std::invalid_argument("Cloud scalar must be numeric.");
                    }
                    return input.at(name).get<double>();
                };
                const auto vector = [&](const char* name, auto& output)
                {
                    const auto& value = input.at(name);
                    if (!value.is_array() || value.size() != output.size())
                    {
                        throw std::invalid_argument("Invalid cloud vector length.");
                    }
                    for (std::size_t axis = 0; axis < output.size(); ++axis)
                    {
                        if (!value.at(axis).is_number())
                        {
                            throw std::invalid_argument("Cloud vector must contain numbers.");
                        }
                        output[axis] = value.at(axis).get<double>();
                    }
                };
                auto& output = result.Layers[index];
                output.Enabled = input.at("enabled").get<bool>();
                output.AltitudeMeters = scalar("altitudeMeters");
                output.PatternSizeMeters = scalar("patternSizeMeters");
                output.Coverage = scalar("coverage");
                output.Opacity = scalar("opacity");
                output.EdgeSoftness = scalar("edgeSoftness");
                output.HorizonFade = scalar("horizonFade");
                vector("windMetersPerSecond", output.WindMetersPerSecond);
                vector("linearColor", output.LinearColor);
                output.Appearance = CloudAppearance::LegacyUnlit;
                if (!legacy)
                {
                    const auto& appearance = input.at("appearance");
                    if (!appearance.is_string() || (appearance != "legacy-unlit" && appearance != "natural-lit"))
                    {
                        throw std::invalid_argument("Unsupported cloud appearance model.");
                    }
                    output.Appearance =
                        appearance == "natural-lit" ? CloudAppearance::NaturalLit : CloudAppearance::LegacyUnlit;
                    const auto& seed = input.at("seed");
                    if (!seed.is_number_integer() || seed < 0 || seed > 0xffffffffULL)
                    {
                        throw std::invalid_argument("Cloud seed must be a uint32 integer.");
                    }
                    output.Seed = seed.get<std::uint32_t>();
                }
            }
            ValidateCloudSettings(result);
            return result;
        }
        catch (const nlohmann::json::exception&)
        {
            throw std::invalid_argument("Malformed cloud settings fields.");
        }
    }
} // namespace Keire::Detail
