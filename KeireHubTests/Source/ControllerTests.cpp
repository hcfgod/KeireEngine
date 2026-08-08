#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/HubController.h"

#include <doctest/doctest.h>

#include <cstdint>

TEST_CASE("HubController composes stores and requeues resumable abandoned downloads")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubController controller({.PreferenceRoot = temporary.Path() / "Hub"});
    REQUIRE(controller.Load(10));

    KeireHub::HubTask task{.Id = "download-editor",
                           .Kind = KeireHub::HubTaskKind::Download,
                           .DisplayName = "Download editor",
                           .PackageIds = {"keire.editor"},
                           .State = KeireHub::HubTaskState::Queued,
                           .CreatedUnixSeconds = 10,
                           .UpdatedUnixSeconds = 10};
    REQUIRE(controller.Tasks().Add(task));
    REQUIRE(controller.Tasks().Transition(task.Id, KeireHub::HubTaskState::Downloading, 11));
    REQUIRE(controller.Tasks().SetWorkerProcess(task.Id, 1234, 12));

    REQUIRE(controller.ReconcileInterruptedTasks(13, [](std::uint64_t) { return false; }));
    const auto snapshot = controller.Snapshot();
    REQUIRE(snapshot.Settings);
    REQUIRE(snapshot.Projects);
    REQUIRE(snapshot.Installations);
    REQUIRE(snapshot.Notifications);
    CHECK(controller.Updates().ResumeTokenPath() == temporary.Path() / "Hub" / "hub-update.json");
    REQUIRE(snapshot.Tasks->size() == 1);
    CHECK(snapshot.Tasks->front().State == KeireHub::HubTaskState::Queued);
    CHECK_FALSE(snapshot.Tasks->front().Failure);
    CHECK_FALSE(snapshot.Tasks->front().WorkerProcessId);
}

TEST_CASE("HubController reports interrupted mutations as retryable failures")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubController controller({.PreferenceRoot = temporary.Path() / "Hub"});
    REQUIRE(controller.Load(10));

    KeireHub::HubTask task{.Id = "install-editor",
                           .Kind = KeireHub::HubTaskKind::Install,
                           .DisplayName = "Install editor",
                           .PackageIds = {"keire.editor"},
                           .TargetInstallationId = "stable-1",
                           .State = KeireHub::HubTaskState::Queued,
                           .CreatedUnixSeconds = 10,
                           .UpdatedUnixSeconds = 10};
    REQUIRE(controller.Tasks().Add(task));
    REQUIRE(controller.Tasks().Claim(task.Id, KeireHub::HubTaskState::Installing, 1234, 11));

    REQUIRE(controller.ReconcileInterruptedTasks(12, [](std::uint64_t) { return false; }));
    const auto tasks = controller.Tasks().Snapshot();
    REQUIRE(tasks->size() == 1);
    CHECK(tasks->front().State == KeireHub::HubTaskState::Failed);
    REQUIRE(tasks->front().Failure);
    CHECK(tasks->front().Failure->Code == KeireHub::HubErrorCode::WorkerInterrupted);
    CHECK(tasks->front().Failure->Retryable);
    CHECK_FALSE(tasks->front().WorkerProcessId);
}

TEST_CASE("HubController preserves queued paused terminal and live worker tasks")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHub::HubController controller({.PreferenceRoot = temporary.Path() / "Hub"});
    REQUIRE(controller.Load(20));

    const auto add = [&](std::string id, const KeireHub::HubTaskState state, const std::uint64_t worker)
    {
        KeireHub::HubTask task{.Id = std::move(id),
                               .Kind = KeireHub::HubTaskKind::Download,
                               .DisplayName = "Task",
                               .PackageIds = {"keire.package"},
                               .State = KeireHub::HubTaskState::Queued,
                               .CreatedUnixSeconds = 20,
                               .UpdatedUnixSeconds = 20};
        REQUIRE(controller.Tasks().Add(task));
        if (state == KeireHub::HubTaskState::Paused)
        {
            REQUIRE(controller.Tasks().Transition(task.Id, KeireHub::HubTaskState::Downloading, 21));
            REQUIRE(controller.Tasks().Transition(task.Id, state, 22));
        }
        else if (state == KeireHub::HubTaskState::Completed)
        {
            REQUIRE(controller.Tasks().Transition(task.Id, KeireHub::HubTaskState::Verifying, 21));
            REQUIRE(controller.Tasks().Transition(task.Id, state, 22));
        }
        else if (state != KeireHub::HubTaskState::Queued)
        {
            REQUIRE(controller.Tasks().Transition(task.Id, state, 21));
        }
        if (worker != 0)
            REQUIRE(controller.Tasks().SetWorkerProcess(task.Id, worker, 22));
    };

    add("queued", KeireHub::HubTaskState::Queued, 0);
    add("paused", KeireHub::HubTaskState::Paused, 0);
    add("live", KeireHub::HubTaskState::Downloading, 77);
    add("complete", KeireHub::HubTaskState::Completed, 0);

    REQUIRE(controller.ReconcileInterruptedTasks(23, [](const std::uint64_t processId) { return processId == 77; }));
    const auto tasks = controller.Tasks().Snapshot();
    REQUIRE(tasks->size() == 4);
    CHECK((*tasks)[0].State == KeireHub::HubTaskState::Queued);
    CHECK((*tasks)[1].State == KeireHub::HubTaskState::Paused);
    CHECK((*tasks)[2].State == KeireHub::HubTaskState::Downloading);
    CHECK((*tasks)[3].State == KeireHub::HubTaskState::Completed);
}
