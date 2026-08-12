#include "Keire/Core.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <system_error>

namespace
{
    class ProjectPackageFixture final
    {
      public:
        ProjectPackageFixture()
            : Root(KeireTests::MakeTestDirectory("project-packages")), Cache(Root / "GlobalCache"),
              ProjectParent(Root / "Projects")
        {
            std::filesystem::create_directories(Cache);
            std::filesystem::create_directories(ProjectParent);
            Project = Keire::Project::Create({ProjectParent, "Packages", Keire::ProjectTemplate::Empty});
            const auto descriptor = Project->Root() / "ProjectSettings" / "Project.keireproject";
            auto text = KeireTests::ReadFile(descriptor);
            const auto current = text.find("\"minimumEngineVersion\": \"0.3.1\"");
            if (current != std::string::npos)
                text.replace(current, std::string("\"minimumEngineVersion\": \"0.3.1\"").size(),
                             "\"minimumEngineVersion\": \"0.1.0\"");
            std::ofstream stream(descriptor, std::ios::binary | std::ios::trunc);
            stream << text;
        }

        ~ProjectPackageFixture() noexcept
        {
            Project.Reset();
            std::error_code ignored;
            for (std::filesystem::recursive_directory_iterator iterator(Root, ignored), end;
                 !ignored && iterator != end; iterator.increment(ignored))
                std::filesystem::permissions(iterator->path(), std::filesystem::perms::owner_write,
                                             std::filesystem::perm_options::add, ignored);
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] Keire::ProjectPackageManager Manager() const
        {
            return Keire::ProjectPackageManager({.ProjectRoot = Project->Root(),
                                                 .GlobalCacheRoot = Cache,
                                                 .EngineVersion = "0.3.1",
                                                 .Platform = "windows",
                                                 .Architecture = "x86_64",
                                                 .RendererCapabilities = {"pbr"},
                                                 .VerifyMarketplaceSignature = [](auto&&...) { return true; }});
        }

        [[nodiscard]] Keire::AssetPackageArchiveMetadata
        CreatePackage(const std::string& packageId, const std::string& version,
                      const std::vector<Keire::AssetPackageDependency>& dependencies = {})
        {
            const auto suffix = packageId + '-' + version;
            const auto payload = Root / (suffix + "-payload");
            std::filesystem::create_directories(payload / "Assets");
            {
                std::ofstream stream(payload / "Assets" / "Data.txt", std::ios::binary);
                stream << packageId << '@' << version;
            }
            Keire::AssetPackageManifest manifest;
            manifest.PackageId = packageId;
            manifest.Version = version;
            manifest.PublisherId = "keire.tests";
            manifest.DisplayName = packageId;
            manifest.InstallKind = Keire::AssetPackageInstallKind::Registry;
            manifest.Compatibility.MinimumEngineVersion = "0.3.1";
            manifest.Compatibility.Platforms = {"windows"};
            manifest.Compatibility.Architectures = {"x86_64"};
            manifest.Compatibility.RendererCapabilities = {"pbr"};
            manifest.Dependencies = dependencies;
            manifest.SignatureKeyId = "marketplace-test";
            manifest = Keire::InventoryAssetPackagePayload(std::move(manifest), payload);
            const auto archive = Root / (suffix + ".keireassetpackage");
            return Keire::WriteAssetPackageArchive(
                {.Manifest = manifest,
                 .PayloadRoot = payload,
                 .Output = archive,
                 .Signature = Keire::AssetPackageSignature{.KeyId = manifest.SignatureKeyId,
                                                           .Bytes = {std::byte{1}, std::byte{2}}}});
        }

        [[nodiscard]] Keire::ProjectPackageArchiveSource
        Source(const Keire::AssetPackageArchiveMetadata& metadata) const
        {
            return {.Archive =
                        Root / (metadata.Manifest.PackageId + '-' + metadata.Manifest.Version + ".keireassetpackage"),
                    .CatalogSource = "https://marketplace.keire.dev/v1/packages/" + metadata.Manifest.PackageId,
                    .ExpectedArchiveSizeBytes = metadata.ArchiveSizeBytes,
                    .ExpectedArchiveSha256 = metadata.ArchiveSha256};
        }

        std::filesystem::path Root;
        std::filesystem::path Cache;
        std::filesystem::path ProjectParent;
        Keire::Ref<Keire::Project> Project;
    };
} // namespace

TEST_CASE("project package documents are canonical and version ranges are deterministic")
{
    Keire::ProjectPackageManifest manifest;
    manifest.Dependencies = {{"com.keire.zeta", "^1.0.0"}, {"com.keire.alpha", ">=2.0.0 <3.0.0"}};
    const auto encoded = Keire::EncodeProjectPackageManifest(manifest);
    const auto decoded = Keire::DecodeProjectPackageManifest(encoded);
    REQUIRE(decoded.Dependencies.size() == 2);
    CHECK(decoded.Dependencies.front().PackageId == "com.keire.alpha");
    CHECK(Keire::EncodeProjectPackageManifest(decoded) == encoded);

    CHECK(Keire::AssetPackageVersionSatisfies("1.9.0", "^1.2.0"));
    CHECK_FALSE(Keire::AssetPackageVersionSatisfies("2.0.0", "^1.2.0"));
    CHECK(Keire::AssetPackageVersionSatisfies("2.5.0", ">=2.0.0 <3.0.0"));
    CHECK_FALSE(Keire::AssetPackageVersionSatisfies("3.0.0", ">=2.0.0 <3.0.0"));
    CHECK_THROWS_AS(
        static_cast<void>(Keire::DecodeProjectPackageManifest("{\"schemaVersion\":1,\"dependencies\":[],\"x\":1}")),
        std::invalid_argument);
}

TEST_CASE("project package manager installs dependency closure, mounts read-only cache, embeds, and removes")
{
    ProjectPackageFixture fixture;
    const auto dependency = fixture.CreatePackage("com.keire.dependency", "1.2.0");
    const auto direct = fixture.CreatePackage("com.keire.direct", "2.0.0", {{"com.keire.dependency", "^1.0.0"}});
    auto manager = fixture.Manager();

    const auto lock = manager.Install({.Archives = {fixture.Source(direct), fixture.Source(dependency)},
                                       .DirectDependencies = {{"com.keire.direct", "^2.0.0"}}});
    REQUIRE(lock.Packages.size() == 2);
    CHECK(std::filesystem::is_regular_file(Keire::ProjectPackageManager::ManifestPath(fixture.Project->Root())));
    CHECK(std::filesystem::is_regular_file(Keire::ProjectPackageManager::LockPath(fixture.Project->Root())));
    CHECK(Keire::Project::InspectMetadata(fixture.Project->Root()).MinimumEngineVersion == "0.3.1");

    const auto mounts = manager.Mounts();
    REQUIRE(mounts.size() == 2);
    CHECK(std::ranges::all_of(mounts, &Keire::ProjectPackageMount::ReadOnly));
    CHECK(std::ranges::all_of(mounts, [](const auto& mount) { return std::filesystem::is_directory(mount.Root); }));

    const auto embedded = manager.Embed("com.keire.direct");
    const auto embeddedEntry = std::ranges::find(embedded.Packages, std::string("com.keire.direct"),
                                                 &Keire::ProjectPackageLockEntry::PackageId);
    REQUIRE(embeddedEntry != embedded.Packages.end());
    CHECK(embeddedEntry->Embedded);
    const auto embeddedMounts = manager.Mounts();
    const auto embeddedMount =
        std::ranges::find(embeddedMounts, std::string("com.keire.direct"), &Keire::ProjectPackageMount::PackageId);
    REQUIRE(embeddedMount != embeddedMounts.end());
    CHECK_FALSE(embeddedMount->ReadOnly);

    const auto removed = manager.Remove("com.keire.direct");
    CHECK(removed.Packages.empty());
    CHECK(manager.Mounts().empty());
}

TEST_CASE("project package preflight reports missing transitive dependencies without mutating the project")
{
    ProjectPackageFixture fixture;
    const auto direct = fixture.CreatePackage("com.keire.direct", "1.0.0", {{"com.keire.missing", ">=1.0.0 <2.0.0"}});
    auto manager = fixture.Manager();
    const auto plan = manager.PreflightInstall(
        {.Archives = {fixture.Source(direct)}, .DirectDependencies = {{"com.keire.direct", "1.0.0"}}});

    CHECK_FALSE(plan.Valid());
    REQUIRE(plan.Conflicts.size() == 1);
    CHECK(plan.Conflicts.front().Kind == Keire::ProjectPackageConflictKind::MissingDependency);
    CHECK_FALSE(std::filesystem::exists(Keire::ProjectPackageManager::ManifestPath(fixture.Project->Root())));
    CHECK(Keire::Project::InspectMetadata(fixture.Project->Root()).MinimumEngineVersion == "0.1.0");
}
