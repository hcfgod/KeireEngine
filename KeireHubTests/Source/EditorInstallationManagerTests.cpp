#include "TestSupport.h"

#include "KeireHub/HubEditorDiscovery.h"

#include "KeireHubRuntime/EditorInstallationManager.h"

#include "Keire/BuildInfo.h"

#include "DistributionEncoding.h"
#include "Sha256.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace KeireHub;

namespace
{
    constexpr std::string_view EmptySha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    struct ManifestWriteResult final
    {
        std::string Fingerprint;
        std::uint64_t InstalledSizeBytes = 0;
    };

    [[nodiscard]] nlohmann::json EditorManifest(const std::string& platform = "Windows",
                                                const std::string& architecture = "x86_64")
    {
        return {{"schemaVersion", 2},
                {"artifact", "editor"},
                {"packageId", "keire.editor"},
                {"version", "2.1.0"},
                {"channel", "Stable"},
                {"platform", platform},
                {"architecture", architecture},
                {"entrypoints", {{"editor", "bin/Editor"}}},
                {"projectSchema", {{"minimum", 1}, {"maximum", 3}}},
                {"inventoryExcludes", {"editor-package.json"}},
                {"files", {{{"path", "bin/Editor"}, {"sizeBytes", 0}, {"sha256", std::string(EmptySha256)}}}},
                {"installedSizeBytes", 1},
                {"manifestFingerprint", std::string(64, '0')}};
    }

    [[nodiscard]] ManifestWriteResult WriteManifest(const std::filesystem::path& root, nlohmann::json& manifest)
    {
        auto fingerprint = ComputeEditorPackageManifestFingerprint(manifest.dump());
        if (!fingerprint)
            throw std::runtime_error(fingerprint.Error().Message);
        manifest["manifestFingerprint"] = fingerprint.Value();
        std::string bytes;
        for (std::size_t iteration = 0; iteration < 16; ++iteration)
        {
            bytes = manifest.dump(2) + '\n';
            if (manifest["installedSizeBytes"].get<std::uint64_t>() == bytes.size())
                break;
            manifest["installedSizeBytes"] = bytes.size();
        }
        bytes = manifest.dump(2) + '\n';
        if (manifest["installedSizeBytes"].get<std::uint64_t>() != bytes.size())
            throw std::runtime_error("The test editor manifest size did not converge.");
        KeireHubTests::WriteText(root / "editor-package.json", bytes);
        return {.Fingerprint = std::move(fingerprint).Value(), .InstalledSizeBytes = bytes.size()};
    }

    [[nodiscard]] EditorInstallation CreateInstallation(const std::filesystem::path& root, const std::string& id,
                                                        const InstallationOwnership ownership,
                                                        nlohmann::json manifest = EditorManifest())
    {
        std::filesystem::create_directories(root / "bin");
        KeireHubTests::WriteText(root / "bin/Editor", {});
        const auto written = WriteManifest(root, manifest);
        EditorInstallation installation{.Id = id,
                                        .Version = "2.1.0",
                                        .Channel = "stable",
                                        .Platform = "windows",
                                        .Architecture = "x86_64",
                                        .Root = root,
                                        .Ownership = ownership,
                                        .ManifestFingerprint = written.Fingerprint,
                                        .Entrypoints = {"bin/Editor"},
                                        .MinimumProjectSchema = 1,
                                        .MaximumProjectSchema = 3,
                                        .InstalledSizeBytes = written.InstalledSizeBytes};
        if (ownership == InstallationOwnership::Managed)
        {
            installation.MarkerNonce = std::string(32, 'a');
            if (!EditorInstallationRegistry::WriteManagedMarker(root, {.InstallationId = id,
                                                                       .ManifestFingerprint = written.Fingerprint,
                                                                       .Nonce = installation.MarkerNonce}))
            {
                throw std::runtime_error("Could not write the test managed-install marker.");
            }
        }
        return installation;
    }

    [[nodiscard]] EditorInstallation CreateReceiptBoundManagedInstallation(const std::filesystem::path& root,
                                                                           const std::string& id)
    {
        auto installation = CreateInstallation(root, id, InstallationOwnership::External);
        KeireHubTests::WriteText(root / "modules/windows.bin", "component");
        const auto manifestSize = std::filesystem::file_size(root / "editor-package.json");
        const auto manifestDigest = Detail::Sha256File(root / "editor-package.json", manifestSize);
        const auto componentDigest = Detail::Sha256File(root / "modules/windows.bin", 9U);
        if (!manifestDigest || !componentDigest)
            throw std::runtime_error("Could not hash the test editor installation.");

        PackageInstallReceipt receipt{
            .AggregateIdentitySha256 = KeireHubTests::Digest('7'),
            .AggregateInstalledSizeBytes = manifestSize + 9U,
            .Packages = {
                {.Id = "keire.editor",
                 .Version = SemanticVersion::Parse("2.1.0").Value(),
                 .Kind = PackageKind::Editor,
                 .ArtifactSizeBytes = 100,
                 .ArtifactSha256 = KeireHubTests::Digest('8'),
                 .InstalledSizeBytes = manifestSize,
                 .Files = {{.Path = "bin/Editor", .SizeBytes = 0, .Sha256 = std::string(EmptySha256), .Mode = 0644U},
                           {.Path = "editor-package.json",
                            .SizeBytes = manifestSize,
                            .Sha256 = manifestDigest.Value(),
                            .Mode = 0644U}}},
                {.Id = "keire.windows",
                 .Version = SemanticVersion::Parse("2.1.0").Value(),
                 .Kind = PackageKind::BuildSupport,
                 .ArtifactSizeBytes = 50,
                 .ArtifactSha256 = KeireHubTests::Digest('9'),
                 .InstalledSizeBytes = 9,
                 .Files = {{.Path = "modules/windows.bin",
                            .SizeBytes = 9,
                            .Sha256 = componentDigest.Value(),
                            .Mode = 0644U}}}}};
        auto receiptDocument = EncodePackageInstallReceipt(receipt);
        if (!receiptDocument)
            throw std::runtime_error(receiptDocument.Error().Message);
        KeireHubTests::WriteText(root / PackageInstallReceiptFileName, receiptDocument.Value());
        receipt.DocumentSha256 = Detail::Sha256Hex(std::as_bytes(std::span(receiptDocument.Value())));

        installation.Ownership = InstallationOwnership::Managed;
        installation.PackageTreeIdentity = receipt.AggregateIdentitySha256;
        installation.PackageReceiptSha256 = receipt.DocumentSha256;
        installation.MarkerNonce = std::string(32, 'a');
        installation.InstalledPackages = receipt.Packages;
        installation.InstalledSizeBytes = receipt.AggregateInstalledSizeBytes;
        const auto marker = EditorInstallationRegistry::WriteManagedMarker(
            root, {.InstallationId = installation.Id,
                   .ManifestFingerprint = installation.ManifestFingerprint,
                   .Nonce = installation.MarkerNonce,
                   .ReceiptSha256 = receipt.DocumentSha256});
        if (!marker)
            throw std::runtime_error(marker.Error().Message);
        return installation;
    }

    [[nodiscard]] bool HasIssue(const EditorInstallationHealthSnapshot& snapshot,
                                const EditorInstallationIssueCode code)
    {
        return std::ranges::any_of(snapshot.Issues, [code](const auto& issue) { return issue.Code == code; });
    }

    [[nodiscard]] EditorInstallationManager
    MakeManager(EditorInstallationRegistry& registry, EditorInstallationManagerSpecification::ActivityProbe probe = {},
                EditorInstallationManagerSpecification::EntrypointActivityProbe entrypointProbe = {})
    {
        if (!entrypointProbe)
        {
            entrypointProbe = [](const std::filesystem::path&) { return EditorEntrypointActivity::NotRunning; };
        }
        return EditorInstallationManager(registry, {.HostPlatform = "windows",
                                                    .HostArchitecture = "x86_64",
                                                    .ProbeActivity = std::move(probe),
                                                    .ProbeEntrypointActivity = std::move(entrypointProbe)});
    }
} // namespace

TEST_CASE("External editor package roots normalize folders bin selections and macOS application bundles")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto direct = temporary.Path() / "Direct";
    const auto bundle = temporary.Path() / "Kéire Editor.app";
    const auto bundlePayload = bundle / "Contents/Resources/Editor";
    KeireHubTests::WriteText(direct / "editor-package.json", "{}\n");
    KeireHubTests::WriteText(bundlePayload / "editor-package.json", "{}\n");

    const auto directResult = ResolveExternalEditorPackageRoot(direct);
    REQUIRE(directResult);
    CHECK(directResult.Value() == std::filesystem::weakly_canonical(direct));

    const auto binResult = ResolveExternalEditorPackageRoot(direct / "bin");
    REQUIRE(binResult);
    CHECK(binResult.Value() == std::filesystem::weakly_canonical(direct));

    const auto bundleResult = ResolveExternalEditorPackageRoot(bundle);
    REQUIRE(bundleResult);
    CHECK(bundleResult.Value() == std::filesystem::weakly_canonical(bundlePayload));

    const auto invalidBundle = ResolveExternalEditorPackageRoot(temporary.Path() / "Empty.app");
    REQUIRE_FALSE(invalidBundle);
    CHECK(invalidBundle.Error().Code == HubErrorCode::InvalidData);
}

TEST_CASE("Editor installation manager verifies schema-2 manifests inventory and entrypoints")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    const auto root = temporary.Path() / "Editor";
    REQUIRE(registry.Upsert(CreateInstallation(root, "editor-a", InstallationOwnership::External)));
    auto manager = MakeManager(registry);
    REQUIRE(manager.Refresh());
    REQUIRE(manager.Snapshot()->size() == 1);
    const auto& snapshot = manager.Snapshot()->front();
    CHECK(snapshot.Health == InstallationHealth::Healthy);
    CHECK(snapshot.VerifiedFileCount == 1);
    CHECK(snapshot.VerifiedBytes == 0);
    CHECK(snapshot.Issues.empty());
}

TEST_CASE("Managed editor package registration derives identity from the verified package and marker")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Managed";
    std::filesystem::create_directories(root / "bin");
    KeireHubTests::WriteText(root / "bin/Editor", {});
    auto manifest = EditorManifest();
    manifest["bundledDotnetSdk"] = "10.0.302";
    const auto written = WriteManifest(root, manifest);

    auto inspected = PrepareManagedEditorPackage({.PackageRoot = root,
                                                  .InstallationRoot = root,
                                                  .InstallationId = "editor-managed",
                                                  .MarkerNonce = std::string(64, 'a'),
                                                  .HostPlatform = "windows",
                                                  .HostArchitecture = "x86_64",
                                                  .VerifiedUnixSeconds = 123});
    REQUIRE(inspected);
    CHECK(inspected.Value().ManifestFingerprint == written.Fingerprint);
    CHECK(inspected.Value().BundledDotnetSdk == "10.0.302");
    CHECK(inspected.Value().Ownership == InstallationOwnership::Managed);
    CHECK(std::filesystem::is_regular_file(root / EditorInstallationRegistry::MarkerFileName));

    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    auto registered = RegisterManagedEditorPackage(registry, root, "editor-managed", "windows", "x86_64", 124);
    REQUIRE(registered);
    REQUIRE(registry.Snapshot()->size() == 1);
    CHECK(registry.Snapshot()->front().MarkerNonce == std::string(64, 'a'));
    CHECK(registry.Snapshot()->front().LastVerifiedUnixSeconds == 124);
}

TEST_CASE("Managed editor health verifies receipt-bound component inventories")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "ManagedReceipt";
    auto installation = CreateReceiptBoundManagedInstallation(root, "editor-receipt");

    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    REQUIRE(registry.Upsert(installation));
    auto manager = MakeManager(registry);
    REQUIRE(manager.Refresh());
    REQUIRE(manager.Snapshot()->size() == 1U);
    CHECK(manager.Snapshot()->front().Health == InstallationHealth::Healthy);
    CHECK(manager.Snapshot()->front().VerifiedFileCount == 3U);
    CHECK(manager.Snapshot()->front().VerifiedBytes == installation.InstalledSizeBytes);

    const auto removal = manager.PrepareManagedRemoval(installation.Id, root);
    REQUIRE(removal);
    CHECK(removal.Value().PackageTreeIdentity == installation.PackageTreeIdentity);
    CHECK(removal.Value().PackageReceiptSha256 == installation.PackageReceiptSha256);

    KeireHubTests::WriteText(root / "modules/windows.bin", "COMPONENT");
    REQUIRE(manager.Refresh());
    const auto& damaged = manager.Snapshot()->front();
    CHECK(damaged.Health == InstallationHealth::Damaged);
    CHECK(HasIssue(damaged, EditorInstallationIssueCode::FileDigestMismatch));
    const auto unsafeRemoval = manager.PrepareManagedRemoval(installation.Id, root);
    REQUIRE_FALSE(unsafeRemoval);
    CHECK(unsafeRemoval.Error().Code == HubErrorCode::UnsafeInstallRoot);
    auto repair = manager.PrepareManagedRepair(installation.Id, root);
    REQUIRE(repair);
    CHECK(std::ranges::find(repair.Value().FilesToRestore, std::filesystem::path("modules/windows.bin")) !=
          repair.Value().FilesToRestore.end());
}

TEST_CASE("Missing and corrupt editor inventory produces immutable damaged health")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    auto missingInventory = EditorManifest();
    missingInventory.erase("files");
    auto corruptInventory = EditorManifest();
    corruptInventory["files"][0]["sha256"] = "not-a-digest";
    REQUIRE(registry.Upsert(CreateInstallation(temporary.Path() / "MissingInventory", "editor-missing-inventory",
                                               InstallationOwnership::External, std::move(missingInventory))));
    REQUIRE(registry.Upsert(CreateInstallation(temporary.Path() / "CorruptInventory", "editor-corrupt-inventory",
                                               InstallationOwnership::External, std::move(corruptInventory))));
    REQUIRE(registry.Upsert(
        CreateInstallation(temporary.Path() / "CorruptFile", "editor-corrupt", InstallationOwnership::External)));
    KeireHubTests::WriteText(temporary.Path() / "CorruptFile/bin/Editor", "corrupt");

    auto manager = MakeManager(registry);
    REQUIRE(manager.Refresh());
    const auto snapshots = manager.Snapshot();
    REQUIRE(snapshots->size() == 3);
    const auto missing = std::ranges::find_if(*snapshots, [](const auto& value)
                                              { return value.Installation.Id == "editor-missing-inventory"; });
    const auto corrupt =
        std::ranges::find_if(*snapshots, [](const auto& value) { return value.Installation.Id == "editor-corrupt"; });
    const auto invalid = std::ranges::find_if(*snapshots, [](const auto& value)
                                              { return value.Installation.Id == "editor-corrupt-inventory"; });
    REQUIRE(missing != snapshots->end());
    REQUIRE(corrupt != snapshots->end());
    REQUIRE(invalid != snapshots->end());
    CHECK(missing->Health == InstallationHealth::Damaged);
    CHECK(HasIssue(*missing, EditorInstallationIssueCode::InventoryInvalid));
    CHECK(invalid->Health == InstallationHealth::Damaged);
    CHECK(HasIssue(*invalid, EditorInstallationIssueCode::InventoryInvalid));
    CHECK(corrupt->Health == InstallationHealth::Damaged);
    CHECK(HasIssue(*corrupt, EditorInstallationIssueCode::FileSizeMismatch));
    CHECK(HasIssue(*corrupt, EditorInstallationIssueCode::MissingEntrypoint));
}

TEST_CASE("Editor health distinguishes host incompatibility from corrupt bytes")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    REQUIRE(
        registry.Upsert(CreateInstallation(temporary.Path() / "Editor", "editor-a", InstallationOwnership::External)));
    EditorInstallationManager manager(registry, {.HostPlatform = "linux", .HostArchitecture = "x86_64"});
    REQUIRE(manager.Refresh());
    REQUIRE(manager.Snapshot()->size() == 1);
    CHECK(manager.Snapshot()->front().Health == InstallationHealth::VerificationRequired);
    CHECK(HasIssue(manager.Snapshot()->front(), EditorInstallationIssueCode::HostIncompatible));
    const auto repair = manager.PrepareManagedRepair("editor-a", temporary.Path() / "Editor");
    REQUIRE_FALSE(repair);
    CHECK(repair.Error().Code == HubErrorCode::UnsafeInstallRoot);
}

TEST_CASE("External removal changes only the registry and never editor files")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    const auto externalRoot = temporary.Path() / "External";
    const auto managedRoot = temporary.Path() / "Managed";
    REQUIRE(registry.Upsert(CreateInstallation(externalRoot, "editor-external", InstallationOwnership::External)));
    REQUIRE(registry.Upsert(CreateInstallation(managedRoot, "editor-managed", InstallationOwnership::Managed)));
    auto manager = MakeManager(registry);
    REQUIRE(manager.Refresh());

    const auto managedRejected = manager.RemoveExternalRegistration("editor-managed", managedRoot);
    REQUIRE_FALSE(managedRejected);
    CHECK(managedRejected.Error().Code == HubErrorCode::UnsafeInstallRoot);
    REQUIRE(manager.RemoveExternalRegistration("editor-external", externalRoot));
    CHECK(std::filesystem::is_regular_file(externalRoot / "bin/Editor"));
    REQUIRE(registry.Snapshot()->size() == 1);
    CHECK(registry.Snapshot()->front().Id == "editor-managed");
}

TEST_CASE("Managed registration reconciliation requires exact identity proof and an absent root")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "ManagedReceipt";
    const auto installation = CreateReceiptBoundManagedInstallation(root, "editor-managed");
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    REQUIRE(registry.Upsert(installation));
    ManagedInstallRemovalProof proof{.InstallationId = installation.Id,
                                     .Root = installation.Root,
                                     .ManifestFingerprint = installation.ManifestFingerprint,
                                     .PackageTreeIdentity = installation.PackageTreeIdentity,
                                     .PackageReceiptSha256 = installation.PackageReceiptSha256,
                                     .MarkerNonce = installation.MarkerNonce};

    const auto rootPresent = registry.RemoveDeletedManagedRegistration(proof);
    REQUIRE_FALSE(rootPresent);
    CHECK(rootPresent.Error().Code == HubErrorCode::UnsafeInstallRoot);
    REQUIRE(registry.Snapshot()->size() == 1U);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    REQUIRE_FALSE(error);
    auto wrongProof = proof;
    wrongProof.PackageTreeIdentity = KeireHubTests::Digest('6');
    const auto mismatch = registry.RemoveDeletedManagedRegistration(wrongProof);
    REQUIRE_FALSE(mismatch);
    CHECK(mismatch.Error().Code == HubErrorCode::UnsafeInstallRoot);
    REQUIRE(registry.Snapshot()->size() == 1U);

    REQUIRE(registry.RemoveDeletedManagedRegistration(proof));
    CHECK(registry.Snapshot()->empty());
}

TEST_CASE("Managed repair plans list damaged files and require an exact marker")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    const auto root = temporary.Path() / "Managed";
    auto installation = CreateInstallation(root, "editor-managed", InstallationOwnership::Managed);
    REQUIRE(registry.Upsert(installation));
    std::filesystem::remove(root / "bin/Editor");
    auto manager = MakeManager(registry);

    auto repair = manager.PrepareManagedRepair("editor-managed", root);
    REQUIRE(repair);
    CHECK(repair.Value().Operation == EditorManagedOperation::Repair);
    CHECK(repair.Value().CurrentHealth == InstallationHealth::Damaged);
    CHECK(repair.Value().EditorEntrypoint == std::filesystem::path("bin/Editor"));
    CHECK_FALSE(repair.Value().RequiresCompletePackage);
    REQUIRE(repair.Value().FilesToRestore.size() == 1);
    CHECK(repair.Value().FilesToRestore.front() == std::filesystem::path("bin/Editor"));
    CHECK(manager.Revalidate(repair.Value()));

    KeireHubTests::WriteText(root / EditorInstallationRegistry::MarkerFileName,
                             R"({"schemaVersion":1,"installationId":"editor-managed","manifestFingerprint":")" +
                                 installation.ManifestFingerprint + R"(","nonce":")" + std::string(32, 'b') + "\"}");
    const auto before = registry.Snapshot();
    const auto stale = manager.Revalidate(repair.Value());
    REQUIRE_FALSE(stale);
    CHECK(stale.Error().Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(registry.Snapshot() == before);
}

TEST_CASE("Managed operations reject running editors and active tasks without state changes")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    const auto root = temporary.Path() / "Managed";
    REQUIRE(registry.Upsert(CreateInstallation(root, "editor-managed", InstallationOwnership::Managed)));
    EditorInstallationActivity activity{.Running = true};
    auto manager = MakeManager(registry, [&](const EditorInstallation&) { return activity; });
    const auto before = registry.Snapshot();

    const auto running = manager.PrepareManagedRemoval("editor-managed", root);
    REQUIRE_FALSE(running);
    CHECK(running.Error().Code == HubErrorCode::EditorRunning);
    CHECK(registry.Snapshot() == before);

    activity = {.HasActiveTask = true};
    const auto busy = manager.PrepareManagedRepair("editor-managed", root);
    REQUIRE_FALSE(busy);
    CHECK(busy.Error().Code == HubErrorCode::InstallationBusy);
    CHECK(registry.Snapshot() == before);

    activity = {};
    const auto removal = manager.PrepareManagedRemoval("editor-managed", root);
    REQUIRE_FALSE(removal);
    CHECK(removal.Error().Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(std::filesystem::is_directory(root));
    CHECK(registry.Snapshot() == before);
}

TEST_CASE("Managed package preparation can validate a completion marker without rewriting it")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Managed";
    const auto installation = CreateReceiptBoundManagedInstallation(root, "managed-preserve-marker");
    const auto changedFingerprint = KeireHubTests::Digest('e');
    auto markerDocument =
        nlohmann::json::parse(KeireHubTests::ReadText(root / EditorInstallationRegistry::MarkerFileName));
    markerDocument["manifestFingerprint"] = changedFingerprint;
    KeireHubTests::WriteText(root / EditorInstallationRegistry::MarkerFileName, markerDocument.dump());

    const auto prepared = PrepareManagedEditorPackage({.PackageRoot = root,
                                                       .InstallationRoot = root,
                                                       .InstallationId = installation.Id,
                                                       .MarkerNonce = installation.MarkerNonce,
                                                       .HostPlatform = installation.Platform,
                                                       .HostArchitecture = installation.Architecture,
                                                       .VerifiedUnixSeconds = 20,
                                                       .RequirePackageReceipt = true,
                                                       .PreserveExistingMarker = true});
    REQUIRE_FALSE(prepared);
    CHECK(prepared.Error().Code == HubErrorCode::UnsafeInstallRoot);
    const auto marker = EditorInstallationRegistry::ReadManagedMarker(root);
    REQUIRE(marker);
    CHECK(marker.Value().ManifestFingerprint == changedFingerprint);
    CHECK(marker.Value().Nonce == installation.MarkerNonce);
}

TEST_CASE("Editor activity combines tracked and operating-system entrypoint probes fail safely")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    const auto root = temporary.Path() / "Managed";
    REQUIRE(registry.Upsert(CreateInstallation(root, "editor-managed", InstallationOwnership::Managed)));

    EditorInstallationActivity tracked;
    auto entrypointState = EditorEntrypointActivity::NotRunning;
    std::filesystem::path probedEntrypoint;
    auto manager = MakeManager(
        registry, [&](const EditorInstallation&) { return tracked; },
        [&](const std::filesystem::path& executable)
        {
            probedEntrypoint = executable;
            return entrypointState;
        });

    REQUIRE(manager.Refresh());
    CHECK_FALSE(manager.Snapshot()->front().Activity.Running);
    CHECK(probedEntrypoint == (root / "bin/Editor").lexically_normal());

    tracked.Running = true;
    REQUIRE(manager.Refresh());
    CHECK(manager.Snapshot()->front().Activity.Running);

    tracked.Running = false;
    entrypointState = EditorEntrypointActivity::Running;
    REQUIRE(manager.Refresh());
    CHECK(manager.Snapshot()->front().Activity.Running);
    const auto externalRunning = manager.Verify("editor-managed");
    REQUIRE_FALSE(externalRunning);
    CHECK(externalRunning.Error().Code == HubErrorCode::EditorRunning);
    const auto repair = manager.PrepareManagedRepair("editor-managed", root);
    REQUIRE_FALSE(repair);
    CHECK(repair.Error().Code == HubErrorCode::EditorRunning);

    entrypointState = EditorEntrypointActivity::Indeterminate;
    const auto indeterminate = manager.Verify("editor-managed");
    REQUIRE_FALSE(indeterminate);
    CHECK(indeterminate.Error().Code == HubErrorCode::EditorRunning);

    entrypointState = EditorEntrypointActivity::NotRunning;
    tracked.HasActiveTask = true;
    const auto activeTask = manager.Verify("editor-managed");
    REQUIRE_FALSE(activeTask);
    CHECK(activeTask.Error().Code == HubErrorCode::InstallationBusy);

    tracked = {};
    const auto verified = manager.Verify("editor-managed");
    REQUIRE(verified);
    CHECK(verified.Value().Health == InstallationHealth::Healthy);
}

TEST_CASE("Editor entrypoint probe exceptions fail closed")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    const auto root = temporary.Path() / "Managed";
    REQUIRE(registry.Upsert(CreateInstallation(root, "editor-managed", InstallationOwnership::Managed)));
    auto manager = MakeManager(registry, {}, [](const std::filesystem::path&) -> EditorEntrypointActivity
                               { throw std::runtime_error("probe failed"); });

    const auto verified = manager.Verify("editor-managed");
    REQUIRE_FALSE(verified);
    CHECK(verified.Error().Code == HubErrorCode::EditorRunning);
}

TEST_CASE("Packaged editor discovery registers only a manifest-backed installation")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(123));

    const auto absent = RegisterPackagedEditorIfPresent(controller, temporary.Path() / "NoPackage");
    REQUIRE(absent);
    CHECK_FALSE(absent.Value());
    CHECK(controller.Installations().Snapshot()->empty());

    const auto invalidRoot = temporary.Path() / "Invalid";
    KeireHubTests::WriteText(invalidRoot / "editor-package.json", "{}\n");
    const auto invalid = RegisterPackagedEditorIfPresent(controller, invalidRoot);
    REQUIRE_FALSE(invalid);
    CHECK(controller.Installations().Snapshot()->empty());

    const auto root = temporary.Path() / "Editor";
    std::filesystem::create_directories(root / "bin");
    KeireHubTests::WriteText(root / "bin/Editor", {});
    KeireHubTests::WriteText(root / "bin/AssetTool", {});
    auto manifest =
        EditorManifest(std::string(Keire::GetBuildInfo().Platform), std::string(Keire::GetBuildInfo().Architecture));
    manifest["entrypoints"]["assetTool"] = "bin/AssetTool";
    manifest["files"].push_back({{"path", "bin/AssetTool"}, {"sizeBytes", 0}, {"sha256", std::string(EmptySha256)}});
    (void)WriteManifest(root, manifest);

    const auto registered = RegisterPackagedEditorIfPresent(controller, root);
    REQUIRE(registered);
    REQUIRE(registered.Value());
    CHECK(registered.Value()->Id.starts_with("packaged-"));
    CHECK(registered.Value()->Id != "bundled-current");
    REQUIRE(controller.Installations().Snapshot()->size() == 1);
    CHECK(controller.Installations().Snapshot()->front().Id == registered.Value()->Id);
    CHECK(controller.Installations().Snapshot()->front().Root == std::filesystem::weakly_canonical(root));

    const auto repeated = RegisterPackagedEditorIfPresent(controller, root);
    REQUIRE(repeated);
    REQUIRE(repeated.Value());
    CHECK(repeated.Value()->Id == registered.Value()->Id);
    CHECK(controller.Installations().Snapshot()->size() == 1);

    EditorInstallationManager manager(controller.Installations(),
                                      {.HostPlatform = std::string(Keire::GetBuildInfo().Platform),
                                       .HostArchitecture = std::string(Keire::GetBuildInfo().Architecture)});
    REQUIRE(manager.Refresh());
    REQUIRE(manager.Snapshot()->size() == 1);
    CHECK(manager.Snapshot()->front().Health == InstallationHealth::Healthy);
}

TEST_CASE("External editor registration remains unusable until inventory verification")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(123));

    const auto root = temporary.Path() / "Editor";
    auto manifest =
        EditorManifest(std::string(Keire::GetBuildInfo().Platform), std::string(Keire::GetBuildInfo().Architecture));
    (void)CreateInstallation(root, "source", InstallationOwnership::External, std::move(manifest));

    const auto registered = RegisterExternalEditor(controller, root, "external-editor");
    REQUIRE(registered);
    CHECK(registered.Value().Health == InstallationHealth::VerificationRequired);
    REQUIRE(controller.Installations().Snapshot()->size() == 1);
    CHECK(controller.Installations().Snapshot()->front().Health == InstallationHealth::VerificationRequired);

    const auto duplicateId = RegisterExternalEditor(controller, root.parent_path() / "Another", "external-editor");
    REQUIRE_FALSE(duplicateId);
    CHECK(duplicateId.Error().Code == HubErrorCode::InvalidData);

    const auto secondRoot = temporary.Path() / "SecondEditor";
    auto secondManifest =
        EditorManifest(std::string(Keire::GetBuildInfo().Platform), std::string(Keire::GetBuildInfo().Architecture));
    (void)CreateInstallation(secondRoot, "source-2", InstallationOwnership::External, std::move(secondManifest));
    const auto collidingId = RegisterExternalEditor(controller, secondRoot, "external-editor");
    REQUIRE_FALSE(collidingId);
    CHECK(collidingId.Error().Code == HubErrorCode::DuplicateIdentifier);
    CHECK(controller.Installations().Snapshot()->size() == 1);
}
