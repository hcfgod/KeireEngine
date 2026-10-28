#include "KeireInternal/Environment/EnvironmentLightingSettingsStore.h"
#include "KeireInternal/Environment/EnvironmentRuntimeManifest.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <limits>

namespace
{
    struct LightingSettingsFixture final
    {
        std::filesystem::path Root = KeireTests::MakeTestDirectory("EnvironmentLightingSettings");

        ~LightingSettingsFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }

        std::filesystem::path File() const { return Root / "ProjectSettings" / "EnvironmentLighting.keiresettings"; }
    };
} // namespace

TEST_CASE("Ambient settings survive project persistence and runtime manifest decoding")
{
    using namespace Keire::Detail;
    LightingSettingsFixture fixture;
    const EnvironmentLightingProfile profile(
        EnvironmentDirectionalOwnership::Environment,
        {{0.0, 0.0, -60.0, {0.1, 0.2, 0.3}, 0.0, Keire::EnvironmentAmbientLight{{0.1F, 0.2F, 0.3F}, 0.05F}},
         {0.5, 180.0, 60.0, {1.0, 0.9, 0.8}, 5.0, Keire::EnvironmentAmbientLight{{0.3F, 0.4F, 0.5F}, 0.75F}}});
    EnvironmentLightingSettingsStore::Save(fixture.Root, profile);
    CHECK(EnvironmentLightingSettingsStore::Load(fixture.Root) == profile);
    const nlohmann::json manifest{{"schemaVersion", EnvironmentRuntimeManifest::LightingSchema},
                                  {"environmentTime", EnvironmentClockRecord::Encode({})},
                                  {"environmentLighting", EnvironmentLightingRecord::Encode(profile)}};
    CHECK(EnvironmentRuntimeManifest::DecodeState(manifest).Lighting == profile);
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::Decode(manifest), std::invalid_argument);
}

TEST_CASE("Project lighting settings preserve authored compatibility and round trip explicit profiles")
{
    using namespace Keire::Detail;
    LightingSettingsFixture fixture;
    CHECK(EnvironmentLightingSettingsStore::Load(fixture.Root) == EnvironmentLightingProfile{});
    CHECK_FALSE(std::filesystem::exists(fixture.Root));
    const EnvironmentLightingProfile profile(
        EnvironmentDirectionalOwnership::Environment,
        {{0.0, 0.0, -90.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 180.0, 90.0, {1.0, 0.9, 0.8}, 20.0}});
    EnvironmentLightingSettingsStore::Save(fixture.Root, profile);
    CHECK(EnvironmentLightingSettingsStore::Load(fixture.Root) == profile);
    const auto bytes = KeireTests::ReadFile(fixture.File());
    CHECK_THROWS_AS(EnvironmentLightingSettingsStore::Save({}, profile), std::invalid_argument);
    CHECK(KeireTests::ReadFile(fixture.File()) == bytes);
    EnvironmentLightingSettingsStore::Save(fixture.Root, EnvironmentLightingProfile{});
    CHECK(EnvironmentLightingSettingsStore::Load(fixture.Root) == EnvironmentLightingProfile{});
}

TEST_CASE("Legacy project lighting migrates without losing keys and runtime accepts both record versions")
{
    using namespace Keire::Detail;
    LightingSettingsFixture fixture;
    const EnvironmentLightingProfile profile(
        EnvironmentDirectionalOwnership::Environment,
        {{0.0, 0.0, -60.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 180.0, 60.0, {1.0, 0.9, 0.8}, 12.0}},
        {Keire::ShadowQuality::Soft, Keire::ShadowResolutionHint::VeryHigh, 0.8F, 0.01F});
    auto legacy = EnvironmentLightingRecord::Encode(profile);
    legacy["version"] = 1;
    legacy.erase("shadows");
    std::filesystem::create_directories(fixture.File().parent_path());
    WriteTextFileAtomically(fixture.File(), nlohmann::json{{"schemaVersion", 1}, {"lighting", legacy}}.dump());
    const auto loaded = EnvironmentLightingSettingsStore::Load(fixture.Root);
    CHECK(loaded.Keyframes() == profile.Keyframes());
    CHECK(loaded.Shadows() == Keire::EnvironmentShadowSettings{});
    EnvironmentLightingSettingsStore::Save(fixture.Root, loaded);
    CHECK(nlohmann::json::parse(KeireTests::ReadFile(fixture.File())).at("lighting").at("version") == 2);
    CHECK(EnvironmentLightingSettingsStore::Load(fixture.Root) == loaded);
    nlohmann::json manifest{
        {"schemaVersion", 6}, {"environmentTime", EnvironmentClockRecord::Encode({})}, {"environmentLighting", legacy}};
    CHECK(EnvironmentRuntimeManifest::DecodeState(manifest).Lighting == loaded);
    manifest["environmentLighting"] = EnvironmentLightingRecord::Encode(profile);
    CHECK(EnvironmentRuntimeManifest::DecodeState(manifest).Lighting == profile);
    manifest["environmentLighting"]["shadows"]["quality"] = 256;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(manifest), std::invalid_argument);
}

TEST_CASE("Project lighting settings reject corruption oversized input and directory targets")
{
    using namespace Keire::Detail;
    LightingSettingsFixture fixture;
    EnvironmentLightingSettingsStore::Save(fixture.Root, EnvironmentLightingProfile{});
    const auto valid = nlohmann::json::parse(KeireTests::ReadFile(fixture.File()));
    for (const auto version : {nlohmann::json(0), nlohmann::json(2), nlohmann::json(1.0), nlohmann::json("1")})
    {
        auto invalid = valid;
        invalid["schemaVersion"] = version;
        WriteTextFileAtomically(fixture.File(), invalid.dump());
        CHECK_THROWS_AS((void)EnvironmentLightingSettingsStore::Load(fixture.Root), std::invalid_argument);
    }
    WriteTextFileAtomically(fixture.File(), "{");
    CHECK_THROWS((void)EnvironmentLightingSettingsStore::Load(fixture.Root));
    auto bounded = valid.dump();
    bounded.resize(256U * 1024U, ' ');
    WriteTextFileAtomically(fixture.File(), bounded);
    CHECK(EnvironmentLightingSettingsStore::Load(fixture.Root) == EnvironmentLightingProfile{});
    bounded.push_back(' ');
    WriteTextFileAtomically(fixture.File(), bounded);
    CHECK_THROWS((void)EnvironmentLightingSettingsStore::Load(fixture.Root));
    CHECK_THROWS_AS((void)EnvironmentLightingSettingsStore::Load({}), std::invalid_argument);
    LightingSettingsFixture directoryFixture;
    std::filesystem::create_directories(directoryFixture.File());
    CHECK_THROWS_AS((void)EnvironmentLightingSettingsStore::Load(directoryFixture.Root), std::runtime_error);
    CHECK_THROWS(EnvironmentLightingSettingsStore::Save(directoryFixture.Root, EnvironmentLightingProfile{}));
    CHECK(std::filesystem::is_directory(directoryFixture.File()));
}

TEST_CASE("Project lighting settings accept the largest supported profile without unreadable output")
{
    using namespace Keire::Detail;
    LightingSettingsFixture fixture;
    std::vector<EnvironmentLightingKeyframe> keys;
    for (std::size_t index = 0; index < EnvironmentLightingProfile::MaximumKeyframes; ++index)
    {
        keys.push_back({static_cast<double>(index) / EnvironmentLightingProfile::MaximumKeyframes,
                        359.1234567890123,
                        -89.1234567890123,
                        {0.1234567890123456, 0.2345678901234567, 1.0},
                        std::numeric_limits<float>::max()});
    }
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Environment, std::move(keys));
    EnvironmentLightingSettingsStore::Save(fixture.Root, profile);
    CHECK(std::filesystem::file_size(fixture.File()) <= 64U * 1024U);
    CHECK(EnvironmentLightingSettingsStore::Load(fixture.Root) == profile);
}
