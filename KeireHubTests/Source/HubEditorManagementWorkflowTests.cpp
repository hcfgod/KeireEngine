#include "KeireHub/HubEditorManagementWorkflow.h"

#include "TestSupport.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <latch>
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
    services.Refresh = [&](std::vector<HubEditorManagementWorkItem> items, std::string, std::string)
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
    services.Verify = [&](HubEditorManagementWorkItem item, std::string, std::string)
    {
        entered.count_down();
        release.wait();
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = std::move(item.Installation), .Health = InstallationHealth::Healthy});
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
    REQUIRE(product.Tasks.size() == 1);
    CHECK(product.Tasks.front().Phase == "Failed");
    CHECK_FALSE(product.Tasks.front().Active);
    CHECK_FALSE(product.Tasks.front().Retryable);
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
    services.Verify = [&](HubEditorManagementWorkItem item, std::string, std::string)
    {
        entered.count_down();
        release.wait();
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = std::move(item.Installation), .Health = InstallationHealth::Healthy});
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
    services.Verify = [&](HubEditorManagementWorkItem item, std::string, std::string)
    {
        entered.count_down();
        release.wait();
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = std::move(item.Installation), .Health = InstallationHealth::Healthy});
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
    services.Verify = [](HubEditorManagementWorkItem item, std::string, std::string)
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
    services.Authorize = [&](HubEditorManagementWorkItem item, std::filesystem::path,
                             const EditorManagedOperation operation, std::string, std::string)
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
    services.Refresh = [&](std::vector<HubEditorManagementWorkItem>, std::string, std::string)
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
    services.Verify = [](HubEditorManagementWorkItem item, std::string, std::string)
    {
        return HubResult<EditorInstallationHealthSnapshot>::Success(
            {.Installation = std::move(item.Installation), .Health = InstallationHealth::Missing});
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
