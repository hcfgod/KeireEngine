#pragma once

#include "KeireInternal/Environment/EnvironmentClockRecord.h"
#include "KeireInternal/Spatial/PlanetProjectSettingsRecord.h"
#include "KeireInternal/Spatial/SceneRuntimePlanetAccess.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <set>
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
            nlohmann::json owners = nlohmann::json::array();
            std::set<AssetId> instances;
            for (const auto& owner : checkpoint.SceneOwners)
            {
                if (!owner.Instance || owner.Frame == 0 || !instances.insert(owner.Instance).second)
                {
                    throw std::invalid_argument("Planet checkpoint owner identity is invalid or duplicated.");
                }
                owners.push_back({{"instance", owner.Instance.ToString()}, {"frame", owner.Frame}});
            }
            return {{"schemaVersion", 2},
                    {"simulationTime", checkpoint.SimulationTime},
                    {"settings", PlanetProjectSettingsRecord::Encode(checkpoint.Settings)},
                    {"clock", EnvironmentClockRecord::Encode(checkpoint.Clock)},
                    {"sceneOwners", std::move(owners)}};
        }

        [[nodiscard]] static PlanetRuntimeCheckpoint Decode(const nlohmann::json& record)
        {
            try
            {
                if (!record.is_object() || !record.at("schemaVersion").is_number_integer() ||
                    (record.at("schemaVersion") != 1 && record.at("schemaVersion") != 2) ||
                    record.size() != (record.at("schemaVersion") == 1 ? 4U : 5U) ||
                    !record.at("simulationTime").is_number())
                {
                    throw std::invalid_argument("Unsupported planet runtime checkpoint record.");
                }
                PlanetRuntimeCheckpoint checkpoint{PlanetProjectSettingsRecord::Decode(record.at("settings")),
                                                   record.at("simulationTime").get<double>(),
                                                   EnvironmentClockRecord::Decode(record.at("clock"))};
                if (record.at("schemaVersion") == 2)
                {
                    const auto& owners = record.at("sceneOwners");
                    if (!owners.is_array())
                    {
                        throw std::invalid_argument("Planet scene owners must be an array.");
                    }
                    std::set<AssetId> instances;
                    for (const auto& owner : owners)
                    {
                        if (!owner.is_object() || owner.size() != 2 || !owner.at("frame").is_number_integer() ||
                            owner.at("frame") == 0 ||
                            (!owner.at("frame").is_number_unsigned() && owner.at("frame").get<std::int64_t>() < 0))
                        {
                            throw std::invalid_argument("Planet owner frame must be a positive integer.");
                        }
                        const auto instance = AssetId::Parse(owner.at("instance").get<std::string>());
                        if (!instance || !instances.insert(instance).second)
                        {
                            throw std::invalid_argument("Planet owner instance is invalid or duplicated.");
                        }
                        checkpoint.SceneOwners.push_back({instance, owner.at("frame").get<std::uint64_t>()});
                    }
                }
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
