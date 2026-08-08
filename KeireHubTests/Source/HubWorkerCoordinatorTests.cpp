#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/HubTaskManager.h"
#include "KeireHubRuntime/HubWorkerCoordinator.h"
#include "KeireHubRuntime/HubWorkerProtocol.h"

#include <KeireHubRuntimeInternal/HubWorkerCoordinatorOperations.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    constexpr std::string_view Payload = "hello world";
    constexpr std::string_view PayloadSha256 = "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";

    struct FakeLaunchRecord final
    {
        HubWorkerLaunch Launch;
        std::uint64_t ProcessId = 0;
    };

    class FakeProcessHost final : public HubWorkerProcessHost
    {
      public:
        using Behavior = std::function<HubStatus(const HubWorkerLaunch&, std::uint64_t, std::size_t)>;

        HubStatus LaunchDetached(const HubWorkerLaunch& launch) override
        {
            Behavior behavior;
            std::uint64_t processId = 0;
            std::size_t index = 0;
            {
                std::scoped_lock lock(m_Mutex);
                processId = m_NextProcessId++;
                index = m_Launches.size();
                m_Launches.push_back({.Launch = launch, .ProcessId = processId});
                if (m_LaunchFailures > 0)
                {
                    --m_LaunchFailures;
                    return HubStatus::Failure({.Code = HubErrorCode::WorkerInterrupted,
                                               .Message = "The fake worker did not start.",
                                               .Retryable = true});
                }
                m_Alive[processId] = true;
                behavior = m_Behavior;
            }
            if (behavior)
                return behavior(launch, processId, index);
            return HubStatus::Success();
        }

        bool IsProcessAlive(const std::uint64_t processId) const noexcept override
        {
            std::scoped_lock lock(m_Mutex);
            const auto found = m_Alive.find(processId);
            return found != m_Alive.end() && found->second;
        }

        void SetAlive(const std::uint64_t processId, const bool alive)
        {
            std::scoped_lock lock(m_Mutex);
            m_Alive[processId] = alive;
        }

        void SetBehavior(Behavior behavior)
        {
            std::scoped_lock lock(m_Mutex);
            m_Behavior = std::move(behavior);
        }

        void FailNextLaunch()
        {
            std::scoped_lock lock(m_Mutex);
            ++m_LaunchFailures;
        }

        [[nodiscard]] std::vector<FakeLaunchRecord> Launches() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Launches;
        }

      private:
        mutable std::mutex m_Mutex;
        std::uint64_t m_NextProcessId = 1000;
        std::size_t m_LaunchFailures = 0;
        std::map<std::uint64_t, bool> m_Alive;
        std::vector<FakeLaunchRecord> m_Launches;
        Behavior m_Behavior;
    };

    [[nodiscard]] PackageManifest Package()
    {
        return {.Id = "test.package",
                .Version = {.Major = 1},
                .Kind = PackageKind::Template,
                .DisplayName = "Test Package",
                .Channel = "stable",
                .Platform = "any",
                .Architecture = "any",
                .ArtifactSizeBytes = Payload.size(),
                .ArtifactSha256 = std::string(PayloadSha256),
                .InstalledSizeBytes = 1,
                .Files = {{.Path = "payload.bin", .SizeBytes = 1, .Sha256 = KeireHubTests::Digest()}},
                .SignatureKeyId = "test-key"};
    }

    [[nodiscard]] CatalogPackageDownloadRequest Download(const std::filesystem::path& root,
                                                         std::string taskId = "download-test")
    {
        return {.TaskId = std::move(taskId),
                .Package = Package(),
                .PackageUrl = "https://packages.example/v1/packages/" + std::string(PayloadSha256),
                .CacheRoot = root / "Cache",
                .BandwidthLimitBytesPerSecond = 4096};
    }

    [[nodiscard]] CatalogEditorInstallRequest EditorInstall(const std::filesystem::path& root)
    {
        auto editor = Download(root, "install-editor");
        editor.Package.Kind = PackageKind::Editor;
        editor.Package.Platform = "windows";
        editor.Package.Architecture = "x86_64";
        auto toolchain = editor;
        toolchain.Package.Id = "test.toolchain";
        toolchain.Package.Kind = PackageKind::Toolchain;
        toolchain.Package.DisplayName = "Test Toolchain";
        toolchain.Package.Files.front().Path = "toolchain.bin";
        auto editorPackage = editor.Package;
        return {.Download = std::move(toolchain),
                .AdditionalDownloads = {std::move(editor)},
                .EditorPackage = std::move(editorPackage),
                .RequestedPackageIds = {"test.package", "test.toolchain"},
                .AllowedInstallRoot = root / "Editors",
                .Destination = root / "Editors" / "1.0.0",
                .InstallationId = "editor-1",
                .MarkerNonce = std::string(64, 'a'),
                .HostPlatform = "windows",
                .HostArchitecture = "x86_64",
                .VerifiedUnixSeconds = 10};
    }

    [[nodiscard]] CatalogEditorRemovalRequest EditorRemoval(const std::filesystem::path& root,
                                                            std::string taskId = "remove-editor")
    {
        return {.TaskId = std::move(taskId),
                .AllowedInstallRoot = root / "Editors",
                .Root = root / "Editors" / "1.0.0",
                .InstallationId = "editor-1",
                .ManifestFingerprint = KeireHubTests::Digest(),
                .PackageTreeIdentity = std::string(64, 'b'),
                .PackageReceiptSha256 = std::string(64, 'c'),
                .MarkerNonce = std::string(64, 'd')};
    }

    [[nodiscard]] DownloadRequest WorkerDownload(const CatalogPackageDownloadRequest& request)
    {
        return {.PackageId = request.Package.Id,
                .Url = request.PackageUrl,
                .Sha256 = request.Package.ArtifactSha256,
                .SizeBytes = request.Package.ArtifactSizeBytes,
                .CacheRoot = request.CacheRoot,
                .Retry = request.Retry,
                .AllowInsecureLoopbackDevelopment = request.AllowInsecureLoopbackDevelopment,
                .CustomProxyUrl = request.CustomProxyUrl,
                .BandwidthLimitBytesPerSecond = request.BandwidthLimitBytesPerSecond};
    }

    [[nodiscard]] HubWorkerCoordinatorSpecification Specification(const std::filesystem::path& root)
    {
        return {.TaskStorePath = root / "State" / "tasks.json",
                .OperationRoot = root / "Operations",
                .WorkerExecutable = root / "Bin" / "KeireHubWorker",
                .MaximumConcurrentDownloads = 2,
                .MaximumPendingCommands = 16,
                .PollInterval = std::chrono::milliseconds(2),
                .WorkerStartupTimeout = std::chrono::seconds(2)};
    }

    [[nodiscard]] std::filesystem::path ArgumentPath(const HubWorkerLaunch& launch, const std::string_view option)
    {
        const auto found = std::ranges::find_if(launch.Arguments, [&](const std::string& argument)
                                                { return std::string_view(argument) == option; });
        if (found == launch.Arguments.end() || std::next(found) == launch.Arguments.end())
            return {};
        return std::filesystem::path(*std::next(found));
    }

    [[nodiscard]] HubStatus PublishStatus(const HubWorkerLaunch& launch, const std::uint64_t processId,
                                          const HubTaskState state, HubTaskProgress progress)
    {
        auto request = ReadHubWorkerRequest(ArgumentPath(launch, "--request"));
        if (!request)
            return HubStatus::Failure(request.Error());
        return WriteHubWorkerStatus(ArgumentPath(launch, "--status"), {.TaskId = request.Value().TaskId,
                                                                       .State = state,
                                                                       .Progress = std::move(progress),
                                                                       .WorkerProcessId = processId,
                                                                       .UpdatedUnixSeconds = 10});
    }

    [[nodiscard]] HubStatus PublishResult(const HubWorkerLaunch& launch, const DownloadOutcome outcome,
                                          std::optional<HubError> failure = {})
    {
        auto request = ReadHubWorkerRequest(ArgumentPath(launch, "--request"));
        if (!request)
            return HubStatus::Failure(request.Error());
        const auto installedRoot = outcome == DownloadOutcome::Completed && request.Value().EditorInstall
                                       ? request.Value().EditorInstall->Destination
                                       : std::filesystem::path{};
        const auto installationId = outcome == DownloadOutcome::Completed && request.Value().EditorInstall
                                        ? request.Value().EditorInstall->InstallationId
                                        : std::string{};
        return WriteHubWorkerResult(ArgumentPath(launch, "--result"),
                                    {.TaskId = request.Value().TaskId,
                                     .Outcome = outcome,
                                     .CachePath = outcome == DownloadOutcome::Completed
                                                      ? DownloadManager::CachePath(request.Value().Download)
                                                      : std::filesystem::path{},
                                     .InstalledRoot = installedRoot,
                                     .InstallationId = installationId,
                                     .Failure = std::move(failure)});
    }

    [[nodiscard]] HubStatus PublishCompleted(const HubWorkerLaunch& launch, const std::uint64_t processId)
    {
        auto request = ReadHubWorkerRequest(ArgumentPath(launch, "--request"));
        if (!request)
            return HubStatus::Failure(request.Error());
        if (auto status = PublishStatus(launch, processId, HubTaskState::Downloading,
                                        {.BytesTransferred = request.Value().Download.SizeBytes,
                                         .TotalBytes = request.Value().Download.SizeBytes,
                                         .CurrentPackage = request.Value().Download.PackageId,
                                         .Phase = "Downloading"});
            !status)
        {
            return status;
        }
        return PublishResult(launch, DownloadOutcome::Completed);
    }

    [[nodiscard]] HubStatus PublishFailed(const HubWorkerLaunch& launch, const std::uint64_t processId,
                                          HubError failure)
    {
        if (auto status = PublishStatus(launch, processId, HubTaskState::Failed,
                                        {.CurrentPackage = "test.package", .Phase = "Failed"});
            !status)
        {
            return status;
        }
        return PublishResult(launch, DownloadOutcome::Failed, std::move(failure));
    }

    [[nodiscard]] HubStatus PublishRemovalCompleted(const HubWorkerLaunch& launch, const std::uint64_t processId)
    {
        auto request = ReadHubWorkerRequest(ArgumentPath(launch, "--request"));
        if (!request)
            return HubStatus::Failure(request.Error());
        if (!request.Value().EditorRemoval)
        {
            return HubStatus::Failure({.Code = HubErrorCode::WorkerProtocolInvalid,
                                       .Message = "The fake worker expected an editor-removal request."});
        }
        const auto& removal = *request.Value().EditorRemoval;
        if (auto status = PublishStatus(launch, processId, HubTaskState::Installing,
                                        {.CurrentPackage = removal.InstallationId, .Phase = "Removing editor"});
            !status)
        {
            return status;
        }
        return WriteHubWorkerResult(ArgumentPath(launch, "--result"), {.TaskId = request.Value().TaskId,
                                                                       .Outcome = DownloadOutcome::Completed,
                                                                       .RemovedRoot = removal.Root,
                                                                       .InstallationId = removal.InstallationId});
    }

    template <typename Predicate>
    [[nodiscard]] bool WaitUntil(Predicate predicate, const std::chrono::milliseconds timeout = std::chrono::seconds(3))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return predicate();
    }

    [[nodiscard]] std::optional<HubTask> FindTask(const HubWorkerCoordinator& coordinator,
                                                  const std::string_view taskId)
    {
        const auto snapshot = coordinator.Snapshot();
        if (!snapshot->Tasks)
            return std::nullopt;
        const auto found = std::ranges::find_if(*snapshot->Tasks, [&](const HubTask& task)
                                                { return std::string_view(task.Id) == taskId; });
        if (found == snapshot->Tasks->end())
            return std::nullopt;
        return *found;
    }

    [[nodiscard]] bool HasState(const HubWorkerCoordinator& coordinator, const std::string_view taskId,
                                const HubTaskState state)
    {
        const auto task = FindTask(coordinator, taskId);
        return task && task->State == state;
    }

    [[nodiscard]] HubStatus SeedActiveTask(const HubWorkerCoordinatorSpecification& specification,
                                           const CatalogPackageDownloadRequest& download, const std::uint64_t processId)
    {
        HubTaskStore store(specification.TaskStorePath);
        HubTaskManager manager(store);
        if (auto status = manager.Enqueue({.Id = download.TaskId,
                                           .Kind = HubTaskKind::Download,
                                           .DisplayName = "Download " + download.Package.DisplayName,
                                           .PackageIds = {download.Package.Id},
                                           .State = HubTaskState::Queued,
                                           .CreatedUnixSeconds = 1,
                                           .UpdatedUnixSeconds = 1});
            !status)
        {
            return status;
        }
        if (auto status = manager.Claim(manager.Dispatchable().front(), processId, 2); !status)
            return status;
        const auto operation = specification.OperationRoot / download.TaskId;
        if (auto status = WriteHubWorkerRequest(operation / "request.json",
                                                {.TaskId = download.TaskId, .Download = WorkerDownload(download)});
            !status)
        {
            return status;
        }
        if (auto status = WriteHubWorkerControl(operation / "control.json", DownloadControl::Continue); !status)
            return status;
        return WriteHubWorkerStatus(operation / "status.json",
                                    {.TaskId = download.TaskId,
                                     .State = HubTaskState::Downloading,
                                     .Progress = {.TotalBytes = download.Package.ArtifactSizeBytes,
                                                  .CurrentPackage = download.Package.Id,
                                                  .Phase = "Downloading"},
                                     .WorkerProcessId = processId,
                                     .UpdatedUnixSeconds = 2});
    }
} // namespace

TEST_CASE("Worker coordinator queues catalog packages and publishes immutable completion snapshots")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto host = std::make_unique<FakeProcessHost>();
    auto* fake = host.get();
    fake->SetBehavior(
        [fake](const HubWorkerLaunch& launch, const std::uint64_t processId, const std::size_t)
        {
            const auto status = PublishCompleted(launch, processId);
            fake->SetAlive(processId, false);
            return status;
        });

    auto created = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::move(host));
    REQUIRE(created);
    auto coordinator = std::move(created).Value();
    const auto initial = coordinator->Snapshot();
    REQUIRE(coordinator->QueuePackageDownload(Download(temporary.Path())));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "download-test", HubTaskState::Completed); }));

    const auto completed = coordinator->Snapshot();
    CHECK(completed != initial);
    CHECK(completed->Revision > initial->Revision);
    REQUIRE(completed->Tasks);
    REQUIRE(completed->Tasks->size() == 1);
    CHECK(completed->Tasks->front().Progress.BytesTransferred == Payload.size());
    CHECK_FALSE(completed->Tasks->front().WorkerProcessId.has_value());
    REQUIRE(completed->VerifiedDownloads);
    REQUIRE(completed->VerifiedDownloads->size() == 1);
    CHECK(completed->VerifiedDownloads->front().TaskId == "download-test");
    CHECK(completed->VerifiedDownloads->front().PackageId == "test.package");
    CHECK(std::string_view(completed->VerifiedDownloads->front().Sha256) == PayloadSha256);
    CHECK(completed->VerifiedDownloads->front().CachePath ==
          DownloadManager::CachePath(WorkerDownload(Download(temporary.Path()))));
    const auto launches = fake->Launches();
    REQUIRE(launches.size() == 1);
    const auto& launch = launches.front().Launch;
    CHECK(launch.Executable == Specification(temporary.Path()).WorkerExecutable);
    CHECK_FALSE(ArgumentPath(launch, "--request").empty());
    CHECK_FALSE(ArgumentPath(launch, "--control").empty());
    const auto workerRequest = ReadHubWorkerRequest(ArgumentPath(launch, "--request"));
    REQUIRE(workerRequest);
    CHECK(workerRequest.Value().Download.BandwidthLimitBytesPerSecond == 4096);

    coordinator->Stop();
    HubTaskStore reloaded(Specification(temporary.Path()).TaskStorePath);
    REQUIRE(reloaded.Load());
    REQUIRE(reloaded.Snapshot()->size() == 1);
    CHECK(reloaded.Snapshot()->front().State == HubTaskState::Completed);

    auto restarted = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::make_unique<FakeProcessHost>());
    REQUIRE(restarted);
    auto recovered = std::move(restarted).Value();
    REQUIRE(WaitUntil([&] { return recovered->Snapshot()->State == HubWorkerCoordinatorState::Ready; }));
    REQUIRE(recovered->Snapshot()->VerifiedDownloads);
    REQUIRE(recovered->Snapshot()->VerifiedDownloads->size() == 1);
    CHECK(std::string_view(recovered->Snapshot()->VerifiedDownloads->front().Sha256) == PayloadSha256);
}

TEST_CASE("Worker coordinator preserves Hub update task identity through completion and restart")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto host = std::make_unique<FakeProcessHost>();
    auto* fake = host.get();
    fake->SetBehavior(
        [fake](const HubWorkerLaunch& launch, const std::uint64_t processId, const std::size_t)
        {
            const auto status = PublishCompleted(launch, processId);
            fake->SetAlive(processId, false);
            return status;
        });

    auto request = Download(temporary.Path(), "hub-update-test");
    request.Package.Kind = PackageKind::HubInstaller;
    request.Package.DisplayName = "Kéire Hub 2.0.0";
    auto created = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::move(host));
    REQUIRE(created);
    auto coordinator = std::move(created).Value();
    REQUIRE(coordinator->QueueHubUpdate(request));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "hub-update-test", HubTaskState::Completed); }));

    const auto completed = coordinator->Snapshot();
    REQUIRE(completed->Tasks);
    REQUIRE(completed->Tasks->size() == 1);
    CHECK(completed->Tasks->front().Kind == HubTaskKind::HubUpdate);
    CHECK(completed->Tasks->front().DisplayName == "Update Kéire Hub 2.0.0");
    REQUIRE(completed->VerifiedDownloads);
    REQUIRE(completed->VerifiedDownloads->size() == 1);
    CHECK(completed->VerifiedDownloads->front().TaskId == "hub-update-test");

    coordinator->Stop();
    auto restarted = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::make_unique<FakeProcessHost>());
    REQUIRE(restarted);
    auto recovered = std::move(restarted).Value();
    REQUIRE(WaitUntil([&] { return recovered->Snapshot()->State == HubWorkerCoordinatorState::Ready; }));
    REQUIRE(recovered->Snapshot()->Tasks);
    REQUIRE(recovered->Snapshot()->Tasks->size() == 1);
    CHECK(recovered->Snapshot()->Tasks->front().Kind == HubTaskKind::HubUpdate);
    REQUIRE(recovered->Snapshot()->VerifiedDownloads);
    REQUIRE(recovered->Snapshot()->VerifiedDownloads->size() == 1);
}

TEST_CASE("Worker coordinator preserves a worker failure that completes before its first poll")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto host = std::make_unique<FakeProcessHost>();
    auto* fake = host.get();
    fake->SetBehavior(
        [fake](const HubWorkerLaunch& launch, const std::uint64_t processId, const std::size_t)
        {
            const auto status = PublishFailed(launch, processId,
                                              {.Code = HubErrorCode::UnsafeInstallRoot,
                                               .Message = "The selected editor root is unavailable.",
                                               .AffectedItem = "test.package"});
            fake->SetAlive(processId, false);
            return status;
        });

    auto created = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::move(host));
    REQUIRE(created);
    auto coordinator = std::move(created).Value();
    REQUIRE(coordinator->QueuePackageDownload(Download(temporary.Path(), "immediate-worker-failure")));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "immediate-worker-failure", HubTaskState::Failed); }));

    const auto task = FindTask(*coordinator, "immediate-worker-failure");
    REQUIRE(task);
    REQUIRE(task->Failure);
    CHECK(task->Failure->Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(task->Failure->Message == "The selected editor root is unavailable.");
}

TEST_CASE("Worker coordinator rejects non-installer packages as Hub updates")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto created = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::make_unique<FakeProcessHost>());
    REQUIRE(created);
    auto coordinator = std::move(created).Value();

    const auto status = coordinator->QueueHubUpdate(Download(temporary.Path(), "invalid-hub-update"));
    REQUIRE_FALSE(status);
    CHECK(status.Error().Code == HubErrorCode::PackageManifestInvalid);
}

TEST_CASE("Worker coordinator preserves catalog-bound editor installs through completion and restart")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto host = std::make_unique<FakeProcessHost>();
    auto* fake = host.get();
    fake->SetBehavior(
        [fake](const HubWorkerLaunch& launch, const std::uint64_t processId, const std::size_t)
        {
            const auto status = PublishCompleted(launch, processId);
            fake->SetAlive(processId, false);
            return status;
        });

    auto created = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::move(host));
    REQUIRE(created);
    auto coordinator = std::move(created).Value();
    const auto install = EditorInstall(temporary.Path());
    CHECK_FALSE(std::filesystem::exists(install.AllowedInstallRoot));
    REQUIRE(coordinator->QueueEditorInstall(install));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "install-editor", HubTaskState::Completed); }));
    CHECK(std::filesystem::is_directory(install.AllowedInstallRoot));

    const auto completed = coordinator->Snapshot();
    REQUIRE(completed->Tasks);
    REQUIRE(completed->Tasks->size() == 1);
    CHECK(completed->Tasks->front().Kind == HubTaskKind::Install);
    CHECK(completed->Tasks->front().PackageIds == std::vector<std::string>{"test.toolchain", "test.package"});
    CHECK(completed->Tasks->front().TargetInstallationId == "editor-1");
    REQUIRE(completed->CompletedEditorInstalls);
    REQUIRE(completed->CompletedEditorInstalls->size() == 1);
    CHECK(completed->CompletedEditorInstalls->front().TaskId == "install-editor");
    CHECK(completed->CompletedEditorInstalls->front().InstallationId == "editor-1");
    CHECK(completed->CompletedEditorInstalls->front().PackageId == "test.package");
    CHECK(completed->CompletedEditorInstalls->front().Root == install.Destination);
    REQUIRE(fake->Launches().size() == 1);
    const auto workerRequest = ReadHubWorkerRequest(ArgumentPath(fake->Launches().front().Launch, "--request"));
    REQUIRE(workerRequest);
    REQUIRE(workerRequest.Value().EditorInstall);
    REQUIRE(workerRequest.Value().EditorInstall->PackageSteps.size() == 2);
    CHECK(workerRequest.Value().EditorInstall->PackageSteps.front().Package.Id == "test.toolchain");
    CHECK(workerRequest.Value().EditorInstall->PackageSteps.back().Package.Id == "test.package");
    CHECK(workerRequest.Value().EditorInstall->Package.ArtifactSha256 == install.Download.Package.ArtifactSha256);
    CHECK(workerRequest.Value().EditorInstall->Destination == install.Destination);
    CHECK(workerRequest.Value().EditorInstall->MarkerNonce == install.MarkerNonce);

    coordinator->Stop();
    auto restarted = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::make_unique<FakeProcessHost>());
    REQUIRE(restarted);
    auto recovered = std::move(restarted).Value();
    REQUIRE(WaitUntil([&] { return recovered->Snapshot()->State == HubWorkerCoordinatorState::Ready; }));
    REQUIRE(recovered->Snapshot()->CompletedEditorInstalls);
    REQUIRE(recovered->Snapshot()->CompletedEditorInstalls->size() == 1);
    CHECK(recovered->Snapshot()->CompletedEditorInstalls->front().Root == install.Destination);
}

TEST_CASE("Worker coordinator persists managed editor repairs as a distinct mutation kind")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto host = std::make_unique<FakeProcessHost>();
    auto* fake = host.get();
    fake->SetBehavior(
        [fake](const HubWorkerLaunch& launch, const std::uint64_t processId, const std::size_t)
        {
            const auto status = PublishCompleted(launch, processId);
            fake->SetAlive(processId, false);
            return status;
        });
    auto created = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::move(host));
    REQUIRE(created);
    auto coordinator = std::move(created).Value();
    auto install = EditorInstall(temporary.Path());
    install.Download.TaskId = "repair-editor";
    for (auto& additional : install.AdditionalDownloads)
        additional.TaskId = install.Download.TaskId;
    const CatalogEditorRepairRequest repair{.Install = install,
                                            .ManifestFingerprint = KeireHubTests::Digest('1'),
                                            .PackageTreeIdentity = KeireHubTests::Digest('2'),
                                            .PackageReceiptSha256 = KeireHubTests::Digest('3'),
                                            .EditorEntrypoint = "bin/Editor.exe"};
    REQUIRE(coordinator->QueueEditorRepair(repair));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "repair-editor", HubTaskState::Completed); }));

    const auto snapshot = coordinator->Snapshot();
    REQUIRE(snapshot->Tasks);
    REQUIRE(snapshot->Tasks->size() == 1U);
    CHECK(snapshot->Tasks->front().Kind == HubTaskKind::Repair);
    CHECK(snapshot->Tasks->front().TargetInstallationId == install.InstallationId);
    REQUIRE(fake->Launches().size() == 1U);
    const auto workerRequest = ReadHubWorkerRequest(ArgumentPath(fake->Launches().front().Launch, "--request"));
    REQUIRE(workerRequest);
    REQUIRE(workerRequest.Value().EditorInstall);
    CHECK(workerRequest.Value().EditorInstall->Mode == HubWorkerEditorInstallMode::Repair);
    REQUIRE(workerRequest.Value().EditorInstall->RepairAuthorization);
    CHECK(workerRequest.Value().EditorInstall->RepairAuthorization->PackageTreeIdentity == repair.PackageTreeIdentity);
    REQUIRE(snapshot->CompletedEditorInstalls);
    REQUIRE(snapshot->CompletedEditorInstalls->size() == 1U);
    const auto& completedRepair = snapshot->CompletedEditorInstalls->front();
    CHECK(completedRepair.RepairsExisting);
    CHECK(completedRepair.ManifestFingerprint == repair.ManifestFingerprint);
    CHECK(completedRepair.PackageTreeIdentity == repair.PackageTreeIdentity);
    CHECK(completedRepair.PackageReceiptSha256 == repair.PackageReceiptSha256);
    CHECK(completedRepair.MarkerNonce == install.MarkerNonce);

    coordinator->Stop();
    auto restarted = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::make_unique<FakeProcessHost>());
    REQUIRE(restarted);
    auto recovered = std::move(restarted).Value();
    REQUIRE(WaitUntil([&] { return recovered->Snapshot()->State == HubWorkerCoordinatorState::Ready; }));
    REQUIRE(recovered->Snapshot()->CompletedEditorInstalls);
    REQUIRE(recovered->Snapshot()->CompletedEditorInstalls->size() == 1U);
    CHECK(recovered->Snapshot()->CompletedEditorInstalls->front().TaskId == "repair-editor");
    CHECK(recovered->Snapshot()->CompletedEditorInstalls->front().InstallationId == install.InstallationId);
    CHECK(recovered->Snapshot()->CompletedEditorInstalls->front().RepairsExisting);
    CHECK(recovered->Snapshot()->CompletedEditorInstalls->front().ManifestFingerprint == repair.ManifestFingerprint);
    CHECK(recovered->Snapshot()->CompletedEditorInstalls->front().PackageTreeIdentity == repair.PackageTreeIdentity);
    CHECK(recovered->Snapshot()->CompletedEditorInstalls->front().PackageReceiptSha256 == repair.PackageReceiptSha256);
    CHECK(recovered->Snapshot()->CompletedEditorInstalls->front().MarkerNonce == install.MarkerNonce);
}

TEST_CASE("Worker coordinator refuses a persisted repair request stored under an install task")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto specification = Specification(temporary.Path());
    auto install = EditorInstall(temporary.Path());
    install.Download.TaskId = "mismatched-editor-mode";
    for (auto& additional : install.AdditionalDownloads)
        additional.TaskId = install.Download.TaskId;
    const CatalogEditorRepairRequest repair{.Install = install,
                                            .ManifestFingerprint = KeireHubTests::Digest('1'),
                                            .PackageTreeIdentity = KeireHubTests::Digest('2'),
                                            .PackageReceiptSha256 = KeireHubTests::Digest('3'),
                                            .EditorEntrypoint = "bin/Editor.exe"};
    const auto workerRequest = Detail::CreateEditorRepairWorkerRequest(repair);
    HubTaskStore store(specification.TaskStorePath);
    HubTaskManager manager(store);
    REQUIRE(manager.Enqueue({.Id = workerRequest.TaskId,
                             .Kind = HubTaskKind::Install,
                             .DisplayName = "Install mismatched editor",
                             .PackageIds = Detail::WorkerRequestPackageIds(workerRequest),
                             .TargetInstallationId = install.InstallationId,
                             .State = HubTaskState::Queued,
                             .CreatedUnixSeconds = 1,
                             .UpdatedUnixSeconds = 1}));
    const auto operation = specification.OperationRoot / workerRequest.TaskId;
    std::filesystem::create_directories(operation);
    REQUIRE(WriteHubWorkerRequest(operation / "request.json", workerRequest));

    auto host = std::make_unique<FakeProcessHost>();
    auto* fake = host.get();
    auto created = HubWorkerCoordinator::Create(specification, std::move(host));
    REQUIRE(created);
    auto coordinator = std::move(created).Value();
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, workerRequest.TaskId, HubTaskState::Failed); }));
    CHECK(fake->Launches().empty());
    const auto failed = FindTask(*coordinator, workerRequest.TaskId);
    REQUIRE(failed);
    REQUIRE(failed->Failure);
    CHECK(failed->Failure->Code == HubErrorCode::WorkerProtocolInvalid);
}

TEST_CASE("Worker coordinator preserves exact managed-editor removal proof through completion and restart")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto host = std::make_unique<FakeProcessHost>();
    auto* fake = host.get();
    fake->SetBehavior(
        [fake](const HubWorkerLaunch& launch, const std::uint64_t processId, const std::size_t)
        {
            const auto status = PublishRemovalCompleted(launch, processId);
            fake->SetAlive(processId, false);
            return status;
        });

    const auto removal = EditorRemoval(temporary.Path());
    auto created = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::move(host));
    REQUIRE(created);
    auto coordinator = std::move(created).Value();
    REQUIRE(coordinator->QueueEditorRemoval(removal));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, removal.TaskId, HubTaskState::Completed); }));

    const auto completed = coordinator->Snapshot();
    REQUIRE(completed->Tasks);
    REQUIRE(completed->Tasks->size() == 1);
    CHECK(completed->Tasks->front().Kind == HubTaskKind::Remove);
    CHECK(completed->Tasks->front().PackageIds.empty());
    CHECK(completed->Tasks->front().TargetInstallationId == removal.InstallationId);
    REQUIRE(completed->CompletedEditorRemovals);
    REQUIRE(completed->CompletedEditorRemovals->size() == 1);
    const auto& proof = completed->CompletedEditorRemovals->front().Proof;
    CHECK(proof.InstallationId == removal.InstallationId);
    CHECK(proof.Root == removal.Root);
    CHECK(proof.ManifestFingerprint == removal.ManifestFingerprint);
    CHECK(proof.PackageTreeIdentity == removal.PackageTreeIdentity);
    CHECK(proof.PackageReceiptSha256 == removal.PackageReceiptSha256);
    CHECK(proof.MarkerNonce == removal.MarkerNonce);
    CHECK(completed->VerifiedDownloads->empty());

    REQUIRE(fake->Launches().size() == 1);
    const auto workerRequest = ReadHubWorkerRequest(ArgumentPath(fake->Launches().front().Launch, "--request"));
    REQUIRE(workerRequest);
    REQUIRE(workerRequest.Value().EditorRemoval);
    CHECK(workerRequest.Value().Download.PackageId.empty());
    CHECK(workerRequest.Value().EditorRemoval->Root == removal.Root);

    coordinator->Stop();
    auto restarted = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::make_unique<FakeProcessHost>());
    REQUIRE(restarted);
    auto recovered = std::move(restarted).Value();
    REQUIRE(WaitUntil([&] { return recovered->Snapshot()->State == HubWorkerCoordinatorState::Ready; }));
    REQUIRE(recovered->Snapshot()->CompletedEditorRemovals);
    REQUIRE(recovered->Snapshot()->CompletedEditorRemovals->size() == 1);
    CHECK(recovered->Snapshot()->CompletedEditorRemovals->front().Proof.Root == removal.Root);
    CHECK(recovered->Snapshot()->CompletedEditorRemovals->front().Proof.PackageTreeIdentity ==
          removal.PackageTreeIdentity);
}

TEST_CASE("Worker coordinator pause resume and cancellation control the detached worker journal")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto host = std::make_unique<FakeProcessHost>();
    auto* fake = host.get();
    fake->SetBehavior(
        [](const HubWorkerLaunch& launch, const std::uint64_t processId, const std::size_t)
        {
            auto request = ReadHubWorkerRequest(ArgumentPath(launch, "--request"));
            if (!request)
                return HubStatus::Failure(request.Error());
            return PublishStatus(launch, processId, HubTaskState::Downloading,
                                 {.TotalBytes = request.Value().Download.SizeBytes,
                                  .CurrentPackage = request.Value().Download.PackageId,
                                  .Phase = "Downloading"});
        });

    auto created = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::move(host));
    REQUIRE(created);
    auto coordinator = std::move(created).Value();
    REQUIRE(coordinator->QueuePackageDownload(Download(temporary.Path(), "controlled-download")));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "controlled-download", HubTaskState::Downloading); }));
    REQUIRE(coordinator->Pause("controlled-download"));
    REQUIRE(WaitUntil(
        [&]
        {
            const auto launches = fake->Launches();
            if (launches.empty())
                return false;
            const auto control = ReadHubWorkerControl(ArgumentPath(launches.back().Launch, "--control"));
            return control && control.Value() == DownloadControl::Pause;
        }));

    auto launches = fake->Launches();
    REQUIRE(launches.size() == 1);
    REQUIRE(PublishStatus(
        launches.back().Launch, launches.back().ProcessId, HubTaskState::Paused,
        {.BytesTransferred = 4, .TotalBytes = Payload.size(), .CurrentPackage = "test.package", .Phase = "Paused"}));
    REQUIRE(PublishResult(launches.back().Launch, DownloadOutcome::Paused));
    fake->SetAlive(launches.back().ProcessId, false);
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "controlled-download", HubTaskState::Paused); }));

    REQUIRE(coordinator->Resume("controlled-download"));
    REQUIRE(WaitUntil(
        [&]
        {
            return fake->Launches().size() == 2 &&
                   HasState(*coordinator, "controlled-download", HubTaskState::Downloading);
        }));
    launches = fake->Launches();
    REQUIRE(coordinator->Cancel("controlled-download"));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "controlled-download", HubTaskState::Cancelling); }));
    REQUIRE(PublishStatus(
        launches.back().Launch, launches.back().ProcessId, HubTaskState::Cancelled,
        {.BytesTransferred = 4, .TotalBytes = Payload.size(), .CurrentPackage = "test.package", .Phase = "Cancelled"}));
    REQUIRE(PublishResult(launches.back().Launch, DownloadOutcome::Cancelled));
    fake->SetAlive(launches.back().ProcessId, false);
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "controlled-download", HubTaskState::Cancelled); }));
}

TEST_CASE("Worker coordinator exposes retryable launch failures and retries from preserved requests")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto host = std::make_unique<FakeProcessHost>();
    auto* fake = host.get();
    fake->FailNextLaunch();
    fake->SetBehavior(
        [fake](const HubWorkerLaunch& launch, const std::uint64_t processId, const std::size_t)
        {
            const auto status = PublishCompleted(launch, processId);
            fake->SetAlive(processId, false);
            return status;
        });

    auto created = HubWorkerCoordinator::Create(Specification(temporary.Path()), std::move(host));
    REQUIRE(created);
    auto coordinator = std::move(created).Value();
    REQUIRE(coordinator->QueuePackageDownload(Download(temporary.Path(), "retry-download")));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "retry-download", HubTaskState::Failed); }));
    const auto failed = FindTask(*coordinator, "retry-download");
    REQUIRE(failed);
    REQUIRE(failed->Failure);
    CHECK(failed->Failure->Code == HubErrorCode::WorkerInterrupted);
    CHECK(failed->Failure->Retryable);
    REQUIRE(coordinator->Retry("retry-download"));
    REQUIRE(WaitUntil([&] { return HasState(*coordinator, "retry-download", HubTaskState::Completed); }));
    CHECK(fake->Launches().size() == 2);
}

TEST_CASE("Worker coordinator resumes dead downloads and adopts live workers after restart")
{
    SUBCASE("dead workers resume from the preserved download request")
    {
        KeireHubTests::TemporaryDirectory temporary;
        const auto specification = Specification(temporary.Path());
        const auto download = Download(temporary.Path(), "restart-dead");
        REQUIRE(SeedActiveTask(specification, download, 700));

        auto host = std::make_unique<FakeProcessHost>();
        auto* fake = host.get();
        fake->SetAlive(700, false);
        fake->SetBehavior(
            [fake](const HubWorkerLaunch& launch, const std::uint64_t processId, const std::size_t)
            {
                const auto status = PublishCompleted(launch, processId);
                fake->SetAlive(processId, false);
                return status;
            });
        auto created = HubWorkerCoordinator::Create(specification, std::move(host));
        REQUIRE(created);
        auto coordinator = std::move(created).Value();
        REQUIRE(WaitUntil([&] { return HasState(*coordinator, "restart-dead", HubTaskState::Completed); }));
        CHECK(fake->Launches().size() == 1);
    }

    SUBCASE("live workers remain authoritative and are not relaunched")
    {
        KeireHubTests::TemporaryDirectory temporary;
        const auto specification = Specification(temporary.Path());
        const auto download = Download(temporary.Path(), "restart-live");
        REQUIRE(SeedActiveTask(specification, download, 701));

        auto host = std::make_unique<FakeProcessHost>();
        auto* fake = host.get();
        fake->SetAlive(701, true);
        auto created = HubWorkerCoordinator::Create(specification, std::move(host));
        REQUIRE(created);
        auto coordinator = std::move(created).Value();
        REQUIRE(WaitUntil(
            [&]
            {
                return coordinator->Snapshot()->State == HubWorkerCoordinatorState::Ready &&
                       HasState(*coordinator, "restart-live", HubTaskState::Downloading);
            }));
        CHECK(fake->Launches().empty());

        const auto operation = specification.OperationRoot / download.TaskId;
        REQUIRE(WriteHubWorkerStatus(operation / "status.json", {.TaskId = download.TaskId,
                                                                 .State = HubTaskState::Completed,
                                                                 .Progress = {.BytesTransferred = Payload.size(),
                                                                              .TotalBytes = Payload.size(),
                                                                              .CurrentPackage = download.Package.Id,
                                                                              .Phase = "Completed"},
                                                                 .WorkerProcessId = 701,
                                                                 .UpdatedUnixSeconds = 3}));
        REQUIRE(WriteHubWorkerResult(operation / "result.json",
                                     {.TaskId = download.TaskId,
                                      .Outcome = DownloadOutcome::Completed,
                                      .CachePath = DownloadManager::CachePath(WorkerDownload(download))}));
        fake->SetAlive(701, false);
        REQUIRE(WaitUntil([&] { return HasState(*coordinator, "restart-live", HubTaskState::Completed); }));
        CHECK(fake->Launches().empty());
    }
}
