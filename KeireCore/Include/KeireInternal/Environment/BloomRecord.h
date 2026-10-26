#pragma once

#include "KeireInternal/Environment/BloomEvaluation.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace Keire::Detail
{
    [[nodiscard]] inline nlohmann::json EncodeBloomSettings(const BloomSettings& settings)
    {
        ValidateBloomSettings(settings);
        return {{"version", 1},
                {"enabled", settings.Enabled},
                {"intensity", settings.Intensity},
                {"threshold", settings.Threshold},
                {"softKnee", settings.SoftKnee},
                {"radius", settings.Radius}};
    }

    [[nodiscard]] inline BloomSettings DecodeBloomSettings(const nlohmann::json& record)
    {
        try
        {
            if (!record.is_object() || record.size() != 6 || !record.at("version").is_number_integer() ||
                record.at("version") != 1 || !record.at("enabled").is_boolean())
            {
                throw std::invalid_argument("Malformed or unsupported bloom settings record.");
            }
            const auto scalar = [&](const char* name)
            {
                const auto& value = record.at(name);
                if (!value.is_number())
                {
                    throw std::invalid_argument("Bloom settings values must be numeric.");
                }
                return value.get<double>();
            };
            BloomSettings result;
            result.Enabled = record.at("enabled").get<bool>();
            result.Intensity = scalar("intensity");
            result.Threshold = scalar("threshold");
            result.SoftKnee = scalar("softKnee");
            result.Radius = scalar("radius");
            ValidateBloomSettings(result);
            return result;
        }
        catch (const nlohmann::json::exception&)
        {
            throw std::invalid_argument("Malformed bloom settings fields.");
        }
    }
} // namespace Keire::Detail
