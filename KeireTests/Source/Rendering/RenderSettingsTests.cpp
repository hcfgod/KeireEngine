#include "Keire/Rendering/RenderSystem.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("keire-render-settings-" + Keire::AssetId::Generate().ToString()))
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

    void WriteSettings(const std::filesystem::path& root, nlohmann::json document)
    {
        std::ofstream output(root / "ProjectSettings" / "Rendering.keiresettings", std::ios::trunc);
        REQUIRE(output);
        output << document.dump(2) << '\n';
    }

    [[nodiscard]] nlohmann::json LegacySettings(const std::uint32_t schema)
    {
        return {{"schemaVersion", schema},
                {"ambientColor", {0.2F, 0.22F, 0.26F, 1.0F}},
                {"ambientIntensity", 0.75F},
                {"exposure", 1.0F}};
    }
} // namespace

TEST_CASE("Rendering settings schema three round trips every GPU occlusion mode")
{
    TemporaryDirectory directory;
    for (const auto mode :
         {Keire::GpuOcclusionMode::Disabled, Keire::GpuOcclusionMode::Automatic, Keire::GpuOcclusionMode::Forced})
    {
        Keire::RenderEnvironmentSettings settings;
        settings.GpuOcclusion = mode;
        Keire::SaveRenderEnvironmentSettings(directory.Path, settings);
        CHECK(Keire::LoadRenderEnvironmentSettings(directory.Path) == settings);
    }
}

TEST_CASE("Legacy rendering settings migrate to automatic GPU occlusion without weakening validation")
{
    TemporaryDirectory directory;
    for (const auto schema : {1U, 2U})
    {
        WriteSettings(directory.Path, LegacySettings(schema));
        const auto migrated = Keire::LoadRenderEnvironmentSettings(directory.Path);
        CHECK(migrated.SchemaVersion == 3U);
        CHECK(migrated.GpuOcclusion == Keire::GpuOcclusionMode::Automatic);
        CHECK_NOTHROW(Keire::ValidateRenderEnvironmentSettings(migrated));
    }

    auto future = LegacySettings(4U);
    WriteSettings(directory.Path, std::move(future));
    CHECK_THROWS_AS((void)Keire::LoadRenderEnvironmentSettings(directory.Path), std::runtime_error);

    auto malformed = LegacySettings(3U);
    malformed["gpuOcclusion"] = "aggressive";
    WriteSettings(directory.Path, std::move(malformed));
    CHECK_THROWS_AS((void)Keire::LoadRenderEnvironmentSettings(directory.Path), std::runtime_error);
}
