#include "KeireHubRuntime/TaskNotificationTracker.h"

#include <KeireHubTests/TestSupport.h>

#include <doctest/doctest.h>

using namespace KeireHub;

TEST_CASE("Task activity creates durable start and completion notifications without startup spam")
{
    KeireHubTests::TemporaryDirectory temporary;
    NotificationStore notifications(temporary.Path() / "notifications.json");
    REQUIRE(notifications.Load());
    TaskNotificationTracker tracker;
    std::vector<HubTask> tasks{{.Id = "editor-install",
                                .Kind = HubTaskKind::Install,
                                .DisplayName = "Install Kéire Editor 0.1.0",
                                .State = HubTaskState::Downloading,
                                .UpdatedUnixSeconds = 10}};

    REQUIRE(tracker.Observe(tasks, notifications, 10));
    CHECK(notifications.Snapshot()->empty());

    tasks.front().State = HubTaskState::Completed;
    tasks.front().UpdatedUnixSeconds = 20;
    REQUIRE(tracker.Observe(tasks, notifications, 20));
    REQUIRE(notifications.Snapshot()->size() == 1);
    CHECK(notifications.Snapshot()->front().Severity == NotificationSeverity::Success);
    CHECK(notifications.Snapshot()->front().RelatedTaskId == tasks.front().Id);

    tasks.push_back({.Id = "editor-repair",
                     .Kind = HubTaskKind::Repair,
                     .DisplayName = "Repair Kéire Editor 0.1.0",
                     .State = HubTaskState::Queued,
                     .UpdatedUnixSeconds = 30});
    REQUIRE(tracker.Observe(tasks, notifications, 30));
    REQUIRE(notifications.Snapshot()->size() == 2);
    CHECK(notifications.Snapshot()->front().Severity == NotificationSeverity::Info);
}
