#pragma once

#include "KeireInternal/Environment/EnvironmentClock.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <string>
#include <system_error>

namespace Keire::Detail
{
    // Internal record shared by future profile/save/manifest adapters, not a project file format by itself.
    // Days are decimal strings so JSON consumers cannot silently round them through a double.
    class EnvironmentClockRecord final
    {
      public:
        [[nodiscard]] static nlohmann::json Encode(const EnvironmentClockState state)
        {
            (void)EnvironmentClock(state);
            return {{"version", 1},
                    {"day", std::to_string(state.Day)},
                    {"dayFraction", state.DayFraction},
                    {"secondsPerDay", state.SecondsPerDay},
                    {"rate", state.Rate},
                    {"paused", state.Paused}};
        }

        [[nodiscard]] static EnvironmentClockState Decode(const nlohmann::json& record)
        {
            if (!record.is_object() || record.size() != 6 || !record.contains("version") ||
                !record.at("version").is_number_integer() || record.at("version") != 1 || !record.contains("day") ||
                !record.at("day").is_string() || !record.contains("paused") || !record.at("paused").is_boolean())
            {
                throw std::invalid_argument("Unsupported or malformed environment clock record.");
            }
            const auto& day = record.at("day").get_ref<const std::string&>();
            std::int64_t parsedDay = 0;
            if (day.empty() || day.size() > 20)
            {
                throw std::invalid_argument("Environment day must be a bounded signed decimal integer.");
            }
            const auto parsed = std::from_chars(day.data(), day.data() + day.size(), parsedDay);
            if (parsed.ec != std::errc{} || parsed.ptr != day.data() + day.size() || std::to_string(parsedDay) != day)
            {
                throw std::invalid_argument("Environment day must be a canonical signed decimal integer.");
            }
            EnvironmentClockState result;
            result.Day = parsedDay;
            result.DayFraction = Number(record, "dayFraction");
            result.SecondsPerDay = Number(record, "secondsPerDay");
            result.Rate = Number(record, "rate");
            result.Paused = record.at("paused").get<bool>();
            (void)EnvironmentClock(result);
            return result;
        }

      private:
        [[nodiscard]] static double Number(const nlohmann::json& record, const char* key)
        {
            if (!record.contains(key) || !record.at(key).is_number())
            {
                throw std::invalid_argument("Environment clock parameters must be JSON numbers.");
            }
            return record.at(key).get<double>();
        }
    };
} // namespace Keire::Detail
