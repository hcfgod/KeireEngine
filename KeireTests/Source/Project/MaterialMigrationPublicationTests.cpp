#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Project/ShaderGraphMigration.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectFileTransaction.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    std::vector<std::byte> Bytes(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        return {bytes.begin(), bytes.end()};
    }

    struct Fixture
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() /
                                     ("Keire-Material-Publication-" + Keire::AssetId::Generate().ToString());
        Keire::AssetId Material = Keire::AssetId::Generate();
        Keire::AssetId Program = Keire::AssetId::Generate();
        Fixture()
        {
            std::filesystem::create_directories(Root / "Assets");
            const Keire::Detail::AnchoredFileSystem fs(Root);
            fs.WriteFileAtomically("Assets/Paint.keirematerial",
                                   Keire::MaterialGraphAsset::EncodeSource(Keire::CreateOpenPbrMaterial()));
            const nlohmann::json metadata{{"schemaVersion", 1},
                                          {"id", Material.ToString()},
                                          {"type", Keire::MaterialGraphAsset::StaticType().ToString()},
                                          {"importer", "Keire.MaterialGraph"},
                                          {"importerVersion", Keire::CreateMaterialGraphAssetImporter().Version},
                                          {"subAssets", nlohmann::json::array()},
                                          {"dependencies", nlohmann::json::array()}};
            fs.WriteFileAtomically("Assets/Paint.keirematerial.keiremeta", Bytes(metadata.dump()));
            fs.WriteFileAtomically("Assets/Program.fixtureshader", Bytes("fixture shader"));
            auto programMetadata = metadata;
            programMetadata["id"] = Program.ToString();
            programMetadata["type"] = Keire::ShaderAsset::StaticType().ToString();
            programMetadata["importer"] = "Test.MigrationShader";
            programMetadata["importerVersion"] = 1;
            fs.WriteFileAtomically("Assets/Program.fixtureshader.keiremeta", Bytes(programMetadata.dump()));
        }
        ~Fixture()
        {
            Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        // Deterministic importer fixtures exercise publication without invoking a platform shader toolchain.
        Keire::AssetDatabaseSpecification Specification() const
        {
            auto material = Keire::CreateMaterialGraphAssetImporter();
            material.ContextualImport =
                [program = Program](const Keire::AssetImportContext& context, const std::span<const std::byte> source)
            {
                Keire::AssetImportOutput result;
                result.Bytes.assign(source.begin(), source.end());
                Keire::MaterialAssetDefinition runtime;
                runtime.Shader = program;
                result.SubAssets.push_back({context.ResolveSubAssetId("material/default"),
                                            Keire::MaterialAsset::StaticType(),
                                            "material/default",
                                            "Runtime Material",
                                            Keire::MaterialAsset::Encode(runtime),
                                            {program},
                                            {}});
                return result;
            };
            auto shader = Keire::CreateShaderGraphAssetImporter();
            shader.ContextualImport = [](const Keire::AssetImportContext&, const std::span<const std::byte> source)
            {
                Keire::AssetImportOutput result;
                result.Bytes.assign(source.begin(), source.end());
                return result;
            };
            Keire::AssetImporterRegistration program;
            program.Name = "Test.MigrationShader";
            program.Type = Keire::ShaderAsset::StaticType();
            program.Extensions = {".fixtureshader"};
            program.Import = [](std::span<const std::byte>)
            {
                Keire::ShaderAssetDefinition definition;
                definition.Source = "Assets/Program.fixtureshader";
                constexpr std::array formats{Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV,
                                             Keire::ShaderBinaryFormat::Msl};
                for (const auto format : formats)
                    definition.Variants.push_back({format, {std::byte{1}}, {std::byte{2}}});
                return Keire::ShaderAsset::Encode(definition);
            };
            return {.ProjectRoot = Root, .Importers = {std::move(material), std::move(shader), std::move(program)}};
        }
    };
} // namespace

TEST_CASE("validated material migration publishes source index and verified runtime with stable material identities")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    auto specification = fixture.Specification();
    std::filesystem::path preparationRoot;
    const auto importMaterial = specification.Importers.front().ContextualImport;
    specification.Importers.front().ContextualImport =
        [&](const Keire::AssetImportContext& context, const std::span<const std::byte> source)
    {
        if (context.ProjectRoot != fixture.Root)
        {
            preparationRoot = context.ProjectRoot;
            CHECK(preparationRoot.parent_path() ==
                  Keire::Detail::CanonicalExistingPath(std::filesystem::temp_directory_path()));
            CHECK(std::filesystem::is_directory(preparationRoot));
        }
        return importMaterial(context, source);
    };
    Keire::AssetId runtime;
    {
        const auto database = Keire::CreateRef<Keire::AssetDatabase>(specification);
        const auto imported = database->ImportAll();
        const auto catalog = Keire::Detail::LoadCatalog(imported.CatalogPath);
        const auto material =
            std::ranges::find(catalog.Entries, Keire::MaterialAsset::StaticType(), &Keire::Detail::CatalogEntry::Type);
        REQUIRE(material != catalog.Entries.end());
        runtime = material->Id;
    }
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    REQUIRE(reviewed.CanApply());
    const auto applied = Keire::ApplyShaderGraphMigration(fixture.Root, reviewed, specification);
    REQUIRE(applied.PendingCount() == 1);
    REQUIRE_FALSE(preparationRoot.empty());
    CHECK_FALSE(std::filesystem::exists(preparationRoot));
    CHECK_NOTHROW(Keire::AssetCooker::Validate(fixture.Root / "Library/AssetCache/Runtime/catalog.json"));
    const auto catalog = Keire::Detail::LoadCatalog(fixture.Root / "Library/AssetCache/Runtime/catalog.json");
    CHECK(std::ranges::find(catalog.Entries, runtime, &Keire::Detail::CatalogEntry::Id) != catalog.Entries.end());
    const auto records =
        Keire::Detail::ReadAssetSourceIndex(fixture.Root / "Library/AssetCache/Runtime/source-index.json");
    const auto source = std::ranges::find(records, fixture.Material, &Keire::AssetSourceRecord::Id);
    REQUIRE(source != records.end());
    CHECK(source->MetadataPath == fixture.Root / "Assets/Paint.keirematerial.keiremeta");
    CHECK(std::ranges::find(records, reviewed.Items.front().GeneratedShaderAsset, &Keire::AssetSourceRecord::Id) !=
          records.end());
    CHECK(Keire::MaterialAsset::DecodeAuthoringSource(fs.Read("Assets/Paint.keirematerial", 1000000)).Shader.Asset ==
          reviewed.Items.front().GeneratedShaderAsset);
    CHECK_FALSE(Keire::Detail::HasPendingMaterialMigration(fixture.Root));
    const auto repeated = Keire::InspectShaderGraphMigration(fixture.Root);
    CHECK(repeated.PendingCount() == 0);
    CHECK(Keire::ApplyShaderGraphMigration(fixture.Root, repeated, specification).PendingCount() == 0);
}

TEST_CASE("validated material migration preserves all authoring bytes when dependency validation fails")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    const auto before = fs.Read("Assets/Paint.keirematerial", 1000000);
    const auto metadata = fs.Read("Assets/Paint.keirematerial.keiremeta", 1000000);
    auto specification = fixture.Specification();
    std::filesystem::path preparationRoot;
    specification.Importers.at(1).ContextualImport = [&](const Keire::AssetImportContext& context,
                                                         std::span<const std::byte>) -> Keire::AssetImportOutput
    {
        preparationRoot = context.ProjectRoot;
        throw std::runtime_error("Fixture shader dependency failed validation.");
    };
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    CHECK_THROWS_AS((void)Keire::ApplyShaderGraphMigration(fixture.Root, reviewed, specification), std::runtime_error);
    REQUIRE_FALSE(preparationRoot.empty());
    CHECK(preparationRoot != fixture.Root);
    CHECK_FALSE(std::filesystem::exists(preparationRoot));
    CHECK(fs.Read("Assets/Paint.keirematerial", 1000000) == before);
    CHECK(fs.Read("Assets/Paint.keirematerial.keiremeta", 1000000) == metadata);
    CHECK_FALSE(fs.Exists("Assets/Paint_Shader.keireshadergraph"));
    CHECK_FALSE(fs.Exists("Library/AssetCache/Runtime/catalog.json"));
    CHECK_FALSE(Keire::Detail::HasPendingMaterialMigration(fixture.Root));
}

TEST_CASE("validated material migration cancellation leaves source and catalog untouched")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    const auto before = fs.Read("Assets/Paint.keirematerial", 1000000);
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    std::stop_source cancellation;
    cancellation.request_stop();
    CHECK_THROWS_AS((void)Keire::ApplyShaderGraphMigration(fixture.Root, reviewed, fixture.Specification(),
                                                           cancellation.get_token()),
                    std::runtime_error);
    CHECK(fs.Read("Assets/Paint.keirematerial", 1000000) == before);
    CHECK_FALSE(fs.Exists("Assets/Paint_Shader.keireshadergraph"));
    CHECK_FALSE(fs.Exists("Library/AssetCache/Runtime/catalog.json"));
}

TEST_CASE("validated material migration rejects missing shaders through the production material importer")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    Keire::MaterialShaderReference shader;
    shader.Asset = Keire::AssetId::Generate();
    const auto legacy = Keire::MaterialGraphAsset::EncodeSource(Keire::CreateMaterialGraph(shader, {}));
    fs.WriteFileAtomically("Assets/Paint.keirematerial", legacy);
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    REQUIRE(reviewed.CanApply());
    REQUIRE_FALSE(reviewed.Items.front().CreatesShader);
    const Keire::AssetDatabaseSpecification specification{.ProjectRoot = fixture.Root,
                                                          .Importers = {Keire::CreateMaterialGraphAssetImporter()}};
    CHECK_THROWS_AS((void)Keire::ApplyShaderGraphMigration(fixture.Root, reviewed, specification), std::exception);
    CHECK(fs.Read("Assets/Paint.keirematerial", 1000000) == legacy);
    CHECK_FALSE(fs.Exists("Library/AssetCache/Runtime/catalog.json"));
}

TEST_CASE("material publication rolls back source index and catalog as one recovery transaction")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    fs.CreateDirectories("Library/AssetCache/Runtime");
    fs.WriteFileAtomically("Library/AssetCache/Runtime/source-index.json", Bytes("old index"));
    fs.WriteFileAtomically("Library/AssetCache/Runtime/catalog.json", Bytes("old catalog"));
    const auto source = fs.Read("Assets/Paint.keirematerial", 1000000);
    const std::vector<Keire::Detail::ProjectFileReplacement> files{
        {"Assets/Paint.keirematerial", source, Bytes("new material")},
        {"Library/AssetCache/Runtime/source-index.json", Bytes("old index"), Bytes("new index")},
        {"Library/AssetCache/Runtime/catalog.json", Bytes("old catalog"), Bytes("new catalog")}};
    bool failed = false;
    bool interruptRollback = false;
    SUBCASE("ordinary failure restores everything immediately") {}
    SUBCASE("interrupted rollback is completed on recovery") { interruptRollback = true; }
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting(
        [&failed, interruptRollback](const std::string_view operation, const std::filesystem::path& path)
        {
            if (failed && interruptRollback && operation == "write-before-publish" &&
                path == "Library/AssetCache/Runtime/source-index.json")
                throw std::runtime_error("Interrupted source index rollback.");
            if (!failed && operation == "write-before-publish" && path == "Library/AssetCache/Runtime/catalog.json")
            {
                failed = true;
                throw std::runtime_error("Interrupted final catalog publication.");
            }
        });
    CHECK_THROWS_AS(Keire::Detail::PublishMaterialMigrationFiles(fixture.Root, files), std::runtime_error);
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
    CHECK(failed);
    if (interruptRollback)
    {
        CHECK(Keire::Detail::HasPendingMaterialMigration(fixture.Root));
        CHECK(Keire::RecoverShaderGraphMigration(fixture.Root) == 1);
    }
    CHECK(fs.Read("Assets/Paint.keirematerial", 1000000) == source);
    CHECK(fs.Read("Library/AssetCache/Runtime/source-index.json", 1000) == Bytes("old index"));
    CHECK(fs.Read("Library/AssetCache/Runtime/catalog.json", 1000) == Bytes("old catalog"));
    CHECK(Keire::RecoverShaderGraphMigration(fixture.Root) == 0);
}

TEST_CASE("validated material migration rejects a dependency edited during preparation")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    fs.CreateDirectories("Packages");
    fs.WriteFileAtomically("Packages/pinned.inc", Bytes("reviewed include"));
    const auto before = fs.Read("Assets/Paint.keirematerial", 1000000);
    auto specification = fixture.Specification();
    const auto import = specification.Importers.front().ContextualImport;
    specification.Importers.front().ContextualImport =
        [&fs, import](const Keire::AssetImportContext& context, const std::span<const std::byte> source)
    {
        fs.WriteFileAtomically("Packages/pinned.inc", Bytes("external include edit"));
        return import(context, source);
    };
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    CHECK_THROWS_AS((void)Keire::ApplyShaderGraphMigration(fixture.Root, reviewed, specification), std::runtime_error);
    CHECK(fs.Read("Assets/Paint.keirematerial", 1000000) == before);
    CHECK(fs.Read("Packages/pinned.inc", 1000) == Bytes("external include edit"));
    CHECK_FALSE(fs.Exists("Assets/Paint_Shader.keireshadergraph"));
    CHECK_FALSE(fs.Exists("Library/AssetCache/Runtime/catalog.json"));
}

TEST_CASE("material migration review expires when a shader include changes before apply")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    fs.CreateDirectories("Packages");
    fs.WriteFileAtomically("Packages/pinned.inc", Bytes("reviewed include"));
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    fs.WriteFileAtomically("Packages/pinned.inc", Bytes("changed include"));
    CHECK(Keire::InspectShaderGraphMigration(fixture.Root).ReviewFingerprint != reviewed.ReviewFingerprint);
    CHECK_THROWS_AS((void)Keire::ApplyShaderGraphMigration(fixture.Root, reviewed, fixture.Specification()),
                    std::runtime_error);
    CHECK_FALSE(fs.Exists("Assets/Paint_Shader.keireshadergraph"));
}

TEST_CASE("material publication rejects library destinations outside the runtime catalog subtree")
{
    Fixture fixture;
    for (const auto* path : {"Library/AssetCache/Private.bin", "Library/AssetCache/Runtime", "Library/Other/file",
                             "Library/AssetCache/Runtime/../outside"})
    {
        const std::vector<Keire::Detail::ProjectFileReplacement> files{{path, std::nullopt, Bytes("invalid")}};
        CHECK_THROWS_AS(Keire::Detail::PublishMaterialMigrationFiles(fixture.Root, files), std::invalid_argument);
    }
}
