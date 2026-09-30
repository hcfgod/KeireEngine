#include "KeireClient/Editor/AssetPackageAuthoring.h"

#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{
    class PackageFixture final
    {
      public:
        PackageFixture()
            : Root(std::filesystem::temp_directory_path() /
                   ("Keire-AssetPackageAuthoring-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(Root / "Assets/Materials");
            std::filesystem::create_directories(Root / "Assets/Textures");
        }

        ~PackageFixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] Keire::AssetSourceRecord Add(const std::filesystem::path& relative,
                                                   const std::string_view contents)
        {
            const auto source = Root / "Assets" / relative;
            std::filesystem::create_directories(source.parent_path());
            {
                std::ofstream stream(source, std::ios::binary);
                stream << contents;
            }
            const auto metadata = Keire::Detail::PathWithSuffix(source, ".keiremeta");
            {
                std::ofstream stream(metadata, std::ios::binary);
                stream << "{}";
            }
            return {.Id = Keire::AssetId::Generate(),
                    .Type = Keire::AssetTypeId::Parse("ed170000-0000-4000-8000-000000000042"),
                    .RelativePath = relative,
                    .MetadataPath = metadata,
                    .Importer = "test"};
        }

        [[nodiscard]] KeireEditor::AssetPackageDraft Draft() const
        {
            return {.PackageId = "com.keire.tests.authored-assets",
                    .Version = "1.2.3",
                    .PublisherId = "tests",
                    .DisplayName = "Authored Assets",
                    .Summary = "A deterministic Editor-authored asset package.",
                    .MinimumEngineVersion = "0.3.1"};
        }

        std::filesystem::path Root;
    };
} // namespace

TEST_CASE("asset-package authoring includes selected assets and their referenced dependencies")
{
    PackageFixture fixture;
    auto material = fixture.Add("Materials/Hero.keirematerial", "material");
    const auto texture = fixture.Add("Textures/Hero.png", "texture");
    material.Dependencies.push_back(texture.Id);
    const std::vector records{material, texture};
    const auto output = fixture.Root / "Exports/Hero.keireassetpackage";

    const auto package =
        KeireEditor::CreateAssetPackageArchive({.ProjectRoot = fixture.Root,
                                                .SourceDirectory = "Assets",
                                                .StagingParent = fixture.Root / "Library/AssetPackageExports",
                                                .Output = output,
                                                .Selection = {.Assets = {material.Id}},
                                                .Draft = fixture.Draft(),
                                                .Records = records});

    REQUIRE(package.Manifest.Assets.size() == 2);
    CHECK(package.Manifest.Files.size() == 4);
    CHECK(package.Manifest.InstallKind == Keire::AssetPackageInstallKind::AssetImport);
    CHECK(package.Manifest.PackageId == "com.keire.tests.authored-assets");
    CHECK(std::filesystem::is_regular_file(output));
    CHECK(std::ranges::any_of(package.Manifest.Assets, [&](const auto& asset) { return asset.Id == material.Id; }));
    CHECK(std::ranges::any_of(package.Manifest.Assets, [&](const auto& asset) { return asset.Id == texture.Id; }));
    CHECK(Keire::InspectAssetPackageArchive(output).ArchiveSha256 == package.ArchiveSha256);

    const auto staging = fixture.Root / "Library/AssetPackageExports";
    CHECK(std::filesystem::is_directory(staging));
    CHECK(std::filesystem::directory_iterator(staging) == std::filesystem::directory_iterator{});
}

TEST_CASE("asset-package folder selection includes nested assets and outside dependencies")
{
    PackageFixture fixture;
    auto material = fixture.Add("Materials/Hero.keirematerial", "material");
    const auto nested = fixture.Add("Materials/Shared/Base.keirematerial", "base");
    const auto texture = fixture.Add("Textures/Hero.png", "texture");
    material.Dependencies.push_back(texture.Id);
    const std::vector records{material, nested, texture};

    const auto selected =
        KeireEditor::ResolveAssetPackageRecords(records, {.Folder = std::filesystem::path("Materials")});

    REQUIRE(selected.size() == 3);
    CHECK(std::ranges::any_of(selected, [&](const auto& record) { return record.Id == nested.Id; }));
    CHECK(std::ranges::any_of(selected, [&](const auto& record) { return record.Id == texture.Id; }));
}

TEST_CASE("asset-package dependencies resolve generated subassets to their source owner")
{
    PackageFixture fixture;
    auto material = fixture.Add("Materials/Hero.keirematerial", "material");
    auto shaderGraph = fixture.Add("Shaders/Lit.keireshadergraph", "shader graph");
    const auto compiledShader = Keire::AssetId::Generate();
    shaderGraph.SubAssets.push_back(compiledShader);
    material.Dependencies = {compiledShader, shaderGraph.Id};
    const std::vector records{material, shaderGraph};

    const auto selected = KeireEditor::ResolveAssetPackageRecords(records, {.Assets = {material.Id}});

    REQUIRE(selected.size() == 2);
    const auto resolvedMaterial =
        std::ranges::find_if(selected, [&](const auto& record) { return record.Id == material.Id; });
    REQUIRE(resolvedMaterial != selected.end());
    CHECK(resolvedMaterial->Dependencies == std::vector{shaderGraph.Id});
}

TEST_CASE("asset-package child-only dependencies include their source owner in the archive manifest")
{
    PackageFixture fixture;
    auto material = fixture.Add("Materials/Hero.keirematerial", "material");
    auto shaderGraph = fixture.Add("Shaders/Lit.keireshadergraph", "shader graph");
    const auto compiledShader = Keire::AssetId::Generate();
    shaderGraph.SubAssets.push_back(compiledShader);
    material.Dependencies = {compiledShader};
    const auto output = fixture.Root / "Exports/GeneratedDependency.keireassetpackage";

    const auto package =
        KeireEditor::CreateAssetPackageArchive({.ProjectRoot = fixture.Root,
                                                .SourceDirectory = "Assets",
                                                .StagingParent = fixture.Root / "Library/AssetPackageExports",
                                                .Output = output,
                                                .Selection = {.Assets = {material.Id}},
                                                .Draft = fixture.Draft(),
                                                .Records = {material, shaderGraph}});

    REQUIRE(package.Manifest.Assets.size() == 2);
    const auto packagedMaterial =
        std::ranges::find_if(package.Manifest.Assets, [&](const auto& asset) { return asset.Id == material.Id; });
    REQUIRE(packagedMaterial != package.Manifest.Assets.end());
    CHECK(packagedMaterial->Dependencies == std::vector{shaderGraph.Id});
    CHECK(std::ranges::none_of(package.Manifest.Assets, [&](const auto& asset) { return asset.Id == compiledShader; }));
    CHECK(Keire::InspectAssetPackageArchive(output).Manifest == package.Manifest);
}

TEST_CASE("asset-package source-owned subasset dependencies do not become self-dependencies")
{
    PackageFixture fixture;
    auto shaderGraph = fixture.Add("Shaders/Lit.keireshadergraph", "shader graph");
    const auto compiledShader = Keire::AssetId::Generate();
    shaderGraph.SubAssets.push_back(compiledShader);
    shaderGraph.Dependencies.push_back(compiledShader);

    const auto selected =
        KeireEditor::ResolveAssetPackageRecords(std::span(&shaderGraph, 1), {.Assets = {shaderGraph.Id}});

    REQUIRE(selected.size() == 1);
    CHECK(selected.front().Dependencies.empty());
}

TEST_CASE("asset-package source inventory rejects empty and colliding subasset identities")
{
    PackageFixture fixture;
    auto first = fixture.Add("Materials/First.keirematerial", "first");
    auto second = fixture.Add("Materials/Second.keirematerial", "second");

    SUBCASE("empty")
    {
        first.SubAssets.push_back(Keire::AssetId{});
        CHECK_THROWS_WITH_AS(
            static_cast<void>(KeireEditor::ResolveAssetPackageRecords(std::span(&first, 1), {.Assets = {first.Id}})),
            "The asset source inventory contains an empty subasset identity.", std::invalid_argument);
    }

    SUBCASE("colliding")
    {
        first.SubAssets.push_back(second.Id);
        const std::vector records{first, second};
        CHECK_THROWS_WITH_AS(
            static_cast<void>(KeireEditor::ResolveAssetPackageRecords(records, {.Assets = {first.Id}})),
            "The asset source inventory contains a colliding subasset identity: " + second.Id.ToString(),
            std::invalid_argument);
    }
}

TEST_CASE("asset-package authoring rejects metadata outside the project Assets root")
{
    PackageFixture fixture;
    auto asset = fixture.Add("Materials/Hero.keirematerial", "material");
    asset.MetadataPath = fixture.Root / "outside.keiremeta";
    {
        std::ofstream stream(asset.MetadataPath);
        stream << "{}";
    }
    const auto output = fixture.Root / "Exports/Unsafe.keireassetpackage";

    CHECK_THROWS_AS(static_cast<void>(KeireEditor::CreateAssetPackageArchive({.ProjectRoot = fixture.Root,
                                                                              .Output = output,
                                                                              .Selection = {.Assets = {asset.Id}},
                                                                              .Draft = fixture.Draft(),
                                                                              .Records = {asset}})),
                    std::invalid_argument);
    CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("asset-package selection rejects a dependency missing from the project database")
{
    PackageFixture fixture;
    auto asset = fixture.Add("Materials/Hero.keirematerial", "material");
    const auto missing = Keire::AssetId::Generate();
    asset.Dependencies.push_back(missing);

    CHECK_THROWS_WITH_AS(
        static_cast<void>(KeireEditor::ResolveAssetPackageRecords(std::span(&asset, 1), {.Assets = {asset.Id}})),
        "Asset-package source 'Materials/Hero.keirematerial' (" + asset.Id.ToString() + ") depends on missing asset " +
            missing.ToString() + ".",
        std::invalid_argument);
}

TEST_CASE("asset-package identifiers are portable and stable")
{
    CHECK(KeireEditor::SuggestedAssetPackageIdentifier("Stylized Forest Materials") ==
          "com.keire.assets.stylized-forest-materials");
    CHECK(KeireEditor::SuggestedAssetPackageIdentifier("   ") == "com.keire.assets.package");
}

TEST_CASE("asset-package authoring records engine bounds and managed assembly scope")
{
    PackageFixture fixture;
    const auto script = fixture.Add("Scripts/Player.cs", "class Player {} ");
    const auto assembly = fixture.Add(
        "Scripts/Player.keireasm",
        R"({"schemaVersion":1,"name":"Player","rootNamespace":"Player","classification":"runtime","sourceRoots":["Assets/Scripts"],"references":[]})");
    auto draft = fixture.Draft();
    draft.MaximumEngineVersion = "0.4.4";
    const auto package = KeireEditor::CreateAssetPackageArchive({.ProjectRoot = fixture.Root,
                                                                 .Output = fixture.Root / "Player.keireassetpackage",
                                                                 .Selection = {.Folder = "Scripts"},
                                                                 .Draft = draft,
                                                                 .Records = {script, assembly}});
    CHECK(package.Manifest.Compatibility.MaximumEngineVersion == "0.4.4");
    REQUIRE(package.Manifest.ManagedAssemblies.size() == 1);
    CHECK(package.Manifest.ManagedAssemblies.front().Scope == Keire::AssetPackageManagedAssemblyScope::Runtime);
}
