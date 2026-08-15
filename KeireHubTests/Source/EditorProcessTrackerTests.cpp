#include "KeireHubRuntime/EditorProcessTracker.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <set>

TEST_CASE("Editor process tracking rejects duplicate project launches and publishes exits")
{
    std::set<std::uint64_t> alive{41, 42};
    KeireHub::EditorProcessTracker tracker([&](const std::uint64_t processId, const std::filesystem::path&)
                                           { return alive.contains(processId); });
    const auto editor = std::filesystem::absolute("EditorOne");

    CHECK(tracker.Track({.ProcessId = 41,
                         .ProjectId = "project-one",
                         .InstallationId = "editor-stable",
                         .ProjectRoot = std::filesystem::absolute("ProjectOne"),
                         .Executable = editor,
                         .LaunchedUnixSeconds = 100}));
    CHECK(tracker.IsProjectRunning("project-one"));
    CHECK(tracker.IsInstallationRunning("editor-stable"));
    REQUIRE(tracker.Snapshot()->size() == 1);

    const auto duplicate = tracker.Track({.ProcessId = 42,
                                          .ProjectId = "project-one",
                                          .InstallationId = "editor-preview",
                                          .ProjectRoot = std::filesystem::absolute("ProjectOne"),
                                          .Executable = std::filesystem::absolute("EditorTwo"),
                                          .LaunchedUnixSeconds = 101});
    CHECK_FALSE(duplicate);
    CHECK(duplicate.Error().Code == KeireHub::HubErrorCode::InvalidTransition);

    alive.erase(41);
    CHECK(tracker.Refresh());
    CHECK_FALSE(tracker.IsProjectRunning("project-one"));
    CHECK(tracker.Snapshot()->empty());
    CHECK_FALSE(tracker.Refresh());
}

TEST_CASE("Editor process tracking rejects a reused process id with a different executable")
{
    const auto editor = std::filesystem::absolute("EditorOne");
    auto currentExecutable = editor;
    KeireHub::EditorProcessTracker tracker(
        [&](const std::uint64_t processId, const std::filesystem::path& expectedExecutable)
        { return processId == 41 && currentExecutable == expectedExecutable; });

    REQUIRE(tracker.Track({.ProcessId = 41,
                           .ProjectId = "project-one",
                           .InstallationId = "editor-stable",
                           .ProjectRoot = std::filesystem::absolute("ProjectOne"),
                           .Executable = editor,
                           .LaunchedUnixSeconds = 100}));
    currentExecutable = std::filesystem::absolute("UnrelatedProcess");

    CHECK(tracker.Refresh());
    CHECK_FALSE(tracker.IsProjectRunning("project-one"));
    CHECK_FALSE(tracker.IsInstallationRunning("editor-stable"));
}

TEST_CASE("Editor process tracking validates bounded launch identities")
{
    KeireHub::EditorProcessTracker tracker([](const std::uint64_t, const std::filesystem::path&) { return true; });
    const auto missingProcess = tracker.Track({.ProjectId = "project",
                                               .InstallationId = "editor",
                                               .ProjectRoot = std::filesystem::absolute("Project"),
                                               .Executable = std::filesystem::absolute("Editor"),
                                               .LaunchedUnixSeconds = 100});
    CHECK_FALSE(missingProcess);
    CHECK(missingProcess.Error().Code == KeireHub::HubErrorCode::InvalidArgument);

    const auto relativeRoot = tracker.Track({.ProcessId = 5,
                                             .ProjectId = "project",
                                             .InstallationId = "editor",
                                             .ProjectRoot = "Project",
                                             .Executable = std::filesystem::absolute("Editor"),
                                             .LaunchedUnixSeconds = 100});
    CHECK_FALSE(relativeRoot);
    CHECK(relativeRoot.Error().Code == KeireHub::HubErrorCode::InvalidArgument);
}
