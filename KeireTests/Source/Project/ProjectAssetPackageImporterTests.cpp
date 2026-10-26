#include "Keire/Assets/InputActionAsset.h"
#include "Keire/Core.h"
#include "Keire/Rendering/RenderSystem.h"
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

        [[nodiscard]] Keire::ProjectAssetImportRequest SettingsPackage(const bool invalidRendering = false,
                                                                       const bool invalidInput = false)
        {
            const auto payload = Root / "settings-payload";
            std::filesystem::create_directories(payload / "Assets");
            std::filesystem::create_directories(payload / "ProjectSettings");
            Keire::SaveRenderEnvironmentSettings(payload, {});
            if (invalidRendering)
                Write(payload / "ProjectSettings/Rendering.keiresettings", "{\"schemaVersion\":999}");
            Write(payload / "ProjectSettings/Project.keireproject", "do not replace project identity");
            Write(payload / "ProjectSettings/EnvironmentTime.keiresettings", "unsupported packaged daily time");
            Write(payload / "ProjectSettings/EnvironmentLighting.keiresettings", "unsupported packaged daily lighting");
            const auto bytes = Keire::InputActionAsset::Encode(Keire::InputActionAsset::GameplayDefinition());
            {
                std::ofstream stream(payload / "Assets/Controls.keireinput", std::ios::binary);
                stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            if (invalidInput)
                Write(payload / "Assets/Controls.keireinput", "invalid input data");
            const auto type = Keire::InputActionAsset::StaticType();
            Write(payload / "Assets/Controls.keireinput.keiremeta",
                  "{\"id\":\"" + PrimaryId.ToString() + "\",\"type\":\"" + type.ToString() + "\"}");
            Keire::AssetPackageManifest manifest;
            manifest.PackageId = "com.keire.tests.settings-import";
            manifest.Version = "1.0.0";
            manifest.PublisherId = "keire.tests";
            manifest.DisplayName = "Optional settings";
            manifest.InstallKind = Keire::AssetPackageInstallKind::AssetImport;
            manifest.Compatibility.MinimumEngineVersion = "0.3.1";
            manifest.Assets = {{.Id = PrimaryId,
                                .Type = type,
                                .SourcePath = "Assets/Controls.keireinput",
                                .MetadataPath = "Assets/Controls.keireinput.keiremeta"}};
            manifest = Keire::InventoryAssetPackagePayload(std::move(manifest), payload);
            const auto archive = Root / "settings.keireassetpackage";
            static_cast<void>(
                Keire::WriteAssetPackageArchive({.Manifest = manifest, .PayloadRoot = payload, .Output = archive}));
            return {.Archive = archive, .RequireMarketplaceSignature = false};
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

TEST_CASE("asset package settings are explicit validated project-owned choices")
{
    ProjectAssetImportFixture fixture;
    CHECK_FALSE(fixture.Project->Writable());
    CHECK_THROWS_AS(fixture.Project->Save(fixture.Project->Descriptor()), std::logic_error);
    fixture.Project = Keire::Project::Open(fixture.Project->Root(), Keire::ProjectOpenMode::Exclusive);
    REQUIRE(fixture.Project->Writable());
    auto request = fixture.SettingsPackage();
    auto importer = fixture.Importer();
    const auto descriptor = fixture.Project->Root() / "ProjectSettings/Project.keireproject";
    const auto original = Keire::Project::Open(fixture.Project->Root())->Descriptor();
    const auto rendering = fixture.Project->Root() / "ProjectSettings/Rendering.keiresettings";
    const auto dailyTime = fixture.Project->Root() / "ProjectSettings/EnvironmentTime.keiresettings";
    const auto dailyLighting = fixture.Project->Root() / "ProjectSettings/EnvironmentLighting.keiresettings";
    ProjectAssetImportFixture::Write(dailyTime, "destination daily time");
    ProjectAssetImportFixture::Write(dailyLighting, "destination daily lighting");
    ProjectAssetImportFixture::Write(rendering, "original rendering settings");
    auto plan = importer.Preflight(request);
    REQUIRE(plan.Valid());
    CHECK(plan.HasRenderingSettings);
    CHECK(plan.UnsupportedSettings ==
          std::vector<std::filesystem::path>{"ProjectSettings/EnvironmentLighting.keiresettings",
                                             "ProjectSettings/EnvironmentTime.keiresettings",
                                             "ProjectSettings/Project.keireproject"});
    CHECK(plan.Entries.size() == 2);
    static_cast<void>(importer.Import(request));
    CHECK(KeireTests::ReadFile(rendering) == "original rendering settings");
    CHECK(KeireTests::ReadFile(dailyTime) == "destination daily time");
    CHECK(KeireTests::ReadFile(dailyLighting) == "destination daily lighting");
    CHECK(Keire::Project::Open(fixture.Project->Root())->Descriptor().DefaultInput == original.DefaultInput);
    CHECK_FALSE(std::filesystem::exists(fixture.Project->Root() / "Assets/ProjectSettings"));

    request.ApplyRenderingSettings = true;
    request.DefaultInput = fixture.PrimaryId;
    plan = importer.Preflight(request);
    REQUIRE(plan.Valid());
    CHECK(plan.Entries.size() == 3);
    const auto result = importer.Import(request);
    const auto adopted = Keire::Project::Open(fixture.Project->Root())->Descriptor();
    CHECK(adopted.Id == original.Id);
    CHECK(adopted.Name == original.Name);
    CHECK(adopted.StartupScene == original.StartupScene);
    CHECK(adopted.DefaultInput == fixture.PrimaryId);
    CHECK(adopted.DefaultInputMap == Keire::InputActionAsset::GameplayDefinition().ActionMaps.front().Id);
    const auto committedDescriptor = KeireTests::ReadFile(descriptor);
    fixture.Project->ReloadDescriptor();
    CHECK(KeireTests::ReadFile(descriptor) == committedDescriptor);
    CHECK(fixture.Project->Descriptor().DefaultInput == fixture.PrimaryId);
    fixture.Project->Save(fixture.Project->Descriptor());
    CHECK(Keire::Project::Open(fixture.Project->Root())->Descriptor().DefaultInput == fixture.PrimaryId);
    CHECK_NOTHROW(Keire::LoadRenderEnvironmentSettings(fixture.Project->Root()));
    CHECK(KeireTests::ReadFile(dailyTime) == "destination daily time");
    CHECK(KeireTests::ReadFile(dailyLighting) == "destination daily lighting");
    CHECK(result.Receipt.Entries.size() == 2);
    CHECK(std::ranges::none_of(result.Receipt.Entries, &Keire::ProjectAssetImportReceiptEntry::Owned));
    static_cast<void>(importer.Remove(result.Receipt.PackageId));
    CHECK(std::filesystem::exists(fixture.Project->Root() / "Assets/Controls.keireinput"));
    CHECK(std::filesystem::exists(rendering));
    CHECK(Keire::Project::Open(fixture.Project->Root())->Descriptor().DefaultInput == fixture.PrimaryId);
}

TEST_CASE("invalid optional package settings fail before publication")
{
    for (const bool renderingFailure : {false, true})
    {
        ProjectAssetImportFixture fixture;
        auto request = fixture.SettingsPackage(renderingFailure, !renderingFailure);
        request.ApplyRenderingSettings = true;
        request.DefaultInput = fixture.PrimaryId;
        auto importer = fixture.Importer();
        const auto descriptor = fixture.Project->Root() / "ProjectSettings/Project.keireproject";
        const auto before = KeireTests::ReadFile(descriptor);
        CHECK_THROWS(importer.Import(request));
        CHECK(KeireTests::ReadFile(descriptor) == before);
        CHECK_FALSE(std::filesystem::exists(fixture.Project->Root() / "Assets/Controls.keireinput"));
    }
}

TEST_CASE("kept local rendering settings determine import validation")
{
    for (const bool invalidLocal : {false, true})
    {
        ProjectAssetImportFixture fixture;
        auto request = fixture.SettingsPackage(!invalidLocal);
        request.ApplyRenderingSettings = true;
        const std::filesystem::path renderingPath = "ProjectSettings/Rendering.keiresettings";
        request.Decisions.push_back({renderingPath, Keire::ProjectAssetImportResolution::KeepLocal});
        Keire::RenderEnvironmentSettings localSettings;
        localSettings.Exposure = 2.0F;
        Keire::SaveRenderEnvironmentSettings(fixture.Project->Root(), localSettings);
        if (invalidLocal)
        {
            ProjectAssetImportFixture::Write(fixture.Project->Root() / renderingPath, "{\"schemaVersion\":999}");
        }
        const auto before = KeireTests::ReadFile(fixture.Project->Root() / renderingPath);
        auto importer = fixture.Importer();
        const auto plan = importer.Preflight(request);
        REQUIRE(plan.Valid());
        const auto entry =
            std::ranges::find(plan.Entries, renderingPath, &Keire::ProjectAssetImportPlanEntry::ProjectPath);
        REQUIRE(entry != plan.Entries.end());
        CHECK(entry->Disposition == Keire::ProjectAssetImportDisposition::KeepLocal);
        if (invalidLocal)
        {
            CHECK_THROWS(importer.Import(request));
            CHECK_FALSE(std::filesystem::exists(fixture.Project->Root() / "Assets/Controls.keireinput"));
            CHECK_FALSE(importer.Receipt("com.keire.tests.settings-import").has_value());
        }
        else
        {
            const auto result = importer.Import(request);
            CHECK(std::ranges::find(result.Retained, renderingPath) != result.Retained.end());
            CHECK(std::filesystem::exists(fixture.Project->Root() / "Assets/Controls.keireinput"));
        }
        CHECK(KeireTests::ReadFile(fixture.Project->Root() / renderingPath) == before);
    }
}

TEST_CASE("project descriptor refresh rejects corrupted or replaced identity without changing cached settings")
{
    ProjectAssetImportFixture fixture;
    const auto path = fixture.Project->Root() / "ProjectSettings/Project.keireproject";
    const auto original = fixture.Project->Descriptor();
    auto text = KeireTests::ReadFile(path);
    ProjectAssetImportFixture::Write(path, "invalid descriptor");
    CHECK_THROWS(fixture.Project->ReloadDescriptor());
    CHECK(fixture.Project->Descriptor().Id == original.Id);
    const auto oldId = original.Id.ToString();
    const auto position = text.find(oldId);
    REQUIRE(position != std::string::npos);
    text.replace(position, oldId.size(), Keire::AssetId::Generate().ToString());
    ProjectAssetImportFixture::Write(path, text);
    CHECK_THROWS(fixture.Project->ReloadDescriptor());
    CHECK(fixture.Project->Descriptor().Id == original.Id);
    CHECK(fixture.Project->Descriptor().DefaultInput == original.DefaultInput);
    CHECK(KeireTests::ReadFile(path) == text);
}

TEST_CASE("optional package settings roll back with assets on publication failure")
{
    ProjectAssetImportFixture fixture;
    auto request = fixture.SettingsPackage();
    request.ApplyRenderingSettings = true;
    request.DefaultInput = fixture.PrimaryId;
    const auto rendering = fixture.Project->Root() / "ProjectSettings/Rendering.keiresettings";
    ProjectAssetImportFixture::Write(rendering, "retain original rendering bytes");
    const auto descriptor = fixture.Project->Root() / "ProjectSettings/Project.keireproject";
    const auto before = KeireTests::ReadFile(descriptor);
    Keire::ProjectAssetPackageImporter importer({.ProjectRoot = fixture.Project->Root(),
                                                 .EngineVersion = "0.3.1",
                                                 .Platform = "windows",
                                                 .Architecture = "x86_64",
                                                 .Events = [](const Keire::ProjectAssetImportEvent& event)
                                                 {
                                                     if (event.State == Keire::ProjectAssetImportState::Publishing &&
                                                         event.Path == "ProjectSettings/Rendering.keiresettings")
                                                         throw std::runtime_error(
                                                             "injected settings publication failure");
                                                 }});
    CHECK_THROWS(importer.Import(request));
    CHECK(KeireTests::ReadFile(rendering) == "retain original rendering bytes");
    CHECK(KeireTests::ReadFile(descriptor) == before);
    CHECK_FALSE(std::filesystem::exists(fixture.Project->Root() / "Assets/Controls.keireinput"));
}

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
    CHECK(Keire::Project::InspectMetadata(fixture.Project->Root()).MinimumEngineVersion == "0.4.4");
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

TEST_CASE("asset package receipts retain compatibility across editor upgrades and recognize legacy imports")
{
    ProjectAssetImportFixture fixture;
    auto importer = fixture.Importer();
    const auto package = fixture.CreatePackage("1.0.0", "primary", "dependency");
    auto receipt = importer.Import(fixture.Request(package)).Receipt;
    REQUIRE(receipt.Manifest.has_value());
    CHECK(importer.Receipts().size() == 1);
    CHECK(importer.CompatibilityDiagnostic(receipt).empty());
    receipt.Manifest->Compatibility.MaximumEngineVersion = "0.3.1";
    Keire::ProjectAssetPackageImporter upgraded({.ProjectRoot = fixture.Project->Root(),
                                                 .EngineVersion = "0.4.4",
                                                 .Platform = "windows",
                                                 .Architecture = "x86_64",
                                                 .RendererCapabilities = {"pbr"}});
    CHECK(upgraded.CompatibilityDiagnostic(receipt).find("Update this package") != std::string::npos);
    receipt.SchemaVersion = 1;
    receipt.Manifest.reset();
    const auto legacy = Keire::DecodeProjectAssetImportReceipt(Keire::EncodeProjectAssetImportReceipt(receipt));
    CHECK_FALSE(legacy.Manifest.has_value());
    CHECK(upgraded.CompatibilityDiagnostic(legacy).find("unknown") != std::string::npos);
}

TEST_CASE("asset package partial updates retain ownership of previously imported files")
{
    ProjectAssetImportFixture fixture;
    auto importer = fixture.Importer();
    const auto first = fixture.CreatePackage("1.0.0", "primary", "dependency");
    auto request = fixture.Request(first);
    request.SelectedAssets.clear();
    static_cast<void>(importer.Import(request));
    const auto second = fixture.CreatePackage("1.1.0", "new primary", "dependency");
    const auto updated = importer.Import(fixture.Request(second));
    CHECK(updated.Receipt.Entries.size() == 6);
    static_cast<void>(importer.Remove(second.Manifest.PackageId));
    CHECK_FALSE(std::filesystem::exists(fixture.Project->Root() / "Assets/Unrelated.txt"));
}

TEST_CASE("asset package failed publication restores project bookkeeping and previous files")
{
    ProjectAssetImportFixture fixture;
    const auto package = fixture.CreatePackage("1.0.0", "primary", "dependency");
    const auto descriptor = fixture.Project->Root() / "ProjectSettings/Project.keireproject";
    const auto before = KeireTests::ReadFile(descriptor);
    Keire::ProjectAssetPackageImporter importer({.ProjectRoot = fixture.Project->Root(),
                                                 .EngineVersion = "0.4.4",
                                                 .Platform = "windows",
                                                 .Architecture = "x86_64",
                                                 .RendererCapabilities = {"pbr"},
                                                 .VerifyMarketplaceSignature = [](auto&&...) { return true; },
                                                 .Events =
                                                     [&](const Keire::ProjectAssetImportEvent& event)
                                                 {
                                                     if (event.State == Keire::ProjectAssetImportState::Publishing)
                                                     {
                                                         ProjectAssetImportFixture::Write(descriptor,
                                                                                          "invalid project descriptor");
                                                         throw std::runtime_error("injected publication failure");
                                                     }
                                                 }});
    CHECK_THROWS(importer.Import(fixture.Request(package)));
    CHECK(KeireTests::ReadFile(descriptor) == before);
    CHECK_FALSE(importer.Receipt(package.Manifest.PackageId).has_value());
    CHECK_FALSE(std::filesystem::exists(fixture.Project->Root() / "Assets/Primary.txt"));
}
