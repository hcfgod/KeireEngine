#include "Keire/Assets/Asset.h"
#include "Keire/Project/ProjectAuthoringSettings.h"
#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Environment/EnvironmentLightingSettingsStore.h"
#include "KeireInternal/Environment/EnvironmentTimeSettingsStore.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectSettingsSerialization.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace
{
    class SettingsDirectory final
    {
      public:
        SettingsDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("keire-settings-serialization-" + Keire::AssetId::Generate().ToString()))
        {
        }

        ~SettingsDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };
} // namespace

TEST_CASE("Pure project settings serializers match saved bytes and round trip")
{
    SettingsDirectory directory;
    std::string serialized;
    std::string filename;

    SUBCASE("Authoring")
    {
        auto settings = Keire::DefaultProjectAuthoringSettings();
        settings.ExternalEditorId = "custom";
        settings.ExternalEditorExecutable = "tools/My Editor.exe";
        settings.PhysicsLayerNames[5] = "Projectiles";
        serialized = Keire::Detail::SerializeProjectAuthoringSettings(settings);
        CHECK_FALSE(std::filesystem::exists(directory.Path));
        Keire::SaveProjectAuthoringSettings(directory.Path, settings);
        CHECK(Keire::LoadProjectAuthoringSettings(directory.Path) == settings);
        filename = "Authoring.keiresettings";
    }
    SUBCASE("Rendering")
    {
        Keire::RenderEnvironmentSettings settings;
        settings.Exposure = 2.5F;
        settings.SkyVisible = false;
        serialized = Keire::Detail::SerializeRenderEnvironmentSettings(settings);
        CHECK_FALSE(std::filesystem::exists(directory.Path));
        Keire::SaveRenderEnvironmentSettings(directory.Path, settings);
        CHECK(Keire::LoadRenderEnvironmentSettings(directory.Path) == settings);
        filename = "Rendering.keiresettings";
    }
    SUBCASE("Environment time")
    {
        Keire::Detail::EnvironmentClockState settings;
        settings.Day = std::numeric_limits<std::int64_t>::max();
        settings.DayFraction = 0.375;
        settings.Rate = -0.5;
        serialized = Keire::Detail::EnvironmentTimeSettingsStore::Serialize(settings);
        CHECK_FALSE(std::filesystem::exists(directory.Path));
        Keire::Detail::EnvironmentTimeSettingsStore::Save(directory.Path, settings);
        CHECK(Keire::Detail::EnvironmentTimeSettingsStore::Load(directory.Path) == settings);
        filename = "EnvironmentTime.keiresettings";
    }
    SUBCASE("Environment lighting")
    {
        const Keire::Detail::EnvironmentLightingProfile settings(
            Keire::Detail::EnvironmentDirectionalOwnership::Environment,
            {{0.0, 350.0, -60.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 10.0, 70.0, {1.0, 0.7, 0.4}, 12.0}});
        serialized = Keire::Detail::EnvironmentLightingSettingsStore::Serialize(settings);
        CHECK_FALSE(std::filesystem::exists(directory.Path));
        Keire::Detail::EnvironmentLightingSettingsStore::Save(directory.Path, settings);
        CHECK(Keire::Detail::EnvironmentLightingSettingsStore::Load(directory.Path) == settings);
        filename = "EnvironmentLighting.keiresettings";
    }

    REQUIRE_FALSE(serialized.empty());
    CHECK(serialized.back() == '\n');
    CHECK(Keire::Detail::ReadTextFile(directory.Path / "ProjectSettings" / filename, 1024U * 1024U) == serialized);
}

TEST_CASE("Invalid project settings serialization leaves new and existing destinations unchanged")
{
    SettingsDirectory directory;
    const auto existingRoot = directory.Path / "Existing";
    const auto newRoot = directory.Path / "New";
    std::string filename;
    std::string original;

    SUBCASE("Authoring")
    {
        auto settings = Keire::DefaultProjectAuthoringSettings();
        original = Keire::Detail::SerializeProjectAuthoringSettings(settings);
        Keire::SaveProjectAuthoringSettings(existingRoot, settings);
        settings.ExternalEditorId.clear();
        CHECK_THROWS_AS((void)Keire::Detail::SerializeProjectAuthoringSettings(settings), std::invalid_argument);
        CHECK_THROWS_AS(Keire::SaveProjectAuthoringSettings(newRoot, settings), std::invalid_argument);
        CHECK_THROWS_AS(Keire::SaveProjectAuthoringSettings(existingRoot, settings), std::invalid_argument);
        filename = "Authoring.keiresettings";
    }
    SUBCASE("Rendering")
    {
        Keire::RenderEnvironmentSettings settings;
        original = Keire::Detail::SerializeRenderEnvironmentSettings(settings);
        Keire::SaveRenderEnvironmentSettings(existingRoot, settings);
        settings.Exposure = std::numeric_limits<float>::quiet_NaN();
        CHECK_THROWS_AS((void)Keire::Detail::SerializeRenderEnvironmentSettings(settings), std::invalid_argument);
        CHECK_THROWS_AS(Keire::SaveRenderEnvironmentSettings(newRoot, settings), std::invalid_argument);
        CHECK_THROWS_AS(Keire::SaveRenderEnvironmentSettings(existingRoot, settings), std::invalid_argument);
        filename = "Rendering.keiresettings";
    }
    SUBCASE("Environment time")
    {
        Keire::Detail::EnvironmentClockState settings;
        original = Keire::Detail::EnvironmentTimeSettingsStore::Serialize(settings);
        Keire::Detail::EnvironmentTimeSettingsStore::Save(existingRoot, settings);
        settings.DayFraction = 1.0;
        CHECK_THROWS_AS((void)Keire::Detail::EnvironmentTimeSettingsStore::Serialize(settings), std::invalid_argument);
        CHECK_THROWS_AS(Keire::Detail::EnvironmentTimeSettingsStore::Save(newRoot, settings), std::invalid_argument);
        CHECK_THROWS_AS(Keire::Detail::EnvironmentTimeSettingsStore::Save(existingRoot, settings),
                        std::invalid_argument);
        filename = "EnvironmentTime.keiresettings";
    }

    CHECK_FALSE(std::filesystem::exists(newRoot));
    CHECK(Keire::Detail::ReadTextFile(existingRoot / "ProjectSettings" / filename, 1024U * 1024U) == original);
}
