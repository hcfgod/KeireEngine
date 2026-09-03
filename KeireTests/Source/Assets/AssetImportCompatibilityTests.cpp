#include "doctest/doctest.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "KeireInternal/Assets/AssetInternal.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    class TemporaryAssetProject final
    {
      public:
        TemporaryAssetProject()
            : Root(std::filesystem::absolute(std::filesystem::path("Build") / ("AssetImportCompatibilityTests-" +
                                                                               Keire::AssetId::Generate().ToString())))
        {
            std::filesystem::create_directories(Root / "Assets");
        }

        ~TemporaryAssetProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        void Write(const std::filesystem::path& relative, const std::string_view content) const
        {
            const auto path = Root / "Assets" / relative;
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            REQUIRE(stream.good());
        }

        std::filesystem::path Root;
    };

    std::string HistoricalMetadata(const Keire::AssetId id, const Keire::AssetTypeId type)
    {
        return std::string("{\n") +
               "  \"schemaVersion\": 1,\n"
               "  \"id\": \"" +
               id.ToString() +
               "\",\n"
               "  \"type\": \"" +
               type.ToString() +
               "\",\n"
               "  \"importer\": \"Test.Material\",\n"
               "  \"importerVersion\": 1,\n"
               "  \"dependencies\": [],\n"
               "  \"subAssets\": []\n"
               "}\n";
    }

    Keire::AssetImporterRegistration Importer(const std::string_view name, const Keire::AssetTypeId type,
                                              const std::string_view extension)
    {
        Keire::AssetImporterRegistration result;
        result.Name = name;
        result.Type = type;
        result.Extensions = {std::string(extension)};
        result.Import = [](const std::span<const std::byte> bytes)
        { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
        return result;
    }
} // namespace

TEST_CASE("Renamed importers resolve historical metadata by extension and asset type")
{
    TemporaryAssetProject project;
    const auto legacyType = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000071");
    const auto currentType = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000072");
    const auto id = Keire::AssetId::Generate();
    project.Write("Historical.legacy", "historical payload");
    project.Write("Historical.legacy.keiremeta", HistoricalMetadata(id, legacyType));

    auto current = Importer("Test.Material", currentType, ".current");
    auto renamed = Importer("Test.LegacyMaterial", legacyType, ".legacy");
    renamed.PreviousNames = {"Test.Material"};
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {current, renamed}});

    const auto imported = database->ImportAll();
    CHECK(imported.Imported == 1);
    CHECK(imported.Statuses.size() == 1);
    CHECK(imported.Statuses.front().State == Keire::AssetImportState::Imported);
    REQUIRE(database->Find(id));
    CHECK(database->Find(id)->Importer == "Test.Material");
}

TEST_CASE("Renamed importers resolve historical metadata after an extension is reallocated")
{
    TemporaryAssetProject project;
    const auto legacyType = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000073");
    const auto currentType = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000074");
    const auto id = Keire::AssetId::Generate();
    project.Write("Historical.material", "historical payload");
    project.Write("Historical.material.keiremeta", HistoricalMetadata(id, legacyType));

    auto current = Importer("Test.Material", currentType, ".material");
    auto renamed = Importer("Test.LegacyMaterial", legacyType, ".legacy-material");
    renamed.PreviousNames = {"Test.Material"};
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {current, renamed}});

    const auto imported = database->ImportAll();
    CHECK(imported.Imported == 1);
    REQUIRE(imported.Statuses.size() == 1);
    CHECK(imported.Statuses.front().State == Keire::AssetImportState::Imported);
    REQUIRE(database->Find(id));
    CHECK(database->Find(id)->Importer == "Test.Material");
}

TEST_CASE("Targeted publication is isolated from unrelated broken sources")
{
    TemporaryAssetProject project;
    project.Write("Stable.ok", "stable");
    auto goodImporter =
        Importer("Test.TargetedPublication", Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000002"), ".ok");
    Keire::AssetImporterRegistration failingImporter;
    failingImporter.Name = "Test.UnrelatedFailure";
    failingImporter.Type = Keire::AssetTypeId::Parse("f1000000-0000-4000-8000-000000000003");
    failingImporter.Extensions = {".bad"};
    failingImporter.Import = [](std::span<const std::byte>) -> std::vector<std::byte>
    { throw std::runtime_error("unrelated import failure"); };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
        .ProjectRoot = project.Root, .Importers = {std::move(goodImporter), std::move(failingImporter)}});
    const auto initial = database->ImportAll();
    REQUIRE(std::filesystem::is_regular_file(initial.CatalogPath));

    project.Write("Broken.bad", "broken");
    project.Write("Lighting/Baked.ok", "baked lighting");
    (void)database->Refresh();
    const auto baked = database->Find("Lighting/Baked.ok");
    REQUIRE(baked);
    const std::array targets{baked->Id};
    const auto targeted = database->ImportAssets(targets, Keire::AssetImportPolicy::FailFast);
    REQUIRE(targeted.Statuses.size() == 1U);
    CHECK(targeted.Statuses.front().State != Keire::AssetImportState::Failed);

    const auto catalog = Keire::Detail::LoadCatalog(targeted.CatalogPath);
    CHECK(std::ranges::find(catalog.Entries, baked->Id, &Keire::Detail::CatalogEntry::Id) != catalog.Entries.end());
    const auto broken = database->Find("Broken.bad");
    REQUIRE(broken);
    CHECK(std::ranges::find(catalog.Entries, broken->Id, &Keire::Detail::CatalogEntry::Id) == catalog.Entries.end());

    project.Write("Models/Imported.ok", "generated model subassets");
    (void)database->Refresh();
    const auto importedModel = database->Find("Models/Imported.ok");
    REQUIRE(importedModel);
    const std::array mixedTargets{importedModel->Id, broken->Id};
    const auto mixed = database->ImportAssets(mixedTargets, Keire::AssetImportPolicy::KeepLastGood);
    REQUIRE(mixed.Statuses.size() == 2U);
    CHECK(std::ranges::count(mixed.Statuses, Keire::AssetImportState::Failed, &Keire::AssetImportStatus::State) == 1U);
    const auto mixedCatalog = Keire::Detail::LoadCatalog(mixed.CatalogPath);
    CHECK(std::ranges::find(mixedCatalog.Entries, importedModel->Id, &Keire::Detail::CatalogEntry::Id) !=
          mixedCatalog.Entries.end());
    CHECK(std::ranges::find(mixedCatalog.Entries, broken->Id, &Keire::Detail::CatalogEntry::Id) ==
          mixedCatalog.Entries.end());
}
