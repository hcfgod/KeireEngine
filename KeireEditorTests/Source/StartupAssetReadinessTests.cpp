#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "KeireClientInternal/Editor/StartupAssetReadiness.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

TEST_CASE("Startup compatibility rejects stale failed and unknown artifacts without blocking compatible sources")
{
    struct TemporaryProject
    {
        std::filesystem::path Root = std::filesystem::absolute(
            std::filesystem::path("Build") / ("StartupReadiness-" + Keire::AssetId::Generate().ToString()));

        ~TemporaryProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } project;
    std::filesystem::create_directories(project.Root / "Assets");
    std::ofstream(project.Root / "Assets/scene.cs") << "source";
    auto importers = Keire::CreateBuiltinAssetImporters();
    for (auto& importer : importers)
    {
        if (importer.Name == "Keire.Text")
        {
            importer.PreviousNames.push_back("Keire.LegacyText");
        }
    }
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = std::move(importers)});
    const auto imported = database->ImportAll();
    auto records = database->Records();
    REQUIRE(records.size() == 1);
    const auto scene = records.front().Id;
    CHECK(KeireEditor::IncompatibleStartupAssets(*database, records).empty());

    SUBCASE("supported legacy importer names remain compatible")
    {
        records.front().Importer = "Keire.LegacyText";
        CHECK(KeireEditor::IncompatibleStartupAssets(*database, records).empty());
        ++records.front().ImporterVersion;
        CHECK(KeireEditor::IncompatibleStartupAssets(*database, records) == std::vector{scene});
    }
    SUBCASE("importer migration excludes generated subassets too")
    {
        const auto generated = Keire::AssetId::Generate();
        records.front().SubAssets = {generated};
        ++records.front().ImporterVersion;
        const auto excluded = KeireEditor::IncompatibleStartupAssets(*database, records);
        CHECK(std::ranges::find(excluded, scene) != excluded.end());
        CHECK(std::ranges::find(excluded, generated) != excluded.end());
    }
    SUBCASE("failed publication remains unavailable despite matching importer version")
    {
        const Keire::AssetImportStatus failed{scene, Keire::AssetImportState::Failed, {}};
        Keire::Detail::AssetDatabaseWorkerAccess::ApplyImportStatuses(*database, std::span(&failed, 1));
        CHECK(KeireEditor::IncompatibleStartupAssets(*database, records) == std::vector{scene});
    }
    SUBCASE("mount allows only known source artifacts and defers stale scene publication")
    {
        const auto mount = KeireEditor::CompatibleStartupMount(imported.CatalogPath, *database, records, scene);
        REQUIRE(mount.AllowedAssets);
        CHECK(*mount.AllowedAssets == std::vector{scene});
        CHECK(mount.ExcludedAssets == std::vector{scene});
    }
}
