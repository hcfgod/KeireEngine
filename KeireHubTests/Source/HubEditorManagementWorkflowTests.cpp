#include "KeireHub/HubEditorManagementWorkflow.h"

#include <KeireHubTests/TestSupport.h>

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace KeireHub;
using namespace std::chrono_literals;

namespace
{
    [[nodiscard]] EditorInstallation TestInstallation(const std::filesystem::path& root, std::string id,
                                                      const InstallationOwnership ownership)
    {
        std::filesystem::create_directories(root / "bin");
        EditorInstallation installation{.Id = std::move(id),
                                        .Version = "1.0.0",
                                        .Channel = "stable",
                                        .Platform = "windows",
                                        .Architecture = "x86_64",
                                        .Root = std::filesystem::absolute(root),
                                        .Ownership = ownership,
                                        .ManifestFingerprint = KeireHubTests::Digest('a'),
                                        .Entrypoints = {"bin/Editor"},
                                        .EditorEntrypoint = "bin/Editor",
                                        .MinimumProjectSchema = 1,
                                        .MaximumProjectSchema = 3,
                                        .Health = InstallationHealth::VerificationRequired};
        if (ownership == InstallationOwnership::Managed)
            installation.MarkerNonce = std::string(32, 'b');
        return installation;
    }

    [[nodiscard]] HubUiCommand Command(const HubUiCommandType type, const EditorInstallation& installation)
    {
        return {.Type = type, .ItemId = installation.Id, .Path = installation.Root};
    }

    [[nodiscard]] EditorManagedOperationPlan Plan(const HubEditorManagementWorkItem& item,
                                                  const EditorManagedOperation operation)
    {
        return {.Operation = operation,
                .InstallationId = item.Installation.Id,
                .Root = item.Installation.Root,
                .ManifestFingerprint = item.Installation.ManifestFingerprint,
                .PackageTreeIdentity = item.Installation.PackageTreeIdentity,
                .PackageReceiptSha256 = item.Installation.PackageReceiptSha256,
                .MarkerNonce = item.Installation.MarkerNonce,
                .EditorEntrypoint = item.Installation.EditorEntrypoint,
                .CurrentHealth = item.Installation.Health};
    }

    [[nodiscard]] bool PollUntilTerminal(HubEditorManagementWorkflow& workflow)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto polled = workflow.Poll();
            if (!polled)
                return false;
            if (workflow.OperationSnapshot()->IsTerminal())
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return workflow.OperationSnapshot()->IsTerminal();
    }
} // namespace

TEST_CASE("Editor management refresh runs on a value-only worker and projects its task state")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "editor-a", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    std::latch entered(1);
    std::latch release(1);
    std::thread::id workerThread;
    std::filesystem::path receivedRoot;
    std::size_t receivedCount = 0;
    HubEditorManagementServices services;
    services.Refresh =
        [&](const std::vector<HubEditorManagementWorkItem>& items, const std::string&, const std::string&)
    {
        workerThread = std::this_thread::get_id();
        receivedCount = items.size();
        if (!items.empty())
            receivedRoot = items.front().Installation.Root;
        entered.count_down();
        release.wait();
        if (items.empty())
        {
            return HubResult<std::vector<EditorInstallationHealthSnapshot>>::Failure(
                {.Code = HubErrorCode::InvalidData, .Message = "The refresh fixture received no installations."});
        }
        return HubResult<std::vector<EditorInstallationHealthSnapshot>>::Success(
            std::vector<EditorInstallationHealthSnapshot>{
                {.Installation = items.front().Installation, .Health = InstallationHealth::Healthy}});
    };
    const auto ownerThread = std::this_thread::get_id();
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; }},
                                         std::move(services));

    REQUIRE(workflow.Refresh());
    entered.wait();
    CHECK(workflow.OperationSnapshot()->State == HubEditorManagementState::Running);
    const auto concurrent = workflow.Execute(Command(HubUiCommandType::VerifyEditor, installation));
    REQUIRE_FALSE(concurrent);
    CHECK(concurrent.Error().Code == HubErrorCode::InvalidTransition);
    HubProductSnapshot product;
    workflow.ApplySnapshot(product);
    workflow.ApplyOperationSnapshot(product);
    workflow.ApplyOperationSnapshot(product);
    CHECK(product.EditorManagementBusy);
    CHECK(product.EditorManagementRefreshing);
    REQUIRE(product.Tasks.size() == 1);
    CHECK(product.Tasks.front().Active);
    CHECK(product.Tasks.front().Phase == "Checking");

    release.count_down();
    REQUIRE(PollUntilTerminal(workflow));
    CHECK(workerThread != ownerThread);
    CHECK(receivedCount == 1);
    CHECK(receivedRoot == installation.Root);
    REQUIRE(workflow.Snapshot()->size() == 1);
    CHECK(workflow.Snapshot()->front().Health == InstallationHealth::Healthy);
    CHECK(controller.Installations().Snapshot()->front().Health == InstallationHealth::Healthy);
    const auto completion = workflow.TakeCompletion();
    REQUIRE(completion);
    CHECK(completion->Operation == HubEditorManagementOperation::Refresh);
    CHECK_FALSE(completion->Failure);
}

TEST_CASE("Editor activity polling clears stale external running state without refreshing package inventory")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "external-editor", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    std::atomic_bool running = true;
    std::atomic_int refreshes = 0;
    std::atomic_bool pathCorrect = false;
    HubEditorManagementServices services;
    services.Refresh =
        [&](const std::vector<HubEditorManagementWorkItem>& items, const std::string&, const std::string&)
    {
        ++refreshes;
        return HubResult<std::vector<EditorInstallationHealthSnapshot>>::Success(
            {{.Installation = items.front().Installation,
              .Health = InstallationHealth::Damaged,
              .Activity = {.Running = true},
              .Issues = {{.Code = EditorInstallationIssueCode::RegistrationMismatch}}}});
    };
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; },
                                          .ProbeEntrypointActivity =
                                              [&](const std::filesystem::path& path)
                                          {
                                              pathCorrect = path == installation.Root / installation.EditorEntrypoint;
                                              return running.load() ? EditorEntrypointActivity::Running
                                                                    : EditorEntrypointActivity::NotRunning;
                                          },
                                          .ActivityProbeInterval = 0ms},
                                         std::move(services));
    REQUIRE(workflow.Refresh());
    REQUIRE(PollUntilTerminal(workflow));
    running = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (workflow.Snapshot()->front().Activity.Running && std::chrono::steady_clock::now() < deadline)
    {
        REQUIRE(workflow.Poll());
        std::this_thread::sleep_for(1ms);
    }
    CHECK_FALSE(workflow.Snapshot()->front().Activity.Running);
    CHECK(pathCorrect);
    CHECK(workflow.Snapshot()->front().Health == InstallationHealth::Damaged);
    CHECK(refreshes == 1);
    HubProductSnapshot product;
    workflow.ApplySnapshot(product);
    REQUIRE(product.Editors.size() == 1);
    CHECK_FALSE(product.Editors.front().Running);
    CHECK(product.Editors.front().RegistrationRefreshAvailable);
}

TEST_CASE("Editor activity probe failure keeps the installation guarded and later recovers")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "external-editor", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    std::atomic_bool fail = true;
    HubEditorManagementWorkflow workflow(controller, {.HostPlatform = "windows",
                                                      .HostArchitecture = "x86_64",
                                                      .ProbeRunning = [](const EditorInstallation&) { return false; },
                                                      .ProbeEntrypointActivity =
                                                          [&](const std::filesystem::path&)
                                                      {
                                                          if (fail.load())
                                                              throw std::runtime_error("probe failed");
                                                          return EditorEntrypointActivity::NotRunning;
                                                      },
                                                      .ActivityProbeInterval = 0ms});
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!workflow.Snapshot()->front().Activity.Running && std::chrono::steady_clock::now() < deadline)
    {
        REQUIRE(workflow.Poll());
        std::this_thread::sleep_for(1ms);
    }
    CHECK(workflow.Snapshot()->front().Activity.Running);
    fail = false;
    while (workflow.Snapshot()->front().Activity.Running && std::chrono::steady_clock::now() < deadline)
    {
        REQUIRE(workflow.Poll());
        std::this_thread::sleep_for(1ms);
    }
    CHECK_FALSE(workflow.Snapshot()->front().Activity.Running);
}

TEST_CASE("Editor activity polling discards a result for an obsolete registration")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    auto installation =
        TestInstallation(temporary.Path() / "Editor", "external-editor", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    std::latch entered(1);
    std::latch release(1);
    std::atomic_int probes = 0;
    HubEditorManagementWorkflow workflow(controller, {.HostPlatform = "windows",
                                                      .HostArchitecture = "x86_64",
                                                      .ProbeRunning = [](const EditorInstallation&) { return false; },
                                                      .ProbeEntrypointActivity =
                                                          [&](const std::filesystem::path&)
                                                      {
                                                          if (++probes == 1)
                                                          {
                                                              entered.count_down();
                                                              release.wait();
                                                              return EditorEntrypointActivity::Running;
                                                          }
                                                          return EditorEntrypointActivity::NotRunning;
                                                      },
                                                      .ActivityProbeInterval = 0ms});
    REQUIRE(workflow.Poll());
    entered.wait();
    installation.Version = "2.0.0";
    REQUIRE(controller.Installations().Upsert(installation));
    workflow.ReloadRegistrations();
    release.count_down();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (probes < 2 && std::chrono::steady_clock::now() < deadline)
    {
        REQUIRE(workflow.Poll());
        std::this_thread::sleep_for(1ms);
    }
    CHECK(probes >= 2);
    CHECK(workflow.Snapshot()->front().Installation.Version == "2.0.0");
    CHECK_FALSE(workflow.Snapshot()->front().Activity.Running);
}

TEST_CASE("Editor activity polling joins an in-flight probe during shutdown")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "external-editor", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    std::latch probeEntered(1);
    std::latch releaseProbe(1);
    std::latch shutdownStarted(1);
    auto workflow = std::make_unique<HubEditorManagementWorkflow>(
        controller, HubEditorManagementSpecification{.HostPlatform = "windows",
                                                     .HostArchitecture = "x86_64",
                                                     .ProbeRunning = [](const EditorInstallation&) { return false; },
                                                     .ProbeEntrypointActivity =
                                                         [&](const std::filesystem::path&)
                                                     {
                                                         probeEntered.count_down();
                                                         releaseProbe.wait();
                                                         return EditorEntrypointActivity::NotRunning;
                                                     }});
    REQUIRE(workflow->Poll());
    probeEntered.wait();
    auto shutdown = std::async(std::launch::async,
                               [&]
                               {
                                   shutdownStarted.count_down();
                                   workflow.reset();
                               });
    shutdownStarted.wait();
    CHECK(shutdown.wait_for(20ms) == std::future_status::timeout);
    releaseProbe.count_down();
    CHECK(shutdown.wait_for(2s) == std::future_status::ready);
    shutdown.get();
}

TEST_CASE("Editor verification rejects a result when current tracked activity changes")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "editor-a", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    std::atomic_bool running = false;
    std::latch entered(1);
    std::latch release(1);
    HubEditorManagementServices services;
    services.Verify = [&](const HubEditorManagementWorkItem& item, const std::string&, const std::string&)
    {
        entered.count_down();
        release.wait();
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = item.Installation, .Health = InstallationHealth::Healthy});
    };
    HubEditorManagementWorkflow workflow(
        controller,
        {.HostPlatform = "windows",
         .HostArchitecture = "x86_64",
         .ProbeRunning = [&](const EditorInstallation&) { return running.load(std::memory_order_relaxed); }},
        std::move(services));

    REQUIRE(workflow.Execute(Command(HubUiCommandType::VerifyEditor, installation)));
    entered.wait();
    running.store(true, std::memory_order_relaxed);
    release.count_down();
    REQUIRE(PollUntilTerminal(workflow));

    const auto completion = workflow.TakeCompletion();
    REQUIRE(completion);
    REQUIRE(completion->Failure);
    CHECK(completion->Failure->Code == HubErrorCode::EditorRunning);
    REQUIRE(controller.Installations().Snapshot()->size() == 1);
    CHECK(controller.Installations().Snapshot()->front().Health == InstallationHealth::VerificationRequired);
    HubProductSnapshot product;
    workflow.ApplySnapshot(product);
    workflow.ApplyOperationSnapshot(product);
    CHECK(product.Tasks.empty());
}

TEST_CASE("Editor verification tasks show the product version instead of an internal registration identity")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    auto previousInstallation =
        TestInstallation(temporary.Path() / "PreviousEditor", "external-editor-0-3-1", InstallationOwnership::External);
    previousInstallation.Version = "0.3.1";
    REQUIRE(controller.Installations().Upsert(previousInstallation));
    auto installation = TestInstallation(temporary.Path() / "CurrentEditor", "external-editor-current",
                                         InstallationOwnership::External);
    installation.Version = "0.4.4";
    REQUIRE(controller.Installations().Upsert(installation));
    std::latch entered(1);
    std::latch release(1);
    HubEditorManagementServices services;
    services.Verify = [&](const HubEditorManagementWorkItem& item, const std::string&, const std::string&)
    {
        entered.count_down();
        release.wait();
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = item.Installation, .Health = InstallationHealth::Healthy});
    };
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; }},
                                         std::move(services));

    REQUIRE(workflow.Execute(Command(HubUiCommandType::VerifyEditor, installation)));
    entered.wait();
    HubProductSnapshot product;
    workflow.ApplySnapshot(product);
    workflow.ApplyOperationSnapshot(product);
    REQUIRE(product.Tasks.size() == 1);
    CHECK(product.Tasks.front().CurrentPackage == "0.4.4");

    release.count_down();
    REQUIRE(PollUntilTerminal(workflow));
}

TEST_CASE("Editor verification rejects a result when a targeted package task starts")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "editor-a", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    std::latch entered(1);
    std::latch release(1);
    HubEditorManagementServices services;
    services.Verify = [&](const HubEditorManagementWorkItem& item, const std::string&, const std::string&)
    {
        entered.count_down();
        release.wait();
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = item.Installation, .Health = InstallationHealth::Healthy});
    };
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; }},
                                         std::move(services));

    REQUIRE(workflow.Execute(Command(HubUiCommandType::VerifyEditor, installation)));
    entered.wait();
    REQUIRE(controller.Tasks().Add({.Id = "repair-editor-a",
                                    .Kind = HubTaskKind::Repair,
                                    .DisplayName = "Repair editor",
                                    .PackageIds = {"keire.editor"},
                                    .TargetInstallationId = installation.Id,
                                    .State = HubTaskState::Queued,
                                    .CreatedUnixSeconds = 2,
                                    .UpdatedUnixSeconds = 2}));
    release.count_down();
    REQUIRE(PollUntilTerminal(workflow));

    const auto completion = workflow.TakeCompletion();
    REQUIRE(completion);
    REQUIRE(completion->Failure);
    CHECK(completion->Failure->Code == HubErrorCode::InstallationBusy);
    CHECK(controller.Installations().Snapshot()->front().Health == InstallationHealth::VerificationRequired);
}

TEST_CASE("Editor verification rejects a result after exact registration identity changes")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "editor-a", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    std::latch entered(1);
    std::latch release(1);
    HubEditorManagementServices services;
    services.Verify = [&](const HubEditorManagementWorkItem& item, const std::string&, const std::string&)
    {
        entered.count_down();
        release.wait();
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = item.Installation, .Health = InstallationHealth::Healthy});
    };
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; }},
                                         std::move(services));

    REQUIRE(workflow.Execute(Command(HubUiCommandType::VerifyEditor, installation)));
    entered.wait();
    auto changed = installation;
    changed.Version = "1.0.1";
    REQUIRE(controller.Installations().Upsert(changed));
    release.count_down();
    REQUIRE(PollUntilTerminal(workflow));

    const auto completion = workflow.TakeCompletion();
    REQUIRE(completion);
    REQUIRE(completion->Failure);
    CHECK(completion->Failure->Code == HubErrorCode::InvalidTransition);
    CHECK(controller.Installations().Snapshot()->front().Version == "1.0.1");
    CHECK(controller.Installations().Snapshot()->front().Health == InstallationHealth::VerificationRequired);
}

TEST_CASE("Editor verification rejects a worker result with changed registration metadata")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "editor-a", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    HubEditorManagementServices services;
    services.Verify = [](HubEditorManagementWorkItem item, const std::string&, const std::string&)
    {
        item.Installation.Channel = "preview";
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = std::move(item.Installation), .Health = InstallationHealth::Healthy});
    };
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; }},
                                         std::move(services));

    REQUIRE(workflow.Execute(Command(HubUiCommandType::VerifyEditor, installation)));
    REQUIRE(PollUntilTerminal(workflow));

    const auto completion = workflow.TakeCompletion();
    REQUIRE(completion);
    REQUIRE(completion->Failure);
    CHECK(completion->Failure->Code == HubErrorCode::InvalidData);
    CHECK(controller.Installations().Snapshot()->front().Channel == "stable");
    CHECK(controller.Installations().Snapshot()->front().Health == InstallationHealth::VerificationRequired);
}

TEST_CASE("Managed editor authorization is single-flight and publishes a value result")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Managed", "managed-a", InstallationOwnership::Managed);
    REQUIRE(controller.Installations().Upsert(installation));
    std::latch entered(1);
    std::latch release(1);
    std::atomic_int calls = 0;
    HubEditorManagementServices services;
    services.Authorize = [&](const HubEditorManagementWorkItem& item, std::filesystem::path,
                             const EditorManagedOperation operation, const std::string&, const std::string&)
    {
        calls.fetch_add(1, std::memory_order_relaxed);
        entered.count_down();
        release.wait();
        return HubResult<EditorManagedOperationPlan>::Success(Plan(item, operation));
    };
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; }},
                                         std::move(services));

    REQUIRE(workflow.Execute(Command(HubUiCommandType::RepairManagedEditor, installation)));
    entered.wait();
    const auto concurrent = workflow.Execute(Command(HubUiCommandType::RemoveManagedEditor, installation));
    REQUIRE_FALSE(concurrent);
    CHECK(concurrent.Error().Code == HubErrorCode::InvalidTransition);
    HubProductSnapshot product;
    workflow.ApplySnapshot(product);
    workflow.ApplyOperationSnapshot(product);
    CHECK(product.EditorManagementBusy);
    REQUIRE(product.Editors.size() == 1);
    CHECK(product.Editors.front().ManagementBusy);
    CHECK_FALSE(product.Editors.front().ManagementStatus.empty());

    release.count_down();
    REQUIRE(PollUntilTerminal(workflow));
    CHECK(calls.load(std::memory_order_relaxed) == 1);
    auto completion = workflow.TakeCompletion();
    REQUIRE(completion);
    REQUIRE(completion->Authorization);
    CHECK(completion->Authorization->Operation == EditorManagedOperation::Repair);
    CHECK(completion->Authorization->InstallationId == installation.Id);
}

TEST_CASE("External editor removal stays owner-thread local and does not start a full refresh")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "External", "external-a", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    std::atomic_int refreshCalls = 0;
    HubEditorManagementServices services;
    services.Refresh = [&](const std::vector<HubEditorManagementWorkItem>&, const std::string&, const std::string&)
    {
        refreshCalls.fetch_add(1, std::memory_order_relaxed);
        return HubResult<std::vector<EditorInstallationHealthSnapshot>>::Success(
            std::vector<EditorInstallationHealthSnapshot>{});
    };
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; }},
                                         std::move(services));

    REQUIRE(workflow.Execute(Command(HubUiCommandType::RemoveExternalEditor, installation)));
    CHECK(controller.Installations().Snapshot()->empty());
    CHECK(refreshCalls.load(std::memory_order_relaxed) == 0);
    CHECK(workflow.OperationSnapshot()->State == HubEditorManagementState::Idle);
}

TEST_CASE("External editor registration refresh atomically adopts and verifies rebuilt package metadata")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    auto installation = TestInstallation(temporary.Path() / "External", "external-a", InstallationOwnership::External);
    installation.Health = InstallationHealth::Damaged;
    REQUIRE(controller.Installations().Upsert(installation));

    HubEditorManagementServices services;
    services.RefreshRegistration = [](HubEditorManagementWorkItem item, const std::filesystem::path& expectedRoot,
                                      const std::string&, const std::string&)
    {
        CHECK(item.Installation.Root == expectedRoot);
        item.Installation.Version = "1.0.1";
        item.Installation.ManifestFingerprint = KeireHubTests::Digest('c');
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = std::move(item.Installation), .Health = InstallationHealth::Healthy});
    };
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; }},
                                         std::move(services));

    REQUIRE(workflow.Execute(Command(HubUiCommandType::RefreshExternalEditorRegistration, installation)));
    REQUIRE(PollUntilTerminal(workflow));

    const auto completion = workflow.TakeCompletion();
    REQUIRE(completion);
    CHECK(completion->Operation == HubEditorManagementOperation::RefreshRegistration);
    CHECK(completion->VerifiedHealth == InstallationHealth::Healthy);
    CHECK_FALSE(completion->Failure);
    REQUIRE(controller.Installations().Snapshot()->size() == 1);
    const auto& refreshed = controller.Installations().Snapshot()->front();
    CHECK(refreshed.Id == installation.Id);
    CHECK(refreshed.Root == installation.Root);
    CHECK(refreshed.Version == "1.0.1");
    CHECK(refreshed.ManifestFingerprint == KeireHubTests::Digest('c'));
    CHECK(refreshed.Health == InstallationHealth::Healthy);
    CHECK(refreshed.LastVerifiedUnixSeconds > 0);
}

TEST_CASE("Registration refresh is offered only for external manifest-registration mismatches")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto external =
        TestInstallation(temporary.Path() / "External", "external-a", InstallationOwnership::External);
    const auto managed = TestInstallation(temporary.Path() / "Managed", "managed-a", InstallationOwnership::Managed);
    const auto issue = EditorInstallationIssue{.Code = EditorInstallationIssueCode::RegistrationMismatch,
                                               .Message = "The package manifest does not match its Hub registration."};
    const std::vector<EditorInstallationHealthSnapshot> snapshots{
        {.Installation = external, .Health = InstallationHealth::Damaged, .Issues = {issue}},
        {.Installation = managed, .Health = InstallationHealth::Damaged, .Issues = {issue}}};

    const auto records = BuildHubEditorUiRecords(snapshots, {});
    REQUIRE(records.size() == 2);
    CHECK(records[0].RegistrationRefreshAvailable);
    CHECK_FALSE(records[1].RegistrationRefreshAvailable);

    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    REQUIRE(controller.Installations().Upsert(managed));
    HubEditorManagementWorkflow workflow(controller, {.HostPlatform = "windows",
                                                      .HostArchitecture = "x86_64",
                                                      .ProbeRunning = [](const EditorInstallation&) { return false; }});
    const auto rejected = workflow.Execute(Command(HubUiCommandType::RefreshExternalEditorRegistration, managed));
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(controller.Installations().Snapshot()->front().ManifestFingerprint == managed.ManifestFingerprint);
}

TEST_CASE("Missing managed editor recovery removes only its stale registration")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    auto installation =
        TestInstallation(temporary.Path() / "MissingManaged", "managed-missing", InstallationOwnership::Managed);
    installation.Health = InstallationHealth::Missing;
    REQUIRE(controller.Installations().Upsert(installation));
    std::error_code error;
    std::filesystem::remove_all(installation.Root, error);
    REQUIRE_FALSE(error);
    HubEditorManagementWorkflow workflow(controller, {.HostPlatform = "windows",
                                                      .HostArchitecture = "x86_64",
                                                      .ProbeRunning = [](const EditorInstallation&) { return false; }});

    REQUIRE(workflow.Execute(Command(HubUiCommandType::RemoveMissingManagedEditor, installation)));
    CHECK(controller.Installations().Snapshot()->empty());
    CHECK(workflow.Snapshot()->empty());
    CHECK(workflow.OperationSnapshot()->State == HubEditorManagementState::Idle);
}

TEST_CASE("Editor verification publishes and persists a missing health result")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "editor-missing", InstallationOwnership::Managed);
    REQUIRE(controller.Installations().Upsert(installation));
    HubEditorManagementServices services;
    services.Verify = [](const HubEditorManagementWorkItem& item, const std::string&, const std::string&)
    {
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = item.Installation, .Health = InstallationHealth::Missing});
    };
    HubEditorManagementWorkflow workflow(controller,
                                         {.HostPlatform = "windows",
                                          .HostArchitecture = "x86_64",
                                          .ProbeRunning = [](const EditorInstallation&) { return false; }},
                                         std::move(services));

    REQUIRE(workflow.Execute(Command(HubUiCommandType::VerifyEditor, installation)));
    REQUIRE(PollUntilTerminal(workflow));
    const auto completion = workflow.TakeCompletion();
    REQUIRE(completion);
    REQUIRE(completion->VerifiedHealth);
    CHECK(*completion->VerifiedHealth == InstallationHealth::Missing);
    CHECK(controller.Installations().Snapshot()->front().Health == InstallationHealth::Missing);
    HubProductSnapshot product;
    workflow.ApplySnapshot(product);
    REQUIRE(product.Editors.size() == 1U);
    CHECK(product.Editors.front().Missing);
    CHECK_FALSE(product.Editors.front().Healthy);
}

TEST_CASE("Editor management rejects non-owner coordination without changing state")
{
    KeireHubTests::TemporaryDirectory temporary;
    HubController controller({.PreferenceRoot = temporary.Path() / "Preferences"});
    REQUIRE(controller.Load(1));
    const auto installation =
        TestInstallation(temporary.Path() / "Editor", "editor-a", InstallationOwnership::External);
    REQUIRE(controller.Installations().Upsert(installation));
    HubEditorManagementWorkflow workflow(controller, {.HostPlatform = "windows",
                                                      .HostArchitecture = "x86_64",
                                                      .ProbeRunning = [](const EditorInstallation&) { return false; }});
    const auto initial = workflow.OperationSnapshot();

    const auto execute = std::async(std::launch::async, [&]
                                    { return workflow.Execute(Command(HubUiCommandType::VerifyEditor, installation)); })
                             .get();
    const auto poll = std::async(std::launch::async, [&] { return workflow.Poll(); }).get();

    REQUIRE_FALSE(execute);
    REQUIRE_FALSE(poll);
    CHECK(execute.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(poll.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(workflow.OperationSnapshot() == initial);
}
