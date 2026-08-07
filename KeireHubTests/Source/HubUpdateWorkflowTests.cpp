#include "TestSupport.h"

#include "KeireHubRuntime/HubUpdateWorkflow.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    constexpr std::string_view KeyId = "ed25519-00000000000000000000000000000000";

    [[nodiscard]] HubUpdateCandidate Candidate()
    {
        const auto platform = std::string(HubUpdateManager::HostPlatformIdentity());
        const auto architecture = std::string(HubUpdateManager::HostArchitectureIdentity());
        return {.Package = {.SchemaVersion = PackageManifest::CurrentSchemaVersion,
                            .Id = "keire.hub.stable",
                            .Version = {.Major = 2, .Minor = 0, .Patch = 0, .Prerelease = {}, .BuildMetadata = {}},
                            .Kind = PackageKind::HubInstaller,
                            .DisplayName = "Kéire Hub 2.0.0",
                            .Channel = "stable",
                            .Platform = platform,
                            .Architecture = architecture,
                            .EngineCompatibility = std::nullopt,
                            .Dependencies = {},
                            .Conflicts = {},
                            .ArtifactSizeBytes = 3,
                            .ArtifactSha256 = KeireHubTests::Digest('a'),
                            .InstalledSizeBytes = 3,
                            .Files = {{.Path = "KeireHubInstaller",
                                       .SizeBytes = 3,
                                       .Sha256 = KeireHubTests::Digest('b'),
                                       .Mode = 0755U}},
                            .LicenseReferences = {},
                            .SignatureKeyId = std::string(KeyId)},
                .CatalogIdentity = {.KeyId = std::string(KeyId),
                                    .Sequence = 7,
                                    .ExpiresAt = "2035-01-01T00:00:00Z",
                                    .Channel = "stable",
                                    .Platform = platform,
                                    .Architecture = architecture},
                .Source = DistributionCatalogSourceState::Online};
    }

    [[nodiscard]] std::string TaskId(const HubUpdateCandidate& candidate)
    {
        return HubUpdateTaskPrefix(candidate) + "00000000000000000000000000000000";
    }
} // namespace

TEST_CASE("Hub update workflow builds a catalog-bound task request")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto candidate = Candidate();
    const auto request = CreateHubUpdateDownloadRequest(candidate, {.TaskId = TaskId(candidate),
                                                                    .ServiceBaseUrl = "https://updates.example/",
                                                                    .CacheRoot = temporary.Path() / "Cache",
                                                                    .CustomProxyUrl = "http://proxy.example:8080",
                                                                    .BandwidthLimitBytesPerSecond = 4096});
    REQUIRE(request);
    CHECK(request.Value().Package.Kind == PackageKind::HubInstaller);
    CHECK(request.Value().PackageUrl == "https://updates.example/v1/packages/" + candidate.Package.ArtifactSha256);
    CHECK(request.Value().CustomProxyUrl == "http://proxy.example:8080");
    CHECK(request.Value().BandwidthLimitBytesPerSecond == 4096);

    auto mismatched = candidate;
    mismatched.Package.Platform = "any";
    const auto rejected = CreateHubUpdateDownloadRequest(mismatched, {.TaskId = TaskId(mismatched),
                                                                      .ServiceBaseUrl = "https://updates.example",
                                                                      .CacheRoot = temporary.Path() / "Cache",
                                                                      .AllowInsecureLoopbackDevelopment = false,
                                                                      .CustomProxyUrl = std::nullopt,
                                                                      .BandwidthLimitBytesPerSecond = 0});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::CatalogIdentityMismatch);
}

TEST_CASE("Hub update workflow exposes only matching verified completion records")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto candidate = Candidate();
    const auto taskId = TaskId(candidate);
    const auto cacheRoot = std::filesystem::absolute(temporary.Path() / "Cache");
    const DownloadRequest download{.PackageId = candidate.Package.Id,
                                   .Url = "https://updates.example/package",
                                   .Sha256 = candidate.Package.ArtifactSha256,
                                   .SizeBytes = candidate.Package.ArtifactSizeBytes,
                                   .CacheRoot = cacheRoot,
                                   .Retry = {},
                                   .AllowInsecureLoopbackDevelopment = false,
                                   .CustomProxyUrl = std::nullopt,
                                   .BandwidthLimitBytesPerSecond = 0};
    const auto installerPath = DownloadManager::CachePath(download);
    KeireHubTests::WriteText(installerPath, "abc");

    HubWorkerCoordinatorSnapshot snapshot{
        .State = HubWorkerCoordinatorState::Ready,
        .Revision = 1,
        .Tasks =
            std::make_shared<const std::vector<HubTask>>(std::vector<HubTask>{{.Id = taskId,
                                                                               .Kind = HubTaskKind::HubUpdate,
                                                                               .DisplayName = "Update Kéire Hub",
                                                                               .PackageIds = {candidate.Package.Id},
                                                                               .TargetInstallationId = std::nullopt,
                                                                               .State = HubTaskState::Completed,
                                                                               .Progress = {},
                                                                               .CreatedUnixSeconds = 10,
                                                                               .UpdatedUnixSeconds = 11,
                                                                               .WorkerProcessId = std::nullopt,
                                                                               .Failure = std::nullopt}}),
        .VerifiedDownloads = std::make_shared<const std::vector<HubVerifiedPackageDownload>>(
            std::vector<HubVerifiedPackageDownload>{{.TaskId = taskId,
                                                     .PackageId = candidate.Package.Id,
                                                     .Sha256 = candidate.Package.ArtifactSha256,
                                                     .SizeBytes = candidate.Package.ArtifactSizeBytes,
                                                     .CachePath = installerPath}}),
        .CompletedEditorInstalls = std::make_shared<const std::vector<HubCompletedEditorInstall>>(),
        .LastFailure = std::nullopt};
    const auto ready = InspectHubUpdateWorkflow(candidate, snapshot, cacheRoot);
    REQUIRE(ready);
    CHECK(ready.Value().State == HubUpdateDownloadState::Ready);
    CHECK(ready.Value().VerifiedInstallerPath == installerPath);

    auto request = CreateHubUpdateHandoffRequest(candidate, ready.Value(), cacheRoot, temporary.Path() / "InstalledHub",
                                                 "1.0.0", 1234, 100, true);
    REQUIRE(request);
    CHECK(request.Value().InstallerPath == installerPath);
    CHECK(request.Value().CatalogSequence == 7);
    CHECK(request.Value().RequirePlatformSignature);

    auto forgedDownloads = *snapshot.VerifiedDownloads;
    forgedDownloads.front().Sha256 = KeireHubTests::Digest('f');
    snapshot.VerifiedDownloads =
        std::make_shared<const std::vector<HubVerifiedPackageDownload>>(std::move(forgedDownloads));
    const auto forged = InspectHubUpdateWorkflow(candidate, snapshot, cacheRoot);
    REQUIRE_FALSE(forged);
    CHECK(forged.Error().Code == HubErrorCode::WorkerProtocolInvalid);
}

TEST_CASE("Hub update workflow reports cache eviction as downloadable again")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto candidate = Candidate();
    const auto taskId = TaskId(candidate);
    const auto cacheRoot = std::filesystem::absolute(temporary.Path() / "Cache");
    const DownloadRequest download{.PackageId = candidate.Package.Id,
                                   .Url = "https://updates.example/package",
                                   .Sha256 = candidate.Package.ArtifactSha256,
                                   .SizeBytes = candidate.Package.ArtifactSizeBytes,
                                   .CacheRoot = cacheRoot,
                                   .Retry = {},
                                   .AllowInsecureLoopbackDevelopment = false,
                                   .CustomProxyUrl = std::nullopt,
                                   .BandwidthLimitBytesPerSecond = 0};
    HubWorkerCoordinatorSnapshot snapshot{
        .State = HubWorkerCoordinatorState::Ready,
        .Revision = 1,
        .Tasks =
            std::make_shared<const std::vector<HubTask>>(std::vector<HubTask>{{.Id = taskId,
                                                                               .Kind = HubTaskKind::HubUpdate,
                                                                               .DisplayName = "Update Kéire Hub",
                                                                               .PackageIds = {candidate.Package.Id},
                                                                               .TargetInstallationId = std::nullopt,
                                                                               .State = HubTaskState::Completed,
                                                                               .Progress = {},
                                                                               .CreatedUnixSeconds = 10,
                                                                               .UpdatedUnixSeconds = 11,
                                                                               .WorkerProcessId = std::nullopt,
                                                                               .Failure = std::nullopt}}),
        .VerifiedDownloads = std::make_shared<const std::vector<HubVerifiedPackageDownload>>(
            std::vector<HubVerifiedPackageDownload>{{.TaskId = taskId,
                                                     .PackageId = candidate.Package.Id,
                                                     .Sha256 = candidate.Package.ArtifactSha256,
                                                     .SizeBytes = candidate.Package.ArtifactSizeBytes,
                                                     .CachePath = DownloadManager::CachePath(download)}}),
        .CompletedEditorInstalls = std::make_shared<const std::vector<HubCompletedEditorInstall>>(),
        .LastFailure = std::nullopt};

    const auto state = InspectHubUpdateWorkflow(candidate, snapshot, cacheRoot);
    REQUIRE(state);
    CHECK(state.Value().State == HubUpdateDownloadState::Available);
    CHECK(state.Value().TaskId.empty());
}
