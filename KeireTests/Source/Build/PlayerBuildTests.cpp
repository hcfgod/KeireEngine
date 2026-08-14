#include "Keire/Build/PlayerBuild.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <vector>

namespace
{
    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("keire-player-build-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(Path / "ProjectSettings");
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    [[nodiscard]] Keire::ProjectDescriptor Project()
    {
        Keire::ProjectDescriptor result;
        result.Id = Keire::ProjectId::Parse("20000000-0000-4000-8000-000000000002");
        result.Name = "Example Game";
        result.CreatedWithEngineVersion = "0.1.0";
        result.MinimumEngineVersion = "0.1.0";
        result.StartupScene = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000004");
        return result;
    }
} // namespace

TEST_CASE("Player settings default from the project and round trip optional platform icons")
{
    TemporaryDirectory directory;
    const auto project = Project();
    auto settings = Keire::LoadPlayerSettings(directory.Path, project);
    CHECK(settings.ProductName == "Example Game");
    CHECK(settings.WindowTitle == "Example Game");
    CHECK(settings.ApplicationIdentifier == "com.keire.project.20000000000040008000000000000002");

    settings.Version = "1.2.3-beta.1+shipping";
    settings.WindowsIcon = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000003");
    Keire::SavePlayerSettings(directory.Path, settings);
    CHECK(Keire::LoadPlayerSettings(directory.Path, project) == settings);
}

TEST_CASE("Player settings reject unsafe names identifiers versions and titles")
{
    auto settings = Keire::DefaultPlayerSettings(Project());
    settings.ProductName = "../Game";
    CHECK_THROWS_AS(Keire::ValidatePlayerSettings(settings), std::invalid_argument);
    settings = Keire::DefaultPlayerSettings(Project());
    settings.Version = "01.0.0";
    CHECK_THROWS_AS(Keire::ValidatePlayerSettings(settings), std::invalid_argument);
    settings = Keire::DefaultPlayerSettings(Project());
    settings.ApplicationIdentifier = "game";
    CHECK_THROWS_AS(Keire::ValidatePlayerSettings(settings), std::invalid_argument);
    settings = Keire::DefaultPlayerSettings(Project());
    settings.WindowTitle = "Bad\nTitle";
    CHECK_THROWS_AS(Keire::ValidatePlayerSettings(settings), std::invalid_argument);
}

TEST_CASE("Player build profiles default to the host and round trip signing hooks")
{
    TemporaryDirectory directory;
    auto profiles = Keire::LoadPlayerBuildProfiles(directory.Path);
    REQUIRE(profiles.Profiles.size() == 1);
    CHECK(profiles.Profiles.front().Platform == Keire::HostPlayerPlatform());
    CHECK(profiles.Profiles.front().Architecture == Keire::HostPlayerArchitecture());
    profiles.Profiles.front().Signing.Policy = Keire::PlayerSigningPolicy::Required;
    profiles.Profiles.front().Signing.Command = "Scripts/SignPlayer";
    profiles.Profiles.front().Signing.Arguments = {"--timestamp"};
    profiles.Profiles.front().Signing.RequiredEnvironment = {"SIGNING_TOKEN"};

    Keire::SavePlayerBuildProfiles(directory.Path, profiles);
    CHECK(Keire::LoadPlayerBuildProfiles(directory.Path) == profiles);
    CHECK(Keire::FindPlayerBuildProfile(profiles, profiles.ActiveProfile) == profiles.Profiles.front());
    CHECK(Keire::FindPlayerBuildProfile(profiles, "desktop development") == profiles.Profiles.front());
}

TEST_CASE("Player build profiles reject collisions traversal and missing required signing commands")
{
    auto profiles = Keire::DefaultPlayerBuildProfiles();
    auto duplicate = profiles.Profiles.front();
    duplicate.Id = Keire::AssetId::Generate();
    profiles.Profiles.push_back(duplicate);
    CHECK_THROWS_AS(Keire::ValidatePlayerBuildProfiles(profiles), std::invalid_argument);

    profiles = Keire::DefaultPlayerBuildProfiles();
    profiles.Profiles.front().OutputSlug = "../outside";
    CHECK_THROWS_AS(Keire::ValidatePlayerBuildProfiles(profiles), std::invalid_argument);

    profiles = Keire::DefaultPlayerBuildProfiles();
    profiles.Profiles.front().Signing.Policy = Keire::PlayerSigningPolicy::Required;
    CHECK_THROWS_AS(Keire::ValidatePlayerBuildProfiles(profiles), std::invalid_argument);
}

TEST_CASE("Player build scenes migrate the project startup scene and round trip ordered enabled state")
{
    TemporaryDirectory directory;
    const auto project = Project();
    auto scenes = Keire::LoadPlayerBuildScenes(directory.Path, project);
    REQUIRE(scenes.Scenes.size() == 1);
    CHECK(scenes.Scenes.front().Scene == project.StartupScene);
    CHECK(scenes.Scenes.front().Enabled);

    const auto second = Keire::AssetId::Parse("50000000-0000-4000-8000-000000000005");
    scenes.Scenes.insert(scenes.Scenes.begin(), {second, false});
    Keire::SavePlayerBuildScenes(directory.Path, scenes);
    CHECK(Keire::LoadPlayerBuildScenes(directory.Path, project) == scenes);
    CHECK(Keire::PlayerBuildStartupScene(scenes) == project.StartupScene);
    CHECK(Keire::EnabledPlayerBuildScenes(scenes) == std::vector{project.StartupScene});
}

TEST_CASE("Player build scenes reject duplicates and require an enabled startup scene when building")
{
    auto scenes = Keire::DefaultPlayerBuildScenes(Project());
    scenes.Scenes.push_back(scenes.Scenes.front());
    CHECK_THROWS_AS(Keire::ValidatePlayerBuildScenes(scenes), std::invalid_argument);

    scenes = Keire::DefaultPlayerBuildScenes(Project());
    scenes.Scenes.front().Enabled = false;
    CHECK_NOTHROW(Keire::ValidatePlayerBuildScenes(scenes));
    CHECK(Keire::EnabledPlayerBuildScenes(scenes).empty());
    CHECK_THROWS_AS((void)Keire::PlayerBuildStartupScene(scenes), std::invalid_argument);
}
