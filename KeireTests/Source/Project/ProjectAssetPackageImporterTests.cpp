#include "Keire/Core.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <system_error>

namespace
{
    class ProjectAssetImportFixture final
    {
      public:
        ProjectAssetImportFixture()
            : Root(KeireTests::MakeTestDirectory("project-asset-import")), ProjectParent(Root / "Projects"),
              PrimaryId(Keire::AssetId::Generate()), DependencyId(Keire::AssetId::Generate()),
              UnrelatedId(Keire::AssetId::Generate()), TypeId(Keire::AssetTypeId(Keire::AssetId::Generate()))
        {
            std::filesystem::create_directories(ProjectParent);
            Project = Keire::Project::Create({ProjectParent, "AssetImport", Keire::ProjectTemplate::Empty});
            const auto descriptor = Project->Root() / "ProjectSettings" / "Project.keireproject";
            auto text = KeireTests::ReadFile(descriptor);
            const auto currentVersion =
                "\"minimumEngineVersion\": \"" + std::string(Keire::GetBuildInfo().Version) + "\"";
            const auto current = text.find(currentVersion);
            if (current != std::string::npos)
                text.replace(current, currentVersion.size(), "\"minimumEngineVersion\": \"0.1.0\"");
            std::ofstream stream(descriptor, std::ios::binary | std::ios::trunc);
            stream << text;
        }

        ~ProjectAssetImportFixture() noexcept
        {
            Project.Reset();
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] Keire::ProjectAssetPackageImporter Importer() const
        {
            return Keire::ProjectAssetPackageImporter({.ProjectRoot = Project->Root(),
                                                       .EngineVersion = "0.3.1",
                                                       .Platform = "windows",
                                                       .Architecture = "x86_64",
                                                       .RendererCapabilities = {"pbr"},
                                                       .VerifyMarketplaceSignature = [](auto&&...) { return true; }});
        }

        [[nodiscard]] Keire::AssetPackageArchiveMetadata CreatePackage(const std::string& version,
                                                                       const std::string& primaryText,
                                                                       const std::string& dependencyText,
                                                                       const bool executableCode = false)
        {
            const auto payload = Root / ("payload-" + version);
            std::filesystem::create_directories(payload / "Assets");
            Write(payload / "Assets" / "Primary.txt", primaryText);
            Write(payload / "Assets" / "Dependency.txt", dependencyText);
            Write(payload / "Assets" / "Unrelated.txt", "unrelated-" + version);
            WriteMetadata(payload / "Assets" / "Primary.txt.keiremeta", PrimaryId);
            WriteMetadata(payload / "Assets" / "Dependency.txt.keiremeta", DependencyId);
            WriteMetadata(payload / "Assets" / "Unrelated.txt.keiremeta", UnrelatedId);
            if (executableCode)
            {
                std::filesystem::create_directories(payload / "Assets" / "Scripts");
                Write(payload / "Assets" / "Scripts" / "Demo.cs", "// " + primaryText + "\nclass Demo {}\n");
                Write(payload / "Assets" / "Scripts" / "Demo.keireassembly", "{\"name\":\"Demo\"}");
            }

            Keire::AssetPackageManifest manifest;
            manifest.PackageId = "com.keire.tests.asset-import";
            manifest.Version = version;
            manifest.PublisherId = "keire.tests";
            manifest.DisplayName = "Asset Import Test";
            manifest.InstallKind = Keire::AssetPackageInstallKind::AssetImport;
            manifest.Compatibility.MinimumEngineVersion = "0.3.1";
            manifest.Compatibility.Platforms = {"windows"};
            manifest.Compatibility.Architectures = {"x86_64"};
            manifest.Compatibility.RendererCapabilities = {"pbr"};
            manifest.Assets = {{.Id = PrimaryId,
                                .Type = TypeId,
                                .SourcePath = "Assets/Primary.txt",
                                .MetadataPath = "Assets/Primary.txt.keiremeta",
                                .Dependencies = {DependencyId}},
                               {.Id = DependencyId,
                                .Type = TypeId,
                                .SourcePath = "Assets/Dependency.txt",
                                .MetadataPath = "Assets/Dependency.txt.keiremeta"},
                               {.Id = UnrelatedId,
                                .Type = TypeId,
                                .SourcePath = "Assets/Unrelated.txt",
                                .MetadataPath = "Assets/Unrelated.txt.keiremeta"}};
            if (executableCode)
            {
                manifest.ManagedAssemblies.push_back({.Name = "Demo",
                                                      .DefinitionPath = "Assets/Scripts/Demo.keireassembly",
                                                      .Scope = Keire::AssetPackageManagedAssemblyScope::Runtime});
            }
            manifest.SignatureKeyId = "marketplace-test";
            manifest = Keire::InventoryAssetPackagePayload(std::move(manifest), payload);
            const auto archive = Root / ("asset-import-" + version + ".keireassetpackage");
            return Keire::WriteAssetPackageArchive(
                {.Manifest = manifest,
                 .PayloadRoot = payload,
                 .Output = archive,
                 .Signature = Keire::AssetPackageSignature{.KeyId = manifest.SignatureKeyId,
                                                           .Bytes = {std::byte{1}, std::byte{2}}}});
        }

        [[nodiscard]] Keire::ProjectAssetImportRequest Request(const Keire::AssetPackageArchiveMetadata& package) const
        {
            return {.Archive = Root / ("asset-import-" + package.Manifest.Version + ".keireassetpackage"),
                    .SelectedAssets = {PrimaryId},
                    .ExpectedArchiveSizeBytes = package.ArchiveSizeBytes,
                    .ExpectedArchiveSha256 = package.ArchiveSha256};
        }

        static void Write(const std::filesystem::path& path, const std::string& value)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream << value;
        }

        void WriteMetadata(const std::filesystem::path& path, const Keire::AssetId id) const
        {
            Write(path, "{\"id\":\"" + id.ToString() + "\",\"type\":\"" + TypeId.ToString() + "\"}");
        }

        std::filesystem::path Root;
        std::filesystem::path ProjectParent;
        Keire::AssetId PrimaryId;
        Keire::AssetId DependencyId;
        Keire::AssetId UnrelatedId;
        Keire::AssetTypeId TypeId;
        Keire::Ref<Keire::Project> Project;
    };
} // namespace

TEST_CASE("asset package import resolves selected asset dependencies and writes a source-controlled receipt")
{
    ProjectAssetImportFixture fixture;
    const auto package = fixture.CreatePackage("1.0.0", "primary-v1", "dependency-v1");
    auto importer = fixture.Importer();
    const auto request = fixture.Request(package);
    const auto plan = importer.Preflight(request);

    REQUIRE(plan.Valid());
    CHECK(plan.ResolvedAssets.size() == 2);
    CHECK(plan.Entries.size() == 4);
    const auto result = importer.Import(request);
    CHECK(result.Written.size() == 4);
    CHECK(std::filesystem::is_regular_file(fixture.Project->Root() / "Assets" / "Primary.txt"));
    CHECK(std::filesystem::is_regular_file(fixture.Project->Root() / "Assets" / "Dependency.txt"));
    CHECK_FALSE(std::filesystem::exists(fixture.Project->Root() / "Assets" / "Unrelated.txt"));
    CHECK(std::filesystem::is_regular_file(
        Keire::ProjectAssetPackageImporter::ReceiptPath(fixture.Project->Root(), package.Manifest.PackageId)));
    CHECK(Keire::Project::InspectMetadata(fixture.Project->Root()).MinimumEngineVersion == "0.3.1");
}

TEST_CASE("asset package update keeps local edits only after an explicit three-way conflict decision")
{
    ProjectAssetImportFixture fixture;
    const auto first = fixture.CreatePackage("1.0.0", "primary-v1", "dependency-v1");
    auto importer = fixture.Importer();
    static_cast<void>(importer.Import(fixture.Request(first)));
    ProjectAssetImportFixture::Write(fixture.Project->Root() / "Assets" / "Primary.txt", "local-edit");

    const auto second = fixture.CreatePackage("2.0.0", "primary-v2", "dependency-v2");
    auto request = fixture.Request(second);
    const auto unresolved = importer.Preflight(request);
    CHECK_FALSE(unresolved.Valid());
    request.Decisions.push_back(
        {.Path = "Assets/Primary.txt", .Resolution = Keire::ProjectAssetImportResolution::KeepLocal});
    const auto updated = importer.Import(request);
    CHECK(KeireTests::ReadFile(fixture.Project->Root() / "Assets" / "Primary.txt") == "local-edit");
    CHECK(KeireTests::ReadFile(fixture.Project->Root() / "Assets" / "Dependency.txt") == "dependency-v2");
    CHECK(updated.Receipt.Version == "2.0.0");

    const auto removed = importer.Remove(second.Manifest.PackageId);
    CHECK(std::ranges::find(removed.RetainedModified, std::filesystem::path("Assets/Primary.txt")) !=
          removed.RetainedModified.end());
    CHECK(std::filesystem::is_regular_file(fixture.Project->Root() / "Assets" / "Primary.txt"));
    CHECK_FALSE(std::filesystem::exists(fixture.Project->Root() / "Assets" / "Dependency.txt"));
}

TEST_CASE("asset package import receipts reject unknown fields and remain deterministic")
{
    Keire::ProjectAssetImportReceipt receipt{
        .PackageId = "com.keire.example",
        .Version = "1.0.0",
        .ArchiveSha256 = std::string(64U, 'a'),
        .Entries = {{.ProjectPath = "Assets/B.txt", .PackageSha256 = std::string(64U, 'b')},
                    {.ProjectPath = "Assets/A.txt", .PackageSha256 = std::string(64U, 'c')}}};
    const auto encoded = Keire::EncodeProjectAssetImportReceipt(receipt);
    CHECK(Keire::EncodeProjectAssetImportReceipt(Keire::DecodeProjectAssetImportReceipt(encoded)) == encoded);
    CHECK_THROWS_AS(static_cast<void>(Keire::DecodeProjectAssetImportReceipt(
                        "{\"schemaVersion\":1,\"packageId\":\"com.keire.example\",\"version\":\"1.0.0\","
                        "\"archiveSha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
                        "\"executableCodeFingerprint\":null,\"executableCodeApproved\":false,"
                        "\"entries\":[],\"unexpected\":true}")),
                    std::invalid_argument);
}

TEST_CASE("asset package executable code requires consent again only when its fingerprint changes")
{
    ProjectAssetImportFixture fixture;
    auto importer = fixture.Importer();
    const auto first = fixture.CreatePackage("1.0.0", "code-v1", "dependency", true);
    auto request = fixture.Request(first);
    CHECK_FALSE(importer.Preflight(request).Valid());
    request.AllowExecutableCode = true;
    const auto imported = importer.Import(request);
    CHECK(imported.Receipt.ExecutableCodeApproved);
    CHECK(imported.Receipt.ExecutableCodeFingerprint.size() == 64U);
    CHECK(std::filesystem::is_regular_file(fixture.Project->Root() / "Assets" / "Scripts" / "Demo.cs"));

    const auto unchanged = fixture.CreatePackage("1.1.0", "code-v1", "dependency-update", true);
    CHECK(importer.Preflight(fixture.Request(unchanged)).Valid());
    const auto changed = fixture.CreatePackage("2.0.0", "code-v2", "dependency-update", true);
    const auto changedPlan = importer.Preflight(fixture.Request(changed));
    CHECK_FALSE(changedPlan.Valid());
    CHECK(
        std::ranges::any_of(changedPlan.Conflicts, [](const auto& conflict)
                            { return conflict.Kind == Keire::ProjectAssetImportConflictKind::ExecutableCodeConsent; }));
}
