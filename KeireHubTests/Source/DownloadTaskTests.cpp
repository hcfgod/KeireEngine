#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/DownloadManager.h"
#include "KeireHubRuntime/HubTaskManager.h"
#include "KeireHubRuntime/HubWorkerProtocol.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

using namespace KeireHub;

namespace
{
    constexpr std::string_view Payload = "hello world";
    constexpr std::string_view PayloadSha256 = "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";

    class MemoryStream final : public DownloadByteStream
    {
      public:
        MemoryStream(std::string data, const std::uint64_t offset) : m_Data(std::move(data)), m_Offset(offset) {}

        HubResult<std::size_t> Read(const std::span<std::byte> destination) override
        {
            const auto available = m_Data.size() - std::min<std::size_t>(m_Offset, m_Data.size());
            const auto bytes = std::min({destination.size(), available, std::size_t{4}});
            if (bytes == 0)
                return HubResult<std::size_t>::Success(0);
            std::memcpy(destination.data(), m_Data.data() + m_Offset, bytes);
            m_Offset += bytes;
            return HubResult<std::size_t>::Success(bytes);
        }

      private:
        std::string m_Data;
        std::size_t m_Offset = 0;
    };

    class FakeTransport final : public DownloadTransport
    {
      public:
        HubResult<DownloadTransportResponse> Open(const DownloadTransportRequest& request) override
        {
            Requests.push_back(request);
            if (FailuresRemaining > 0)
            {
                --FailuresRemaining;
                return HubResult<DownloadTransportResponse>::Failure(
                    {.Code = HubErrorCode::DownloadUnavailable,
                     .Message = "The test transport is temporarily unavailable.",
                     .Retryable = true,
                     .AffectedItem = "test.package"});
            }
            const auto offset = request.IfRange.empty() || request.IfRange == ETag ? request.Offset : 0;
            return HubResult<DownloadTransportResponse>::Success(
                {.AcceptedOffset = offset,
                 .TotalBytes = Data.size(),
                 .ETag = ETag,
                 .Body = std::make_unique<MemoryStream>(Data, offset)});
        }

        std::string Data{Payload};
        std::string ETag{"\"test-v1\""};
        std::uint32_t FailuresRemaining = 0;
        std::vector<DownloadTransportRequest> Requests;
    };

    [[nodiscard]] DownloadRequest Request(const std::filesystem::path& root)
    {
        return {.PackageId = "test.package",
                .Url = "https://packages.example/test.package",
                .Sha256 = std::string(PayloadSha256),
                .SizeBytes = Payload.size(),
                .CacheRoot = root,
                .Retry = {.MaximumAttempts = 3,
                          .BaseDelay = std::chrono::milliseconds(10),
                          .MaximumDelay = std::chrono::milliseconds(100),
                          .JitterPermille = 200}};
    }

    [[nodiscard]] HubTask Task(std::string id, const HubTaskKind kind, const std::uint64_t created,
                               std::optional<std::string> target = {}, const bool hasPackage = true)
    {
        HubTask result{.Id = std::move(id),
                       .Kind = kind,
                       .DisplayName = "Test task",
                       .State = HubTaskState::Queued,
                       .CreatedUnixSeconds = created,
                       .UpdatedUnixSeconds = created};
        if (hasPackage)
            result.PackageIds = {result.Id + ".package"};
        result.TargetInstallationId = std::move(target);
        return result;
    }
} // namespace

TEST_CASE("Package downloads publish verified content-addressed cache files")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto request = Request(temporary.Path() / "Cache");
    FakeTransport transport;
    std::vector<std::string> phases;
    DownloadManager manager;
    auto acquired =
        manager.Acquire(request, transport,
                        {.Progress = [&](const DownloadProgress& progress) { phases.push_back(progress.Phase); },
                         .WaitBeforeRetry = [](const auto) {}});
    if (!acquired)
        FAIL_CHECK(ToString(acquired.Error().Code)
                   << ": " << acquired.Error().Message << " / " << acquired.Error().TechnicalDetails);
    REQUIRE(acquired);
    CHECK(acquired.Value().Outcome == DownloadOutcome::Completed);
    CHECK_FALSE(acquired.Value().CacheHit);
    CHECK(acquired.Value().Attempts == 1);
    CHECK(KeireHubTests::ReadText(acquired.Value().CachePath) == Payload);
    CHECK(acquired.Value().CachePath == DownloadManager::CachePath(request));
    CHECK_FALSE(std::filesystem::exists(DownloadManager::PartialPath(request)));
    CHECK_FALSE(std::filesystem::exists(DownloadManager::ResumeMetadataPath(request)));
    CHECK(std::ranges::find(phases, "Verifying") != phases.end());

    FakeTransport unused;
    auto cached = manager.Acquire(request, unused);
    REQUIRE(cached);
    CHECK(cached.Value().CacheHit);
    CHECK(unused.Requests.empty());
}

TEST_CASE("Paused and cancelled downloads preserve resumable partial data")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto request = Request(temporary.Path() / "Cache");
    DownloadManager manager;
    FakeTransport transport;
    bool progressed = false;
    auto paused =
        manager.Acquire(request, transport,
                        {.Control = [&] { return progressed ? DownloadControl::Pause : DownloadControl::Continue; },
                         .Progress = [&](const DownloadProgress&) { progressed = true; },
                         .WaitBeforeRetry = [](const auto) {}});
    if (!paused)
        FAIL_CHECK(ToString(paused.Error().Code)
                   << ": " << paused.Error().Message << " / " << paused.Error().TechnicalDetails);
    REQUIRE(paused);
    CHECK(paused.Value().Outcome == DownloadOutcome::Paused);
    REQUIRE(std::filesystem::exists(DownloadManager::PartialPath(request)));
    const auto pausedBytes = std::filesystem::file_size(DownloadManager::PartialPath(request));
    CHECK(pausedBytes > 0);
    CHECK(pausedBytes < request.SizeBytes);
    CHECK(std::filesystem::exists(DownloadManager::ResumeMetadataPath(request)));

    auto resumed = manager.Acquire(request, transport, {.WaitBeforeRetry = [](const auto) {}});
    REQUIRE(resumed);
    CHECK(resumed.Value().Outcome == DownloadOutcome::Completed);
    REQUIRE(transport.Requests.size() >= 2);
    CHECK(transport.Requests[1].Offset == pausedBytes);
    CHECK(transport.Requests[1].IfRange == "\"test-v1\"");

    const auto cancelRequest = Request(temporary.Path() / "CancelCache");
    FakeTransport cancelTransport;
    progressed = false;
    auto cancelled =
        manager.Acquire(cancelRequest, cancelTransport,
                        {.Control = [&] { return progressed ? DownloadControl::Cancel : DownloadControl::Continue; },
                         .Progress = [&](const DownloadProgress&) { progressed = true; },
                         .WaitBeforeRetry = [](const auto) {}});
    REQUIRE(cancelled);
    CHECK(cancelled.Value().Outcome == DownloadOutcome::Cancelled);
    CHECK(std::filesystem::exists(DownloadManager::PartialPath(cancelRequest)));
    CHECK(std::filesystem::exists(DownloadManager::ResumeMetadataPath(cancelRequest)));
}

TEST_CASE("Bandwidth limits pace downloads with bounded cancellation-responsive waits")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto request = Request(temporary.Path() / "PacedCache");
    request.BandwidthLimitBytesPerSecond = 4;
    FakeTransport transport;
    auto now = std::chrono::steady_clock::time_point{};
    std::vector<std::chrono::milliseconds> waits;
    std::vector<std::uint64_t> speeds;
    DownloadManager manager;
    auto acquired = manager.Acquire(request, transport,
                                    {.Progress =
                                         [&](const DownloadProgress& progress)
                                     {
                                         if (progress.Phase == "Downloading")
                                             speeds.push_back(progress.BytesPerSecond);
                                     },
                                     .MonotonicNow = [&] { return now; },
                                     .WaitForThrottle =
                                         [&](const std::chrono::milliseconds delay)
                                     {
                                         waits.push_back(delay);
                                         now += delay;
                                     }});
    REQUIRE(acquired);
    CHECK(acquired.Value().Outcome == DownloadOutcome::Completed);
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) ==
          std::chrono::milliseconds(2750));
    REQUIRE_FALSE(waits.empty());
    CHECK(std::ranges::all_of(
        waits, [](const auto delay)
        { return delay > std::chrono::milliseconds::zero() && delay <= std::chrono::milliseconds(100); }));
    REQUIRE_FALSE(speeds.empty());
    CHECK(std::ranges::all_of(speeds, [](const auto speed) { return speed <= 4; }));

    auto cancelledRequest = Request(temporary.Path() / "CancelledPacedCache");
    cancelledRequest.BandwidthLimitBytesPerSecond = 1;
    FakeTransport cancelledTransport;
    now = std::chrono::steady_clock::time_point{};
    bool cancel = false;
    waits.clear();
    auto cancelled =
        manager.Acquire(cancelledRequest, cancelledTransport,
                        {.Control = [&] { return cancel ? DownloadControl::Cancel : DownloadControl::Continue; },
                         .MonotonicNow = [&] { return now; },
                         .WaitForThrottle =
                             [&](const std::chrono::milliseconds delay)
                         {
                             waits.push_back(delay);
                             now += delay;
                             cancel = true;
                         }});
    REQUIRE(cancelled);
    CHECK(cancelled.Value().Outcome == DownloadOutcome::Cancelled);
    CHECK(cancelled.Value().BytesTransferred == 1);
    REQUIRE(waits.size() == 1);
    CHECK(waits.front() == std::chrono::milliseconds(100));
    CHECK(std::filesystem::file_size(DownloadManager::PartialPath(cancelledRequest)) == 1);
    CHECK(std::filesystem::exists(DownloadManager::ResumeMetadataPath(cancelledRequest)));
}

TEST_CASE("Bandwidth limit validation is bounded")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto request = Request(temporary.Path() / "Cache");
    request.BandwidthLimitBytesPerSecond = DownloadManager::MaximumBandwidthBytesPerSecond;
    CHECK(DownloadManager::Validate(request));
    request.BandwidthLimitBytesPerSecond = DownloadManager::MaximumBandwidthBytesPerSecond + 1ULL;
    const auto invalid = DownloadManager::Validate(request);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.Error().Code == HubErrorCode::InvalidArgument);
}

TEST_CASE("A changed ETag discards stale partial bytes before restarting")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto request = Request(temporary.Path() / "Cache");
    DownloadManager manager;
    FakeTransport transport;
    bool progressed = false;
    auto first =
        manager.Acquire(request, transport,
                        {.Control = [&] { return progressed ? DownloadControl::Pause : DownloadControl::Continue; },
                         .Progress = [&](const DownloadProgress&) { progressed = true; },
                         .WaitBeforeRetry = [](const auto) {}});
    if (!first)
        FAIL_CHECK(ToString(first.Error().Code)
                   << ": " << first.Error().Message << " / " << first.Error().TechnicalDetails);
    REQUIRE(first);
    const auto partialBytes = std::filesystem::file_size(DownloadManager::PartialPath(request));
    transport.ETag = "\"test-v2\"";
    auto restarted = manager.Acquire(request, transport, {.WaitBeforeRetry = [](const auto) {}});
    REQUIRE(restarted);
    REQUIRE(transport.Requests.size() == 3);
    CHECK(transport.Requests[1].Offset == partialBytes);
    CHECK(transport.Requests[1].IfRange == "\"test-v1\"");
    CHECK(transport.Requests[2].Offset == 0);
    CHECK(transport.Requests[2].IfRange.empty());
    CHECK(KeireHubTests::ReadText(restarted.Value().CachePath) == Payload);
}

TEST_CASE("Download retry and integrity failures remain bounded and typed")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto request = Request(temporary.Path() / "Cache");
    FakeTransport transport;
    transport.FailuresRemaining = 2;
    std::vector<std::chrono::milliseconds> waits;
    DownloadManager manager;
    auto acquired =
        manager.Acquire(request, transport, {.WaitBeforeRetry = [&](const auto delay) { waits.push_back(delay); }});
    if (!acquired)
        FAIL_CHECK(ToString(acquired.Error().Code)
                   << ": " << acquired.Error().Message << " / " << acquired.Error().TechnicalDetails);
    REQUIRE(acquired);
    CHECK(acquired.Value().Attempts == 3);
    REQUIRE(waits.size() == 2);
    CHECK(waits[0] <= request.Retry.MaximumDelay);
    CHECK(waits[1] <= request.Retry.MaximumDelay);

    auto corruptRequest = Request(temporary.Path() / "CorruptCache");
    corruptRequest.Sha256 = std::string(64, '0');
    FakeTransport corruptTransport;
    auto corrupt = manager.Acquire(corruptRequest, corruptTransport, {.WaitBeforeRetry = [](const auto) {}});
    REQUIRE_FALSE(corrupt);
    CHECK(corrupt.Error().Code == HubErrorCode::DownloadChecksumMismatch);
    CHECK(corrupt.Error().Retryable);
    CHECK_FALSE(std::filesystem::exists(DownloadManager::PartialPath(corruptRequest)));
    CHECK_FALSE(std::filesystem::exists(DownloadManager::ResumeMetadataPath(corruptRequest)));
}

TEST_CASE("Task dispatch bounds downloads and serializes installation mutations")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubTaskStore store(temporary.Path() / "tasks.json");
    HubTaskManager manager(store, {.MaximumConcurrentDownloads = 2});
    REQUIRE(manager.Enqueue(Task("download-c", HubTaskKind::Download, 3)));
    REQUIRE(manager.Enqueue(Task("download-a", HubTaskKind::Download, 1)));
    REQUIRE(manager.Enqueue(Task("download-b", HubTaskKind::Download, 2)));
    auto ready = manager.Dispatchable();
    REQUIRE(ready.size() == 2);
    CHECK(ready[0].TaskId == "download-a");
    CHECK(ready[1].TaskId == "download-b");
    REQUIRE(manager.Claim(ready[0], 101, 10));
    REQUIRE(manager.Claim(manager.Dispatchable().front(), 102, 11));
    CHECK(manager.Dispatchable().empty());
    REQUIRE(manager.Advance("download-a", HubTaskState::Verifying, 12));
    ready = manager.Dispatchable();
    REQUIRE(ready.size() == 1);
    CHECK(ready.front().TaskId == "download-c");

    HubTaskStore mutationStore(temporary.Path() / "mutations.json");
    HubTaskManager mutations(mutationStore);
    REQUIRE(mutations.Enqueue(Task("install-a", HubTaskKind::Install, 1, "editor-a", false)));
    REQUIRE(mutations.Enqueue(Task("repair-a", HubTaskKind::Repair, 2, "editor-a", false)));
    REQUIRE(mutations.Enqueue(Task("install-b", HubTaskKind::Install, 3, "editor-b", false)));
    ready = mutations.Dispatchable();
    REQUIRE(ready.size() == 2);
    CHECK(ready[0].TaskId == "install-a");
    CHECK(ready[1].TaskId == "install-b");
    CHECK(ready[0].InitialState == HubTaskState::Installing);

    HubTaskStore removalStore(temporary.Path() / "removals.json");
    HubTaskManager removals(removalStore);
    REQUIRE(removals.Enqueue(Task("remove-a", HubTaskKind::Remove, 1, "editor-a", false)));
    ready = removals.Dispatchable();
    REQUIRE(ready.size() == 1);
    CHECK(ready.front().InitialState == HubTaskState::Removing);

    HubTaskStore packageStore(temporary.Path() / "shared-package.json");
    HubTaskManager packages(packageStore);
    auto firstPackage = Task("package-first", HubTaskKind::Download, 1);
    auto secondPackage = Task("package-second", HubTaskKind::Download, 2);
    firstPackage.PackageIds = {"shared.package"};
    secondPackage.PackageIds = {"shared.package"};
    REQUIRE(packages.Enqueue(std::move(firstPackage)));
    REQUIRE(packages.Enqueue(std::move(secondPackage)));
    ready = packages.Dispatchable();
    REQUIRE(ready.size() == 1);
    CHECK(ready.front().TaskId == "package-first");
}

TEST_CASE("Worker reconciliation resumes downloads and fails interrupted mutations")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubTaskStore store(temporary.Path() / "tasks.json");
    HubTaskManager manager(store);
    REQUIRE(manager.Enqueue(Task("download", HubTaskKind::Download, 1)));
    REQUIRE(manager.Enqueue(Task("install", HubTaskKind::Install, 2, "editor-a", false)));
    REQUIRE(manager.Claim(manager.Dispatchable()[0], 101, 10));
    const auto ready = manager.Dispatchable();
    const auto install = std::ranges::find(ready, "install", &HubTaskDispatch::TaskId);
    REQUIRE(install != ready.end());
    REQUIRE(manager.Claim(*install, 102, 11));
    REQUIRE(manager.ReconcileWorkers(20, [](const std::uint64_t) { return false; }));
    const auto snapshot = store.Snapshot();
    const auto download = std::ranges::find(*snapshot, "download", &HubTask::Id);
    const auto interrupted = std::ranges::find(*snapshot, "install", &HubTask::Id);
    REQUIRE(download != snapshot->end());
    REQUIRE(interrupted != snapshot->end());
    CHECK(download->State == HubTaskState::Queued);
    CHECK_FALSE(download->WorkerProcessId.has_value());
    CHECK(interrupted->State == HubTaskState::Failed);
    REQUIRE(interrupted->Failure);
    CHECK(interrupted->Failure->Code == HubErrorCode::WorkerInterrupted);
    CHECK(interrupted->Failure->Retryable);
}

TEST_CASE("Hub worker request status result and control journals round trip atomically")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto operation = temporary.Path() / "Operation";
    const HubWorkerRequest request{.TaskId = "task-download", .Download = Request(temporary.Path() / "Cache")};
    REQUIRE(WriteHubWorkerRequest(operation / "request.json", request));
    auto decodedRequest = ReadHubWorkerRequest(operation / "request.json");
    REQUIRE(decodedRequest);
    CHECK(decodedRequest.Value().TaskId == request.TaskId);
    CHECK(decodedRequest.Value().Download.Sha256 == request.Download.Sha256);
    CHECK(decodedRequest.Value().Download.BandwidthLimitBytesPerSecond == 0);

    const HubWorkerStatus status{.TaskId = request.TaskId,
                                 .State = HubTaskState::Downloading,
                                 .Progress = {.BytesTransferred = 4,
                                              .TotalBytes = request.Download.SizeBytes,
                                              .BytesPerSecond = 200,
                                              .CurrentPackage = request.Download.PackageId,
                                              .Phase = "Downloading"},
                                 .WorkerProcessId = 42,
                                 .UpdatedUnixSeconds = 100};
    REQUIRE(WriteHubWorkerStatus(operation / "status.json", status));
    auto decodedStatus = ReadHubWorkerStatus(operation / "status.json");
    REQUIRE(decodedStatus);
    CHECK(decodedStatus.Value().WorkerProcessId == 42);
    CHECK(decodedStatus.Value().Progress.BytesTransferred == 4);

    REQUIRE(WriteHubWorkerControl(operation / "control.json", DownloadControl::Pause));
    auto control = ReadHubWorkerControl(operation / "control.json");
    REQUIRE(control);
    CHECK(control.Value() == DownloadControl::Pause);

    const HubWorkerResult result{.TaskId = request.TaskId,
                                 .Outcome = DownloadOutcome::Completed,
                                 .CachePath = DownloadManager::CachePath(request.Download)};
    REQUIRE(WriteHubWorkerResult(operation / "result.json", result));
    auto decodedResult = ReadHubWorkerResult(operation / "result.json");
    REQUIRE(decodedResult);
    CHECK(decodedResult.Value().Outcome == DownloadOutcome::Completed);
    CHECK(decodedResult.Value().CachePath == result.CachePath);

    KeireHubTests::WriteText(operation / "status.json", R"({"schemaVersion":1,"state":"installing"})");
    auto malformed = ReadHubWorkerStatus(operation / "status.json");
    REQUIRE_FALSE(malformed);
    CHECK(malformed.Error().Code == HubErrorCode::WorkerProtocolInvalid);
}

TEST_CASE("Hub worker journal readers preserve retryable filesystem failures")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto missing = ReadHubWorkerStatus(temporary.Path() / "status.json");
    REQUIRE_FALSE(missing);
    CHECK(missing.Error().Code == HubErrorCode::IoRead);
    CHECK(missing.Error().Retryable);
    CHECK(missing.Error().AffectedItem == "status.json");
}

TEST_CASE("Hub worker journals preserve a confined catalog-bound editor install")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto download = Request(temporary.Path() / "Cache");
    auto version = SemanticVersion::Parse("2.0.0");
    REQUIRE(version);
    PackageManifest package{.Id = download.PackageId,
                            .Version = std::move(version).Value(),
                            .Kind = PackageKind::Editor,
                            .DisplayName = "Kéire Editor 2.0.0",
                            .Channel = "stable",
                            .Platform = "windows",
                            .Architecture = "x86_64",
                            .ArtifactSizeBytes = download.SizeBytes,
                            .ArtifactSha256 = download.Sha256,
                            .InstalledSizeBytes = 1,
                            .Files = {{"bin/Editor.exe", 1, KeireHubTests::Digest('b'), 0755U}},
                            .SignatureKeyId = "release-key"};
    const auto installRoot = std::filesystem::absolute(temporary.Path() / "Editors");
    HubWorkerRequest request{
        .TaskId = "install-editor",
        .Download = download,
        .EditorInstall = HubWorkerEditorInstallRequest{.Package = package,
                                                       .PackageSteps = {{.Package = package, .Download = download}},
                                                       .RequestedPackageIds = {package.Id},
                                                       .AllowedInstallRoot = installRoot,
                                                       .Destination = installRoot / "2.0.0",
                                                       .InstallationId = "editor-2-0-0",
                                                       .MarkerNonce = std::string(64, 'a'),
                                                       .HostPlatform = "windows",
                                                       .HostArchitecture = "x86_64",
                                                       .VerifiedUnixSeconds = 123}};
    const auto operation = temporary.Path() / "Operation";
    REQUIRE(WriteHubWorkerRequest(operation / "request.json", request));
    auto decoded = ReadHubWorkerRequest(operation / "request.json");
    REQUIRE(decoded);
    REQUIRE(decoded.Value().EditorInstall);
    CHECK(decoded.Value().EditorInstall->Package.ArtifactSha256 == package.ArtifactSha256);
    CHECK(decoded.Value().EditorInstall->RequestedPackageIds == std::vector<std::string>{package.Id});
    CHECK(decoded.Value().EditorInstall->Destination == installRoot / "2.0.0");
    CHECK(decoded.Value().EditorInstall->MarkerNonce == std::string(64, 'a'));

    auto repair = request;
    repair.TaskId = "repair-editor";
    repair.EditorInstall->Mode = HubWorkerEditorInstallMode::Repair;
    repair.EditorInstall->RepairAuthorization = {.ManifestFingerprint = KeireHubTests::Digest('c'),
                                                 .PackageTreeIdentity = KeireHubTests::Digest('d'),
                                                 .PackageReceiptSha256 = KeireHubTests::Digest('e'),
                                                 .EditorEntrypoint = "bin/Editor.exe"};
    REQUIRE(WriteHubWorkerRequest(operation / "repair-request.json", repair));
    const auto decodedRepair = ReadHubWorkerRequest(operation / "repair-request.json");
    REQUIRE(decodedRepair);
    REQUIRE(decodedRepair.Value().EditorInstall);
    CHECK(decodedRepair.Value().EditorInstall->Mode == HubWorkerEditorInstallMode::Repair);
    REQUIRE(decodedRepair.Value().EditorInstall->RepairAuthorization);
    CHECK(decodedRepair.Value().EditorInstall->RepairAuthorization->EditorEntrypoint == "bin/Editor.exe");

    auto missingAuthorization = repair;
    missingAuthorization.EditorInstall->RepairAuthorization.reset();
    CHECK_FALSE(ValidateHubWorkerRequest(missingAuthorization));
    auto installWithAuthorization = repair;
    installWithAuthorization.EditorInstall->Mode = HubWorkerEditorInstallMode::Install;
    CHECK_FALSE(ValidateHubWorkerRequest(installWithAuthorization));
    auto escapedEntrypoint = repair;
    escapedEntrypoint.EditorInstall->RepairAuthorization->EditorEntrypoint = "../Editor.exe";
    CHECK_FALSE(ValidateHubWorkerRequest(escapedEntrypoint));
#if defined(_WIN32)
    auto rootedEntrypoint = repair;
    rootedEntrypoint.EditorInstall->RepairAuthorization->EditorEntrypoint = "\\Other\\Editor.exe";
    CHECK_FALSE(ValidateHubWorkerRequest(rootedEntrypoint));
    auto driveRelativeEntrypoint = repair;
    driveRelativeEntrypoint.EditorInstall->RepairAuthorization->EditorEntrypoint = "C:Editor.exe";
    CHECK_FALSE(ValidateHubWorkerRequest(driveRelativeEntrypoint));
#endif

    auto unrequested = request;
    auto extraPackage = package;
    extraPackage.Id = "test.toolchain";
    extraPackage.Kind = PackageKind::Toolchain;
    auto extraDownload = download;
    extraDownload.PackageId = extraPackage.Id;
    unrequested.EditorInstall->PackageSteps.push_back({.Package = extraPackage, .Download = extraDownload});
    CHECK_FALSE(ValidateHubWorkerRequest(unrequested));

    auto lateDependency = unrequested;
    auto exactVersion = VersionConstraint::Parse("=2.0.0");
    REQUIRE(exactVersion);
    lateDependency.EditorInstall->Package.Dependencies = {{extraPackage.Id, exactVersion.Value()}};
    lateDependency.EditorInstall->PackageSteps.front().Package = lateDependency.EditorInstall->Package;
    lateDependency.EditorInstall->RequestedPackageIds = {package.Id};
    CHECK_FALSE(ValidateHubWorkerRequest(lateDependency));

    REQUIRE(WriteHubWorkerStatus(
        operation / "status.json",
        {.TaskId = request.TaskId,
         .State = HubTaskState::Installing,
         .Progress = {.TotalBytes = download.SizeBytes, .CurrentPackage = download.PackageId, .Phase = "Publishing"},
         .WorkerProcessId = 42,
         .UpdatedUnixSeconds = 125}));
    auto status = ReadHubWorkerStatus(operation / "status.json");
    REQUIRE(status);
    CHECK(status.Value().State == HubTaskState::Installing);

    HubWorkerResult result{.TaskId = request.TaskId,
                           .Outcome = DownloadOutcome::Completed,
                           .CachePath = DownloadManager::CachePath(download),
                           .InstalledRoot = installRoot / "2.0.0",
                           .InstallationId = "editor-2-0-0"};
    REQUIRE(WriteHubWorkerResult(operation / "result.json", result));
    auto decodedResult = ReadHubWorkerResult(operation / "result.json");
    REQUIRE(decodedResult);
    CHECK(decodedResult.Value().InstalledRoot == result.InstalledRoot);
    CHECK(decodedResult.Value().InstallationId == result.InstallationId);
}

TEST_CASE("Hub worker journals bind managed removal to one exact installation identity")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto allowedRoot = std::filesystem::absolute(temporary.Path() / "Editors");
    const auto root = allowedRoot / "2.0.0";
    HubWorkerRequest request{.TaskId = "remove-editor",
                             .EditorRemoval =
                                 HubWorkerEditorRemovalRequest{.AllowedInstallRoot = allowedRoot,
                                                               .Root = root,
                                                               .InstallationId = "editor-2-0-0",
                                                               .ManifestFingerprint = KeireHubTests::Digest('1'),
                                                               .PackageTreeIdentity = KeireHubTests::Digest('2'),
                                                               .PackageReceiptSha256 = KeireHubTests::Digest('3'),
                                                               .MarkerNonce = std::string(64, 'a')}};
    const auto operation = temporary.Path() / "Operation";
    REQUIRE(WriteHubWorkerRequest(operation / "request.json", request));
    auto decoded = ReadHubWorkerRequest(operation / "request.json");
    REQUIRE(decoded);
    REQUIRE(decoded.Value().EditorRemoval);
    CHECK(decoded.Value().EditorRemoval->Root == root);
    CHECK(decoded.Value().Download.PackageId.empty());

    auto escaped = request;
    escaped.EditorRemoval->Root = allowedRoot.parent_path() / "Other";
    CHECK_FALSE(ValidateHubWorkerRequest(escaped));
    auto ambiguous = request;
    ambiguous.Download = Request(temporary.Path() / "Cache");
    CHECK_FALSE(ValidateHubWorkerRequest(ambiguous));

    const HubWorkerResult result{.TaskId = request.TaskId,
                                 .Outcome = DownloadOutcome::Completed,
                                 .RemovedRoot = root,
                                 .InstallationId = "editor-2-0-0"};
    REQUIRE(WriteHubWorkerResult(operation / "result.json", result));
    auto decodedResult = ReadHubWorkerResult(operation / "result.json");
    REQUIRE(decodedResult);
    CHECK(decodedResult.Value().RemovedRoot == root);
    CHECK(decodedResult.Value().CachePath.empty());
    auto ambiguousResult = result;
    ambiguousResult.InstalledRoot = root;
    CHECK_FALSE(WriteHubWorkerResult(operation / "ambiguous-result.json", ambiguousResult));
}
