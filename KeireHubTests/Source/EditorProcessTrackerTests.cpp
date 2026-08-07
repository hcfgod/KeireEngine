#include "KeireHubRuntime/EditorProcessTracker.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <set>

TEST_CASE("Editor process tracking rejects duplicate project launches and publishes exits")
{
    std::set<std::uint64_t> alive{41, 42};
    KeireHub::EditorProcessTracker tracker([&](const std::uint64_t processId) { return alive.contains(processId); });

    CHECK(tracker.Track({.ProcessId = 41,
                         .ProjectId = "project-one",
                         .InstallationId = "editor-stable",
                         .ProjectRoot = std::filesystem::absolute("ProjectOne"),
                         .LaunchedUnixSeconds = 100}));
    CHECK(tracker.IsProjectRunning("project-one"));
    CHECK(tracker.IsInstallationRunning("editor-stable"));
    REQUIRE(tracker.Snapshot()->size() == 1);

    const auto duplicate = tracker.Track({.ProcessId = 42,
                                          .ProjectId = "project-one",
                                          .InstallationId = "editor-preview",
                                          .ProjectRoot = std::filesystem::absolute("ProjectOne"),
                                          .LaunchedUnixSeconds = 101});
    CHECK_FALSE(duplicate);
    CHECK(duplicate.Error().Code == KeireHub::HubErrorCode::InvalidTransition);

    alive.erase(41);
    CHECK(tracker.Refresh());
    CHECK_FALSE(tracker.IsProjectRunning("project-one"));
    CHECK(tracker.Snapshot()->empty());
    CHECK_FALSE(tracker.Refresh());
}

TEST_CASE("Editor process tracking validates bounded launch identities")
{
    KeireHub::EditorProcessTracker tracker([](const std::uint64_t) { return true; });
    const auto missingProcess = tracker.Track({.ProjectId = "project",
                                               .InstallationId = "editor",
                                               .ProjectRoot = std::filesystem::absolute("Project"),
                                               .LaunchedUnixSeconds = 100});
    CHECK_FALSE(missingProcess);
    CHECK(missingProcess.Error().Code == KeireHub::HubErrorCode::InvalidArgument);

    const auto relativeRoot = tracker.Track({.ProcessId = 5,
                                             .ProjectId = "project",
                                             .InstallationId = "editor",
                                             .ProjectRoot = "Project",
                                             .LaunchedUnixSeconds = 100});
    CHECK_FALSE(relativeRoot);
    CHECK(relativeRoot.Error().Code == KeireHub::HubErrorCode::InvalidArgument);
}
