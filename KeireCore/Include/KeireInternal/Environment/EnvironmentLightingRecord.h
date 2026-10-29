#pragma once

#include "KeireInternal/Environment/EnvironmentLightingProfile.h"
#include "KeireInternal/Environment/EnvironmentSkyRecord.h"

#include <nlohmann/json.hpp>

#include <string>

namespace Keire::Detail
{
    // Internal versioned payload; project/asset persistence and migration are separate adapters.
    class EnvironmentLightingRecord final
    {
      public:
        [[nodiscard]] static nlohmann::json Encode(const EnvironmentLightingProfile& profile)
        {
            auto keys = nlohmann::json::array();
            const bool ambient = !profile.Keyframes().empty() && profile.Keyframes().front().Ambient.has_value();
            for (const auto& key : profile.Keyframes())
            {
                keys.push_back({{"phase", key.DayFraction},
                                {"azimuth", key.AzimuthDegrees},
                                {"elevation", key.ElevationDegrees},
                                {"color", key.LinearColor},
                                {"intensity", key.Intensity}});
                if (ambient)
                {
                    keys.back()["ambient"] = {{"color", key.Ambient->LinearColor},
                                              {"intensity", key.Ambient->Intensity}};
                }
            }
            const auto shadows = profile.Shadows();
            nlohmann::json result{{"version", profile.DirectionFrame() != EnvironmentDirectionalFrame::LegacyWorld ? 5
                                              : profile.Sky()                                                      ? 4
                                              : ambient                                                            ? 3
                                                                                                                   : 2},
                                  {"ownership", profile.Ownership() == EnvironmentDirectionalOwnership::Authored
                                                    ? "authored"
                                                    : "environment"},
                                  {"keyframes", std::move(keys)},
                                  {"shadows",
                                   {{"quality", static_cast<int>(shadows.Quality)},
                                    {"resolution", static_cast<int>(shadows.Resolution)},
                                    {"strength", shadows.Strength},
                                    {"bias", shadows.Bias}}}};
            if (profile.Sky())
            {
                result["sky"] = EncodeEnvironmentSky(*profile.Sky());
            }
            if (profile.DirectionFrame() != EnvironmentDirectionalFrame::LegacyWorld)
            {
                result["directionFrame"] = profile.DirectionFrame() == EnvironmentDirectionalFrame::PlanetFixed
                                               ? "planet-fixed"
                                               : "inertial-root";
            }
            return result;
        }

        [[nodiscard]] static EnvironmentLightingProfile Decode(const nlohmann::json& record)
        {
            try
            {
                if (!record.is_object() || !record.at("version").is_number_integer() ||
                    (record.at("version") != 1 && record.at("version") != 2 && record.at("version") != 3 &&
                     record.at("version") != 4 && record.at("version") != 5) ||
                    record.size() != (record.at("version") == 1   ? 3U
                                      : record.at("version") == 5 ? (record.contains("sky") ? 6U : 5U)
                                      : record.at("version") == 4 ? 5U
                                                                  : 4U) ||
                    !record.at("ownership").is_string() || !record.at("keyframes").is_array() ||
                    record.at("keyframes").size() > EnvironmentLightingProfile::MaximumKeyframes)
                {
                    throw std::invalid_argument("Unsupported or malformed environment lighting record.");
                }
                EnvironmentShadowSettings shadows;
                auto directionFrame = EnvironmentDirectionalFrame::LegacyWorld;
                if (record.at("version") == 5)
                {
                    const auto frame = record.at("directionFrame").get<std::string>();
                    if (frame != "planet-fixed" && frame != "inertial-root")
                    {
                        throw std::invalid_argument("Unknown daily-light reference frame.");
                    }
                    directionFrame = frame == "planet-fixed" ? EnvironmentDirectionalFrame::PlanetFixed
                                                             : EnvironmentDirectionalFrame::InertialRoot;
                }
                if (record.at("version") != 1)
                {
                    const auto& encoded = record.at("shadows");
                    if (!encoded.is_object() || encoded.size() != 4 || !encoded.at("quality").is_number_integer() ||
                        encoded.at("quality") < 0 || encoded.at("quality") > 2 ||
                        !encoded.at("resolution").is_number_integer() || encoded.at("resolution") < 0 ||
                        encoded.at("resolution") > 3 || !encoded.at("strength").is_number() ||
                        !encoded.at("bias").is_number() || encoded.at("strength") < 0 || encoded.at("strength") > 1 ||
                        encoded.at("bias") < 0 || encoded.at("bias") > 1)
                    {
                        throw std::invalid_argument("Malformed environment shadow settings.");
                    }
                    shadows.Quality = static_cast<ShadowQuality>(encoded.at("quality").get<int>());
                    shadows.Resolution = static_cast<ShadowResolutionHint>(encoded.at("resolution").get<int>());
                    shadows.Strength = encoded.at("strength").get<float>();
                    shadows.Bias = encoded.at("bias").get<float>();
                }
                const auto& ownership = record.at("ownership").get_ref<const std::string&>();
                if (ownership != "authored" && ownership != "environment")
                {
                    throw std::invalid_argument("Unknown environment lighting ownership.");
                }
                std::vector<EnvironmentLightingKeyframe> keys;
                keys.reserve(record.at("keyframes").size());
                for (const auto& encoded : record.at("keyframes"))
                {
                    const bool ambient =
                        record.at("version") == 3 || (record.at("version") >= 4 && encoded.contains("ambient"));
                    if (!encoded.is_object() || encoded.size() != (ambient ? 6U : 5U) ||
                        !encoded.at("phase").is_number() || !encoded.at("azimuth").is_number() ||
                        !encoded.at("elevation").is_number() || !encoded.at("intensity").is_number() ||
                        !encoded.at("color").is_array() || encoded.at("color").size() != 3)
                    {
                        throw std::invalid_argument("Malformed environment lighting keyframe record.");
                    }
                    EnvironmentLightingKeyframe key;
                    key.DayFraction = encoded.at("phase").get<double>();
                    key.AzimuthDegrees = encoded.at("azimuth").get<double>();
                    key.ElevationDegrees = encoded.at("elevation").get<double>();
                    key.Intensity = encoded.at("intensity").get<double>();
                    if (ambient)
                    {
                        const auto& value = encoded.at("ambient");
                        if (!value.is_object() || value.size() != 2 || !value.at("color").is_array() ||
                            value.at("color").size() != 3 || !value.at("intensity").is_number())
                        {
                            throw std::invalid_argument("Malformed environment ambient keyframe.");
                        }
                        EnvironmentAmbientLight lighting;
                        lighting.Intensity = value.at("intensity").get<float>();
                        for (std::size_t channel = 0; channel < lighting.LinearColor.size(); ++channel)
                        {
                            if (!value.at("color").at(channel).is_number())
                            {
                                throw std::invalid_argument("Ambient color must contain three numbers.");
                            }
                            lighting.LinearColor[channel] = value.at("color").at(channel).get<float>();
                        }
                        key.Ambient = lighting;
                    }
                    for (std::size_t channel = 0; channel < key.LinearColor.size(); ++channel)
                    {
                        const auto& value = encoded.at("color").at(channel);
                        if (!value.is_number())
                        {
                            throw std::invalid_argument("Environment lighting color must contain three numbers.");
                        }
                        key.LinearColor[channel] = value.get<double>();
                    }
                    keys.push_back(key);
                }
                return EnvironmentLightingProfile(
                    ownership == "authored" ? EnvironmentDirectionalOwnership::Authored
                                            : EnvironmentDirectionalOwnership::Environment,
                    std::move(keys), shadows,
                    record.contains("sky") ? std::optional(DecodeEnvironmentSky(record.at("sky"))) : std::nullopt,
                    directionFrame);
            }
            catch (const nlohmann::json::exception&)
            {
                throw std::invalid_argument("Malformed environment lighting record fields.");
            }
        }
    };
} // namespace Keire::Detail
