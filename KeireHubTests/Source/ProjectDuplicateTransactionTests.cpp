#include "TestSupport.h"

#include "KeireHubRuntime/ProjectWorkflowManager.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <future>
#include <ranges>
#include <string>
#include <thread>

using namespace KeireHub;

namespace
{
    constexpr auto SourceId = "11111111-1111-4111-8111-111111111111";
    constexpr auto DuplicateId = "22222222-2222-4222-8222-222222222222";

    void WriteProject(const std::filesystem::path& root)
    {
        const nlohmann::json descriptor{{"schemaVersion", 3},
                                        {"id", SourceId},
                                        {"name", "Source Project"},
                                        {"createdWithEngineVersion", "0.1.0"},
                                        {"minimumEngineVersion", "0.1.0"},
                                        {"createdAt", "2026-01-02T03:04:05Z"},
                                        {"lastSavedWithEngineVersion", "0.1.0"},
                                        {"startupScene", nullptr},
                                        {"defaultInput", nullptr},
                                        {"requiredModules", nlohmann::json::array()}};
        KeireHubTests::WriteText(root / "ProjectSettings" / "Project.keireproject", descriptor.dump(2) + '\n');
        KeireHubTests::WriteText(root / "Assets" / "hello.txt", "hello");
    }

    [[nodiscard]] HubRecentProject RecentProject(const std::filesystem::path& root)
    {
        return {.Id = SourceId,
                .Root = root,
                .Name = "Source Project",
                .AddedUnixSeconds = 10,
                .LastOpenedUnixSeconds = 20,
                .Pinned = true,
                .PreferredEditorInstallationId = "editor-stable"};
    }

    [[nodiscard]] ProjectWorkflowServices Services(bool& locked)
    {
        return {
            .GenerateProjectId = [] { return HubResult<std::string>::Success(std::string(DuplicateId)); },
            .CurrentUtcTimestamp = [] { return HubResult<std::string>::Success(std::string("2026-08-07T12:34:56Z")); },
            .CurrentUnixSeconds = [] { return 42ULL; },
            .IsProjectLocked = [&locked](const std::filesystem::path&) { return HubResult<bool>::Success(locked); }};
    }

    [[nodiscard]] ProjectDuplicateRequest Request(const std::filesystem::path& destination)
    {
        return {.SourceProjectId = SourceId, .Destination = destination, .DisplayName = "Duplicated Project"};
    }
} // namespace

TEST_CASE("Project duplicate stages on a worker and publishes only during owner-thread commit")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = temporary.Path() / "Source";
    const auto destination = temporary.Path() / "Duplicate";
    WriteProject(source);
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(source)));
    bool locked = false;
    ProjectWorkflowManager workflow(catalog, Services(locked));

    auto prepared = workflow.PrepareDuplicate(Request(destination));
    REQUIRE(prepared);
    CHECK_FALSE(std::filesystem::exists(prepared.Value().Staging));
    const auto staging = prepared.Value().Staging;
    const auto ownerThread = std::this_thread::get_id();
    std::thread::id stageThread;
    auto future = std::async(std::launch::async,
                             [&workflow, &stageThread, plan = std::move(prepared).Value()]() mutable
                             {
                                 stageThread = std::this_thread::get_id();
                                 return workflow.StageDuplicate(std::move(plan));
                             });
    auto staged = future.get();
    REQUIRE(staged);
    CHECK(stageThread != ownerThread);
    CHECK(staged.Value().State == ProjectDuplicateStageState::ReadyToCommit);
    CHECK(std::filesystem::is_directory(staging));
    CHECK_FALSE(std::filesystem::exists(destination));
    REQUIRE(catalog.Snapshot()->size() == 1);

    const auto committed = workflow.CommitDuplicate(std::move(staged).Value());
    REQUIRE(committed);
    CHECK(committed.Value().ProjectId == DuplicateId);
    CHECK(std::filesystem::is_regular_file(destination / "Assets" / "hello.txt"));
    CHECK_FALSE(std::filesystem::exists(staging));
    const auto snapshot = catalog.Snapshot();
    REQUIRE(snapshot->size() == 2);
    CHECK(std::ranges::any_of(*snapshot, [](const HubRecentProject& project) { return project.Id == DuplicateId; }));
}

TEST_CASE("Project duplicate cancellation removes staging and leaves catalog and destination unchanged")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = temporary.Path() / "Source";
    const auto destination = temporary.Path() / "Cancelled";
    WriteProject(source);
    KeireHubTests::WriteText(source / "Assets" / "large.bin", std::string(2 * 1024 * 1024, 'x'));
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(source)));
    bool locked = false;
    ProjectWorkflowManager workflow(catalog, Services(locked));
    auto prepared = workflow.PrepareDuplicate(Request(destination));
    REQUIRE(prepared);
    const auto staging = prepared.Value().Staging;
    std::atomic_bool cancel = false;

    auto staged = workflow.StageDuplicate(std::move(prepared).Value(),
                                          {.IsCancelled = [&cancel] { return cancel.load(std::memory_order_acquire); },
                                           .ReportProgress =
                                               [&cancel](const std::uint64_t bytes, const std::size_t)
                                           {
                                               if (bytes >= 64 * 1024)
                                                   cancel.store(true, std::memory_order_release);
                                           }});
    REQUIRE(staged);
    CHECK(staged.Value().State == ProjectDuplicateStageState::Cancelled);
    CHECK(cancel.load(std::memory_order_acquire));
    CHECK_FALSE(std::filesystem::exists(staging));
    CHECK_FALSE(std::filesystem::exists(destination));
    REQUIRE(catalog.Snapshot()->size() == 1);
    CHECK(workflow.DiscardDuplicate(staged.Value()));
}

TEST_CASE("Project duplicate commit rechecks source state and leaves staged data explicitly discardable")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto source = temporary.Path() / "Source";
    const auto destination = temporary.Path() / "Blocked";
    WriteProject(source);
    HubProjectCatalog catalog(temporary.Path() / "projects.json");
    REQUIRE(catalog.Upsert(RecentProject(source)));
    bool locked = false;
    ProjectWorkflowManager workflow(catalog, Services(locked));
    auto prepared = workflow.PrepareDuplicate(Request(destination));
    REQUIRE(prepared);
    auto staged = workflow.StageDuplicate(std::move(prepared).Value());
    REQUIRE(staged);
    const auto staging = staged.Value().Plan.Staging;

    locked = true;
    const auto committed = workflow.CommitDuplicate(staged.Value());
    REQUIRE_FALSE(committed);
    CHECK(committed.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(std::filesystem::is_directory(staging));
    CHECK_FALSE(std::filesystem::exists(destination));
    REQUIRE(catalog.Snapshot()->size() == 1);
    REQUIRE(workflow.DiscardDuplicate(staged.Value()));
    CHECK_FALSE(std::filesystem::exists(staging));
}
