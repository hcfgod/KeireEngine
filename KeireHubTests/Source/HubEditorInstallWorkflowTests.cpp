#include <KeireHubTests/TestSupport.h>

#include "KeireHub/HubEditorInstallWorkflow.h"

#include "KeireHubRuntime/HubWorkerProtocol.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace KeireHub;

TEST_CASE("Active editor install matching requires the same package and version")
{
    const std::vector<HubTaskUiRecord> tasks{
        {.Id = "install", .Active = true, .EditorPackageId = "keire.editor", .EditorVersion = "1.2.3"},
        {.Id = "finished", .Active = false, .EditorPackageId = "keire.editor", .EditorVersion = "2.0.0"}};

    CHECK(HasActiveEditorInstall(tasks, "keire.editor", "1.2.3"));
    CHECK_FALSE(HasActiveEditorInstall(tasks, "keire.editor", "1.2.4"));
    CHECK_FALSE(HasActiveEditorInstall(tasks, "other.editor", "1.2.3"));
    CHECK_FALSE(HasActiveEditorInstall(tasks, "keire.editor", "2.0.0"));
}

namespace
{
    constexpr std::string_view CatalogKeyId = "ed25519-00000000000000000000000000000000";

    [[nodiscard]] SemanticVersion Version(const std::string_view value)
    {
        auto version = SemanticVersion::Parse(value);
        if (!version)
            throw std::runtime_error(version.Error().Message);
        return std::move(version).Value();
    }

    [[nodiscard]] PackageManifest Package(std::string id, const std::string_view version, const PackageKind kind,
                                          const std::uint64_t size)
    {
        const auto file = id + ".bin";
        return {.Id = std::move(id),
                .Version = Version(version),
                .Kind = kind,
                .DisplayName = file,
                .Channel = "stable",
                .Platform = "windows",
                .Architecture = "x86_64",
                .ArtifactSizeBytes = size,
                .ArtifactSha256 = KeireHubTests::Digest('a'),
                .InstalledSizeBytes = size * 2,
                .Files = {{file, size * 2, KeireHubTests::Digest('b')}},
                .SignatureKeyId = "release-key"};
    }

    [[nodiscard]] VersionConstraint Constraint(const std::string_view value)
    {
        auto constraint = VersionConstraint::Parse(value);
        if (!constraint)
            throw std::runtime_error(constraint.Error().Message);
        return std::move(constraint).Value();
    }

    [[nodiscard]] std::shared_ptr<const DistributionCatalogSnapshot> Distribution(std::vector<PackageManifest> packages)
    {
        auto catalog = std::make_shared<DistributionPackageCatalog>();
        catalog->Identity = {.KeyId = std::string(CatalogKeyId),
                             .Sequence = 9,
                             .ExpiresAt = "2035-01-01T00:00:00Z",
                             .Channel = "stable",
                             .Platform = "windows",
                             .Architecture = "x86_64"};
        catalog->Packages = std::move(packages);
        auto distribution = std::make_shared<DistributionCatalogSnapshot>();
        distribution->OnlineDiscoveryEnabled = true;
        distribution->PackageCatalogs.push_back(
            {.Channel = "stable",
             .Catalog = std::shared_ptr<const DistributionPackageCatalog>(std::move(catalog)),
             .Status = {.State = DistributionCatalogSourceState::Online,
                        .Sequence = 9,
                        .KeyId = std::string(CatalogKeyId),
                        .ExpiresAt = "2035-01-01T00:00:00Z"}});
        return std::shared_ptr<const DistributionCatalogSnapshot>(std::move(distribution));
    }
} // namespace

TEST_CASE("Hub editor install workflow maps only populated verified channels")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    auto editor = Package("keire.editor", "1.2.0", PackageKind::Editor, 20);
    auto support = Package("keire.support.windows", "1.0.0", PackageKind::BuildSupport, 8);

    HubEditorInstallWorkflow workflow(registry, "windows", "x86_64");
    HubSettings settings;
    REQUIRE(workflow.Refresh({.Failure = HubError{.Code = HubErrorCode::CatalogTransportFailed,
                                                  .Message = "Editor discovery is temporarily unavailable."}},
                             settings));
    HubProductSnapshot product;
    workflow.ApplySnapshot(product);
    CHECK(product.EditorCatalogMessage == "Editor discovery is temporarily unavailable.");
    REQUIRE(workflow.Refresh({.Failure = HubError{.Code = HubErrorCode::CatalogExpired,
                                                  .Message = "The cached editor catalog has expired."}},
                             settings));
    workflow.ApplySnapshot(product);
    CHECK(product.EditorCatalogMessage == "The cached editor catalog has expired.");

    const HubDistributionWorkflowSnapshot distribution{.Catalogs = Distribution({editor, support}),
                                                       .ServiceBaseUrl = "https://downloads.example.test"};
    REQUIRE(workflow.Refresh(distribution, settings));

    workflow.ApplySnapshot(product);
    CHECK(product.PopulatedEditorChannels == std::vector<std::string>{"stable"});
    REQUIRE(product.AvailableEditors);
    REQUIRE(product.AvailableEditors->size() == 1U);
    CHECK(product.AvailableEditors->front().PackageId == editor.Id);
    CHECK(product.AvailableEditors->front().DownloadBytes == 20U);
    REQUIRE(product.AvailableEditors->front().Components.size() == 1U);
    CHECK(product.AvailableEditors->front().Components.front().PackageId == support.Id);
    CHECK(workflow.EndpointContext().ServiceBaseUrl == "https://downloads.example.test");
    CHECK_FALSE(workflow.EndpointContext().AllowInsecureLoopbackDevelopment);

    REQUIRE(registry.Upsert({.Id = "located-editor",
                             .Version = "1.2.0",
                             .Channel = "stable",
                             .Platform = "windows",
                             .Architecture = "x86_64",
                             .Root = temporary.Path() / "Located",
                             .Ownership = InstallationOwnership::External,
                             .ManifestFingerprint = KeireHubTests::Digest('c'),
                             .Entrypoints = {"KeireClient.exe"},
                             .EditorEntrypoint = "KeireClient.exe",
                             .MinimumProjectSchema = 1,
                             .MaximumProjectSchema = 3,
                             .InstalledSizeBytes = 40,
                             .Health = InstallationHealth::Healthy}));
    REQUIRE(workflow.Refresh(distribution, settings));
    workflow.ApplySnapshot(product);
    CHECK(product.AvailableEditors->front().InstalledInstallationIds == std::vector<std::string>{"located-editor"});
}

TEST_CASE("Hub editor install workflow publishes an exact dependency and disk preview")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    auto editor = Package("keire.editor", "1.2.0", PackageKind::Editor, 20);
    auto support = Package("keire.support.windows", "1.0.0", PackageKind::BuildSupport, 8);

    HubEditorInstallWorkflow workflow(registry, "windows", "x86_64");
    HubSettings settings;
    REQUIRE(workflow.Refresh({.Catalogs = Distribution({editor, support}),
                              .ServiceBaseUrl = "http://127.0.0.1:5080",
                              .AllowInsecureLoopbackDevelopment = true},
                             settings));
    auto preview = workflow.PreviewInstall({.Destination = temporary.Path() / "Editors" / "1.2.0",
                                            .EditorPackageId = editor.Id,
                                            .EditorVersion = "1.2.0",
                                            .Components = {{support.Id, "1.0.0"}}});
    REQUIRE(preview);
    CHECK(preview.Value().DownloadSizeBytes == 28U);
    CHECK(preview.Value().RequiredDiskBytes == 56U);
    CHECK(preview.Value().InstallationId.starts_with("managed-editor-"));

    HubProductSnapshot product;
    workflow.ApplySnapshot(product);
    REQUIRE(product.EditorInstallPreview);
    CHECK(product.EditorInstallPreview->DownloadBytes == 28U);
    CHECK(product.EditorInstallPreview->RequiredDiskBytes == 56U);
    CHECK(product.EditorInstallPreview->Steps.size() == 2U);
    CHECK(product.EditorInstallPreview->Request.Components ==
          std::vector<HubEditorComponentSelectionUiRecord>{{support.Id, "1.0.0"}});
    CHECK(workflow.EndpointContext().AllowInsecureLoopbackDevelopment);

    const auto conflictRoot = temporary.Path() / "Editors" / "Existing";
    std::filesystem::create_directories(conflictRoot);
    const auto conflict =
        workflow.PreviewInstall({.Destination = conflictRoot, .EditorPackageId = editor.Id, .EditorVersion = "1.2.0"});
    REQUIRE_FALSE(conflict);
    CHECK(conflict.Error().Code == HubErrorCode::DestinationConflict);
    workflow.ApplySnapshot(product);
    CHECK_FALSE(product.EditorInstallPreview);
    CHECK(product.EditorInstallPreviewMessage == "The selected editor destination already exists.");
}

TEST_CASE("Hub editor install workflow rejects plans larger than the worker protocol bound")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    auto editor = Package("keire.editor", "1.2.0", PackageKind::Editor, 20);
    std::vector<PackageManifest> packages;
    for (std::size_t index = 0; index < MaximumHubWorkerInstallPackageSteps; ++index)
    {
        auto dependency = Package("keire.toolchain." + std::to_string(index), "1.0.0", PackageKind::Toolchain, 1);
        editor.Dependencies.push_back({dependency.Id, Constraint("=1.0.0")});
        packages.push_back(std::move(dependency));
    }
    packages.push_back(editor);

    HubEditorInstallWorkflow workflow(registry, "windows", "x86_64");
    HubSettings settings;
    REQUIRE(workflow.Refresh(
        {.Catalogs = Distribution(std::move(packages)), .ServiceBaseUrl = "https://downloads.example.test"}, settings));
    const auto preview = workflow.PreviewInstall({.Destination = temporary.Path() / "Editors" / "1.2.0",
                                                  .EditorPackageId = editor.Id,
                                                  .EditorVersion = "1.2.0"});
    REQUIRE_FALSE(preview);
    CHECK(preview.Error().Code == HubErrorCode::PackageManifestInvalid);
    CHECK(preview.Error().Message == "The editor install requires too many package steps.");
}
