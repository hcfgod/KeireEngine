#include "TestSupport.h"

#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/HubTaskStore.h"
#include "KeireHubRuntime/NotificationStore.h"

#include "DistributionEncoding.h"

#include <doctest/doctest.h>

#include <array>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

using namespace KeireHub;

namespace
{
    [[nodiscard]] EditorInstallation ExternalInstallation(const std::filesystem::path& root)
    {
        return {.Id = "editor-external",
                .Version = "1.2.3",
                .Channel = "stable",
                .Platform = "windows",
                .Architecture = "x86_64",
                .Root = root,
                .Ownership = InstallationOwnership::External,
                .ManifestFingerprint = KeireHubTests::Digest(),
                .Entrypoints = {"KeireClient.exe"},
                .MinimumProjectSchema = 1,
                .MaximumProjectSchema = 3};
    }

    [[nodiscard]] HubTask QueuedTask()
    {
        return {.Id = "task-a",
                .Kind = HubTaskKind::Install,
                .DisplayName = "Install editor",
                .PackageIds = {"editor.windows.x86_64"},
                .State = HubTaskState::Queued,
                .CreatedUnixSeconds = 10,
                .UpdatedUnixSeconds = 10};
    }

    [[nodiscard]] SemanticVersion Version()
    {
        auto parsed = SemanticVersion::Parse("2.0.0");
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }
} // namespace

TEST_CASE("External installations can be registered but not treated as managed")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    REQUIRE(registry.Upsert(ExternalInstallation(temporary.Path() / "External")));
    REQUIRE(registry.Snapshot()->size() == 1);

    const auto unsafe = registry.CanMutateManagedInstall("editor-external", temporary.Path() / "External");
    REQUIRE_FALSE(unsafe);
    CHECK(unsafe.Error().Code == HubErrorCode::UnsafeInstallRoot);
    REQUIRE(registry.RemoveExternal("editor-external"));
    CHECK(registry.Snapshot()->empty());
}

TEST_CASE("Editor registry batch import preserves existing roots and rejects duplicate input atomically")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "installations.json";
    EditorInstallationRegistry registry(path);
    auto existing = ExternalInstallation(temporary.Path() / "Existing");
    REQUIRE(registry.Upsert(existing));

    auto rediscovered = ExternalInstallation(existing.Root);
    rediscovered.Id = "rediscovered-id";
    auto added = ExternalInstallation(temporary.Path() / "Added");
    added.Id = "editor-added";
    const std::array batch{rediscovered, added};
    REQUIRE(registry.UpsertMany(batch));
    REQUIRE(registry.Snapshot()->size() == 2);
    CHECK(std::ranges::any_of(*registry.Snapshot(), [](const auto& value) { return value.Id == "editor-external"; }));
    CHECK_FALSE(
        std::ranges::any_of(*registry.Snapshot(), [](const auto& value) { return value.Id == "rediscovered-id"; }));

    const auto beforeSnapshot = registry.Snapshot();
    const auto beforeDocument = KeireHubTests::ReadText(path);
    auto duplicateA = ExternalInstallation(temporary.Path() / "DuplicateA");
    duplicateA.Id = "duplicate";
    auto duplicateB = ExternalInstallation(temporary.Path() / "DuplicateB");
    duplicateB.Id = "duplicate";
    const std::array duplicateIds{duplicateA, duplicateB};
    const auto rejectedIds = registry.UpsertMany(duplicateIds);
    REQUIRE_FALSE(rejectedIds);
    CHECK(rejectedIds.Error().Code == HubErrorCode::DuplicateIdentifier);
    CHECK(registry.Snapshot() == beforeSnapshot);
    CHECK(KeireHubTests::ReadText(path) == beforeDocument);

    duplicateB.Id = "other-id";
    duplicateB.Root = duplicateA.Root;
    const std::array duplicateRoots{duplicateA, duplicateB};
    const auto rejectedRoots = registry.UpsertMany(duplicateRoots);
    REQUIRE_FALSE(rejectedRoots);
    CHECK(rejectedRoots.Error().Code == HubErrorCode::DuplicateIdentifier);
    CHECK(registry.Snapshot() == beforeSnapshot);
    CHECK(KeireHubTests::ReadText(path) == beforeDocument);
}

TEST_CASE("Editor registry preserves entrypoint roles instead of relying on manifest object order")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "installations.json";
    auto installation = ExternalInstallation(temporary.Path() / "External");
    installation.Entrypoints = {"bin/KeireAssetTool.exe", "bin/KeireClient.exe"};

    EditorInstallationRegistry writer(path);
    REQUIRE(writer.Upsert(installation));
    REQUIRE(writer.Snapshot()->size() == 1);
    CHECK(ResolveEditorEntrypoint(writer.Snapshot()->front()) == std::filesystem::path("bin/KeireClient.exe"));
    CHECK(ResolveAssetToolEntrypoint(writer.Snapshot()->front()) == std::filesystem::path("bin/KeireAssetTool.exe"));

    EditorInstallationRegistry reader(path);
    REQUIRE(reader.Load());
    REQUIRE(reader.Snapshot()->size() == 1);
    CHECK(reader.Snapshot()->front().EditorEntrypoint == std::filesystem::path("bin/KeireClient.exe"));
    CHECK(reader.Snapshot()->front().AssetToolEntrypoint == std::filesystem::path("bin/KeireAssetTool.exe"));
}

TEST_CASE("Managed installation mutation requires exact registry root and marker fields")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Managed";
    std::filesystem::create_directories(root);
    const ManagedInstallMarker marker{"editor-managed", KeireHubTests::Digest('b'), std::string(32, 'c')};
    REQUIRE(EditorInstallationRegistry::WriteManagedMarker(root, marker));

    EditorInstallation installation{.Id = marker.InstallationId,
                                    .Version = "2.0.0",
                                    .Channel = "stable",
                                    .Platform = "windows",
                                    .Architecture = "x86_64",
                                    .Root = root,
                                    .Ownership = InstallationOwnership::Managed,
                                    .ManifestFingerprint = marker.ManifestFingerprint,
                                    .MarkerNonce = marker.Nonce,
                                    .Entrypoints = {"KeireClient.exe"},
                                    .MinimumProjectSchema = 2,
                                    .MaximumProjectSchema = 3};
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    REQUIRE(registry.Upsert(installation));
    CHECK(registry.CanMutateManagedInstall("editor-managed", root));
    CHECK_FALSE(registry.CanMutateManagedInstall("editor-managed", temporary.Path() / "Other"));

    KeireHubTests::WriteText(root / EditorInstallationRegistry::MarkerFileName,
                             R"({"schemaVersion":1,"installationId":"editor-managed","manifestFingerprint":")" +
                                 KeireHubTests::Digest('b') + R"(","nonce":")" + std::string(32, 'd') + "\"}");
    const auto mismatched = registry.CanMutateManagedInstall("editor-managed", root);
    REQUIRE_FALSE(mismatched);
    CHECK(mismatched.Error().Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(registry.Snapshot()->size() == 1);
}

TEST_CASE("Managed markers cannot be overwritten by a different installation")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Editor";
    std::filesystem::create_directories(root);
    REQUIRE(EditorInstallationRegistry::WriteManagedMarker(
        root,
        {.InstallationId = "editor-a", .ManifestFingerprint = KeireHubTests::Digest(), .Nonce = std::string(32, '1')}));
    const auto rejected = EditorInstallationRegistry::WriteManagedMarker(
        root,
        {.InstallationId = "editor-b", .ManifestFingerprint = KeireHubTests::Digest(), .Nonce = std::string(32, '2')});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::UnsafeInstallRoot);
    auto marker = EditorInstallationRegistry::ReadManagedMarker(root);
    REQUIRE(marker);
    CHECK(marker.Value().InstallationId == "editor-a");
}

TEST_CASE("Editor registry persists receipt-bound package inventories")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Managed";
    std::filesystem::create_directories(root);
    EditorInstallation installation{.Id = "editor-receipt",
                                    .Version = "2.0.0",
                                    .Channel = "stable",
                                    .Platform = "windows",
                                    .Architecture = "x86_64",
                                    .Root = root,
                                    .Ownership = InstallationOwnership::Managed,
                                    .ManifestFingerprint = KeireHubTests::Digest('b'),
                                    .PackageTreeIdentity = KeireHubTests::Digest('e'),
                                    .MarkerNonce = std::string(32, 'd'),
                                    .InstalledPackages = {{.Id = "keire.editor",
                                                           .Version = Version(),
                                                           .Kind = PackageKind::Editor,
                                                           .ArtifactSizeBytes = 10,
                                                           .ArtifactSha256 = KeireHubTests::Digest('f'),
                                                           .InstalledSizeBytes = 3,
                                                           .Files = {{.Path = "bin/Editor.exe",
                                                                      .SizeBytes = 3,
                                                                      .Sha256 = KeireHubTests::Digest('1'),
                                                                      .Mode = 0755U}}}},
                                    .Entrypoints = {"bin/Editor.exe"},
                                    .MinimumProjectSchema = 1,
                                    .MaximumProjectSchema = 3,
                                    .InstalledSizeBytes = 3};
    PackageInstallReceipt receipt{.AggregateIdentitySha256 = installation.PackageTreeIdentity,
                                  .AggregateInstalledSizeBytes = installation.InstalledSizeBytes,
                                  .Packages = installation.InstalledPackages};
    auto encodedReceipt = EncodePackageInstallReceipt(receipt);
    REQUIRE(encodedReceipt);
    const auto receiptSha = Detail::Sha256Hex(std::as_bytes(std::span(encodedReceipt.Value())));
    installation.PackageReceiptSha256 = receiptSha;
    const ManagedInstallMarker marker{.InstallationId = installation.Id,
                                      .ManifestFingerprint = installation.ManifestFingerprint,
                                      .Nonce = installation.MarkerNonce,
                                      .ReceiptSha256 = receiptSha};
    REQUIRE(EditorInstallationRegistry::WriteManagedMarker(root, marker));
    const auto registryPath = temporary.Path() / "installations.json";
    EditorInstallationRegistry writer(registryPath);
    REQUIRE(writer.Upsert(installation));

    EditorInstallationRegistry reader(registryPath);
    REQUIRE(reader.Load());
    REQUIRE(reader.Snapshot()->size() == 1U);
    const auto& restored = reader.Snapshot()->front();
    CHECK(restored.PackageTreeIdentity == installation.PackageTreeIdentity);
    CHECK(restored.PackageReceiptSha256 == receiptSha);
    REQUIRE(restored.InstalledPackages.size() == 1U);
    REQUIRE(restored.InstalledPackages.front().Files.size() == 1U);
    CHECK(restored.InstalledPackages.front().Files.front().Path == "bin/Editor.exe");
    CHECK(reader.CanMutateManagedInstall(marker.InstallationId, root));
}

TEST_CASE("Installation registry rejects duplicate roots and unsafe entrypoints")
{
    KeireHubTests::TemporaryDirectory temporary;
    EditorInstallationRegistry registry(temporary.Path() / "installations.json");
    auto first = ExternalInstallation(temporary.Path() / "Editor");
    REQUIRE(registry.Upsert(first));
    auto duplicate = first;
    duplicate.Id = "editor-other";
    const auto duplicateStatus = registry.Upsert(duplicate);
    REQUIRE_FALSE(duplicateStatus);
    CHECK(duplicateStatus.Error().Code == HubErrorCode::DuplicateIdentifier);

    auto unsafe = ExternalInstallation(temporary.Path() / "Unsafe");
    unsafe.Id = "editor-unsafe";
    unsafe.Entrypoints = {"../outside.exe"};
    CHECK_FALSE(registry.Upsert(unsafe));
}

TEST_CASE("Task transition table preserves deterministic lifecycle rules")
{
    CHECK(IsValidTaskTransition(HubTaskState::Queued, HubTaskState::Downloading));
    CHECK(IsValidTaskTransition(HubTaskState::Downloading, HubTaskState::Paused));
    CHECK(IsValidTaskTransition(HubTaskState::Paused, HubTaskState::Queued));
    CHECK(IsValidTaskTransition(HubTaskState::Verifying, HubTaskState::Completed));
    CHECK(IsValidTaskTransition(HubTaskState::Cancelling, HubTaskState::Completed));
    CHECK(IsValidTaskTransition(HubTaskState::Cancelling, HubTaskState::Cancelled));
    CHECK_FALSE(IsValidTaskTransition(HubTaskState::Queued, HubTaskState::Completed));
    CHECK_FALSE(IsValidTaskTransition(HubTaskState::Completed, HubTaskState::Queued));
    CHECK_FALSE(IsValidTaskTransition(HubTaskState::Downloading, HubTaskState::Downloading));
}

TEST_CASE("Task store rejects regressing progress without mutating its snapshot")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubTaskStore store(temporary.Path() / "tasks.json");
    REQUIRE(store.Add(QueuedTask()));
    REQUIRE(store.Transition("task-a", HubTaskState::Downloading, 11));
    REQUIRE(store.UpdateProgress("task-a", {.BytesTransferred = 50, .TotalBytes = 100}, 12));
    const auto before = store.Snapshot();
    const auto rejected = store.UpdateProgress("task-a", {.BytesTransferred = 49, .TotalBytes = 100}, 13);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::InvalidArgument);
    CHECK(store.Snapshot() == before);
    CHECK(store.Snapshot()->front().Progress.BytesTransferred == 50);
}

TEST_CASE("Failed tasks require a typed error and can be retried")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubTaskStore store(temporary.Path() / "tasks.json");
    REQUIRE(store.Add(QueuedTask()));
    REQUIRE(store.Transition("task-a", HubTaskState::Downloading, 11));
    CHECK_FALSE(store.Transition("task-a", HubTaskState::Failed, 12));
    const HubError failure{.Code = HubErrorCode::IoRead,
                           .Message = "Download interrupted.",
                           .Retryable = true,
                           .AffectedItem = "editor.windows.x86_64"};
    REQUIRE(store.Transition("task-a", HubTaskState::Failed, 12, failure));
    CHECK(store.Snapshot()->front().Failure->Code == HubErrorCode::IoRead);
    REQUIRE(store.Transition("task-a", HubTaskState::Queued, 13));
    CHECK_FALSE(store.Snapshot()->front().Failure.has_value());

    HubTaskStore reloaded(store.Path());
    REQUIRE(reloaded.Load());
    CHECK(reloaded.Snapshot()->front().State == HubTaskState::Queued);
}

TEST_CASE("Active tasks cannot be removed from history")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubTaskStore store(temporary.Path() / "tasks.json");
    REQUIRE(store.Add(QueuedTask()));
    CHECK_FALSE(store.RemoveTerminal("task-a"));
    REQUIRE(store.Transition("task-a", HubTaskState::Cancelled, 11));
    REQUIRE(store.RemoveTerminal("task-a"));
    CHECK(store.Snapshot()->empty());
}

TEST_CASE("Notification history is bounded newest-first and tracks unread count")
{
    KeireHubTests::TemporaryDirectory temporary;
    NotificationStore store(temporary.Path() / "notifications.json", 2);
    REQUIRE(store.Add({.Id = "one",
                       .Severity = NotificationSeverity::Info,
                       .Title = "One",
                       .Message = "First",
                       .CreatedUnixSeconds = 1}));
    REQUIRE(store.Add({.Id = "two",
                       .Severity = NotificationSeverity::Success,
                       .Title = "Two",
                       .Message = "Second",
                       .CreatedUnixSeconds = 2}));
    REQUIRE(store.Add({.Id = "three",
                       .Severity = NotificationSeverity::Warning,
                       .Title = "Three",
                       .Message = "Third",
                       .CreatedUnixSeconds = 3}));
    REQUIRE(store.Snapshot()->size() == 2);
    CHECK(store.Snapshot()->front().Id == "three");
    CHECK(store.Snapshot()->back().Id == "two");
    CHECK(store.UnreadCount() == 2);
    REQUIRE(store.MarkRead("three"));
    CHECK(store.UnreadCount() == 1);
}
