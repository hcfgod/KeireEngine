#include "KeireInternal/Environment/EnvironmentRuntimeManifest.h"
#include "KeireInternal/Spatial/PlanetProjectSettingsStore.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace
{
    using namespace Keire::Detail;

    nlohmann::json PlanetManifest(const PlanetProjectSettings& planets = {})
    {
        return {{"schemaVersion", EnvironmentRuntimeManifest::PlanetSchema},
                {"environmentTime", EnvironmentClockRecord::Encode({1234567890123456789LL, 0.375})},
                {"environmentLighting", EnvironmentLightingRecord::Encode(Keire::Detail::EnvironmentLightingProfile{})},
                {"planets", PlanetProjectSettingsRecord::Encode(planets)},
                {"rendering",
                 {{"atmosphere", EncodeAtmosphere({})},
                  {"clouds", EncodeCloudSettings({})},
                  {"bloom", EncodeBloomSettings({})},
                  {"stars", EncodeStarField({})},
                  {"fog", EncodeFogDensity({})}}}};
    }

    PlanetProjectSettings AuthoredPlanets()
    {
        PlanetDefinition planet;
        planet.Planet = Keire::AssetId(1, 2);
        planet.Frame = 2;
        planet.ParentCenter = SectorPosition({1, 1}, {1'000'000'000'000, -1'000'000'000'000, 7}, {0.125, 0.25, 0.5});
        planet.Orientation = FrameRotation::Normalize(0.0, 0.0, 0.5, 0.5);
        planet.AngularVelocity = {0.0, 0.000072921, 0.0};
        planet.GravitationalParameter = 3.986004418e14;
        PlanetProjectSettings result;
        result.Planets = {planet};
        result.SelectedPlanet = planet.Planet;
        result.Geometry = PlanetGeometryMode::Planetary;
        result.Gravity = PlanetGravityMode::Radial;
        return result;
    }
} // namespace

TEST_CASE("Runtime planet manifests preserve authored identity sector placement rotation and gravity")
{
    auto planets = AuthoredPlanets();
    for (const auto gravity : {PlanetGravityMode::Uniform, PlanetGravityMode::Radial, PlanetGravityMode::Combined})
    {
        planets.Gravity = gravity;
        const auto record = nlohmann::json::parse(PlanetManifest(planets).dump());
        const auto state = EnvironmentRuntimeManifest::DecodeState(record);
        CHECK(state.Planets == planets);
        CHECK(state.Time.Day == 1234567890123456789LL);
    }
}

TEST_CASE("Runtime planet schema is strict and cannot be silently downgraded")
{
    const auto baseline = PlanetManifest(AuthoredPlanets());
    auto record = baseline;
    record.erase("planets");
    CHECK_THROWS_AS(EnvironmentRuntimeManifest::DecodeState(record), std::invalid_argument);
    for (const auto invalid : {nlohmann::json(nullptr), nlohmann::json::array(), nlohmann::json::object()})
    {
        record = baseline;
        record["planets"] = invalid;
        CHECK_THROWS_AS(EnvironmentRuntimeManifest::DecodeState(record), std::invalid_argument);
    }
    record = baseline;
    record["planets"]["geometry"] = "camera-follow";
    CHECK_THROWS_AS(EnvironmentRuntimeManifest::DecodeState(record), std::invalid_argument);
    record = baseline;
    record["planets"]["selectedPlanet"] = Keire::AssetId(8, 9).ToString();
    CHECK_THROWS_AS(EnvironmentRuntimeManifest::DecodeState(record), std::invalid_argument);
    record = baseline;
    record["planets"]["unknown"] = true;
    CHECK_THROWS_AS(EnvironmentRuntimeManifest::DecodeState(record), std::invalid_argument);
    record = baseline;
    record["planets"]["planets"].push_back(record["planets"]["planets"][0]);
    CHECK_THROWS_AS(EnvironmentRuntimeManifest::DecodeState(record), std::invalid_argument);
    record = baseline;
    record["schemaVersion"] = EnvironmentRuntimeManifest::GravitySchema;
    CHECK_THROWS_AS(EnvironmentRuntimeManifest::DecodeState(record), std::invalid_argument);
}

TEST_CASE("Earlier runtime manifests retain explicit flat geometry and uniform gravity defaults")
{
    for (unsigned schema = 4; schema < EnvironmentRuntimeManifest::PlanetSchema; ++schema)
    {
        auto record = PlanetManifest();
        record["schemaVersion"] = schema;
        record.erase("planets");
        if (schema < EnvironmentRuntimeManifest::FogSchema)
        {
            record["rendering"].erase("fog");
        }
        if (schema < EnvironmentRuntimeManifest::StarSchema)
        {
            record["rendering"].erase("stars");
        }
        if (schema < EnvironmentRuntimeManifest::BloomSchema)
        {
            record["rendering"].erase("bloom");
        }
        if (schema < EnvironmentRuntimeManifest::SkySchema)
        {
            record.erase("rendering");
        }
        if (schema < EnvironmentRuntimeManifest::LightingSchema)
        {
            record.erase("environmentLighting");
        }
        if (schema == 4)
        {
            record.erase("environmentTime");
        }
        const auto decoded = EnvironmentRuntimeManifest::DecodeState(record);
        CHECK(decoded.Planets == PlanetProjectSettings{});
        record["planets"] = PlanetProjectSettingsRecord::Encode({});
        CHECK_THROWS_AS(EnvironmentRuntimeManifest::DecodeState(record), std::invalid_argument);
    }
}

TEST_CASE("Cooked runtime planet records originate from persisted project settings")
{
    struct Fixture
    {
        std::filesystem::path Root = KeireTests::MakeTestDirectory("CookedPlanetSettings");

        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    } fixture;
    const auto expected = AuthoredPlanets();
    PlanetProjectSettingsStore::Save(fixture.Root, expected);
    const auto manifest = PlanetManifest(PlanetProjectSettingsStore::Load(fixture.Root));
    CHECK(EnvironmentRuntimeManifest::DecodeState(nlohmann::json::parse(manifest.dump())).Planets == expected);
}
