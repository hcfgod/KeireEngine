#include "Keire/Assets/Asset.h"
#include "Keire/Project/Project.h"
#include "Keire/Project/ShaderGraphMigration.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectFileTransaction.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct Fixture
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() /
                                     ("Keire-Material-Recovery-" + Keire::AssetId::Generate().ToString());
        Fixture() { std::filesystem::create_directories(Root / "Assets"); }
        ~Fixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    };

    std::vector<std::byte> Bytes(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        return {bytes.begin(), bytes.end()};
    }

    std::string Digest(const std::string_view text)
    {
        return Keire::Detail::DigestToString(Keire::Detail::Sha256(Bytes(text)));
    }

    void Interrupted(const Keire::Detail::AnchoredFileSystem& fs)
    {
        const std::filesystem::path journal = "Library/MaterialShaderUpgrade/interrupted";
        fs.CreateDirectories(journal);
        fs.WriteFileAtomically(journal / "0.backup", Bytes("old material"));
        fs.WriteFileAtomically(journal / "0.staged", Bytes("new material"));
        fs.WriteFileAtomically(journal / "1.staged", Bytes("new shader"));
        fs.WriteFileAtomically("Assets/Material.keirematerial", Bytes("new material"));
        fs.WriteFileAtomically("Assets/Shader.keireshadergraph", Bytes("new shader"));
        const nlohmann::json document{{"schemaVersion", 1},
                                      {"state", "prepared"},
                                      {"files",
                                       {{{"destination", "Assets/Material.keirematerial"},
                                         {"original", Digest("old material")},
                                         {"replacement", Digest("new material")}},
                                        {{"destination", "Assets/Shader.keireshadergraph"},
                                         {"original", nullptr},
                                         {"replacement", Digest("new shader")}}}}};
        fs.WriteFileAtomically(journal / "journal.json", Bytes(document.dump()));
    }

    void WriteMaterial(const Keire::Detail::AnchoredFileSystem& fs, const Keire::MaterialGraphDefinition& material,
                       const Keire::AssetId id)
    {
        fs.WriteFileAtomically("Assets/Paint.keirematerial", Keire::MaterialGraphAsset::EncodeSource(material));
        const nlohmann::json metadata{{"schemaVersion", 1},
                                      {"id", id.ToString()},
                                      {"subAssets", nlohmann::json::array()},
                                      {"dependencies", nlohmann::json::array()}};
        fs.WriteFileAtomically("Assets/Paint.keirematerial.keiremeta", Bytes(metadata.dump()));
    }
} // namespace

TEST_CASE("material migration extracts standalone OpenPBR logic without changing its graph")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    const auto original = Keire::CreateOpenPbrMaterial();
    const auto id = Keire::AssetId::Generate();
    WriteMaterial(fs, original, id);
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    REQUIRE(reviewed.CanApply());
    REQUIRE(reviewed.PendingCount() == 1);
    CHECK(reviewed.Items.front().CreatesShader);
    CHECK(Keire::ApplyShaderGraphMigration(fixture.Root, reviewed).PendingCount() == 1);
    auto extracted = Keire::ShaderGraphAsset::DecodeSource(fs.Read("Assets/Paint_Shader.keireshadergraph", 1000000));
    CHECK(extracted.GeneratedAssetOwner == id);
    extracted.GeneratedAssetOwner = original.SurfaceGraph.GeneratedAssetOwner;
    CHECK(extracted == original.SurfaceGraph);
    const auto material = Keire::MaterialAsset::DecodeAuthoringSource(fs.Read("Assets/Paint.keirematerial", 1000000));
    CHECK(material.Shader.Asset == reviewed.Items.front().GeneratedShaderAsset);
    CHECK(material.Surface == original.Surface);
    CHECK(Keire::ApplyShaderGraphMigration(fixture.Root).PendingCount() == 0);
}

TEST_CASE("material migration rejects edits after the reviewed report")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    auto original = Keire::CreateOpenPbrMaterial();
    const auto id = Keire::AssetId::Generate();
    WriteMaterial(fs, original, id);
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    REQUIRE(reviewed.CanApply());
    original.EmissiveGIIntensity = 2.0F;
    WriteMaterial(fs, original, id);
    const auto changed = fs.Read("Assets/Paint.keirematerial", 1000000);
    CHECK_THROWS_AS((void)Keire::ApplyShaderGraphMigration(fixture.Root, reviewed), std::runtime_error);
    CHECK(fs.Read("Assets/Paint.keirematerial", 1000000) == changed);
    CHECK_FALSE(fs.Exists("Assets/Paint_Shader.keireshadergraph"));
}

TEST_CASE("material migration converts property bindings without creating another shader")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    Keire::MaterialShaderReference shader;
    shader.Asset = Keire::AssetId::Generate();
    Keire::ShaderInterfaceDefinition reflection;
    Keire::ShaderPropertyDefinition property;
    property.Id = Keire::AssetId::Generate();
    property.Name = "Roughness";
    property.DefaultValue.X = 0.4F;
    reflection.Properties.push_back(property);
    auto graph = Keire::CreateMaterialGraph(shader, reflection);
    graph.Properties.front().Value = 0.75F;
    const auto sourceId = Keire::AssetId::Generate();
    WriteMaterial(fs, graph, sourceId);
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    REQUIRE(reviewed.CanApply());
    REQUIRE(reviewed.Items.size() == 1);
    CHECK_FALSE(reviewed.Items.front().CreatesShader);
    CHECK(reviewed.Items.front().ShaderGraph.empty());
    CHECK(Keire::ApplyShaderGraphMigration(fixture.Root, reviewed).PendingCount() == 1);
    const auto material = Keire::MaterialAsset::DecodeAuthoringSource(fs.Read("Assets/Paint.keirematerial", 1000000));
    CHECK(material.Shader == shader);
    REQUIRE(material.PropertyOverrides.size() == 1);
    CHECK(material.PropertyOverrides.front().Property == property.Id);
    CHECK(std::get<float>(material.PropertyOverrides.front().Value) == 0.75F);
    CHECK_FALSE(fs.Exists("Assets/Paint_Shader.keireshadergraph"));
}

TEST_CASE("material migration reports generated shader identity collisions")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    WriteMaterial(fs, Keire::CreateOpenPbrMaterial(), Keire::AssetId::Generate());
    const auto reviewed = Keire::InspectShaderGraphMigration(fixture.Root);
    REQUIRE(reviewed.CanApply());
    REQUIRE(reviewed.Items.size() == 1);
    const nlohmann::json metadata{{"id", reviewed.Items.front().GeneratedShaderAsset.ToString()}};
    fs.WriteFileAtomically("Assets/Other.keiretexture.keiremeta", Bytes(metadata.dump()));
    const auto collision = Keire::InspectShaderGraphMigration(fixture.Root);
    CHECK_FALSE(collision.CanApply());
    CHECK(collision.Items.front().Disposition == Keire::ShaderGraphMigrationDisposition::Conflict);
    CHECK_FALSE(fs.Exists("Assets/Paint_Shader.keireshadergraph"));
}

TEST_CASE("material migration journal publishes all files and retains verified backups")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    fs.WriteFileAtomically("Assets/Material.keirematerial", Bytes("old material"));
    const std::vector<Keire::Detail::ProjectFileReplacement> files{
        {"Assets/Material.keirematerial", Bytes("old material"), Bytes("new material")},
        {"Assets/Shader.keireshadergraph", std::nullopt, Bytes("new shader")}};
    Keire::Detail::PublishMaterialMigrationFiles(fixture.Root, files);
    CHECK(fs.Read("Assets/Material.keirematerial", 1024) == Bytes("new material"));
    CHECK(fs.Read("Assets/Shader.keireshadergraph", 1024) == Bytes("new shader"));
    CHECK_FALSE(Keire::Detail::HasPendingMaterialMigration(fixture.Root));
    CHECK(Keire::RecoverShaderGraphMigration(fixture.Root) == 0);
    std::size_t backups = 0;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(fixture.Root / "Library/MaterialShaderUpgrade"))
        if (entry.path().extension() == ".backup")
            ++backups;
    CHECK(backups == 1);
}

TEST_CASE("project open recovers interrupted material publication before granting asset access")
{
    Fixture fixture;
    const auto created = Keire::Project::Create({fixture.Root, "Recovery", Keire::ProjectTemplate::Empty});
    const auto root = fixture.Root / "Recovery";
    const Keire::Detail::AnchoredFileSystem fs(root);
    Interrupted(fs);
    CHECK(Keire::Project::Inspect(root) == Keire::ProjectStatus::RecoveryRequired);
    CHECK_THROWS_AS((void)Keire::Project::Open(root, Keire::ProjectOpenMode::ReadOnly), std::runtime_error);
    CHECK(fs.Read("Assets/Material.keirematerial", 1024) == Bytes("new material"));
    {
        const auto opened = Keire::Project::Open(root, Keire::ProjectOpenMode::Exclusive);
        CHECK(fs.Read("Assets/Material.keirematerial", 1024) == Bytes("old material"));
        CHECK_FALSE(fs.Exists("Assets/Shader.keireshadergraph"));
        CHECK(Keire::Project::Inspect(root) == Keire::ProjectStatus::Ready);
    }
    CHECK_NOTHROW((void)Keire::Project::Open(root, Keire::ProjectOpenMode::Exclusive));
}

TEST_CASE("material migration journal rejects stale review snapshots before changing any source")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    fs.WriteFileAtomically("Assets/Material.keirematerial", Bytes("external edit"));
    const std::vector<Keire::Detail::ProjectFileReplacement> files{
        {"Assets/Shader.keireshadergraph", std::nullopt, Bytes("new shader")},
        {"Assets/Material.keirematerial", Bytes("old material"), Bytes("new material")}};
    CHECK_THROWS_AS(Keire::Detail::PublishMaterialMigrationFiles(fixture.Root, files), std::runtime_error);
    CHECK_FALSE(fs.Exists("Assets/Shader.keireshadergraph"));
    CHECK(fs.Read("Assets/Material.keirematerial", 1024) == Bytes("external edit"));
    CHECK_FALSE(Keire::Detail::HasPendingMaterialMigration(fixture.Root));
}

TEST_CASE("interrupted material migration rolls back idempotently before another publication")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    Interrupted(fs);
    CHECK(Keire::Detail::HasPendingMaterialMigration(fixture.Root));
    CHECK(Keire::RecoverShaderGraphMigration(fixture.Root) == 1);
    CHECK(fs.Read("Assets/Material.keirematerial", 1024) == Bytes("old material"));
    CHECK_FALSE(fs.Exists("Assets/Shader.keireshadergraph"));
    CHECK_FALSE(Keire::Detail::HasPendingMaterialMigration(fixture.Root));
    CHECK(Keire::RecoverShaderGraphMigration(fixture.Root) == 0);
}

TEST_CASE("material migration recovery preserves external edits and corrupt backups for diagnosis")
{
    Fixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    Interrupted(fs);
    SUBCASE("external source edit")
    {
        fs.WriteFileAtomically("Assets/Material.keirematerial", Bytes("external edit"));
        CHECK_THROWS_AS((void)Keire::RecoverShaderGraphMigration(fixture.Root), std::runtime_error);
        CHECK(fs.Read("Assets/Material.keirematerial", 1024) == Bytes("external edit"));
    }
    SUBCASE("corrupt backup")
    {
        fs.WriteFileAtomically("Library/MaterialShaderUpgrade/interrupted/0.backup", Bytes("corrupt"));
        CHECK_THROWS_AS((void)Keire::RecoverShaderGraphMigration(fixture.Root), std::runtime_error);
        CHECK(fs.Read("Assets/Material.keirematerial", 1024) == Bytes("new material"));
    }
    CHECK(Keire::Detail::HasPendingMaterialMigration(fixture.Root));
    CHECK(fs.Read("Assets/Shader.keireshadergraph", 1024) == Bytes("new shader"));
}
