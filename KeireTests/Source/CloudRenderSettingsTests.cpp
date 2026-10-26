#include "Keire/Rendering/RenderSystem.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace
{
    class CloudSettingsDirectory final
    {
      public:
        CloudSettingsDirectory()
            : Root(std::filesystem::temp_directory_path() /
                   ("keire-cloud-settings-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(Root / "ProjectSettings");
        }

        ~CloudSettingsDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] std::filesystem::path SettingsPath() const
        {
            return Root / "ProjectSettings" / "Rendering.keiresettings";
        }

        [[nodiscard]] std::string ReadBytes() const
        {
            std::ifstream input(SettingsPath(), std::ios::binary);
            REQUIRE(input);
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        void Write(const nlohmann::json& document) const
        {
            std::ofstream output(SettingsPath(), std::ios::trunc);
            REQUIRE(output);
            output << document.dump(2) << '\n';
            REQUIRE(output);
        }

        std::filesystem::path Root;
    };

    [[nodiscard]] Keire::RenderEnvironmentSettings TwoLayerSettings()
    {
        Keire::RenderEnvironmentSettings settings;
        settings.Clouds.Layers[0] = {true, 1234.5, 14000.0, 0.65, 0.75, 0.2, 0.15, {-12.5, 3.0}, {0.8, 0.9, 1.2}};
        settings.Clouds.Layers[1] = {true, 6400.0, 42000.0, 0.25, 0.4, 0.35, 0.3, {20.0, -7.0}, {1.1, 0.7, 0.5}};
        return settings;
    }
} // namespace

TEST_CASE("Bloom settings round trip and older rendering settings migrate with bloom disabled")
{
    CloudSettingsDirectory directory;
    Keire::RenderEnvironmentSettings settings;
    settings.Bloom = {true, 0.35, 2.5, 0.2, 3.0};
    Keire::SaveRenderEnvironmentSettings(directory.Root, settings);
    CHECK(Keire::LoadRenderEnvironmentSettings(directory.Root).Bloom == settings.Bloom);
    auto document = nlohmann::json::parse(directory.ReadBytes());
    document["schemaVersion"] = 6;
    document.erase("bloom");
    document.erase("stars");
    directory.Write(document);
    CHECK_FALSE(Keire::LoadRenderEnvironmentSettings(directory.Root).Bloom.Enabled);
    document["schemaVersion"] = 7;
    directory.Write(document);
    CHECK_THROWS((void)Keire::LoadRenderEnvironmentSettings(directory.Root));
    settings.Bloom.Radius = -1.0;
    CHECK_THROWS_AS(Keire::SaveRenderEnvironmentSettings(directory.Root, settings), std::invalid_argument);
    CHECK(nlohmann::json::parse(directory.ReadBytes()) == document);
}

TEST_CASE("Star settings round trip and require an explicit rendering schema")
{
    CloudSettingsDirectory directory;
    Keire::RenderEnvironmentSettings settings;
    settings.Stars.Enabled = true;
    settings.Stars.Seed = 421;
    settings.Stars.Intensity = 3.5;
    Keire::SaveRenderEnvironmentSettings(directory.Root, settings);
    CHECK(Keire::LoadRenderEnvironmentSettings(directory.Root).Stars == settings.Stars);
    auto document = nlohmann::json::parse(directory.ReadBytes());
    document["schemaVersion"] = 7;
    directory.Write(document);
    CHECK_THROWS((void)Keire::LoadRenderEnvironmentSettings(directory.Root));
    document.erase("stars");
    directory.Write(document);
    CHECK(Keire::LoadRenderEnvironmentSettings(directory.Root).Stars == Keire::StarFieldSettings{});
    document["schemaVersion"] = 8;
    directory.Write(document);
    CHECK_THROWS((void)Keire::LoadRenderEnvironmentSettings(directory.Root));
    settings.Stars.Density = -1.0;
    CHECK_THROWS_AS(Keire::SaveRenderEnvironmentSettings(directory.Root, settings), std::invalid_argument);
    CHECK(nlohmann::json::parse(directory.ReadBytes()) == document);
}

TEST_CASE("Current rendering schema round trips two independent authored cloud layers")
{
    CloudSettingsDirectory directory;
    const auto settings = TwoLayerSettings();
    Keire::SaveRenderEnvironmentSettings(directory.Root, settings);
    const auto saved = nlohmann::json::parse(directory.ReadBytes());
    CHECK(saved.at("schemaVersion") == Keire::RenderEnvironmentSettingsSchemaVersion);
    CHECK(saved.at("clouds").at("version") == 2);
    REQUIRE(saved.at("clouds").at("layers").size() == 2);
    CHECK(saved.at("clouds").at("layers").at(0).at("altitudeMeters") == 1234.5);
    CHECK(saved.at("clouds").at("layers").at(1).at("windMetersPerSecond") == nlohmann::json::array({20.0, -7.0}));
    CHECK(Keire::LoadRenderEnvironmentSettings(directory.Root) == settings);
}

TEST_CASE("Legacy rendering schema five without clouds migrates to disabled cloud defaults")
{
    CloudSettingsDirectory directory;
    Keire::SaveRenderEnvironmentSettings(directory.Root, TwoLayerSettings());
    auto legacy = nlohmann::json::parse(directory.ReadBytes());
    legacy["schemaVersion"] = 5;
    legacy.erase("clouds");
    legacy.erase("atmosphere");
    legacy.erase("stars");
    directory.Write(legacy);
    const auto migrated = Keire::LoadRenderEnvironmentSettings(directory.Root);
    CHECK(migrated.SchemaVersion == Keire::RenderEnvironmentSettingsSchemaVersion);
    CHECK(migrated.Clouds == Keire::CloudSettings{});
    for (const auto& layer : migrated.Clouds.Layers)
    {
        CHECK_FALSE(layer.Enabled);
    }
}

TEST_CASE("Rendering load rejects malformed present cloud records")
{
    CloudSettingsDirectory directory;
    Keire::SaveRenderEnvironmentSettings(directory.Root, TwoLayerSettings());
    const auto saved = nlohmann::json::parse(directory.ReadBytes());
    for (const auto& invalid : {nlohmann::json(nullptr), nlohmann::json::array(),
                                nlohmann::json{{"version", 3}, {"layers", nlohmann::json::array()}}})
    {
        auto malformed = saved;
        malformed["clouds"] = invalid;
        directory.Write(malformed);
        CHECK_THROWS((void)Keire::LoadRenderEnvironmentSettings(directory.Root));
    }
    auto malformed = saved;
    malformed["clouds"]["layers"][0]["enabled"] = 1;
    directory.Write(malformed);
    CHECK_THROWS((void)Keire::LoadRenderEnvironmentSettings(directory.Root));
    malformed = saved;
    malformed["clouds"]["layers"][1]["coverage"] = 1.01;
    directory.Write(malformed);
    CHECK_THROWS((void)Keire::LoadRenderEnvironmentSettings(directory.Root));
    malformed = saved;
    malformed["clouds"]["layers"][0]["windMetersPerSecond"] = nlohmann::json::array({1.0});
    directory.Write(malformed);
    CHECK_THROWS((void)Keire::LoadRenderEnvironmentSettings(directory.Root));
}

TEST_CASE("Invalid cloud settings save preserves existing settings bytes")
{
    CloudSettingsDirectory directory;
    const auto settings = TwoLayerSettings();
    Keire::SaveRenderEnvironmentSettings(directory.Root, settings);
    const auto before = directory.ReadBytes();
    for (const double invalid : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN()})
    {
        auto rejected = settings;
        rejected.Clouds.Layers[1].Coverage = invalid;
        CHECK_THROWS(Keire::SaveRenderEnvironmentSettings(directory.Root, rejected));
        CHECK(directory.ReadBytes() == before);
        CHECK(Keire::LoadRenderEnvironmentSettings(directory.Root) == settings);
    }
}

TEST_CASE("Cloud version one project settings preserve legacy appearance when migrated and saved")
{
    CloudSettingsDirectory directory;
    Keire::SaveRenderEnvironmentSettings(directory.Root, TwoLayerSettings());
    auto legacy = nlohmann::json::parse(directory.ReadBytes());
    legacy["clouds"]["version"] = 1;
    for (auto& layer : legacy["clouds"]["layers"])
    {
        layer.erase("appearance");
        layer.erase("seed");
    }
    directory.Write(legacy);
    const auto migrated = Keire::LoadRenderEnvironmentSettings(directory.Root);
    auto expected = TwoLayerSettings();
    for (auto& layer : expected.Clouds.Layers)
    {
        layer.Appearance = Keire::CloudAppearance::LegacyUnlit;
        layer.Seed = 1;
    }
    CHECK(migrated == expected);
    Keire::SaveRenderEnvironmentSettings(directory.Root, migrated);
    const auto saved = nlohmann::json::parse(directory.ReadBytes());
    CHECK(saved.at("clouds").at("version") == 2);
    CHECK(saved.at("clouds").at("layers").at(0).at("appearance") == "legacy-unlit");
    CHECK(saved.at("clouds").at("layers").at(1).at("appearance") == "legacy-unlit");
    CHECK(Keire::LoadRenderEnvironmentSettings(directory.Root) == migrated);
}
