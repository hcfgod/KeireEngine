#pragma once

#include "KeireInternal/Environment/EnvironmentClockRecord.h"
#include "KeireInternal/Spatial/PlanetProjectSettingsRecord.h"
#include "KeireInternal/Spatial/SceneRuntimePlanetAccess.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
    class PlanetRuntimeCheckpointRecord final
    {
      public:
        [[nodiscard]] static nlohmann::json Encode(const PlanetRuntimeCheckpoint& checkpoint)
        {
            if (!std::isfinite(checkpoint.SimulationTime) || checkpoint.SimulationTime < 0.0)
            {
                throw std::invalid_argument("Planet checkpoint time must be finite and nonnegative.");
            }
            return {{"schemaVersion", 1},
                    {"simulationTime", checkpoint.SimulationTime},
                    {"settings", PlanetProjectSettingsRecord::Encode(checkpoint.Settings)},
                    {"clock", EnvironmentClockRecord::Encode(checkpoint.Clock)}};
        }

        [[nodiscard]] static PlanetRuntimeCheckpoint Decode(const nlohmann::json& record)
        {
            try
            {
                if (!record.is_object() || record.size() != 4 || !record.at("schemaVersion").is_number_integer() ||
                    record.at("schemaVersion") != 1 || !record.at("simulationTime").is_number())
                {
                    throw std::invalid_argument("Unsupported planet runtime checkpoint record.");
                }
                PlanetRuntimeCheckpoint checkpoint{PlanetProjectSettingsRecord::Decode(record.at("settings")),
                                                   record.at("simulationTime").get<double>(),
                                                   EnvironmentClockRecord::Decode(record.at("clock"))};
                if (!std::isfinite(checkpoint.SimulationTime) || checkpoint.SimulationTime < 0.0)
                {
                    throw std::invalid_argument("Planet runtime checkpoint time must be finite and nonnegative.");
                }
                return checkpoint;
            }
            catch (const nlohmann::json::exception& error)
            {
                throw std::invalid_argument(std::string("Malformed planet runtime checkpoint: ") + error.what());
            }
        }
    };
} // namespace Keire::Detail
