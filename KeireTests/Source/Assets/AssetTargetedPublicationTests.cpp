#include "doctest/doctest.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetInternal.h"

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
