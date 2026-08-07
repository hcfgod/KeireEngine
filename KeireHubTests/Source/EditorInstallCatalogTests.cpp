#include "TestSupport.h"

#include "KeireHubRuntime/EditorInstallCatalog.h"

#include "DistributionEncoding.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    constexpr std::string_view CatalogKeyId = "ed25519-00000000000000000000000000000000";

    [[nodiscard]] SemanticVersion Version(const std::string_view value)
    {
        auto parsed = SemanticVersion::Parse(value);
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] VersionConstraint Constraint(const std::string_view value)
    {
        auto parsed = VersionConstraint::Parse(value);
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] PackageManifest Package(std::string id, const std::string_view version, const PackageKind kind,
                                          const std::string_view channel = "stable", const std::uint64_t size = 10)
    {
        auto file = id + '-' + std::string(version) + ".bin";
        return {.Id = std::move(id),
                .Version = Version(version),
                .Kind = kind,
                .DisplayName = file,
                .Channel = std::string(channel),
                .Platform = "windows",
                .Architecture = "x86_64",
                .ArtifactSizeBytes = size,
                .ArtifactSha256 = KeireHubTests::Digest('a'),
                .InstalledSizeBytes = size,
                .Files = {{std::move(file), size, KeireHubTests::Digest('b')}},
                .SignatureKeyId = "release-key"};
    }

    [[nodiscard]] DistributionPackageCatalogSnapshot Catalog(std::string channel, std::vector<PackageManifest> packages,
                                                             const std::uint64_t sequence = 7)
    {
        auto catalog = std::make_shared<DistributionPackageCatalog>();
        catalog->Identity = {.KeyId = std::string(CatalogKeyId),
                             .Sequence = sequence,
                             .ExpiresAt = "2035-01-01T00:00:00Z",
                             .Channel = channel,
                             .Platform = "windows",
                             .Architecture = "x86_64"};
        catalog->Packages = std::move(packages);
        return {.Channel = std::move(channel),
                .Catalog = std::shared_ptr<const DistributionPackageCatalog>(std::move(catalog)),
                .Status = {.State = DistributionCatalogSourceState::Online,
                           .Sequence = sequence,
                           .KeyId = std::string(CatalogKeyId),
                           .ExpiresAt = "2035-01-01T00:00:00Z"}};
    }

    [[nodiscard]] std::shared_ptr<const DistributionCatalogSnapshot>
    Distribution(std::vector<DistributionPackageCatalogSnapshot> catalogs)
    {
        auto result = std::make_shared<DistributionCatalogSnapshot>();
        result->OnlineDiscoveryEnabled = true;
        result->PackageCatalogs = std::move(catalogs);
        return std::shared_ptr<const DistributionCatalogSnapshot>(std::move(result));
    }

    [[nodiscard]] EditorInstallation InstalledEditor(const std::filesystem::path& root,
                                                     const std::string_view id = "installed-editor")
    {
        return {.Id = std::string(id),
                .Version = "1.2.0+external",
                .Channel = "stable",
                .Platform = "windows",
                .Architecture = "x86_64",
                .Root = root,
                .Ownership = InstallationOwnership::External,
                .ManifestFingerprint = KeireHubTests::Digest('c'),
                .Entrypoints = {"KeireClient.exe"},
                .MinimumProjectSchema = 1,
                .MaximumProjectSchema = 3,
                .InstalledSizeBytes = 30,
                .Health = InstallationHealth::Healthy};
    }

    [[nodiscard]] EditorInstallCatalogSpecification Specification(const bool preview = false,
                                                                  const bool nightly = false)
    {
        return {.HostPlatform = "windows",
                .HostArchitecture = "x86_64",
                .EnablePreReleaseChannel = preview,
                .EnableNightlyChannel = nightly};
    }

    [[nodiscard]] std::vector<std::string> StepIds(const EditorInstallPlan& plan)
    {
        std::vector<std::string> result;
        for (const auto& step : plan.Steps)
            result.push_back(step.Manifest.Id);
        return result;
    }

    [[nodiscard]] InstalledPackageRecord Installed(const PackageManifest& package)
    {
        return {.Id = package.Id,
                .Version = package.Version,
                .Kind = package.Kind,
                .ArtifactSizeBytes = package.ArtifactSizeBytes,
                .ArtifactSha256 = package.ArtifactSha256,
                .InstalledSizeBytes = package.InstalledSizeBytes,
                .Dependencies = package.Dependencies,
                .Files = package.Files,
                .LicenseReferences = package.LicenseReferences};
    }
} // namespace

TEST_CASE("Editor install catalogs expose stable editors and compatible components by default")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    REQUIRE(registry.Upsert(InstalledEditor(temporary.Path() / "Existing")));

    auto runtime = Package("keire.toolchain.dotnet", "1.0.0", PackageKind::Toolchain);
    auto editor = Package("keire.editor", "1.2.0", PackageKind::Editor);
    editor.Dependencies.push_back({runtime.Id, Constraint("=1.0.0")});
    auto windowsSupport = Package("keire.support.windows", "1.0.0", PackageKind::BuildSupport);
    windowsSupport.EngineCompatibility = Constraint("^1.0.0");
    auto incompatible = Package("keire.support.future", "2.0.0", PackageKind::BuildSupport);
    incompatible.EngineCompatibility = Constraint("^2.0.0");
    auto previewEditor = Package("keire.editor", "2.0.0-beta.1", PackageKind::Editor, "preview");

    const auto distribution = Distribution(
        {Catalog("stable", {editor, runtime, windowsSupport, incompatible}), Catalog("preview", {previewEditor}, 8)});
    EditorInstallCatalog stable(registry, Specification());
    REQUIRE(stable.Refresh(distribution));
    const auto snapshot = stable.Snapshot();
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*snapshot)>>);
    REQUIRE(snapshot);
    REQUIRE(snapshot->AvailableEditors.size() == 1U);
    CHECK(snapshot->PopulatedChannels == std::vector<std::string>{"stable"});
    const auto& available = snapshot->AvailableEditors.front();
    CHECK(available.PackageId == "keire.editor");
    CHECK(available.Version == "1.2.0");
    CHECK(available.InstalledInstallationIds == std::vector<std::string>{"installed-editor"});
    CHECK_FALSE(available.AvailabilityError.has_value());
    REQUIRE(available.Components.size() == 2U);
    const auto required = std::ranges::find(available.Components, runtime.Id, &AvailableEditorComponent::PackageId);
    REQUIRE(required != available.Components.end());
    CHECK(required->RequiredByEditor);
    CHECK(required->RequiredByPackageIds == std::vector<std::string>{"keire.editor"});
    CHECK(std::ranges::find(available.Components, incompatible.Id, &AvailableEditorComponent::PackageId) ==
          available.Components.end());
    CHECK(snapshot->InstalledEditors == registry.Snapshot());

    EditorInstallCatalog optedIn(registry, Specification(true));
    REQUIRE(optedIn.Refresh(distribution));
    CHECK(optedIn.Snapshot()->AvailableEditors.size() == 2U);
    CHECK(optedIn.Snapshot()->PopulatedChannels == std::vector<std::string>{"stable", "preview"});
}

TEST_CASE("Editor install catalog refresh rejects invalid bounded sources without replacing its snapshot")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    EditorInstallCatalog catalog(registry, Specification());
    auto editor = Package("keire.editor", "1.0.0", PackageKind::Editor);
    REQUIRE(catalog.Refresh(Distribution({Catalog("stable", {editor})})));
    const auto before = catalog.Snapshot();

    const auto duplicateChannels = catalog.Refresh(Distribution(
        {Catalog("stable", {editor}), Catalog("stable", {Package("other.editor", "1.0.0", PackageKind::Editor)}, 8)}));
    REQUIRE_FALSE(duplicateChannels);
    CHECK(duplicateChannels.Error().Code == HubErrorCode::InvalidData);
    CHECK(catalog.Snapshot() == before);

    auto mismatched = Catalog("stable", {editor});
    auto mutableCatalog = std::make_shared<DistributionPackageCatalog>(*mismatched.Catalog);
    mutableCatalog->Identity.Platform = "linux";
    mismatched.Catalog = std::shared_ptr<const DistributionPackageCatalog>(std::move(mutableCatalog));
    const auto wrongHost = catalog.Refresh(Distribution({std::move(mismatched)}));
    REQUIRE_FALSE(wrongHost);
    CHECK(wrongHost.Error().Code == HubErrorCode::CatalogIdentityMismatch);
    CHECK(catalog.Snapshot() == before);

    auto unverified = Catalog("stable", {editor});
    unverified.Status = {};
    const auto wrongSourceState = catalog.Refresh(Distribution({std::move(unverified)}));
    REQUIRE_FALSE(wrongSourceState);
    CHECK(wrongSourceState.Error().Code == HubErrorCode::CatalogIdentityMismatch);
    CHECK(catalog.Snapshot() == before);

    const auto excessive = catalog.Refresh(
        Distribution({Catalog("stable", {}), Catalog("preview", {}), Catalog("nightly", {}), Catalog("extra", {})}));
    REQUIRE_FALSE(excessive);
    CHECK(excessive.Error().Code == HubErrorCode::InvalidArgument);
    CHECK(catalog.Snapshot() == before);
}

TEST_CASE("Editor install previews produce deterministic dependency closure with provenance")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    auto core = Package("keire.core", "1.0.0", PackageKind::Toolchain, "stable", 5);
    auto editor = Package("keire.editor", "1.2.0", PackageKind::Editor, "stable", 20);
    editor.Dependencies.push_back({core.Id, Constraint("=1.0.0")});
    auto toolchain = Package("keire.toolchain.compiler", "1.0.0", PackageKind::Toolchain, "stable", 7);
    auto support = Package("keire.support.windows", "1.0.0", PackageKind::BuildSupport, "stable", 11);
    support.EngineCompatibility = Constraint("^1.0.0");
    support.Dependencies.push_back({toolchain.Id, Constraint("=1.0.0")});
    auto documentation = Package("keire.toolchain.docs", "1.0.0", PackageKind::Toolchain, "stable", 3);

    EditorInstallCatalog catalog(registry, Specification());
    REQUIRE(catalog.Refresh(Distribution({Catalog("stable", {support, editor, documentation, toolchain, core}, 42)})));
    const EditorInstallPreviewRequest request{.InstallationId = "editor-managed-1",
                                              .Destination = temporary.Path() / "Editors" / "1.2.0",
                                              .EditorPackageId = editor.Id,
                                              .EditorVersion = "1.2.0",
                                              .Components = {{support.Id, "1.0.0"}, {documentation.Id, "1.0.0"}},
                                              .AvailableDiskBytes = 100};
    auto plan = catalog.PreviewInstall(request);
    REQUIRE(plan);
    CHECK(plan.Value().DownloadSizeBytes == 46U);
    CHECK(plan.Value().RequiredDiskBytes == 46U);
    CHECK(plan.Value().Steps.size() == 5U);
    CHECK(plan.Value().SelectedComponents[0].PackageId == support.Id);
    CHECK(plan.Value().SelectedComponents[1].PackageId == documentation.Id);
    for (const auto& step : plan.Value().Steps)
    {
        CHECK(step.CatalogKeyId == CatalogKeyId);
        CHECK(step.CatalogSequence == 42U);
    }
    const auto coreStep =
        std::ranges::find_if(plan.Value().Steps, [&](const auto& step) { return step.Manifest.Id == core.Id; });
    const auto editorStep =
        std::ranges::find_if(plan.Value().Steps, [&](const auto& step) { return step.Manifest.Id == editor.Id; });
    const auto toolchainStep =
        std::ranges::find_if(plan.Value().Steps, [&](const auto& step) { return step.Manifest.Id == toolchain.Id; });
    const auto supportStep =
        std::ranges::find_if(plan.Value().Steps, [&](const auto& step) { return step.Manifest.Id == support.Id; });
    REQUIRE(coreStep != plan.Value().Steps.end());
    REQUIRE(editorStep != plan.Value().Steps.end());
    REQUIRE(toolchainStep != plan.Value().Steps.end());
    REQUIRE(supportStep != plan.Value().Steps.end());
    CHECK(coreStep < editorStep);
    CHECK(toolchainStep < supportStep);
    CHECK(coreStep->RequiredByPackageIds == std::vector<std::string>{editor.Id});
    CHECK(toolchainStep->RequiredByPackageIds == std::vector<std::string>{support.Id});
    CHECK_FALSE(coreStep->ExplicitlySelected);
    CHECK(editorStep->ExplicitlySelected);
    CHECK(supportStep->ExplicitlySelected);

    auto reversed = request;
    std::ranges::reverse(reversed.Components);
    const auto second = catalog.PreviewInstall(reversed);
    REQUIRE(second);
    CHECK(StepIds(second.Value()) == StepIds(plan.Value()));
    CHECK(second.Value().SelectedComponents[0].PackageId == support.Id);
}

TEST_CASE("Editor install previews report compatibility disk and registry conflicts")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    REQUIRE(registry.Upsert(InstalledEditor(temporary.Path() / "Existing")));
    auto editor = Package("keire.editor", "1.2.0", PackageKind::Editor, "stable", 20);
    auto incompatible = Package("keire.support.future", "2.0.0", PackageKind::BuildSupport, "stable", 10);
    incompatible.EngineCompatibility = Constraint("^2.0.0");
    EditorInstallCatalog catalog(registry, Specification());
    REQUIRE(catalog.Refresh(Distribution({Catalog("stable", {editor, incompatible})})));

    EditorInstallPreviewRequest request{.InstallationId = "new-editor",
                                        .Destination = temporary.Path() / "NewEditor",
                                        .EditorPackageId = editor.Id,
                                        .EditorVersion = "1.2.0",
                                        .AvailableDiskBytes = 100};
    request.Components = {{incompatible.Id, "2.0.0"}};
    const auto compatibility = catalog.PreviewInstall(request);
    REQUIRE_FALSE(compatibility);
    CHECK(compatibility.Error().Code == HubErrorCode::PackageHostIncompatible);

    request.Components.clear();
    request.AvailableDiskBytes = 19;
    const auto disk = catalog.PreviewInstall(request);
    REQUIRE_FALSE(disk);
    CHECK(disk.Error().Code == HubErrorCode::InsufficientDiskSpace);

    request.AvailableDiskBytes = 100;
    request.InstallationId = "installed-editor";
    const auto duplicateId = catalog.PreviewInstall(request);
    REQUIRE_FALSE(duplicateId);
    CHECK(duplicateId.Error().Code == HubErrorCode::DuplicateIdentifier);

    request.InstallationId = "new-editor";
    request.Destination = temporary.Path() / "Existing";
    const auto duplicateRoot = catalog.PreviewInstall(request);
    REQUIRE_FALSE(duplicateRoot);
    CHECK(duplicateRoot.Error().Code == HubErrorCode::DestinationConflict);
}

TEST_CASE("Editor install catalog records dependency failures without hiding the published version")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    auto editor = Package("keire.editor", "1.2.0", PackageKind::Editor);
    editor.Dependencies.push_back({"keire.missing", Constraint("*")});
    EditorInstallCatalog catalog(registry, Specification());
    REQUIRE(catalog.Refresh(Distribution({Catalog("stable", {editor})})));
    REQUIRE(catalog.Snapshot()->AvailableEditors.size() == 1U);
    REQUIRE(catalog.Snapshot()->AvailableEditors.front().AvailabilityError.has_value());
    CHECK(catalog.Snapshot()->AvailableEditors.front().AvailabilityError->Code ==
          HubErrorCode::PackageMissingDependency);

    const auto preview = catalog.PreviewInstall({.InstallationId = "new-editor",
                                                 .Destination = temporary.Path() / "Editor",
                                                 .EditorPackageId = editor.Id,
                                                 .EditorVersion = "1.2.0",
                                                 .AvailableDiskBytes = 100});
    REQUIRE_FALSE(preview);
    CHECK(preview.Error().Code == HubErrorCode::PackageMissingDependency);
}

TEST_CASE("Editor repair previews require the exact receipt-bound signed package closure")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "ManagedEditor";
    std::filesystem::create_directories(root);
    auto runtime = Package("keire.toolchain.dotnet", "1.0.0", PackageKind::Toolchain, "stable", 5);
    auto editor = Package("keire.editor", "1.2.0", PackageKind::Editor, "stable", 20);
    editor.Dependencies.push_back({runtime.Id, Constraint("=1.0.0")});
    const auto fingerprint = KeireHubTests::Digest('c');
    const auto treeIdentity = KeireHubTests::Digest('d');
    PackageInstallReceipt receipt{.AggregateIdentitySha256 = treeIdentity,
                                  .AggregateInstalledSizeBytes = 25,
                                  .Packages = {Installed(editor), Installed(runtime)}};
    const auto encodedReceipt = EncodePackageInstallReceipt(receipt);
    REQUIRE(encodedReceipt);
    const auto receiptSha = Detail::Sha256Hex(std::as_bytes(std::span(encodedReceipt.Value())));
    const auto nonce = std::string(64, 'f');
    REQUIRE(EditorInstallationRegistry::WriteManagedMarker(root, {.InstallationId = "managed-repair",
                                                                  .ManifestFingerprint = fingerprint,
                                                                  .Nonce = nonce,
                                                                  .ReceiptSha256 = receiptSha}));

    EditorInstallation installation{.Id = "managed-repair",
                                    .Version = "1.2.0",
                                    .Channel = "stable",
                                    .Platform = "windows",
                                    .Architecture = "x86_64",
                                    .Root = root,
                                    .Ownership = InstallationOwnership::Managed,
                                    .ManifestFingerprint = fingerprint,
                                    .PackageTreeIdentity = treeIdentity,
                                    .PackageReceiptSha256 = receiptSha,
                                    .MarkerNonce = nonce,
                                    .InstalledPackages = receipt.Packages,
                                    .Entrypoints = {"KeireClient.exe"},
                                    .EditorEntrypoint = "KeireClient.exe",
                                    .InstalledSizeBytes = 25,
                                    .Health = InstallationHealth::Damaged};
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    REQUIRE(registry.Upsert(installation));
    EditorInstallCatalog catalog(registry, Specification());
    REQUIRE(catalog.Refresh(Distribution({Catalog("stable", {editor, runtime}, 42)})));

    const EditorRepairPreviewRequest request{.InstallationId = installation.Id,
                                             .Destination = root,
                                             .ManifestFingerprint = fingerprint,
                                             .PackageTreeIdentity = treeIdentity,
                                             .PackageReceiptSha256 = receiptSha,
                                             .MarkerNonce = nonce,
                                             .AvailableDiskBytes = 100};
    const auto repair = catalog.PreviewRepair(request);
    REQUIRE(repair);
    CHECK(repair.Value().Install.InstallationId == installation.Id);
    CHECK(repair.Value().Install.Destination == root);
    CHECK(StepIds(repair.Value().Install) == std::vector<std::string>{runtime.Id, editor.Id});
    CHECK(repair.Value().MarkerNonce == nonce);

    const auto ordinaryInstall = catalog.PreviewInstall({.InstallationId = installation.Id,
                                                         .Destination = root,
                                                         .EditorPackageId = editor.Id,
                                                         .EditorVersion = editor.Version.ToString(),
                                                         .AvailableDiskBytes = 100});
    REQUIRE_FALSE(ordinaryInstall);
    CHECK(ordinaryInstall.Error().Code == HubErrorCode::DuplicateIdentifier);

    auto changedEditor = editor;
    changedEditor.ArtifactSha256 = KeireHubTests::Digest('9');
    EditorInstallCatalog changed(registry, Specification());
    REQUIRE(changed.Refresh(Distribution({Catalog("stable", {changedEditor, runtime}, 43)})));
    const auto refused = changed.PreviewRepair(request);
    REQUIRE_FALSE(refused);
    CHECK(refused.Error().Code == HubErrorCode::PackageManifestInvalid);
}
