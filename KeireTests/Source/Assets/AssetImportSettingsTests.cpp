#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

TEST_CASE("Asset import settings update atomically without rewriting source bytes")
{
    const auto root =
        std::filesystem::temp_directory_path() / ("keire-import-settings-" + Keire::AssetId::Generate().ToString());
    std::filesystem::create_directories(root / "Assets");
    {
        std::ofstream source(root / "Assets" / "character.fixture", std::ios::binary);
        source << "unchanged-source";
    }

    Keire::AssetImporterRegistration importer;
    importer.Name = "Keire.Tests.Settings";
    importer.Type = Keire::AssetTypeId(Keire::AssetId(0x4b45495245544553ULL, 0x5453455454494e47ULL));
    importer.Extensions = {".fixture"};
    importer.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
    importer.ImportOptions = {{"profile",
                               "Profile",
                               "Rig",
                               Keire::AssetImportOptionKind::Choice,
                               std::string("humanoid"),
                               {},
                               {},
                               1.0,
                               {"humanoid", "quadruped"}}};

    {
        Keire::AssetDatabaseSpecification specification;
        specification.ProjectRoot = root;
        specification.SourceDirectory = "Assets";
        specification.CacheDirectory = "Library/AssetCache";
        specification.Importers = {importer};
        auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
        REQUIRE(database->Refresh() == 1);
        const auto records = database->Records();
        REQUIRE(records.size() == 1);

        database->SetImportSettings(records.front().Id, {{"profile", std::string("quadruped")}});
        const auto updated = database->Find(records.front().Id);
        REQUIRE(updated);
        REQUIRE(updated->ImportSettings.contains("profile"));
        CHECK(std::get<std::string>(updated->ImportSettings.at("profile")) == "quadruped");
        const auto metadataDigest = updated->MetadataDigest;

        database->RequestReimport(records.front().Id);
        const auto invalidated = database->Find(records.front().Id);
        REQUIRE(invalidated);
        CHECK(invalidated->MetadataDigest != metadataDigest);
        CHECK(invalidated->Id == records.front().Id);

        CHECK_THROWS_AS(database->SetImportSettings(records.front().Id, {{"profile", std::string("invalid")}}),
                        std::invalid_argument);
        const auto lastGood = database->Find(records.front().Id);
        REQUIRE(lastGood);
        CHECK(std::get<std::string>(lastGood->ImportSettings.at("profile")) == "quadruped");
    }

    std::ifstream source(root / "Assets" / "character.fixture", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()) == "unchanged-source");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
