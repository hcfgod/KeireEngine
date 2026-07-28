#include "Keire/Project/ProjectAuthoringSettings.h"

#include <doctest/doctest.h>

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
}
