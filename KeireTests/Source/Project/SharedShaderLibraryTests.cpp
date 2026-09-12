#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Project/SharedShaderLibrary.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <vector>

namespace
{
    struct SharedShaderFixture
    {
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() / ("Keire-Shared-Shaders-" + Keire::AssetId::Generate().ToString());
        SharedShaderFixture() { std::filesystem::create_directories(Root / "Assets"); }
        ~SharedShaderFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };
} // namespace

TEST_CASE("Shared shader library pins deterministic sources and identities across projects")
{
    SharedShaderFixture first;
    SharedShaderFixture second;
    CHECK(Keire::ReadSharedShaderLibrary(first.Root).Shaders.empty());
    const auto a = Keire::EnsureSharedShaderLibrary(first.Root);
    const auto b = Keire::EnsureSharedShaderLibrary(second.Root);
    REQUIRE(a.Version == "1.0.0");
    REQUIRE(a.Shaders.size() == 6);
    REQUIRE(b.Shaders.size() == a.Shaders.size());
    constexpr std::array versionOneHashes{"a3ab5fe6adad4a3915d82a973566fd98cab7617878baf30f1d9a276496975e02",
                                          "99a06e012c33f9042faa43c986e00f8001e13ed485b1a3da0282791611a5c31f",
                                          "d8dbd978155ca724e234ab77d661f13b73d2d0756f8bd4c17011e64654938d6e",
                                          "1ef46059c7f695dc1e7735749fe924668c00032842fe16a58918f34b542715a1",
                                          "0f450adb2241f2f6b31427566303125056c3f3921ec959968eec07207a960694",
                                          "8f6e1a200219d267088fd39814ff82ff194ec2a9e30737a7cff21c70cf91f718"};
    std::set<Keire::AssetId> identities;
    const Keire::Detail::AnchoredFileSystem fs(first.Root);
    const Keire::Detail::AnchoredFileSystem other(second.Root);
    for (std::size_t index = 0; index < a.Shaders.size(); ++index)
    {
        const auto& shader = a.Shaders[index];
        CHECK(identities.insert(shader.Id).second);
        CHECK(shader.Id == b.Shaders[index].Id);
        CHECK(shader.Target != Keire::ShaderGraphTarget::Compute);
        const auto path = std::filesystem::path("Assets") / shader.SourcePath;
        const auto bytes = fs.Read(path, 4U * 1024U * 1024U);
        CHECK(Keire::Detail::DigestToString(Keire::Detail::Sha256(bytes)) == versionOneHashes[index]);
        CHECK(bytes == other.Read(path, 4U * 1024U * 1024U));
        const auto graph = Keire::ShaderGraphAsset::DecodeSource(bytes);
        CHECK(graph.Nodes.size() > 1);
        CHECK(graph.Connections.size() + 1 == graph.Nodes.size());
        const auto compilation = Keire::CompileShaderGraph(graph);
        CHECK(compilation.Succeeded());
    }
    CHECK(a.Shaders.front().Name == "Kéire/Lit");
    CHECK(Keire::EnsureSharedShaderLibrary(first.Root).Shaders.front().Id == a.Shaders.front().Id);
    auto database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
        .ProjectRoot = first.Root, .Importers = {Keire::CreateShaderGraphAssetImporter()}});
    (void)database->Refresh();
    for (const auto& shader : a.Shaders)
        REQUIRE(database->Find(shader.Id).has_value());
    const auto& lit = a.Shaders.front();
    const auto original = fs.Read(std::filesystem::path("Assets") / lit.SourcePath, 4U * 1024U * 1024U);
    CHECK_THROWS_AS((void)database->ReplaceAssetSource(lit.Id, original), std::invalid_argument);
    CHECK_THROWS_AS((void)database->Rename(lit.Id, "Changed.keireshadergraph"), std::invalid_argument);
    CHECK_THROWS_AS((void)database->TrashAsset(lit.Id), std::invalid_argument);
    CHECK_THROWS_AS((void)database->TrashFolder("Keire"), std::invalid_argument);
    CHECK_THROWS_AS((void)database->MoveFolder("Keire", "Moved"), std::invalid_argument);
    CHECK_THROWS_AS((void)database->Duplicate(lit.Id, "keire/sharedshaders/Copy.keireshadergraph"),
                    std::invalid_argument);
    const auto copied = database->Duplicate(lit.Id, "EditableLit.keireshadergraph");
    CHECK(copied != lit.Id);
    CHECK(database->Find(copied)->RelativePath == "EditableLit.keireshadergraph");
    CHECK(fs.Read("Assets/EditableLit.keireshadergraph", 4U * 1024U * 1024U) == original);
    CHECK_NOTHROW((void)database->Rename(copied, "RenamedLit.keireshadergraph"));
    CHECK_NOTHROW((void)Keire::ReadSharedShaderLibrary(first.Root));
}

TEST_CASE("Shared shader library refuses modified or missing pinned sources without rewriting them")
{
    SharedShaderFixture fixture;
    const auto library = Keire::EnsureSharedShaderLibrary(fixture.Root);
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    const auto path = std::filesystem::path("Assets") / library.Shaders.front().SourcePath;
    const auto original = fs.Read(path, 4U * 1024U * 1024U);
    auto changed = original;
    changed.push_back(std::byte{' '});
    fs.WriteFileAtomically(path, changed);
    CHECK_THROWS_AS((void)Keire::EnsureSharedShaderLibrary(fixture.Root), std::runtime_error);
    CHECK(fs.Read(path, 4U * 1024U * 1024U) == changed);
    fs.Remove(path);
    CHECK_THROWS((void)Keire::EnsureSharedShaderLibrary(fixture.Root));
    CHECK_FALSE(fs.Exists(path));
    fs.WriteFileAtomically(path, original);
    CHECK_NOTHROW((void)Keire::ReadSharedShaderLibrary(fixture.Root));
}

TEST_CASE("Shared shader installation refuses occupied destinations before publishing its lock")
{
    SharedShaderFixture fixture;
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    const std::filesystem::path source = "Assets/Keire/SharedShaders/1.0.0/Lit.keireshadergraph";
    fs.CreateDirectories(source.parent_path());
    const std::vector<std::byte> occupied{std::byte{'x'}};
    fs.WriteFileAtomically(source, occupied);
    CHECK_THROWS((void)Keire::EnsureSharedShaderLibrary(fixture.Root));
    CHECK(fs.Read(source, 16) == occupied);
    CHECK_FALSE(fs.Exists("ProjectSettings/SharedShaders.lock"));
    CHECK_FALSE(fs.Exists("Assets/Keire/SharedShaders/1.0.0/Unlit.keireshadergraph"));
}

TEST_CASE("Shared shader installation rejects identities owned by another source")
{
    SharedShaderFixture original;
    SharedShaderFixture collision;
    const auto library = Keire::EnsureSharedShaderLibrary(original.Root);
    const Keire::Detail::AnchoredFileSystem source(original.Root);
    const Keire::Detail::AnchoredFileSystem target(collision.Root);
    auto metadata = std::filesystem::path("Assets") / library.Shaders.front().SourcePath;
    metadata += ".keiremeta";
    target.WriteFileAtomically("Assets/Other.keireshadergraph.keiremeta", source.Read(metadata, 4U * 1024U * 1024U));
    CHECK_THROWS_AS((void)Keire::EnsureSharedShaderLibrary(collision.Root), std::runtime_error);
    CHECK_FALSE(target.Exists("ProjectSettings/SharedShaders.lock"));
    CHECK_FALSE(target.Exists("Assets/Keire/SharedShaders/1.0.0/Lit.keireshadergraph"));
}

TEST_CASE("Shared shader package input upgrades require review and preserve shader identities")
{
    SharedShaderFixture fixture;
    const auto original = Keire::EnsureSharedShaderLibrary(fixture.Root);
    const Keire::Detail::AnchoredFileSystem fs(fixture.Root);
    std::vector<Keire::SharedShaderInput> inputs{
        {Keire::SharedShaderInputKind::Compiler, "CompilerIdentity.json", {}},
        {Keire::SharedShaderInputKind::Include, "Surface.hlsli", {}},
        {Keire::SharedShaderInputKind::VisualFixture, "VisualBaseline.json", {}},
        {Keire::SharedShaderInputKind::PackageLock, "Packages.lock", {}}};
    const std::vector<std::byte> first{std::byte{'a'}};
    const std::vector<std::byte> second{std::byte{'b'}};
    for (const auto& input : inputs)
        fs.WriteFileAtomically(input.Path, first);
    const auto oldLock = fs.Read("ProjectSettings/SharedShaders.lock", 4U * 1024U * 1024U);
    const auto review = Keire::ReviewSharedShaderInputs(fixture.Root, inputs);
    CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 4U * 1024U * 1024U) == oldLock);
    CHECK(review.Inputs.front().Sha256.size() == 64);
    CHECK(review.Inputs.front().Sha256 != inputs.front().Sha256);

    SUBCASE("Changed input invalidates a review without publication")
    {
        fs.WriteFileAtomically(inputs[1].Path, second);
        CHECK_THROWS_AS((void)Keire::ApplySharedShaderInputs(fixture.Root, review), std::runtime_error);
        CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 4U * 1024U * 1024U) == oldLock);
    }
    SUBCASE("Changed compiler contract invalidates a review")
    {
        auto changed = review;
        ++changed.GraphImporterVersion;
        CHECK_THROWS_AS((void)Keire::ApplySharedShaderInputs(fixture.Root, changed), std::runtime_error);
        CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 4U * 1024U * 1024U) == oldLock);
    }
    SUBCASE("Missing input leaves the previous lock intact and restoration permits retry")
    {
        fs.Remove(inputs[1].Path);
        CHECK_THROWS((void)Keire::ApplySharedShaderInputs(fixture.Root, review));
        CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 4U * 1024U * 1024U) == oldLock);
        fs.WriteFileAtomically(inputs[1].Path, first);
        CHECK_NOTHROW((void)Keire::ApplySharedShaderInputs(fixture.Root, review));
    }
    SUBCASE("Changed library review cannot overwrite a newer lock")
    {
        auto changedLock = oldLock;
        changedLock.push_back(std::byte{' '});
        fs.WriteFileAtomically("ProjectSettings/SharedShaders.lock", changedLock);
        CHECK_THROWS_AS((void)Keire::ApplySharedShaderInputs(fixture.Root, review), std::runtime_error);
        CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 4U * 1024U * 1024U) == changedLock);
    }
    SUBCASE("Publication pins inputs and explicit re-review accepts a dependency upgrade")
    {
        const auto upgraded = Keire::ApplySharedShaderInputs(fixture.Root, review);
        REQUIRE(upgraded.Shaders.size() == original.Shaders.size());
        CHECK(upgraded.Inputs == review.Inputs);
        Keire::AssetImportContext context;
        context.Asset = upgraded.Shaders.front().Id;
        context.ProjectRoot = fixture.Root;
        context.SourceRoot = fixture.Root / "Assets";
        context.RelativePath = upgraded.Shaders.front().SourcePath;
        context.SourcePath = context.SourceRoot / context.RelativePath;
        context.ReadProjectFile = [&](const std::filesystem::path& path) { return fs.Read(path, 4U * 1024U * 1024U); };
        context.ResolveSubAssetId = [](std::string_view) { return Keire::AssetId::Generate(); };
        const auto imported = Keire::CreateShaderGraphAssetImporter().ContextualImport(
            context, fs.Read(std::filesystem::path("Assets") / context.RelativePath, 4U * 1024U * 1024U));
        for (const auto& input : review.Inputs)
            CHECK(std::ranges::any_of(
                imported.SourceDependencies, [&](const auto& dependency)
                { return dependency.RelativePath == input.Path && dependency.Digest == input.Sha256; }));
        for (std::size_t index = 0; index < original.Shaders.size(); ++index)
        {
            CHECK(upgraded.Shaders[index].Id == original.Shaders[index].Id);
            CHECK(upgraded.Shaders[index].SourcePath == original.Shaders[index].SourcePath);
        }
        CHECK_THROWS_AS((void)Keire::ApplySharedShaderInputs(fixture.Root, review), std::runtime_error);
        fs.WriteFileAtomically(inputs[3].Path, second);
        CHECK_THROWS_AS((void)Keire::ReadSharedShaderLibrary(fixture.Root), std::runtime_error);
        const auto updatedReview = Keire::ReviewSharedShaderInputs(fixture.Root, inputs);
        CHECK_NOTHROW((void)Keire::ApplySharedShaderInputs(fixture.Root, updatedReview));
        fs.Remove(inputs[2].Path);
        CHECK_THROWS((void)Keire::ReadSharedShaderLibrary(fixture.Root));
    }
    SUBCASE("Incomplete, duplicate and escaping input paths are rejected")
    {
        auto invalid = inputs;
        invalid.pop_back();
        CHECK_THROWS_AS((void)Keire::ReviewSharedShaderInputs(fixture.Root, invalid), std::invalid_argument);
        invalid = inputs;
        invalid.push_back(inputs.front());
        CHECK_THROWS_AS((void)Keire::ReviewSharedShaderInputs(fixture.Root, invalid), std::invalid_argument);
        invalid = inputs;
        invalid.front().Path = "../outside";
        CHECK_THROWS_AS((void)Keire::ReviewSharedShaderInputs(fixture.Root, invalid), std::invalid_argument);
        invalid.front().Path = "projectsettings/sharedshaders.lock";
        CHECK_THROWS_AS((void)Keire::ReviewSharedShaderInputs(fixture.Root, invalid), std::invalid_argument);
        CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 4U * 1024U * 1024U) == oldLock);
    }
}

TEST_CASE("Shared shader default review names missing project evidence without creating it")
{
    SharedShaderFixture fixture;
    (void)Keire::EnsureSharedShaderLibrary(fixture.Root);
    const auto defaults = Keire::DefaultSharedShaderInputs();
    REQUIRE(defaults.size() == 4);
    CHECK(defaults.back().Path == "Packages/packages-lock.keirejson");
    CHECK_THROWS_WITH(
        (void)Keire::ReviewSharedShaderInputs(fixture.Root),
        "Shared shader review requires the missing input: ProjectSettings/SharedShaderInputs/Compiler.json");
    CHECK_FALSE(std::filesystem::exists(fixture.Root / defaults.front().Path));
}
