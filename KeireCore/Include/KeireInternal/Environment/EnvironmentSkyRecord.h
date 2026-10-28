#pragma once

#include "KeireInternal/Environment/EnvironmentColorTrackRecord.h"
#include "KeireInternal/Environment/EnvironmentSkyProfile.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace Keire::Detail
{
    [[nodiscard]] inline nlohmann::json EncodeEnvironmentSky(const EnvironmentSkyProfile& sky)
    {
        sky.Validate();
        return {{"enabled", sky.Enabled},
                {"horizon", EnvironmentColorTrackRecord::Encode(sky.Horizon)},
                {"zenith", EnvironmentColorTrackRecord::Encode(sky.Zenith)},
                {"weight", sky.BlendWeight},
                {"exponent", sky.GradientExponent}};
    }

    [[nodiscard]] inline EnvironmentSkyProfile DecodeEnvironmentSky(const nlohmann::json& record)
    {
        try
        {
            if (!record.is_object() || record.size() != 5 || !record.at("enabled").is_boolean() ||
                !record.at("weight").is_number() || !record.at("exponent").is_number())
            {
                throw std::invalid_argument("Malformed environment sky profile.");
            }
            EnvironmentSkyProfile sky;
            sky.Enabled = record.at("enabled").get<bool>();
            sky.Horizon = EnvironmentColorTrackRecord::Decode(record.at("horizon"));
            sky.Zenith = EnvironmentColorTrackRecord::Decode(record.at("zenith"));
            sky.BlendWeight = record.at("weight").get<double>();
            sky.GradientExponent = record.at("exponent").get<double>();
            sky.Validate();
            return sky;
        }
        catch (const nlohmann::json::exception&)
        {
            throw std::invalid_argument("Malformed environment sky profile fields.");
        }
    }
} // namespace Keire::Detail
