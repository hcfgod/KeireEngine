#pragma once

#include "KeireInternal/Environment/AtmosphereValidation.h"

#include <nlohmann/json.hpp>

namespace Keire::Detail
{
    [[nodiscard]] inline nlohmann::json EncodeAtmosphere(const AtmosphereSettings& settings)
    {
        ValidateAtmosphere(settings);
        return {{"enabled", settings.Enabled},
                {"planetCenter", settings.PlanetCenter},
                {"planetRadius", settings.PlanetRadius},
                {"height", settings.Height},
                {"rayleighScaleHeight", settings.RayleighScaleHeight},
                {"mieScaleHeight", settings.MieScaleHeight},
                {"rayleighScattering", settings.RayleighScattering},
                {"mieScattering", settings.MieScattering},
                {"mieExtinction", settings.MieExtinction},
                {"mieAnisotropy", settings.MieAnisotropy},
                {"absorptionExtinction", settings.AbsorptionExtinction},
                {"absorptionPeakAltitude", settings.AbsorptionPeakAltitude},
                {"absorptionHalfWidth", settings.AbsorptionHalfWidth},
                {"solarIrradiance", settings.SolarIrradiance},
                {"sunAngularRadius", settings.SunAngularRadius},
                {"viewSamples", settings.ViewSamples},
                {"lightSamples", settings.LightSamples}};
    }

    [[nodiscard]] inline AtmosphereSettings DecodeAtmosphere(const nlohmann::json& record)
    {
        try
        {
            if (!record.is_object() || record.size() != 17 || !record.at("enabled").is_boolean())
            {
                throw std::invalid_argument("Malformed atmosphere settings.");
            }
            const auto scalar = [&](const char* name)
            {
                if (!record.at(name).is_number())
                {
                    throw std::invalid_argument("Atmosphere scalar must be a number.");
                }
                return record.at(name).get<double>();
            };
            const auto spectrum = [&](const char* name)
            {
                const auto& value = record.at(name);
                if (!value.is_array() || value.size() != 3)
                {
                    throw std::invalid_argument("Atmosphere vector must contain three numbers.");
                }
                std::array<double, 3> result{};
                for (std::size_t axis = 0; axis < result.size(); ++axis)
                {
                    if (!value.at(axis).is_number())
                    {
                        throw std::invalid_argument("Atmosphere vector must contain three numbers.");
                    }
                    result[axis] = value.at(axis).get<double>();
                }
                return result;
            };
            const auto samples = [&](const char* name, const std::uint32_t maximum)
            {
                const auto& value = record.at(name);
                if (!value.is_number_integer() || value < 0 || value > maximum)
                {
                    throw std::invalid_argument("Atmosphere sample budget must be a bounded integer.");
                }
                return value.get<std::uint32_t>();
            };
            AtmosphereSettings result;
            result.Enabled = record.at("enabled").get<bool>();
            result.PlanetCenter = spectrum("planetCenter");
            result.PlanetRadius = scalar("planetRadius");
            result.Height = scalar("height");
            result.RayleighScaleHeight = scalar("rayleighScaleHeight");
            result.MieScaleHeight = scalar("mieScaleHeight");
            result.RayleighScattering = spectrum("rayleighScattering");
            result.MieScattering = scalar("mieScattering");
            result.MieExtinction = scalar("mieExtinction");
            result.MieAnisotropy = scalar("mieAnisotropy");
            result.AbsorptionExtinction = spectrum("absorptionExtinction");
            result.AbsorptionPeakAltitude = scalar("absorptionPeakAltitude");
            result.AbsorptionHalfWidth = scalar("absorptionHalfWidth");
            result.SolarIrradiance = spectrum("solarIrradiance");
            result.SunAngularRadius = scalar("sunAngularRadius");
            result.ViewSamples = samples("viewSamples", 64);
            result.LightSamples = samples("lightSamples", 32);
            ValidateAtmosphere(result);
            return result;
        }
        catch (const nlohmann::json::exception&)
        {
            throw std::invalid_argument("Malformed atmosphere settings fields.");
        }
    }
} // namespace Keire::Detail
