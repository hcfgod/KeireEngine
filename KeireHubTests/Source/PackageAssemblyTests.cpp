#include "TestSupport.h"

#include "KeireHubRuntime/PackageAssembly.h"
#include "KeireHubRuntime/PackagePublish.h"

#include "DistributionEncoding.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    [[nodiscard]] SemanticVersion Version(const std::string_view value)
    {
        auto parsed = SemanticVersion::Parse(value);
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] std::string Digest(const std::string_view contents)
    {
        return Detail::Sha256Hex(std::as_bytes(std::span(contents)));
    }

    [[nodiscard]] PackageManifest Package(const std::string& id, const PackageKind kind, const std::string& path,
                                          const std::string_view contents)
    {
        return {.Id = id,
                .Version = Version(kind == PackageKind::Editor ? "1.2.3" : "2.0.0"),
                .Kind = kind,
                .DisplayName = id,
                .Channel = "stable",
                .Platform = "any",
                .Architecture = "any",
                .ArtifactSizeBytes = 100,
                .ArtifactSha256 = KeireHubTests::Digest(kind == PackageKind::Editor ? 'a' : 'b'),
                .InstalledSizeBytes = contents.size(),
                .Files = {{.Path = path,
                           .SizeBytes = contents.size(),
                           .Sha256 = Digest(contents),
                           .Mode = path.ends_with(".exe") ? 0755U : 0644U}},
                .SignatureKeyId = "release-key"};
    }

    [[nodiscard]] std::vector<PackageManifest> Manifests()
    {
        auto editor = Package("keire.editor", PackageKind::Editor, "bin/editor.exe", "editor");
        auto component = Package("keire.windows", PackageKind::BuildSupport, "modules/windows.bin", "component");
        auto versions = VersionConstraint::Parse("=1.2.3");
        if (!versions)
            throw std::runtime_error(versions.Error().Message);
        component.Dependencies.push_back({.PackageId = editor.Id, .Versions = std::move(versions).Value()});
        return {std::move(editor), std::move(component)};
    }

    void WriteSources(const std::filesystem::path& parent, const std::span<const PackageManifest> manifests,
                      std::vector<PackageAssemblySource>& sources)
    {
        const std::vector<std::string> contents{"editor", "component"};
        for (std::size_t index = 0; index < manifests.size(); ++index)
        {
            const auto root = parent / ("source-" + std::to_string(index));
            KeireHubTests::WriteText(root / manifests[index].Files.front().Path, contents[index]);
#if !defined(_WIN32)
            const auto permissions = manifests[index].Files.front().Mode == 0755U
                                         ? std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                               std::filesystem::perms::group_exec |
                                               std::filesystem::perms::others_read | std::filesystem::perms::others_exec
                                         : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                               std::filesystem::perms::group_read | std::filesystem::perms::others_read;
            std::filesystem::permissions(root / manifests[index].Files.front().Path, permissions,
                                         std::filesystem::perm_options::replace);
#endif
            sources.push_back({.Root = root, .Manifest = manifests[index]});
        }
    }
} // namespace

TEST_CASE("Package assembly publishes a receipt-bound editor and component tree")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto manifests = Manifests();
    std::vector<PackageAssemblySource> sources;
    WriteSources(temporary.Path(), manifests, sources);
    const auto allowed = temporary.Path() / "Installs";
    std::filesystem::create_directory(allowed);
    const auto staging = allowed / ".keire-stage-editor-install";

    std::vector<PackageArchiveProgress> progress;
    auto assembled = AssemblePackageTreesToStaging(
        {.Sources = sources,
         .AllowedStagingParent = allowed,
         .StagingRoot = staging,
         .Callbacks = {.Progress = [&](const auto& value) { progress.push_back(value); }}});
    REQUIRE(assembled);
    CHECK(KeireHubTests::ReadText(staging / "bin/editor.exe") == "editor");
    CHECK(KeireHubTests::ReadText(staging / "modules/windows.bin") == "component");
    CHECK(std::filesystem::is_regular_file(staging / PackageInstallReceiptFileName));
    CHECK_FALSE(progress.empty());

    auto receipt = ReadPackageInstallReceipt(staging);
    REQUIRE(receipt);
    REQUIRE(receipt.Value().Packages.size() == 2U);
    CHECK(receipt.Value().Packages.front().Id == "keire.editor");
    CHECK(receipt.Value().Packages.back().Kind == PackageKind::BuildSupport);
    REQUIRE(receipt.Value().Packages.back().Dependencies.size() == 1U);
    CHECK(receipt.Value().Packages.back().Dependencies.front().PackageId == "keire.editor");
    CHECK(receipt.Value().AggregateInstalledSizeBytes == 15U);
    CHECK(assembled.Value().PublicationManifest.InstalledSizeBytes > receipt.Value().AggregateInstalledSizeBytes);

    auto missingDependency = receipt.Value();
    missingDependency.Packages.back().Dependencies.front().PackageId = "keire.missing";
    CHECK_FALSE(ValidatePackageInstallReceipt(missingDependency));
    auto cycle = receipt.Value();
    cycle.Packages.front().Dependencies.push_back(
        {.PackageId = cycle.Packages.back().Id, .Versions = VersionConstraint{}});
    const auto rejectedCycle = ValidatePackageInstallReceipt(cycle);
    REQUIRE_FALSE(rejectedCycle);
    CHECK(rejectedCycle.Error().Code == HubErrorCode::PackageDependencyCycle);

    const auto markerText = "{\"schemaVersion\":1,\"installationId\":\"managed-editor\"}\n";
    KeireHubTests::WriteText(staging / ".keirehub-install.json", markerText);
    auto finalized = FinalizePackageAssemblyMarker(staging, assembled.Value().PublicationManifest);
    REQUIRE(finalized);
    auto finalizedAgain = FinalizePackageAssemblyMarker(staging, finalized.Value());
    REQUIRE(finalizedAgain);
    auto firstDocument = EncodePackageManifest(finalized.Value());
    auto secondDocument = EncodePackageManifest(finalizedAgain.Value());
    REQUIRE(firstDocument);
    REQUIRE(secondDocument);
    CHECK(firstDocument.Value() == secondDocument.Value());

    const auto destination = allowed / "Editor-1.2.3";
    auto paths = PlanPackagePublish(allowed, destination, "editor-install");
    REQUIRE(paths);
    REQUIRE(PublishStagedPackage(paths.Value(), finalized.Value(), "editor-install"));
    CHECK(std::filesystem::is_regular_file(destination / PackageInstallReceiptFileName));
    CHECK(std::filesystem::is_regular_file(destination / ".keirehub-install.json"));

    auto base = CreatePackagePublicationManifest(manifests);
    REQUIRE(base);
    auto recoveredReceipt = FinalizePackageAssemblyReceipt(destination, base.Value(), manifests);
    REQUIRE(recoveredReceipt);
    auto recoveredFinal = FinalizePackageAssemblyMarker(destination, recoveredReceipt.Value());
    REQUIRE(recoveredFinal);
    auto recoveredDocument = EncodePackageManifest(recoveredFinal.Value());
    REQUIRE(recoveredDocument);
    CHECK(recoveredDocument.Value() == firstDocument.Value());
}

TEST_CASE("Package assembly rejects cross-package path collisions and reserved metadata")
{
    auto manifests = Manifests();
    manifests.back().Files.front().Path = "BIN/EDITOR.EXE";
    auto collision = CreatePackageTreeSeal(manifests);
    REQUIRE_FALSE(collision);
    CHECK(collision.Error().Code == HubErrorCode::PackageConflict);

    manifests = Manifests();
    manifests.back().Files.front().Path = PackageInstallReceiptFileName;
    auto publication = CreatePackagePublicationManifest(manifests);
    REQUIRE_FALSE(publication);
    CHECK(publication.Error().Code == HubErrorCode::PackageConflict);
}

#if defined(_WIN32)
TEST_CASE("Package assembly rejects a case-aliased staging root inside a source tree")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto manifests = Manifests();
    std::vector<PackageAssemblySource> sources;
    WriteSources(temporary.Path(), manifests, sources);
    const auto alias = sources.front().Root.parent_path() / "SOURCE-0";
    std::error_code equivalentError;
    if (!std::filesystem::equivalent(sources.front().Root, alias, equivalentError) || equivalentError)
        return;

    const auto staging = alias / ".keire-stage-contained";
    const auto assembled =
        AssemblePackageTreesToStaging({.Sources = sources, .AllowedStagingParent = alias, .StagingRoot = staging});
    REQUIRE_FALSE(assembled);
    CHECK(assembled.Error().Code == HubErrorCode::UnsafeInstallRoot);
    CHECK_FALSE(std::filesystem::exists(staging));
}
#endif

TEST_CASE("Package assembly receipt recovery rejects changed metadata and payloads")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto manifests = Manifests();
    std::vector<PackageAssemblySource> sources;
    WriteSources(temporary.Path(), manifests, sources);
    const auto allowed = temporary.Path() / "Installs";
    std::filesystem::create_directory(allowed);
    const auto staging = allowed / ".keire-stage-tamper";
    auto assembled =
        AssemblePackageTreesToStaging({.Sources = sources, .AllowedStagingParent = allowed, .StagingRoot = staging});
    REQUIRE(assembled);

    auto base = CreatePackagePublicationManifest(manifests);
    REQUIRE(base);
    KeireHubTests::WriteText(staging / PackageInstallReceiptFileName, "{}\n");
    const auto changedReceipt = FinalizePackageAssemblyReceipt(staging, base.Value(), manifests);
    REQUIRE_FALSE(changedReceipt);
    CHECK(changedReceipt.Error().Code == HubErrorCode::InvalidData);

    std::filesystem::remove_all(staging);
    assembled =
        AssemblePackageTreesToStaging({.Sources = sources, .AllowedStagingParent = allowed, .StagingRoot = staging});
    REQUIRE(assembled);
    KeireHubTests::WriteText(staging / "bin/editor.exe", "edited");
    KeireHubTests::WriteText(staging / ".keirehub-install.json", "marker\n");
    const auto changedPayload = FinalizePackageAssemblyMarker(staging, assembled.Value().PublicationManifest);
    REQUIRE_FALSE(changedPayload);
    CHECK(changedPayload.Error().Code == HubErrorCode::DownloadChecksumMismatch);
}
