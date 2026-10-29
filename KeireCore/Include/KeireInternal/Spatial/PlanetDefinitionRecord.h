#pragma once

#include "KeireInternal/Spatial/PlanetDefinition.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
    class PlanetDefinitionRecord final
    {
      public:
        [[nodiscard]] static nlohmann::json Encode(const PlanetDefinition& definition)
        {
            ValidatePlanetDefinition(definition);
            std::array<std::int64_t, 3> sectors{};
            std::array<double, 3> offsets{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                sectors[axis] = definition.ParentCenter.Axes()[axis].Sector();
                offsets[axis] = definition.ParentCenter.Axes()[axis].Offset();
            }
            return {{"version", 1},
                    {"planet", definition.Planet.ToString()},
                    {"frame", definition.Frame},
                    {"world", definition.ParentCenter.Identity().World},
                    {"parentFrame", definition.ParentCenter.Identity().Frame},
                    {"sectors", sectors},
                    {"offsets", offsets},
                    {"orientation", definition.Orientation.Components()},
                    {"angularVelocity", definition.AngularVelocity},
                    {"epochSeconds", definition.EpochSeconds},
                    {"radiusMetres", definition.RadiusMetres},
                    {"gravitationalParameter", definition.GravitationalParameter}};
        }

        [[nodiscard]] static PlanetDefinition Decode(const nlohmann::json& record)
        {
            try
            {
                if (!record.is_object() || record.size() != 12 || !record.at("version").is_number_integer() ||
                    record.at("version") != 1)
                {
                    throw std::invalid_argument("Unsupported planet definition record.");
                }
                const auto key = [&](const char* name)
                {
                    const auto& value = record.at(name);
                    if (!value.is_number_integer() || value == 0 ||
                        (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
                    {
                        throw std::invalid_argument("Planet frame keys must be positive integers.");
                    }
                    return value.get<std::uint64_t>();
                };
                const auto vector = [&](const char* name)
                {
                    const auto& value = record.at(name);
                    if (!value.is_array() || value.size() != 3)
                    {
                        throw std::invalid_argument("Planet vectors require three coordinates.");
                    }
                    return value.get<std::array<double, 3>>();
                };
                const auto& sectorRecord = record.at("sectors");
                if (!sectorRecord.is_array() || sectorRecord.size() != 3)
                {
                    throw std::invalid_argument("Planet sector address requires three integers.");
                }
                std::array<std::int64_t, 3> sectors{};
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const auto& value = sectorRecord[axis];
                    if (!value.is_number_integer() ||
                        (value.is_number_unsigned() &&
                         value.get<std::uint64_t>() >
                             static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
                    {
                        throw std::invalid_argument("Planet sector exceeds signed 64-bit range.");
                    }
                    sectors[axis] = value.get<std::int64_t>();
                }
                if (!record.at("orientation").is_array() || record.at("orientation").size() != 4)
                {
                    throw std::invalid_argument("Planet orientation requires four quaternion components.");
                }
                const auto orientation = record.at("orientation").get<std::array<double, 4>>();
                PlanetDefinition definition;
                definition.Planet = AssetId::Parse(record.at("planet").get<std::string>());
                definition.Frame = key("frame");
                definition.ParentCenter =
                    SectorPosition({key("world"), key("parentFrame")}, sectors, vector("offsets"));
                definition.Orientation =
                    FrameRotation::Normalize(orientation[0], orientation[1], orientation[2], orientation[3]);
                definition.AngularVelocity = vector("angularVelocity");
                definition.EpochSeconds = record.at("epochSeconds").get<double>();
                definition.RadiusMetres = record.at("radiusMetres").get<double>();
                definition.GravitationalParameter = record.at("gravitationalParameter").get<double>();
                ValidatePlanetDefinition(definition);
                return definition;
            }
            catch (const nlohmann::json::exception& error)
            {
                throw std::invalid_argument(std::string("Malformed planet definition: ") + error.what());
            }
        }
    };
} // namespace Keire::Detail
