#include "KeireHub/HubProjectMutationWorkflow.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace KeireHub;
using namespace std::chrono_literals;

namespace
{
    constexpr auto SourceId = "11111111-1111-4111-8111-111111111111";
    constexpr auto DuplicateId = "22222222-2222-4222-8222-222222222222";

    [[nodiscard]] HubError TestError(const HubErrorCode code, std::string message)
    {
        return {.Code = code, .Message = std::move(message), .AffectedItem = SourceId};
    }

    [[nodiscard]] ProjectDuplicatePlan Plan(const std::filesystem::path& destination)
    {
        return {
            .Request = {.SourceProjectId = SourceId, .Destination = destination, .DisplayName = "Duplicated Project"},
            .SourceRoot = destination.parent_path() / "Source",
            .Destination = destination,
            .Staging = destination.parent_path() / (std::string(".keire-duplicate-") + DuplicateId),
            .NewProjectId = DuplicateId,
            .CreatedAt = "2026-08-07T12:34:56Z",
            .AddedUnixSeconds = 42};
    }

    [[nodiscard]] ProjectDuplicateResult Result(const ProjectDuplicateStagedResult& staged)
    {
        return {.ProjectId = staged.Plan.NewProjectId,
                .Root = staged.Plan.Destination,
                .DisplayName = staged.Plan.Request.DisplayName,
                .CopiedBytes = staged.CopiedBytes,
                .CopiedEntries = staged.CopiedEntries};
    }

    [[nodiscard]] bool WaitFor(const std::atomic_bool& value)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!value.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        return value.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool PollUntilTerminal(HubProjectMutationWorkflow& workflow)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            (void)workflow.Poll();
            if (workflow.Snapshot()->IsTerminal())
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return workflow.Snapshot()->IsTerminal();
    }
} // namespace

TEST_CASE("Project mutation runs one staging worker and commits through owner-thread polling")
{
    const auto destination = std::filesystem::path("Duplicate");
    const auto ownerThread = std::this_thread::get_id();
    std::atomic_bool stageEntered = false;
    std::atomic_bool releaseStage = false;
    std::thread::id stageThread;
    std::thread::id commitThread;
    std::atomic_int commitCalls = 0;
    std::atomic_int discardCalls = 0;
    HubProjectMutationWorkflow workflow(
        {.PrepareDuplicate = [destination](const std::string&, const std::filesystem::path&, std::string)
         { return HubResult<ProjectDuplicatePlan>::Success(Plan(destination)); },
         .StageDuplicate =
             [&stageEntered, &releaseStage, &stageThread](ProjectDuplicatePlan plan,
                                                          ProjectDuplicateCallbacks callbacks)
         {
             stageThread = std::this_thread::get_id();
             callbacks.ReportProgress(128, 2);
             stageEntered.store(true, std::memory_order_release);
             while (!releaseStage.load(std::memory_order_acquire))
             {
                 if (callbacks.IsCancelled())
                 {
                     return HubResult<ProjectDuplicateStagedResult>::Success(
                         {.Plan = std::move(plan), .State = ProjectDuplicateStageState::Cancelled});
                 }
                 std::this_thread::yield();
             }
             return HubResult<ProjectDuplicateStagedResult>::Success(
                 {.Plan = std::move(plan), .CopiedBytes = 256, .CopiedEntries = 3});
         },
         .CommitDuplicate =
             [&commitThread, &commitCalls](ProjectDuplicateStagedResult staged)
         {
             commitThread = std::this_thread::get_id();
             commitCalls.fetch_add(1, std::memory_order_relaxed);
             return HubResult<ProjectDuplicateResult>::Success(Result(staged));
         },
         .DiscardDuplicate =
             [&discardCalls](const ProjectDuplicateStagedResult&)
         {
             discardCalls.fetch_add(1, std::memory_order_relaxed);
             return HubStatus::Success();
         }});

    const auto started = workflow.StartDuplicate(SourceId, destination, "Duplicated Project");
    REQUIRE(started);
    CHECK(started.Value() == 1);
    const auto initial = workflow.Snapshot();
    CHECK(initial->State == HubProjectMutationState::Duplicating);
    REQUIRE(WaitFor(stageEntered));
    REQUIRE(workflow.Poll());
    const auto progress = workflow.Snapshot();
    CHECK(progress->CopiedBytes == 128);
    CHECK(progress->CopiedEntries == 2);

    const auto second = workflow.StartDuplicate(SourceId, "Other", "Other");
    REQUIRE_FALSE(second);
    CHECK(second.Error().Code == HubErrorCode::InvalidTransition);
    releaseStage.store(true, std::memory_order_release);
    REQUIRE(PollUntilTerminal(workflow));

    const auto completed = workflow.Snapshot();
    CHECK(completed->OperationId == started.Value());
    CHECK(completed->State == HubProjectMutationState::Completed);
    REQUIRE(completed->Result);
    CHECK(completed->Result->ProjectId == DuplicateId);
    CHECK(completed->CopiedBytes == 256);
    CHECK(completed->CopiedEntries == 3);
    CHECK(stageThread != ownerThread);
    CHECK(commitThread == ownerThread);
    CHECK(commitCalls.load(std::memory_order_relaxed) == 1);
    CHECK(discardCalls.load(std::memory_order_relaxed) == 0);
    CHECK(initial->State == HubProjectMutationState::Duplicating);
    CHECK(initial->CopiedBytes == 0);
}

TEST_CASE("Project mutation cancellation preserves progress and discards staged output")
{
    std::atomic_bool stageEntered = false;
    std::atomic_bool discardCalled = false;
    std::atomic_bool commitCalled = false;
    HubProjectMutationWorkflow workflow(
        {.PrepareDuplicate = [](const std::string&, const std::filesystem::path& destination, std::string)
         { return HubResult<ProjectDuplicatePlan>::Success(Plan(destination)); },
         .StageDuplicate =
             [&stageEntered](ProjectDuplicatePlan plan, ProjectDuplicateCallbacks callbacks)
         {
             callbacks.ReportProgress(512, 4);
             stageEntered.store(true, std::memory_order_release);
             while (!callbacks.IsCancelled())
                 std::this_thread::yield();
             return HubResult<ProjectDuplicateStagedResult>::Success(
                 {.Plan = std::move(plan), .State = ProjectDuplicateStageState::Cancelled});
         },
         .CommitDuplicate =
             [&commitCalled](ProjectDuplicateStagedResult staged)
         {
             commitCalled.store(true, std::memory_order_release);
             return HubResult<ProjectDuplicateResult>::Success(Result(staged));
         },
         .DiscardDuplicate =
             [&discardCalled](const ProjectDuplicateStagedResult&)
         {
             discardCalled.store(true, std::memory_order_release);
             return HubStatus::Success();
         }});

    const auto started = workflow.StartDuplicate(SourceId, "Cancelled", "Cancelled");
    REQUIRE(started);
    REQUIRE(WaitFor(stageEntered));
    REQUIRE(workflow.Poll());
    const auto beforeRejectedCancellation = workflow.Snapshot();
    const auto wrongOperation = workflow.Cancel(started.Value() + 1);
    REQUIRE_FALSE(wrongOperation);
    CHECK(wrongOperation.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(workflow.Snapshot() == beforeRejectedCancellation);
    REQUIRE(workflow.Cancel(started.Value()));
    CHECK(workflow.Snapshot()->State == HubProjectMutationState::Cancelling);
    REQUIRE(PollUntilTerminal(workflow));

    const auto cancelled = workflow.Snapshot();
    CHECK(cancelled->OperationId == started.Value());
    CHECK(cancelled->State == HubProjectMutationState::Cancelled);
    CHECK(cancelled->CopiedBytes == 512);
    CHECK(cancelled->CopiedEntries == 4);
    CHECK(discardCalled.load(std::memory_order_acquire));
    CHECK_FALSE(commitCalled.load(std::memory_order_acquire));
    CHECK_FALSE(cancelled->Failure);
}

TEST_CASE("Project mutation translates preparation and commit exceptions into terminal errors")
{
    std::atomic_int prepareCalls = 0;
    std::atomic_int discardCalls = 0;
    HubProjectMutationWorkflow workflow(
        {.PrepareDuplicate =
             [&prepareCalls](const std::string&, const std::filesystem::path& destination, std::string)
         {
             if (prepareCalls.fetch_add(1, std::memory_order_relaxed) == 0)
             {
                 return HubResult<ProjectDuplicatePlan>::Failure(
                     TestError(HubErrorCode::ProjectValidationFailed, "The source is invalid."));
             }
             return HubResult<ProjectDuplicatePlan>::Success(Plan(destination));
         },
         .StageDuplicate =
             [](ProjectDuplicatePlan plan, ProjectDuplicateCallbacks)
         {
             return HubResult<ProjectDuplicateStagedResult>::Success(
                 {.Plan = std::move(plan), .CopiedBytes = 64, .CopiedEntries = 1});
         },
         .CommitDuplicate = [](ProjectDuplicateStagedResult) -> HubResult<ProjectDuplicateResult>
         { throw std::runtime_error("fixture commit failure"); },
         .DiscardDuplicate =
             [&discardCalls](const ProjectDuplicateStagedResult&)
         {
             discardCalls.fetch_add(1, std::memory_order_relaxed);
             return HubStatus::Success();
         }});

    const auto rejected = workflow.StartDuplicate(SourceId, "Rejected", "Rejected");
    REQUIRE(rejected);
    CHECK(rejected.Value() == 1);
    const auto rejectedSnapshot = workflow.Snapshot();
    CHECK(rejectedSnapshot->State == HubProjectMutationState::Failed);
    REQUIRE(rejectedSnapshot->Failure);
    CHECK(rejectedSnapshot->Failure->Code == HubErrorCode::ProjectValidationFailed);

    const auto started = workflow.StartDuplicate(SourceId, "Failure", "Failure");
    REQUIRE(started);
    CHECK(started.Value() == 2);
    REQUIRE(PollUntilTerminal(workflow));
    const auto failed = workflow.Snapshot();
    CHECK(failed->OperationId == started.Value());
    CHECK(failed->State == HubProjectMutationState::Failed);
    REQUIRE(failed->Failure);
    CHECK(failed->Failure->Code == HubErrorCode::IoWrite);
    CHECK(failed->Failure->TechnicalDetails.find("fixture commit failure") != std::string::npos);
    CHECK(discardCalls.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("Project mutation destructor requests cancellation, joins the worker, and discards staging")
{
    std::atomic_bool stageEntered = false;
    std::atomic_bool cancellationObserved = false;
    std::atomic_bool discardCalled = false;
    {
        HubProjectMutationWorkflow workflow(
            {.PrepareDuplicate = [](const std::string&, const std::filesystem::path& destination, std::string)
             { return HubResult<ProjectDuplicatePlan>::Success(Plan(destination)); },
             .StageDuplicate =
                 [&stageEntered, &cancellationObserved](ProjectDuplicatePlan plan, ProjectDuplicateCallbacks callbacks)
             {
                 stageEntered.store(true, std::memory_order_release);
                 while (!callbacks.IsCancelled())
                     std::this_thread::yield();
                 cancellationObserved.store(true, std::memory_order_release);
                 return HubResult<ProjectDuplicateStagedResult>::Success(
                     {.Plan = std::move(plan), .State = ProjectDuplicateStageState::Cancelled});
             },
             .CommitDuplicate = [](ProjectDuplicateStagedResult staged)
             { return HubResult<ProjectDuplicateResult>::Success(Result(staged)); },
             .DiscardDuplicate =
                 [&discardCalled](const ProjectDuplicateStagedResult&)
             {
                 discardCalled.store(true, std::memory_order_release);
                 return HubStatus::Success();
             }});
        REQUIRE(workflow.StartDuplicate(SourceId, "Shutdown", "Shutdown"));
        REQUIRE(WaitFor(stageEntered));
    }
    CHECK(cancellationObserved.load(std::memory_order_acquire));
    CHECK(discardCalled.load(std::memory_order_acquire));
}

TEST_CASE("Project mutation owner-thread operations reject worker callers without changing state")
{
    std::atomic_bool stageEntered = false;
    std::atomic_bool releaseStage = false;
    HubProjectMutationWorkflow workflow(
        {.PrepareDuplicate = [](const std::string&, const std::filesystem::path& destination, std::string)
         { return HubResult<ProjectDuplicatePlan>::Success(Plan(destination)); },
         .StageDuplicate =
             [&stageEntered, &releaseStage](ProjectDuplicatePlan plan, ProjectDuplicateCallbacks callbacks)
         {
             stageEntered.store(true, std::memory_order_release);
             while (!releaseStage.load(std::memory_order_acquire) && !callbacks.IsCancelled())
                 std::this_thread::yield();
             return HubResult<ProjectDuplicateStagedResult>::Success({.Plan = std::move(plan)});
         },
         .CommitDuplicate = [](ProjectDuplicateStagedResult staged)
         { return HubResult<ProjectDuplicateResult>::Success(Result(staged)); },
         .DiscardDuplicate = [](const ProjectDuplicateStagedResult&) { return HubStatus::Success(); }});
    const auto started = workflow.StartDuplicate(SourceId, "Owner", "Owner");
    REQUIRE(started);
    REQUIRE(WaitFor(stageEntered));

    const auto rejected = std::async(std::launch::async, [&workflow, operationId = started.Value()]
                                     { return std::pair(workflow.Poll(), workflow.Cancel(operationId)); })
                              .get();
    REQUIRE_FALSE(rejected.first);
    REQUIRE_FALSE(rejected.second);
    CHECK(rejected.first.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(rejected.second.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(workflow.Snapshot()->State == HubProjectMutationState::Duplicating);

    releaseStage.store(true, std::memory_order_release);
    REQUIRE(PollUntilTerminal(workflow));
    CHECK(workflow.Snapshot()->State == HubProjectMutationState::Completed);
}
