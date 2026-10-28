#include "doctest/doctest.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetInternal.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class TemporaryAssetProject final
    {
      public:
        TemporaryAssetProject()
            : Root(std::filesystem::absolute(std::filesystem::path("Build") / ("AssetTargetedPublicationTests-" +
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

    [[nodiscard]] std::vector<char> ReadAll(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
} // namespace

TEST_CASE("targeted publication includes newly referenced dependency sources and generated assets")
{
    TemporaryAssetProject project;
    Keire::AssetId leaf;
    Keire::AssetId shader;
    Keire::AssetId generated;
    bool failDependency = false;
    std::size_t dependencyImports = 0;
    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.NewDependencies";
    importer.Type = Keire::TextAsset::StaticType();
    importer.Extensions = {".newdeps"};
    importer.ContextualImport = [&](const Keire::AssetImportContext& context, const std::span<const std::byte> bytes)
    {
        const std::string source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (source == "leaf" || source == "shader")
            ++dependencyImports;
        if (source == "leaf" && failDependency)
            throw std::runtime_error("dependency import failed");
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        if (source == "shader")
        {
            generated = context.ResolveSubAssetId("generated");
            output.SubAssets.push_back({generated, importer.Type, "generated", "Generated", output.Bytes, {leaf}, {}});
        }
        else if (source == "material")
        {
            generated = context.ResolveSubAssetIdFor(shader, "generated");
            output.AssetDependencies = {shader, generated};
        }
        return output;
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {importer}});
    const auto create = [&](const std::string& name)
    { return database->CreateAsset(name + ".newdeps", importer, std::as_bytes(std::span(name))); };
    const auto baseline = create("baseline");
    const auto initial = database->ImportAll();
    const auto initialCatalog = ReadAll(initial.CatalogPath);
    project.Write("leaf.newdeps", "leaf");
    project.Write("shader.newdeps", "shader");
    (void)database->Refresh();
    leaf = database->Find("leaf.newdeps")->Id;
    shader = database->Find("shader.newdeps")->Id;
    CHECK(database->Find(shader)->SubAssets.empty());
    const auto material = create("material");
    const auto unrelated = create("unrelated");
    const std::array targets{material};

    SUBCASE("missing dependencies are published transitively without importing unrelated sources")
    {
        const auto imported = Keire::Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndex(
            *database, targets, Keire::AssetImportPolicy::FailFast);
        const auto catalog = Keire::Detail::LoadCatalog(imported.CatalogPath);
        REQUIRE(catalog.Entries.size() == 5);
        CHECK(imported.Statuses.size() == 3);
        REQUIRE(database->Find(shader));
        CHECK(database->Find(shader)->SubAssets == std::vector{generated});
        for (const auto id : {baseline, leaf, shader, generated, material})
            CHECK(std::ranges::find(catalog.Entries, id, &Keire::Detail::CatalogEntry::Id) != catalog.Entries.end());
        CHECK(std::ranges::find(catalog.Entries, unrelated, &Keire::Detail::CatalogEntry::Id) == catalog.Entries.end());
        CHECK_NOTHROW(Keire::AssetCooker::Validate(imported.CatalogPath));
        const auto previousDependencyImports = dependencyImports;
        const auto repeated = Keire::Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndex(
            *database, targets, Keire::AssetImportPolicy::FailFast);
        CHECK(dependencyImports == previousDependencyImports);
        CHECK(Keire::Detail::LoadCatalog(repeated.CatalogPath).Entries.size() == 5);
    }
    SUBCASE("dependency failure preserves the last-good catalog")
    {
        failDependency = true;
        CHECK_THROWS_WITH_AS((void)Keire::Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndex(
                                 *database, targets, Keire::AssetImportPolicy::FailFast),
                             "dependency import failed", std::runtime_error);
        CHECK(ReadAll(initial.CatalogPath) == initialCatalog);
        CHECK_NOTHROW(Keire::AssetCooker::Validate(initial.CatalogPath));
    }
    SUBCASE("reimport replaces stale dependencies in the source record and metadata")
    {
        (void)Keire::Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndex(*database, targets,
                                                                                    Keire::AssetImportPolicy::FailFast);
        const auto importedMaterial = database->Find(material);
        REQUIRE(importedMaterial);
        CHECK(importedMaterial->Dependencies.size() == 2);
        CHECK(std::ranges::find(importedMaterial->Dependencies, shader) != importedMaterial->Dependencies.end());
        CHECK(std::ranges::find(importedMaterial->Dependencies, generated) != importedMaterial->Dependencies.end());

        project.Write("material.newdeps", "baseline");
        (void)database->Refresh();
        (void)Keire::Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndex(*database, targets,
                                                                                    Keire::AssetImportPolicy::FailFast);

        REQUIRE(database->Find(material));
        CHECK(database->Find(material)->Dependencies.empty());
        const auto metadata = ReadAll(project.Root / "Assets/material.newdeps.keiremeta");
        const std::string metadataText(metadata.begin(), metadata.end());
        CHECK(metadataText.find(shader.ToString()) == std::string::npos);
        CHECK(metadataText.find(generated.ToString()) == std::string::npos);
    }
}

TEST_CASE("targeted publication migrates cataloged dependencies through unchanged owners")
{
    TemporaryAssetProject project;
    Keire::AssetId leaf;
    Keire::AssetId middle;
    Keire::AssetImporterRegistration container;
    container.Name = "Test.MigrationContainer";
    container.Type = Keire::TextAsset::StaticType();
    container.Extensions = {".container"};
    container.ContextualImport = [&](const Keire::AssetImportContext&, const std::span<const std::byte> bytes)
    {
        const std::string source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::AssetImportOutput output;
        output.Bytes.assign(bytes.begin(), bytes.end());
        output.AssetDependencies = {source == "root" ? middle : leaf};
        return output;
    };
    Keire::AssetImporterRegistration shader;
    shader.Name = "Test.MigrationLeaf";
    shader.Version = 1;
    shader.Type = Keire::TextAsset::StaticType();
    shader.Extensions = {".leaf"};
    std::size_t leafImports = 0;
    bool failMigration = false;
    shader.Import = [&](const std::span<const std::byte> bytes)
    {
        ++leafImports;
        if (failMigration)
        {
            throw std::runtime_error("migration failed");
        }
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {container, shader}});
    const std::string payload = "leaf";
    leaf = database->CreateAsset("used.leaf", shader, std::as_bytes(std::span(payload)));
    const auto unrelated = database->CreateAsset("unrelated.leaf", shader, std::as_bytes(std::span(payload)));
    const std::string middlePayload = "middle";
    middle = database->CreateAsset("middle.container", container, std::as_bytes(std::span(middlePayload)));
    const std::string rootPayload = "root";
    const auto root = database->CreateAsset("root.container", container, std::as_bytes(std::span(rootPayload)));
    (void)database->ImportAll();
    shader.Version = 2;
    database = Keire::CreateRef<Keire::AssetDatabase>(
        Keire::AssetDatabaseSpecification{.ProjectRoot = project.Root, .Importers = {container, shader}});
    const auto before = leafImports;
    const std::array targets{root};
    SUBCASE("failed migration retains the previous published catalog")
    {
        const auto catalogPath = project.Root / "Library/AssetCache/Runtime/catalog.json";
        const auto previousCatalog = ReadAll(catalogPath);
        failMigration = true;
        CHECK_THROWS((void)database->ImportAssets(targets, Keire::AssetImportPolicy::FailFast));
        CHECK(ReadAll(catalogPath) == previousCatalog);
        REQUIRE(database->Find(leaf));
        CHECK(database->Find(leaf)->ImporterVersion == 1);
    }
    SUBCASE("successful migration leaves unrelated stale assets deferred")
    {
        (void)database->ImportAssets(targets, Keire::AssetImportPolicy::FailFast);
        REQUIRE(database->Find(leaf));
        REQUIRE(database->Find(unrelated));
        CHECK(database->Find(leaf)->ImporterVersion == 2);
        CHECK(database->Find(unrelated)->ImporterVersion == 1);
        CHECK(leafImports == before + 1);
    }
    SUBCASE("worker keep-last-good policy reports dependency migration failure")
    {
        failMigration = true;
        const auto result = database->ImportAssets(targets, Keire::AssetImportPolicy::KeepLastGood);
        const auto failed = std::ranges::find(result.Statuses, leaf, &Keire::AssetImportStatus::Id);
        REQUIRE(failed != result.Statuses.end());
        CHECK(failed->State == Keire::AssetImportState::Failed);
        REQUIRE(database->Find(leaf));
        CHECK(database->Find(leaf)->ImporterVersion == 1);
        REQUIRE(database->Find(unrelated));
        CHECK(database->Find(unrelated)->ImporterVersion == 1);
        CHECK_NOTHROW(Keire::AssetCooker::Validate(result.CatalogPath));

        failMigration = false;
        const auto retry = database->ImportAssets(targets, Keire::AssetImportPolicy::KeepLastGood);
        CHECK(std::ranges::none_of(retry.Statuses, [](const Keire::AssetImportStatus& status)
                                   { return status.State == Keire::AssetImportState::Failed; }));
        REQUIRE(database->Find(leaf));
        CHECK(database->Find(leaf)->ImporterVersion == 2);
    }
}
