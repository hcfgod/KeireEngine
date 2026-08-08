#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/PackagePublish.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace KeireHub;

namespace
{
    [[nodiscard]] SemanticVersion Version()
    {
        auto parsed = SemanticVersion::Parse("1.2.3");
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] PackageManifest Manifest(const std::string& path, const std::string_view contents)
    {
        const auto bytes = std::as_bytes(std::span(contents.data(), contents.size()));
        return {.Id = "keire.editor",
                .Version = Version(),
                .Kind = PackageKind::Editor,
                .DisplayName = "Kéire Editor",
                .Channel = "stable",
                .Platform = "any",
                .Architecture = "any",
                .ArtifactSizeBytes = 1,
                .ArtifactSha256 = KeireHubTests::Digest(),
                .InstalledSizeBytes = contents.size(),
                .Files = {{.Path = path,
                           .SizeBytes = contents.size(),
                           .Sha256 = KeireHub::Detail::Sha256Hex(bytes),
                           .Mode = 0644U}},
                .SignatureKeyId = "release-key"};
    }
} // namespace

TEST_CASE("Package publication atomically installs and replaces staged directories")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor-1.2.3";

    auto firstPaths = PlanPackagePublish(parent, destination, "install-first");
    REQUIRE(firstPaths);
    std::filesystem::create_directory(firstPaths.Value().StagingRoot);
    KeireHubTests::WriteText(firstPaths.Value().StagingRoot / "version.txt", "first");
    REQUIRE(PublishStagedPackage(firstPaths.Value(), Manifest("version.txt", "first"), "install-first"));
    CHECK(KeireHubTests::ReadText(destination / "version.txt") == "first");
    CHECK_FALSE(std::filesystem::exists(firstPaths.Value().StagingRoot));
    CHECK_FALSE(std::filesystem::exists(firstPaths.Value().BackupRoot));
    CHECK_FALSE(std::filesystem::exists(firstPaths.Value().Journal));

    auto replacementPaths = PlanPackagePublish(parent, destination, "repair-second");
    REQUIRE(replacementPaths);
    std::filesystem::create_directory(replacementPaths.Value().StagingRoot);
    KeireHubTests::WriteText(replacementPaths.Value().StagingRoot / "version.txt", "second");
    REQUIRE(PublishStagedPackage(replacementPaths.Value(), Manifest("version.txt", "second"), "repair-second"));
    CHECK(KeireHubTests::ReadText(destination / "version.txt") == "second");
    CHECK_FALSE(std::filesystem::exists(replacementPaths.Value().StagingRoot));
    CHECK_FALSE(std::filesystem::exists(replacementPaths.Value().BackupRoot));
    CHECK_FALSE(std::filesystem::exists(replacementPaths.Value().Journal));
}

TEST_CASE("Package publication recovery completes a move interrupted after backup")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "state.txt", "old");

    auto paths = PlanPackagePublish(parent, destination, "recover-forward");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    auto prepared = PreparePackagePublish(paths.Value(), manifest, "recover-forward");
    REQUIRE(prepared);
    CHECK(prepared.Value().Phase == PackagePublishPhase::Prepared);
    CHECK(prepared.Value().ReplacesExisting);
    std::filesystem::rename(destination, paths.Value().BackupRoot);

    REQUIRE(RecoverPackagePublish(parent, paths.Value().Journal, manifest, "recover-forward"));
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "new");
    CHECK_FALSE(std::filesystem::exists(paths.Value().StagingRoot));
    CHECK_FALSE(std::filesystem::exists(paths.Value().BackupRoot));
    CHECK_FALSE(std::filesystem::exists(paths.Value().Journal));
}

TEST_CASE("Package publication recovery promotes a complete operation-owned lock staging")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    auto paths = PlanPackagePublish(parent, destination, "recover-lock-staging");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    REQUIRE(PreparePackagePublish(paths.Value(), manifest, "recover-lock-staging"));
    const auto operationLock = parent / ".keire-publish-lock-recover-lock-staging";
    std::filesystem::rename(paths.Value().LockRoot, operationLock);

    REQUIRE(RecoverPackagePublish(parent, paths.Value().Journal, manifest, "recover-lock-staging"));
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "new");
    CHECK_FALSE(std::filesystem::exists(operationLock));
    CHECK_FALSE(std::filesystem::exists(paths.Value().LockRoot));
}

TEST_CASE("Package publication recovery removes only incomplete operation-owned lock staging")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    auto paths = PlanPackagePublish(parent, destination, "recover-incomplete-lock");
    REQUIRE(paths);
    const auto operationLock = parent / ".keire-publish-lock-recover-incomplete-lock";
    std::filesystem::create_directory(operationLock);
    KeireHubTests::WriteText(operationLock / "journal.json.tmp-uncommitted", "partial");

    const auto recovered =
        RecoverPackagePublish(parent, paths.Value().Journal, Manifest("state.txt", "new"), "recover-incomplete-lock");
    REQUIRE_FALSE(recovered);
    CHECK(recovered.Error().Code == HubErrorCode::WorkerInterrupted);
    CHECK_FALSE(std::filesystem::exists(operationLock));
    CHECK_FALSE(std::filesystem::exists(destination));
}

TEST_CASE("Package publication recovery restores the previous root when staging is lost")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "state.txt", "old");

    auto paths = PlanPackagePublish(parent, destination, "recover-back");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    REQUIRE(PreparePackagePublish(paths.Value(), manifest, "recover-back"));
    std::filesystem::rename(destination, paths.Value().BackupRoot);
    std::filesystem::remove_all(paths.Value().StagingRoot);

    const auto recovered = RecoverPackagePublish(parent, paths.Value().Journal, manifest, "recover-back");
    REQUIRE_FALSE(recovered);
    CHECK(recovered.Error().Code == HubErrorCode::WorkerInterrupted);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "old");
    CHECK_FALSE(std::filesystem::exists(paths.Value().BackupRoot));
    CHECK_FALSE(std::filesystem::exists(paths.Value().Journal));
}

TEST_CASE("Prepared publication recovery never mistakes an unchanged destination for the staged payload")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "state.txt", "old");

    auto paths = PlanPackagePublish(parent, destination, "recover-prepared-lost");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    auto prepared = PreparePackagePublish(paths.Value(), manifest, "recover-prepared-lost");
    REQUIRE(prepared);
    REQUIRE(prepared.Value().Phase == PackagePublishPhase::Prepared);
    std::filesystem::remove_all(paths.Value().StagingRoot);

    const auto recovered = RecoverPackagePublish(parent, paths.Value().Journal, manifest, "recover-prepared-lost");
    REQUIRE_FALSE(recovered);
    CHECK(recovered.Error().Code == HubErrorCode::WorkerInterrupted);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "old");
    CHECK_FALSE(std::filesystem::exists(paths.Value().Journal));
}

TEST_CASE("Backup-moved recovery completes journal cleanup after an interrupted rollback")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "state.txt", "old");

    auto paths = PlanPackagePublish(parent, destination, "recover-rollback-cleanup");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    REQUIRE(PreparePackagePublish(paths.Value(), manifest, "recover-rollback-cleanup"));
    auto document = nlohmann::json::parse(KeireHubTests::ReadText(paths.Value().Journal));
    document["phase"] = "backupMoved";
    KeireHubTests::WriteText(paths.Value().Journal, document.dump());
    std::filesystem::rename(destination, paths.Value().BackupRoot);
    std::filesystem::remove_all(paths.Value().StagingRoot);
    std::filesystem::rename(paths.Value().BackupRoot, destination);

    const auto recovered = RecoverPackagePublish(parent, paths.Value().Journal, manifest, "recover-rollback-cleanup");
    REQUIRE_FALSE(recovered);
    CHECK(recovered.Error().Code == HubErrorCode::WorkerInterrupted);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "old");
    CHECK_FALSE(std::filesystem::exists(paths.Value().Journal));
}

TEST_CASE("Prepared new-install recovery does not bless an unrelated occupied destination")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";

    auto paths = PlanPackagePublish(parent, destination, "recover-unrelated");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    REQUIRE(PreparePackagePublish(paths.Value(), manifest, "recover-unrelated"));
    std::filesystem::remove_all(paths.Value().StagingRoot);
    KeireHubTests::WriteText(destination / "unrelated.txt", "keep");

    const auto recovered = RecoverPackagePublish(parent, paths.Value().Journal, manifest, "recover-unrelated");
    REQUIRE_FALSE(recovered);
    CHECK(recovered.Error().Code == HubErrorCode::InvalidData);
    CHECK(KeireHubTests::ReadText(destination / "unrelated.txt") == "keep");
    CHECK(std::filesystem::is_regular_file(paths.Value().Journal));
}

TEST_CASE("Package publication journals reject path substitution and unsafe roots")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    auto paths = PlanPackagePublish(parent, destination, "journal-safe");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    auto prepared = PreparePackagePublish(paths.Value(), Manifest("state.txt", "new"), "journal-safe");
    REQUIRE(prepared);

    auto document = nlohmann::json::parse(KeireHubTests::ReadText(paths.Value().Journal));
    document["paths"]["destination"] = (temporary.Path() / "Outside").generic_string();
    KeireHubTests::WriteText(paths.Value().Journal, document.dump());
    const auto hostile = LoadPackagePublishJournal(parent, paths.Value().Journal);
    REQUIRE_FALSE(hostile);
    CHECK(hostile.Error().Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(std::filesystem::is_directory(paths.Value().StagingRoot));
    CHECK_FALSE(std::filesystem::exists(temporary.Path() / "Outside"));

    const auto reserved = PlanPackagePublish(parent, parent / ".keire-stage-reserved", "reserved");
    REQUIRE_FALSE(reserved);
    CHECK(reserved.Error().Code == HubErrorCode::UnsafeInstallRoot);

    const auto mixedCaseReserved = PlanPackagePublish(parent, parent / ".KEIRE-BACKUP-mixed", "mixed");
    REQUIRE_FALSE(mixedCaseReserved);
    CHECK(mixedCaseReserved.Error().Code == HubErrorCode::UnsafeInstallRoot);

    const auto escaped = PlanPackagePublish(parent, temporary.Path() / "Outside", "escaped");
    REQUIRE_FALSE(escaped);
    CHECK(escaped.Error().Code == HubErrorCode::InvalidArgument);

    const auto wrongAuthority = LoadPackagePublishJournal(temporary.Path(), paths.Value().Journal);
    REQUIRE_FALSE(wrongAuthority);
    CHECK(wrongAuthority.Error().Code == HubErrorCode::InvalidArgument);
}

TEST_CASE("Package publication refuses occupied operation paths without changing either tree")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "old.txt", "old");
    auto paths = PlanPackagePublish(parent, destination, "collision");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "new.txt", "new");
    std::filesystem::create_directory(paths.Value().BackupRoot);
    KeireHubTests::WriteText(paths.Value().BackupRoot / "sentinel.txt", "keep");

    const auto result = PublishStagedPackage(paths.Value(), Manifest("new.txt", "new"), "collision");
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::DestinationConflict);
    CHECK(KeireHubTests::ReadText(destination / "old.txt") == "old");
    CHECK(KeireHubTests::ReadText(paths.Value().StagingRoot / "new.txt") == "new");
    CHECK(KeireHubTests::ReadText(paths.Value().BackupRoot / "sentinel.txt") == "keep");
    CHECK_FALSE(std::filesystem::exists(paths.Value().Journal));
}

TEST_CASE("Package publication serializes every mutation within an authorized installation parent")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";

    auto first = PlanPackagePublish(parent, destination, "concurrent-first");
    auto second = PlanPackagePublish(parent, destination, "concurrent-second");
    REQUIRE(first);
    REQUIRE(second);
    std::filesystem::create_directory(first.Value().StagingRoot);
    std::filesystem::create_directory(second.Value().StagingRoot);
    KeireHubTests::WriteText(first.Value().StagingRoot / "state.txt", "first");
    KeireHubTests::WriteText(second.Value().StagingRoot / "state.txt", "second");
    const auto firstManifest = Manifest("state.txt", "first");
    REQUIRE(PreparePackagePublish(first.Value(), firstManifest, "concurrent-first"));

    const auto blocked = PreparePackagePublish(second.Value(), Manifest("state.txt", "second"), "concurrent-second");
    REQUIRE_FALSE(blocked);
    CHECK(blocked.Error().Code == HubErrorCode::DestinationConflict);
    CHECK(KeireHubTests::ReadText(first.Value().StagingRoot / "state.txt") == "first");
    CHECK(KeireHubTests::ReadText(second.Value().StagingRoot / "state.txt") == "second");
}

TEST_CASE("Package publication revalidates the signed staging inventory immediately before commit")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "state.txt", "old");

    auto paths = PlanPackagePublish(parent, destination, "mutated-stage");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    auto prepared = PreparePackagePublish(paths.Value(), manifest, "mutated-stage");
    REQUIRE(prepared);
    const auto& journal = prepared.Value();
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "bad");

    const auto committed = ContinuePackagePublish(journal, manifest);
    REQUIRE_FALSE(committed);
    CHECK(committed.Error().Code == HubErrorCode::DownloadChecksumMismatch);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "old");
    CHECK(std::filesystem::is_regular_file(paths.Value().Journal));

    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    KeireHubTests::WriteText(paths.Value().StagingRoot / "undeclared.txt", "extra");
    const auto added = ContinuePackagePublish(journal, manifest);
    REQUIRE_FALSE(added);
    CHECK(added.Error().Code == HubErrorCode::InvalidData);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "old");
}

TEST_CASE("Package publication rejects a caller-authorized parent with a symbolic-link ancestor")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto realParent = temporary.Path() / "real-installs";
    const auto linkedParent = temporary.Path() / "linked-installs";
    std::filesystem::create_directory(realParent);
    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(realParent, linkedParent, symlinkError);
    if (symlinkError)
        return;

    const auto destination = linkedParent / "Editor";
    auto paths = PlanPackagePublish(linkedParent, destination, "linked-parent");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");

    const auto published = PublishStagedPackage(paths.Value(), Manifest("state.txt", "new"), "linked-parent");
    REQUIRE_FALSE(published);
    CHECK(published.Error().Code == HubErrorCode::InvalidArgument);
    CHECK_FALSE(std::filesystem::exists(realParent / "Editor"));
    CHECK(KeireHubTests::ReadText(realParent / ".keire-stage-linked-parent/state.txt") == "new");
}

TEST_CASE("Package publication binds catalog transport identity and rejects before recovery mutation")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    auto paths = PlanPackagePublish(parent, parent / "Editor", "exact-manifest");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    REQUIRE(PreparePackagePublish(paths.Value(), manifest, "exact-manifest"));
    const auto obsoleteLock = parent / ".keire-publish-lock-exact-manifest";
    std::filesystem::create_directory(obsoleteLock);
    KeireHubTests::WriteText(obsoleteLock / "sentinel.txt", "keep");

    auto substituted = manifest;
    substituted.ArtifactSizeBytes += 1U;
    substituted.ArtifactSha256 = KeireHubTests::Digest('f');
    const auto recovered = RecoverPackagePublish(parent, paths.Value().Journal, substituted, "exact-manifest");
    REQUIRE_FALSE(recovered);
    CHECK(recovered.Error().Code == HubErrorCode::PackageManifestInvalid);
    CHECK(KeireHubTests::ReadText(obsoleteLock / "sentinel.txt") == "keep");
    CHECK(std::filesystem::is_directory(paths.Value().StagingRoot));
    CHECK_FALSE(std::filesystem::exists(paths.Value().Destination));
}

TEST_CASE("Package recovery removes only operation-owned journal replacement artifacts")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    auto paths = PlanPackagePublish(parent, parent / "Editor", "journal-temp");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    REQUIRE(PreparePackagePublish(paths.Value(), manifest, "journal-temp"));
    KeireHubTests::WriteText(paths.Value().LockRoot / "journal.json.tmp-interrupted", "partial");

    REQUIRE(RecoverPackagePublish(parent, paths.Value().Journal, manifest, "journal-temp"));
    CHECK(KeireHubTests::ReadText(paths.Value().Destination / "state.txt") == "new");
    CHECK_FALSE(std::filesystem::exists(paths.Value().LockRoot));
}

TEST_CASE("New package publication refuses a backup planted after preparation")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    auto paths = PlanPackagePublish(parent, parent / "Editor", "planted-backup");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    auto prepared = PreparePackagePublish(paths.Value(), manifest, "planted-backup");
    REQUIRE(prepared);
    std::filesystem::create_directory(paths.Value().BackupRoot);

    const auto published = ContinuePackagePublish(prepared.Value(), manifest);
    REQUIRE_FALSE(published);
    CHECK(published.Error().Code == HubErrorCode::DestinationConflict);
    CHECK_FALSE(std::filesystem::exists(paths.Value().Destination));
    CHECK(KeireHubTests::ReadText(paths.Value().StagingRoot / "state.txt") == "new");
}

TEST_CASE("Replacement recovery restores the previous install when staged validation fails")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "state.txt", "old");
    auto paths = PlanPackagePublish(parent, destination, "rollback-invalid-stage");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    REQUIRE(PreparePackagePublish(paths.Value(), manifest, "rollback-invalid-stage"));
    auto document = nlohmann::json::parse(KeireHubTests::ReadText(paths.Value().Journal));
    document["phase"] = "backupMoved";
    KeireHubTests::WriteText(paths.Value().Journal, document.dump());
    std::filesystem::rename(destination, paths.Value().BackupRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "bad");

    const auto failed = RecoverPackagePublish(parent, paths.Value().Journal, manifest, "rollback-invalid-stage");
    REQUIRE_FALSE(failed);
    CHECK(failed.Error().Code == HubErrorCode::DownloadChecksumMismatch);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "old");
    CHECK_FALSE(std::filesystem::exists(paths.Value().BackupRoot));
    CHECK(std::filesystem::is_regular_file(paths.Value().Journal));

    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    REQUIRE(RecoverPackagePublish(parent, paths.Value().Journal, manifest, "rollback-invalid-stage"));
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "new");
}

TEST_CASE("Published replacement recovery quarantines an invalid new tree and restores the prior install")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "state.txt", "old");
    auto paths = PlanPackagePublish(parent, destination, "rollback-invalid-published");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    REQUIRE(PreparePackagePublish(paths.Value(), manifest, "rollback-invalid-published"));
    auto document = nlohmann::json::parse(KeireHubTests::ReadText(paths.Value().Journal));
    document["phase"] = "published";
    KeireHubTests::WriteText(paths.Value().Journal, document.dump());
    std::filesystem::rename(destination, paths.Value().BackupRoot);
    std::filesystem::rename(paths.Value().StagingRoot, destination);
    KeireHubTests::WriteText(destination / "state.txt", "bad");

    const auto failed = RecoverPackagePublish(parent, paths.Value().Journal, manifest, "rollback-invalid-published");
    REQUIRE_FALSE(failed);
    CHECK(failed.Error().Code == HubErrorCode::DownloadChecksumMismatch);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "old");
    CHECK(KeireHubTests::ReadText(paths.Value().StagingRoot / "state.txt") == "bad");
    CHECK_FALSE(std::filesystem::exists(paths.Value().BackupRoot));
}

TEST_CASE("New package publication never replaces a destination that appears after preparation")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    auto paths = PlanPackagePublish(parent, destination, "late-destination");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    const PackagePublishOptions options{.DestinationPolicy = PackagePublishDestinationPolicy::RequireAbsent};
    auto prepared = PreparePackagePublish(paths.Value(), manifest, "late-destination", options);
    REQUIRE(prepared);

    KeireHubTests::WriteText(destination / "state.txt", "unrelated");
    const auto published = ContinuePackagePublish(prepared.Value(), manifest, options);
    REQUIRE_FALSE(published);
    CHECK(published.Error().Code == HubErrorCode::DestinationConflict);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "unrelated");
    CHECK(KeireHubTests::ReadText(paths.Value().StagingRoot / "state.txt") == "new");
    CHECK_FALSE(std::filesystem::exists(paths.Value().BackupRoot));
}

TEST_CASE("Replacement publication reauthorizes the exact tree after moving it to backup")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "state.txt", "old");
    auto paths = PlanPackagePublish(parent, destination, "guard-replacement");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    std::size_t authorizations = 0;
    const PackagePublishOptions options{
        .DestinationPolicy = PackagePublishDestinationPolicy::RequireExisting,
        .AuthorizeMutation = [&](const PackagePublishJournal& journal)
        {
            ++authorizations;
            if (std::filesystem::exists(journal.Paths.BackupRoot))
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::UnsafeInstallRoot, .Message = "The replacement proof changed."});
            }
            return HubStatus::Success();
        }};

    const auto published = PublishStagedPackage(paths.Value(), manifest, "guard-replacement", options);
    REQUIRE_FALSE(published);
    CHECK(published.Error().Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(authorizations == 2U);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "old");
    CHECK_FALSE(std::filesystem::exists(paths.Value().BackupRoot));
}

TEST_CASE("Replacement recovery reauthorizes before mutating an interrupted publication")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto parent = temporary.Path() / "Installs";
    std::filesystem::create_directory(parent);
    const auto destination = parent / "Editor";
    KeireHubTests::WriteText(destination / "state.txt", "old");
    auto paths = PlanPackagePublish(parent, destination, "guard-recovery");
    REQUIRE(paths);
    std::filesystem::create_directory(paths.Value().StagingRoot);
    KeireHubTests::WriteText(paths.Value().StagingRoot / "state.txt", "new");
    const auto manifest = Manifest("state.txt", "new");
    const PackagePublishOptions replacement{.DestinationPolicy = PackagePublishDestinationPolicy::RequireExisting};
    REQUIRE(PreparePackagePublish(paths.Value(), manifest, "guard-recovery", replacement));
    std::size_t authorizations = 0;
    const PackagePublishOptions guarded{
        .DestinationPolicy = PackagePublishDestinationPolicy::RequireExisting,
        .AuthorizeMutation = [&](const PackagePublishJournal&)
        {
            ++authorizations;
            return HubStatus::Failure(
                {.Code = HubErrorCode::EditorRunning, .Message = "The editor started before recovery."});
        }};

    const auto recovered = RecoverPackagePublish(parent, paths.Value().Journal, manifest, "guard-recovery", guarded);
    REQUIRE_FALSE(recovered);
    CHECK(recovered.Error().Code == HubErrorCode::EditorRunning);
    CHECK(authorizations == 1U);
    CHECK(KeireHubTests::ReadText(destination / "state.txt") == "old");
    CHECK(KeireHubTests::ReadText(paths.Value().StagingRoot / "state.txt") == "new");
}
