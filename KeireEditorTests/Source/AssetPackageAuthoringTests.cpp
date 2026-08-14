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
    asset.Dependencies.push_back(Keire::AssetId::Generate());

    CHECK_THROWS_AS(
        static_cast<void>(KeireEditor::ResolveAssetPackageRecords(std::span(&asset, 1), {.Assets = {asset.Id}})),
        std::invalid_argument);
}

TEST_CASE("asset-package identifiers are portable and stable")
{
    CHECK(KeireEditor::SuggestedAssetPackageIdentifier("Stylized Forest Materials") ==
          "com.keire.assets.stylized-forest-materials");
    CHECK(KeireEditor::SuggestedAssetPackageIdentifier("   ") == "com.keire.assets.package");
}
