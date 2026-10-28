#pragma once

#include "KeireInternal/Environment/EnvironmentColorTrack.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    // Internal payload for named sky tracks; the containing project/runtime schema owns feature availability.
    class EnvironmentColorTrackRecord final
    {
      public:
        [[nodiscard]] static nlohmann::json Encode(const EnvironmentColorTrack& track)
        {
            auto keys = nlohmann::json::array();
            for (const auto& key : track.Keys())
            {
                keys.push_back({{"phase", key.DayFraction},
                                {"color", key.LinearColor},
                                {"interpolation", static_cast<unsigned>(key.Interpolation)}});
            }
            return {{"version", 1}, {"keys", std::move(keys)}};
        }

        [[nodiscard]] static EnvironmentColorTrack Decode(const nlohmann::json& record)
        {
            try
            {
                if (!record.is_object() || record.size() != 2 || !record.at("version").is_number_integer() ||
                    record.at("version") != 1 || !record.at("keys").is_array() || record.at("keys").empty() ||
                    record.at("keys").size() > EnvironmentColorTrack::MaximumKeys)
                {
                    throw std::invalid_argument("Unsupported or malformed sky color track record.");
                }
                std::vector<EnvironmentColorKey> keys;
                keys.reserve(record.at("keys").size());
                for (const auto& encoded : record.at("keys"))
                {
                    if (!encoded.is_object() || encoded.size() != 3 || !encoded.at("phase").is_number() ||
                        !encoded.at("color").is_array() || encoded.at("color").size() != 3 ||
                        !encoded.at("interpolation").is_number_integer() || encoded.at("interpolation") < 0 ||
                        encoded.at("interpolation") > 2)
                    {
                        throw std::invalid_argument("Malformed sky color key record.");
                    }
                    EnvironmentColorKey key;
                    key.DayFraction = encoded.at("phase").get<double>();
                    key.Interpolation =
                        static_cast<EnvironmentKeyInterpolation>(encoded.at("interpolation").get<unsigned>());
                    for (std::size_t channel = 0; channel < key.LinearColor.size(); ++channel)
                    {
                        const auto& value = encoded.at("color").at(channel);
                        if (!value.is_number())
                        {
                            throw std::invalid_argument("Sky color channels must be numbers.");
                        }
                        key.LinearColor[channel] = value.get<double>();
                    }
                    keys.push_back(key);
                }
                return EnvironmentColorTrack(std::move(keys));
            }
            catch (const nlohmann::json::exception&)
            {
                throw std::invalid_argument("Malformed sky color track fields.");
            }
        }
    };
} // namespace Keire::Detail
