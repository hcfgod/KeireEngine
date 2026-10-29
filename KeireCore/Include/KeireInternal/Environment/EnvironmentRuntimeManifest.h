#pragma once

#include "KeireInternal/Environment/AtmosphereRecord.h"
#include "KeireInternal/Environment/BloomRecord.h"
#include "KeireInternal/Environment/CloudRecord.h"
#include "KeireInternal/Environment/EnvironmentClockRecord.h"
#include "KeireInternal/Environment/EnvironmentLightingRecord.h"
#include "KeireInternal/Environment/FogDensityRecord.h"
#include "KeireInternal/Environment/StarFieldRecord.h"
#include "KeireInternal/Spatial/PlanetProjectSettingsRecord.h"

namespace Keire::Detail
{
    class EnvironmentRuntimeManifest final
    {
      public:
        static constexpr unsigned CurrentSchema = 12;
        static constexpr unsigned PlanetSchema = 12;
        static constexpr unsigned GravitySchema = 11;
        static constexpr unsigned FogSchema = 10;
        static constexpr unsigned StarSchema = 9;
        static constexpr unsigned BloomSchema = 8;
        static constexpr unsigned LightingSchema = 6;
        static constexpr unsigned SkySchema = 7;

        struct State
        {
            EnvironmentClockState Time;
            EnvironmentLightingProfile Lighting{};
            AtmosphereSettings Atmosphere;
            CloudSettings Clouds;
            BloomSettings Bloom;
            StarFieldSettings Stars;
            FogSettings Fog;
            PlanetProjectSettings Planets;
        };

        [[nodiscard]] static EnvironmentClockState Decode(const nlohmann::json& manifest)
        {
            auto state = DecodeState(manifest);
            if (manifest.at("schemaVersion") >= LightingSchema)
            {
                throw std::invalid_argument("Lighting manifests require a lighting-aware runtime consumer.");
            }
            return state.Time;
        }

        [[nodiscard]] static State DecodeState(const nlohmann::json& manifest)
        {
            if (!manifest.is_object() || !manifest.contains("schemaVersion") ||
                !manifest.at("schemaVersion").is_number_integer())
            {
                throw std::invalid_argument("Runtime manifest requires an integer schema version.");
            }
            const auto& schema = manifest.at("schemaVersion");
            if (schema < 4 || schema > CurrentSchema)
            {
                throw std::invalid_argument("Unsupported runtime manifest schema; recook content.");
            }
            if (schema < LightingSchema && manifest.contains("environmentLighting"))
            {
                throw std::invalid_argument("Runtime environment lighting requires manifest schema 6.");
            }
            if (schema < PlanetSchema && manifest.contains("planets"))
            {
                throw std::invalid_argument("Runtime planet settings require manifest schema 12.");
            }
            const auto rendering = manifest.find("rendering");
            if (schema < SkySchema && rendering != manifest.end() && rendering->is_object() &&
                (rendering->contains("atmosphere") || rendering->contains("clouds")))
            {
                throw std::invalid_argument("Runtime atmosphere/cloud settings require manifest schema 7.");
            }
            if (schema < BloomSchema && rendering != manifest.end() && rendering->is_object() &&
                rendering->contains("bloom"))
            {
                throw std::invalid_argument("Runtime bloom settings require manifest schema 8.");
            }
            if (schema < StarSchema && rendering != manifest.end() && rendering->is_object() &&
                rendering->contains("stars"))
            {
                throw std::invalid_argument("Runtime star settings require manifest schema 9.");
            }
            if (schema < FogSchema && rendering != manifest.end() && rendering->is_object() &&
                rendering->contains("fog"))
            {
                throw std::invalid_argument("Runtime fog settings require manifest schema 10.");
            }
            if (schema == 4)
            {
                if (manifest.contains("environmentTime"))
                {
                    throw std::invalid_argument("Runtime environment time requires manifest schema 5.");
                }
                return {};
            }
            if (!manifest.contains("environmentTime"))
            {
                throw std::invalid_argument(
                    "Unsupported runtime manifest or missing environment time; recook content.");
            }
            State result;
            result.Time = EnvironmentClockRecord::Decode(manifest.at("environmentTime"));
            if (schema >= LightingSchema)
            {
                if (!manifest.contains("environmentLighting"))
                {
                    throw std::invalid_argument("Runtime manifest schema 6 or 7 requires environment lighting.");
                }
                result.Lighting = EnvironmentLightingRecord::Decode(manifest.at("environmentLighting"));
            }
            if (schema >= SkySchema)
            {
                if (rendering == manifest.end() || !rendering->is_object() || !rendering->contains("atmosphere") ||
                    !rendering->contains("clouds"))
                {
                    throw std::invalid_argument("Runtime manifest schema 7 requires atmosphere and cloud settings.");
                }
                result.Atmosphere = DecodeAtmosphere(rendering->at("atmosphere"));
                result.Clouds = DecodeCloudSettings(rendering->at("clouds"));
            }
            if (schema >= BloomSchema)
            {
                if (!rendering->contains("bloom"))
                {
                    throw std::invalid_argument("Runtime manifest schema 8 requires bloom settings.");
                }
                result.Bloom = DecodeBloomSettings(rendering->at("bloom"));
            }
            if (schema >= StarSchema)
            {
                if (!rendering->contains("stars"))
                {
                    throw std::invalid_argument("Runtime manifest schema 9 requires star settings.");
                }
                result.Stars = DecodeStarField(rendering->at("stars"));
            }
            if (schema >= FogSchema)
            {
                if (!rendering->contains("fog"))
                {
                    throw std::invalid_argument("Runtime manifest schema 10 requires fog settings.");
                }
                result.Fog = DecodeFogDensity(rendering->at("fog"));
            }
            if (schema >= PlanetSchema)
            {
                if (!manifest.contains("planets"))
                {
                    throw std::invalid_argument("Runtime manifest schema 12 requires planet settings.");
                }
                result.Planets = PlanetProjectSettingsRecord::Decode(manifest.at("planets"));
            }
            return result;
        }
    };
} // namespace Keire::Detail
