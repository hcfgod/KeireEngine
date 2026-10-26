#pragma once

#include "Keire/Rendering/StarField.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace Keire::Detail
{
    inline void ValidateStarField(const StarFieldSettings& settings)
    {
        const auto bounded = [](const double value, const double minimum, const double maximum)
        { return std::isfinite(value) && value >= minimum && value <= maximum; };
        if (!bounded(settings.Density, 0.0, 1.0) || !bounded(settings.Intensity, 0.0, 1000.0) ||
            !bounded(settings.ColorVariation, 0.0, 1.0) || !bounded(settings.AngularRadius, 0.000001, 0.01) ||
            !bounded(settings.RotationDegrees, 0.0, 360.0) || !bounded(settings.TwinkleAmplitude, 0.0, 1.0) ||
            !bounded(settings.TwinkleFrequency, 0.0, 10.0))
        {
            throw std::invalid_argument("Star appearance parameters must be finite and within supported bounds.");
        }
        for (const double channel : settings.Tint)
        {
            if (!bounded(channel, 0.0, 1.0))
            {
                throw std::invalid_argument("Star tint must contain linear RGB values in 0..1.");
            }
        }
        if (!bounded(settings.FadeStartElevation, -90.0, 90.0) ||
            !bounded(settings.FullVisibilityElevation, -90.0, 90.0) ||
            settings.FullVisibilityElevation >= settings.FadeStartElevation)
        {
            throw std::invalid_argument("Full star visibility must be below the fade-start solar elevation.");
        }
    }

    [[nodiscard]] inline nlohmann::json EncodeStarField(const StarFieldSettings& settings)
    {
        ValidateStarField(settings);
        return {{"version", 1},
                {"enabled", settings.Enabled},
                {"seed", settings.Seed},
                {"density", settings.Density},
                {"intensity", settings.Intensity},
                {"tint", settings.Tint},
                {"colorVariation", settings.ColorVariation},
                {"angularRadius", settings.AngularRadius},
                {"rotationDegrees", settings.RotationDegrees},
                {"twinkleAmplitude", settings.TwinkleAmplitude},
                {"twinkleFrequency", settings.TwinkleFrequency},
                {"fadeStartElevation", settings.FadeStartElevation},
                {"fullVisibilityElevation", settings.FullVisibilityElevation}};
    }

    [[nodiscard]] inline StarFieldSettings DecodeStarField(const nlohmann::json& record)
    {
        try
        {
            if (!record.is_object() || record.size() != 13 || !record.at("version").is_number_integer() ||
                record.at("version") != 1 || !record.at("enabled").is_boolean())
            {
                throw std::invalid_argument("Malformed or unsupported star-field record.");
            }
            const auto& seed = record.at("seed");
            if (!seed.is_number_integer() || seed < 0 || seed > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument("Star seed must be an unsigned 32-bit integer.");
            }
            const auto scalar = [&](const char* field)
            {
                const auto& value = record.at(field);
                if (!value.is_number())
                {
                    throw std::invalid_argument("Star parameters must be numeric.");
                }
                return value.get<double>();
            };
            StarFieldSettings result;
            result.Enabled = record.at("enabled").get<bool>();
            result.Seed = seed.get<std::uint32_t>();
            result.Density = scalar("density");
            result.Intensity = scalar("intensity");
            result.ColorVariation = scalar("colorVariation");
            result.AngularRadius = scalar("angularRadius");
            result.RotationDegrees = scalar("rotationDegrees");
            result.TwinkleAmplitude = scalar("twinkleAmplitude");
            result.TwinkleFrequency = scalar("twinkleFrequency");
            result.FadeStartElevation = scalar("fadeStartElevation");
            result.FullVisibilityElevation = scalar("fullVisibilityElevation");
            const auto& tint = record.at("tint");
            if (!tint.is_array() || tint.size() != result.Tint.size())
            {
                throw std::invalid_argument("Star tint must contain three numeric channels.");
            }
            for (std::size_t channel = 0; channel < result.Tint.size(); ++channel)
            {
                if (!tint.at(channel).is_number())
                {
                    throw std::invalid_argument("Star tint channels must be numeric.");
                }
                result.Tint[channel] = tint.at(channel).get<double>();
            }
            ValidateStarField(result);
            return result;
        }
        catch (const nlohmann::json::exception&)
        {
            throw std::invalid_argument("Malformed star-field settings fields.");
        }
    }
} // namespace Keire::Detail
