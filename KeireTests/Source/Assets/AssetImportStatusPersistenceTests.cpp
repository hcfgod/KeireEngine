#include "Keire/Assets/AssetPipeline.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    struct ImportStatusProject
    {
        std::filesystem::path Root = std::filesystem::absolute(
            std::filesystem::path("Build") / ("ImportStatus-" + Keire::AssetId::Generate().ToString()));

        ImportStatusProject() { std::filesystem::create_directories(Root / "Assets"); }
        ~ImportStatusProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
        void Write(const std::string_view name, const std::string_view text) const
        {
            std::ofstream stream(Root / "Assets" / name, std::ios::binary | std::ios::trunc);
            stream << text;
            REQUIRE(stream.good());
        }
    };
} // namespace

TEST_CASE("Published source index retains import failures across restarts and "
          "targeted worker operations")
{
    ImportStatusProject project;
    project.Write("good.status", "good");
    project.Write("bad.status", "bad");
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.PersistedImportStatus";
    importer.Type = Keire::TextAsset::StaticType();
    importer.Extensions = {".status"};
    importer.Import = [](const std::span<const std::byte> bytes)
    {
        if (!bytes.empty() && bytes.front() == std::byte{'b'})
            throw std::runtime_error("Correct the invalid QA source.");
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    Keire::AssetDatabaseSpecification specification{.ProjectRoot = project.Root, .Importers = {importer}};
    auto database = Keire::CreateRef<Keire::AssetDatabase>(specification);
    (void)database->ImportAll(Keire::AssetImportPolicy::KeepLastGood);
    const auto bad = database->Find("bad.status");
    const auto good = database->Find("good.status");
    REQUIRE(bad);
    REQUIRE(good);
    const auto original = database->ImportStatus(bad->Id);
    REQUIRE(original.State == Keire::AssetImportState::Failed);
    REQUIRE_FALSE(original.Diagnostics.empty());
    const auto index = project.Root / "source-index.json";
    Keire::Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(*database, index);
    auto reopened = Keire::Detail::AssetDatabaseWorkerAccess::CreateFromSourceIndex(specification, index);
    const auto restored = reopened->ImportStatus(bad->Id);
    CHECK(restored.State == Keire::AssetImportState::Failed);
    REQUIRE(restored.Diagnostics.size() == original.Diagnostics.size());
    CHECK(restored.Diagnostics.front().Message == original.Diagnostics.front().Message);
    CHECK(restored.Diagnostics.front().RelativePath == original.Diagnostics.front().RelativePath);

    SUBCASE("Unrelated targeted import retains failure and successful correction "
            "replaces it")
    {
        const std::array targets{good->Id};
        (void)Keire::Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndex(
            *reopened, targets, Keire::AssetImportPolicy::KeepLastGood);
        Keire::Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(*reopened, index);
        reopened = Keire::Detail::AssetDatabaseWorkerAccess::CreateFromSourceIndex(specification, index);
        CHECK(reopened->ImportStatus(bad->Id).State == Keire::AssetImportState::Failed);
        project.Write("bad.status", "corrected");
        const std::array correction{bad->Id};
        (void)reopened->ImportAssets(correction, Keire::AssetImportPolicy::KeepLastGood);
        Keire::Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(*reopened, index);
        reopened = Keire::Detail::AssetDatabaseWorkerAccess::CreateFromSourceIndex(specification, index);
        CHECK(reopened->ImportStatus(bad->Id).State == Keire::AssetImportState::Imported);
        CHECK(reopened->ImportStatus(bad->Id).Diagnostics.empty());
    }
    SUBCASE("Legacy index without import results remains compatible")
    {
        Keire::Detail::WriteAssetSourceIndex(index, database->Records());
        nlohmann::json legacy;
        {
            std::ifstream stream(index);
            stream >> legacy;
        }
        legacy.erase("importResults");
        {
            std::ofstream stream(index, std::ios::trunc);
            stream << legacy;
            REQUIRE(stream.good());
        }
        reopened = Keire::Detail::AssetDatabaseWorkerAccess::CreateFromSourceIndex(specification, index);
        CHECK(reopened->Records().size() == 2);
        CHECK(reopened->ImportStatus(bad->Id).State == Keire::AssetImportState::NotImported);
    }
    SUBCASE("Rejected import statuses leave the current database unchanged")
    {
        for (const int invalidCase : {0, 1, 2})
        {
            auto invalid = original;
            if (invalidCase == 0)
                invalid.Id = Keire::AssetId::Generate();
            if (invalidCase == 1)
                invalid.State = static_cast<Keire::AssetImportState>(255);
            std::vector statuses{invalid};
            if (invalidCase == 2)
                statuses.push_back(invalid);
            Keire::Detail::WriteAssetSourceIndex(index, database->Records(), statuses);
            CHECK_THROWS((void)Keire::Detail::AssetDatabaseWorkerAccess::ReloadSourceIndex(*reopened, index));
            CHECK(reopened->Records().size() == 2);
            CHECK(reopened->ImportStatus(bad->Id).State == Keire::AssetImportState::Failed);
            REQUIRE_FALSE(reopened->ImportStatus(bad->Id).Diagnostics.empty());
            CHECK(reopened->ImportStatus(bad->Id).Diagnostics.front().Message == original.Diagnostics.front().Message);
        }
    }
}

TEST_CASE("Failed catalog publication never advertises upgraded artifacts after source-index restart")
{
    ImportStatusProject project;
    project.Write("shader.publish", "shader");
    bool failPublication = false;
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.PublicationCompatibility";
    importer.Type = Keire::TextAsset::StaticType();
    importer.Extensions = {".publish"};
    importer.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
    importer.Cook = [&failPublication](const std::span<const std::byte> bytes, Keire::AssetTargetPlatform)
    {
        if (failPublication)
        {
            throw std::runtime_error("publication rejected");
        }
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    Keire::AssetDatabaseSpecification specification{.ProjectRoot = project.Root, .Importers = {importer}};
    auto database = Keire::CreateRef<Keire::AssetDatabase>(specification);
    const auto baseline = database->ImportAll();
    const auto readCatalog = [&baseline]
    {
        std::ifstream input(baseline.CatalogPath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const auto originalCatalog = readCatalog();
    const auto id = database->Records().front().Id;
    specification.Importers.front().Version = 2;
    database = Keire::CreateRef<Keire::AssetDatabase>(specification);
    failPublication = true;
    const auto result = database->ImportAll(Keire::AssetImportPolicy::KeepLastGood);
    CHECK(result.CatalogPath.empty());
    REQUIRE(result.Statuses.size() == 1);
    CHECK(result.Statuses.front().State == Keire::AssetImportState::Failed);
    CHECK(readCatalog() == originalCatalog);
    REQUIRE(database->Find(id));
    CHECK(database->Find(id)->ImporterVersion == 2);

    const auto index = project.Root / "publication-index.json";
    Keire::Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(*database, index);
    database = Keire::Detail::AssetDatabaseWorkerAccess::CreateFromSourceIndex(specification, index);
    CHECK(database->ImportStatus(id).State == Keire::AssetImportState::Failed);
    REQUIRE_FALSE(database->ImportStatus(id).Diagnostics.empty());
    CHECK(database->ImportStatus(id).Diagnostics.back().Message.find("publication failed") != std::string::npos);
    failPublication = false;
    const auto retry = database->ImportAll(Keire::AssetImportPolicy::KeepLastGood);
    CHECK_FALSE(retry.CatalogPath.empty());
    CHECK(database->ImportStatus(id).State != Keire::AssetImportState::Failed);
}
