#include "Keire/Project/ProjectAuthoringSettings.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("keire-authoring-settings-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };
} // namespace

TEST_CASE("Project authoring settings round trip default mixer and a symmetric 32-layer matrix")
{
    TemporaryDirectory directory;
    auto settings = Keire::DefaultProjectAuthoringSettings();
    settings.DefaultMixer = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000001");
    settings.PhysicsLayerNames[5] = "Projectiles";
    settings.PhysicsCollisionMatrix[1] &= ~(1U << 5U);
    settings.PhysicsCollisionMatrix[5] &= ~(1U << 1U);
    settings.Audio.MixSampleRate = 96000;
    settings.Audio.PeriodFrames = 512;
    settings.Audio.OutputLayout = Keire::AudioChannelLayout::Surround71;
    settings.Audio.MaximumVoices = 512;
    settings.Audio.MaximumVirtualVoices = 2048;
    settings.Audio.PlaybackDeviceId = "a0b1c2d3";

    Keire::SaveProjectAuthoringSettings(directory.Path, settings);
    CHECK(Keire::LoadProjectAuthoringSettings(directory.Path) == settings);
}

TEST_CASE("Project authoring settings reject duplicate names and asymmetric matrices")
{
    auto settings = Keire::DefaultProjectAuthoringSettings();
    settings.PhysicsLayerNames[1] = settings.PhysicsLayerNames[0];
    CHECK_THROWS_AS(Keire::ValidateProjectAuthoringSettings(settings), std::invalid_argument);

    settings = Keire::DefaultProjectAuthoringSettings();
    settings.PhysicsCollisionMatrix[2] &= ~(1U << 3U);
    CHECK_THROWS_AS(Keire::ValidateProjectAuthoringSettings(settings), std::invalid_argument);

    settings = Keire::DefaultProjectAuthoringSettings();
    settings.Audio.PeriodFrames = 64;
    CHECK_THROWS_AS(Keire::ValidateProjectAuthoringSettings(settings), std::invalid_argument);

    settings = Keire::DefaultProjectAuthoringSettings();
    settings.Audio.MaximumVirtualVoices = settings.Audio.MaximumVoices - 1;
    CHECK_THROWS_AS(Keire::ValidateProjectAuthoringSettings(settings), std::invalid_argument);

    settings = Keire::DefaultProjectAuthoringSettings();
    settings.Audio.PlaybackDeviceId = "not-a-device";
    CHECK_THROWS_AS(Keire::ValidateProjectAuthoringSettings(settings), std::invalid_argument);
}

TEST_CASE("Project authoring settings migrate schema one projects to safe audio defaults")
{
    TemporaryDirectory directory;
    const auto path = directory.Path / "ProjectSettings" / "Authoring.keiresettings";
    auto settings = Keire::DefaultProjectAuthoringSettings();
    settings.DefaultMixer = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000002");
    Keire::SaveProjectAuthoringSettings(directory.Path, settings);

    std::ifstream input(path);
    REQUIRE(input);
    auto document = nlohmann::json::parse(input);
    document["schemaVersion"] = 1;
    document.erase("audio");
    input.close();
    std::ofstream output(path, std::ios::trunc);
    REQUIRE(output);
    output << document.dump(2) << '\n';
    output.close();

    const auto migrated = Keire::LoadProjectAuthoringSettings(directory.Path);
    CHECK(migrated.SchemaVersion == Keire::ProjectAuthoringSettingsSchemaVersion);
    CHECK(migrated.DefaultMixer == settings.DefaultMixer);
    CHECK(migrated.Audio == Keire::AudioProjectSettings{});
}
