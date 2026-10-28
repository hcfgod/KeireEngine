#include "KeireInternal/Environment/EnvironmentRuntimeManifest.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>

TEST_CASE("Runtime manifests preserve HDR sky curves and disabled authoring across JSON serialization")
{
    using namespace Keire::Detail;
    for (const bool enabled : {false, true})
    {
        EnvironmentSkyProfile sky;
        sky.Enabled = enabled;
        sky.BlendWeight = 0.375;
        sky.GradientExponent = 2.25;
        sky.Horizon = EnvironmentColorTrack({{0.125, {12.0, 0.25, 2.0}, EnvironmentKeyInterpolation::Hold},
                                             {0.75, {0.1, 6.0, 0.5}, EnvironmentKeyInterpolation::Smooth}});
        sky.Zenith = EnvironmentColorTrack({{0.0, {65504.0, 0.0, 1.0}}, {0.5, {0.0, 0.5, 3.0}}});
        const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Authored, {}, {}, sky);
        const EnvironmentClockState clock{1234567890123456789LL, 0.875};
        const nlohmann::json manifest{{"schemaVersion", EnvironmentRuntimeManifest::CurrentSchema},
                                      {"environmentTime", EnvironmentClockRecord::Encode(clock)},
                                      {"environmentLighting", EnvironmentLightingRecord::Encode(profile)},
                                      {"rendering",
                                       {{"atmosphere", EncodeAtmosphere({})},
                                        {"clouds", EncodeCloudSettings({})},
                                        {"bloom", EncodeBloomSettings({})},
                                        {"stars", EncodeStarField({})},
                                        {"fog", EncodeFogDensity({})}}}};
        const auto decoded = EnvironmentRuntimeManifest::DecodeState(nlohmann::json::parse(manifest.dump()));
        CHECK(decoded.Lighting == profile);
        CHECK(decoded.Time == clock);
        REQUIRE(decoded.Lighting.Sky());
        CHECK(decoded.Lighting.Sky()->Evaluate(decoded.Time) == sky.Evaluate(clock));

        auto malformed = manifest;
        malformed["environmentLighting"]["sky"]["horizon"]["version"] = 999;
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(malformed), std::invalid_argument);
        malformed = manifest;
        malformed["environmentLighting"]["version"] = 3;
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(malformed), std::invalid_argument);
    }
}

namespace
{
    nlohmann::json SkyManifest()
    {
        using namespace Keire::Detail;
        Keire::AtmosphereSettings atmosphere;
        atmosphere.Enabled = true;
        atmosphere.PlanetCenter = {1.0e12, -6'360'000.0, 19.125};
        atmosphere.MieAnisotropy = -0.25;
        atmosphere.ViewSamples = 48;
        Keire::CloudSettings clouds;
        clouds.Layers[0].Enabled = true;
        clouds.Layers[0].WindMetersPerSecond = {-20.0, 5.5};
        clouds.Layers[1].Enabled = true;
        clouds.Layers[1].AltitudeMeters = 5000.0;
        clouds.Layers[1].Coverage = 0.2;
        const EnvironmentLightingProfile lighting(
            EnvironmentDirectionalOwnership::Environment,
            {{0.0, 0.0, -60.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 180.0, 70.0, {1.0, 0.9, 0.8}, 12.0}});
        return {{"schemaVersion", EnvironmentRuntimeManifest::SkySchema},
                {"environmentTime", EnvironmentClockRecord::Encode({1234567890123456789LL, 0.375})},
                {"environmentLighting", EnvironmentLightingRecord::Encode(lighting)},
                {"rendering", {{"atmosphere", EncodeAtmosphere(atmosphere)}, {"clouds", EncodeCloudSettings(clouds)}}}};
    }
} // namespace

TEST_CASE("Schema eight retains bloom and legacy manifests cannot silently discard it")
{
    using namespace Keire::Detail;
    auto manifest = SkyManifest();
    CHECK_FALSE(EnvironmentRuntimeManifest::DecodeState(manifest).Bloom.Enabled);
    manifest["schemaVersion"] = EnvironmentRuntimeManifest::BloomSchema;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
    const Keire::BloomSettings bloom{true, 0.35, 2.5, 0.2, 3.0};
    manifest["rendering"]["bloom"] = EncodeBloomSettings(bloom);
    CHECK(EnvironmentRuntimeManifest::DecodeState(nlohmann::json::parse(manifest.dump())).Bloom == bloom);
    for (const auto schema : {4, 5, 6, 7, 9})
    {
        auto invalid = manifest;
        invalid["schemaVersion"] = schema;
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(invalid), std::invalid_argument);
    }
    manifest["rendering"]["bloom"]["radius"] = -1.0;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
}

TEST_CASE("Schema nine preserves stars and rejects missing malformed or downgraded star records")
{
    using namespace Keire::Detail;
    auto manifest = SkyManifest();
    CHECK_FALSE(EnvironmentRuntimeManifest::DecodeState(manifest).Stars.Enabled);
    manifest["schemaVersion"] = EnvironmentRuntimeManifest::StarSchema;
    manifest["rendering"]["bloom"] = EncodeBloomSettings({});
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
    Keire::StarFieldSettings stars;
    stars.Enabled = true;
    stars.Seed = 0xffffffffU;
    stars.TwinkleAmplitude = 0.35;
    stars.Tint = {0.2, 0.4, 0.8};
    manifest["rendering"]["stars"] = EncodeStarField(stars);
    CHECK(EnvironmentRuntimeManifest::DecodeState(nlohmann::json::parse(manifest.dump())).Stars == stars);
    for (const int schema : {4, 5, 6, 7, 8})
    {
        auto downgraded = manifest;
        downgraded["schemaVersion"] = schema;
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(downgraded), std::invalid_argument);
    }
    manifest["rendering"]["stars"]["density"] = -1.0;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
}

TEST_CASE("Schema seven runtime sky manifest retains authored atmosphere clouds clock and lighting")
{
    using namespace Keire::Detail;
    const auto original = SkyManifest();
    const auto manifest = nlohmann::json::parse(original.dump());
    const auto decoded = EnvironmentRuntimeManifest::DecodeState(manifest);
    CHECK(EncodeAtmosphere(decoded.Atmosphere) == original.at("rendering").at("atmosphere"));
    CHECK(EncodeCloudSettings(decoded.Clouds) == original.at("rendering").at("clouds"));
    CHECK(EnvironmentClockRecord::Encode(decoded.Time) == original.at("environmentTime"));
    CHECK(EnvironmentLightingRecord::Encode(decoded.Lighting) == original.at("environmentLighting"));
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::Decode(manifest), std::invalid_argument);
}

TEST_CASE("Schema seven requires both valid sky records and the clock and lighting records")
{
    using namespace Keire::Detail;
    for (const auto* field : {"rendering", "environmentTime", "environmentLighting"})
    {
        auto missing = SkyManifest();
        missing.erase(field);
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(missing), std::invalid_argument);
    }
    for (const auto* field : {"atmosphere", "clouds"})
    {
        auto missing = SkyManifest();
        missing["rendering"].erase(field);
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(missing), std::invalid_argument);
        for (const auto& value : {nlohmann::json(nullptr), nlohmann::json::array(), nlohmann::json("bad")})
        {
            auto malformed = SkyManifest();
            malformed["rendering"][field] = value;
            CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(malformed), std::invalid_argument);
        }
    }
    auto malformed = SkyManifest();
    malformed["rendering"]["atmosphere"]["mieExtinction"] = -1.0;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(malformed), std::invalid_argument);
    malformed = SkyManifest();
    malformed["rendering"]["clouds"]["layers"][1]["coverage"] = 1.5;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(malformed), std::invalid_argument);
    malformed = SkyManifest();
    malformed["rendering"] = nullptr;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(malformed), std::invalid_argument);
}

TEST_CASE("Legacy runtime schemas retain disabled skies and cannot silently discard embedded sky settings")
{
    using namespace Keire::Detail;
    for (const unsigned schema : {4U, 5U, 6U})
    {
        auto legacy = SkyManifest();
        legacy["schemaVersion"] = schema;
        legacy.erase("rendering");
        if (schema < 6)
        {
            legacy.erase("environmentLighting");
        }
        if (schema < 5)
        {
            legacy.erase("environmentTime");
        }
        const auto decoded = EnvironmentRuntimeManifest::DecodeState(legacy);
        CHECK(decoded.Atmosphere == Keire::AtmosphereSettings{});
        CHECK(decoded.Clouds == Keire::CloudSettings{});
        for (const auto* field : {"atmosphere", "clouds"})
        {
            auto invalid = legacy;
            invalid["rendering"][field] = SkyManifest().at("rendering").at(field);
            CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(invalid), std::invalid_argument);
        }
        if (schema < 6)
        {
            CHECK_NOTHROW((void)EnvironmentRuntimeManifest::Decode(legacy));
        }
    }
}

TEST_CASE("Runtime sky manifest schemas reject unknown signed oversized and noninteger versions")
{
    using namespace Keire::Detail;
    for (const auto& schema : {nlohmann::json(-1), nlohmann::json(0), nlohmann::json(3), nlohmann::json(8),
                               nlohmann::json(std::numeric_limits<std::uint64_t>::max()), nlohmann::json(7.0),
                               nlohmann::json("7"), nlohmann::json(true), nlohmann::json(nullptr)})
    {
        auto invalid = SkyManifest();
        invalid["schemaVersion"] = schema;
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(invalid), std::invalid_argument);
    }
    auto missing = SkyManifest();
    missing.erase("schemaVersion");
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(missing), std::invalid_argument);
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(nlohmann::json::array()), std::invalid_argument);
}

TEST_CASE("Runtime sky manifest preserves independently disabled atmosphere and cloud authoring")
{
    using namespace Keire::Detail;
    for (const bool atmosphereEnabled : {false, true})
    {
        for (const bool cloudsEnabled : {false, true})
        {
            auto manifest = SkyManifest();
            manifest["rendering"]["atmosphere"]["enabled"] = atmosphereEnabled;
            for (auto& layer : manifest["rendering"]["clouds"]["layers"])
            {
                layer["enabled"] = cloudsEnabled;
            }
            const auto decoded = EnvironmentRuntimeManifest::DecodeState(manifest);
            CHECK(EncodeAtmosphere(decoded.Atmosphere) == manifest.at("rendering").at("atmosphere"));
            CHECK(EncodeCloudSettings(decoded.Clouds) == manifest.at("rendering").at("clouds"));

            // Disabled data is retained for re-enabling, so it must still meet the persisted contract.
            manifest["rendering"]["atmosphere"]["viewSamples"] = 0;
            CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
        }
    }
    auto disabledCloud = SkyManifest();
    disabledCloud["rendering"]["clouds"]["layers"][1]["enabled"] = false;
    disabledCloud["rendering"]["clouds"]["layers"][1]["patternSizeMeters"] = 0;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(disabledCloud), std::invalid_argument);
}

TEST_CASE("Runtime sky manifest rejects incompatible nested record versions without dropping other systems")
{
    using namespace Keire::Detail;
    for (const auto* field : {"environmentTime", "environmentLighting"})
    {
        auto incompatible = SkyManifest();
        incompatible[field]["version"] = 999;
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(incompatible), std::invalid_argument);
    }
    auto incompatible = SkyManifest();
    incompatible["rendering"]["clouds"]["version"] = 999;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(incompatible), std::invalid_argument);
}

TEST_CASE("Runtime atmosphere preserves ground reflectance and migrates legacy ground records")
{
    using namespace Keire::Detail;
    auto manifest = SkyManifest();
    Keire::AtmosphereSettings atmosphere;
    atmosphere.Enabled = true;
    atmosphere.GroundReflectance = {0.02, 0.3, 0.7};
    atmosphere.GroundIntensity = 3.5;
    manifest["rendering"]["atmosphere"] = EncodeAtmosphere(atmosphere);
    CHECK(EnvironmentRuntimeManifest::DecodeState(manifest).Atmosphere == atmosphere);
    auto& record = manifest["rendering"]["atmosphere"];
    record.erase("schemaVersion");
    record.erase("groundReflectance");
    record.erase("groundIntensity");
    atmosphere.GroundReflectance = Keire::AtmosphereSettings{}.GroundReflectance;
    atmosphere.GroundIntensity = 1.0;
    CHECK(EnvironmentRuntimeManifest::DecodeState(manifest).Atmosphere == atmosphere);
    record["groundReflectance"] = {1, 1, 1};
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
}

TEST_CASE("Runtime fog preserves authored volumes and rejects missing malformed or downgraded records")
{
    using namespace Keire::Detail;
    auto manifest = SkyManifest();
    CHECK(EnvironmentRuntimeManifest::DecodeState(manifest).Fog == Keire::FogSettings{});
    manifest["schemaVersion"] = EnvironmentRuntimeManifest::FogSchema;
    manifest["rendering"]["bloom"] = EncodeBloomSettings({});
    manifest["rendering"]["stars"] = EncodeStarField({});
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
    Keire::FogSettings fog;
    fog.Enabled = true;
    fog.LocalVolumes.emplace_back();
    fog.LocalVolumes.front().Center = {1.0e12 + 0.125, 25.0, -30.0};
    fog.LocalVolumes.front().Enabled = false;
    fog.LocalVolumes.emplace_back();
    fog.Anisotropy = -0.4;
    fog.ExclusionRegions.emplace_back();
    fog.ExclusionRegions.front().Center = {1.0e12 + 0.125, 7.0, 8.0};
    fog.ExclusionRegions.front().Strength = 0.6;
    manifest["rendering"]["fog"] = EncodeFogDensity(fog);
    CHECK(EnvironmentRuntimeManifest::DecodeState(nlohmann::json::parse(manifest.dump())).Fog == fog);
    for (const unsigned schema : {4U, 5U, 6U, 7U, 8U, 9U})
    {
        auto downgraded = manifest;
        downgraded["schemaVersion"] = schema;
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(downgraded), std::invalid_argument);
    }
    manifest["rendering"]["fog"]["localVolumes"][0]["halfExtent"] = {0, 1, 1};
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
    manifest["schemaVersion"] = EnvironmentRuntimeManifest::FogSchema + 1;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
}
