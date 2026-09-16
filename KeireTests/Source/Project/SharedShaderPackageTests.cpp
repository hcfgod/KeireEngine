#include "Keire/Project/SharedShaderLibrary.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <span>
#include <stdexcept>

namespace
{
    struct PackageFixture
    {
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() / ("KeireShaderPackage-" + Keire::AssetId::Generate().ToString());
        PackageFixture()
        {
            for (const auto* folder : {"Project", "Package"})
            {
                const auto root = Root / folder;
                std::filesystem::create_directories(root / "Assets");
                (void)Keire::EnsureSharedShaderLibrary(root);
                const Keire::Detail::AnchoredFileSystem fs(root);
                for (const auto& input : Keire::DefaultSharedShaderInputs())
                {
                    fs.CreateDirectories(input.Path.parent_path());
                    const std::string content = "fixture dependency";
                    fs.WriteFileAtomically(input.Path, std::as_bytes(std::span(content.data(), content.size())));
                }
                (void)Keire::ApplySharedShaderInputs(root, Keire::ReviewSharedShaderInputs(root));
            }
            const Keire::Detail::AnchoredFileSystem fs(Root / "Package");
            auto lock = nlohmann::json::parse(fs.Read("ProjectSettings/SharedShaders.lock", 1024 * 1024));
            lock["version"] = "1.1.0";
            const auto text = lock.dump(2);
            fs.WriteFileAtomically("ProjectSettings/SharedShaders.lock",
                                   std::as_bytes(std::span(text.data(), text.size())));
        }
        ~PackageFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };
} // namespace

TEST_CASE("Shared shader package reviewed version upgrade preserves identities and rejects stale publication")
{
    PackageFixture fixture;
    const auto project = fixture.Root / "Project";
    const auto package = fixture.Root / "Package";
    const Keire::Detail::AnchoredFileSystem fs(project);
    const auto before = Keire::ReadSharedShaderLibrary(project);
    const auto oldLock = fs.Read("ProjectSettings/SharedShaders.lock", 1024 * 1024);
    const auto review = Keire::ReviewSharedShaderPackage(project, package);
    CHECK(review.PreviousVersion == "1.0.0");
    CHECK(review.Version == "1.1.0");
    CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 1024 * 1024) == oldLock);
    SUBCASE("Apply publishes version and retains exact identities")
    {
        const auto upgraded = Keire::ApplySharedShaderPackage(project, review);
        CHECK(upgraded.Version == "1.1.0");
        REQUIRE(upgraded.Shaders.size() == before.Shaders.size());
        for (std::size_t i = 0; i < before.Shaders.size(); ++i)
        {
            CHECK(upgraded.Shaders[i].Id == before.Shaders[i].Id);
            CHECK(upgraded.Shaders[i].SourcePath == before.Shaders[i].SourcePath);
        }
        CHECK(Keire::EnsureSharedShaderLibrary(project).Version == "1.1.0");
        CHECK_THROWS((void)Keire::ApplySharedShaderPackage(project, review));
    }
    SUBCASE("A package lock change invalidates review")
    {
        const Keire::Detail::AnchoredFileSystem source(package);
        auto bytes = source.Read("ProjectSettings/SharedShaders.lock", 1024 * 1024);
        bytes.push_back(std::byte{' '});
        source.WriteFileAtomically("ProjectSettings/SharedShaders.lock", bytes);
        CHECK_THROWS_AS((void)Keire::ApplySharedShaderPackage(project, review), std::runtime_error);
        CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 1024 * 1024) == oldLock);
    }
    SUBCASE("A project lock change invalidates review")
    {
        auto bytes = oldLock;
        bytes.push_back(std::byte{' '});
        fs.WriteFileAtomically("ProjectSettings/SharedShaders.lock", bytes);
        CHECK_THROWS_AS((void)Keire::ApplySharedShaderPackage(project, review), std::runtime_error);
        CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 1024 * 1024) == bytes);
    }
    SUBCASE("A changed pinned package input rejects publication")
    {
        const Keire::Detail::AnchoredFileSystem source(package);
        const std::vector<std::byte> changed{std::byte{'x'}};
        source.WriteFileAtomically(Keire::DefaultSharedShaderInputs().front().Path, changed);
        CHECK_THROWS((void)Keire::ApplySharedShaderPackage(project, review));
        CHECK(fs.Read("ProjectSettings/SharedShaders.lock", 1024 * 1024) == oldLock);
    }
}
