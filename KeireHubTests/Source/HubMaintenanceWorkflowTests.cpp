#include "KeireHub/HubMaintenanceWorkflow.h"
#include "KeireHub/HubProductUi.h"

#include "TestSupport.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace KeireHub;
using namespace std::chrono_literals;

namespace
{
    [[nodiscard]] bool PollUntilTerminal(HubMaintenanceWorkflow& workflow)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto polled = workflow.Poll();
            if (!polled)
                return false;
            if (workflow.Snapshot()->IsTerminal())
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return workflow.Snapshot()->IsTerminal();
    }
} // namespace

TEST_CASE("Hub maintenance clears verified cache on a worker and publishes completion on the owner thread")
{
    KeireHubTests::TemporaryDirectory temporary;
    std::latch entered(1);
    std::latch release(1);
    const auto ownerThread = std::this_thread::get_id();
    std::thread::id serviceThread;
    std::filesystem::path receivedRoot;
    HubMaintenanceWorkflow workflow({.ClearVerifiedCache = [&](const std::filesystem::path& cacheRoot)
                                     {
                                         serviceThread = std::this_thread::get_id();
                                         receivedRoot = cacheRoot;
                                         entered.count_down();
                                         release.wait();
                                         return HubStatus::Success();
                                     }});
    const std::vector<HubTask> tasks;
    const auto cacheRoot = std::filesystem::absolute(temporary.Path() / "Cache");

    const auto started = workflow.StartClearVerifiedCache(tasks, cacheRoot);
    REQUIRE(started);
    CHECK(started.Value() == 1);
    entered.wait();
    const auto running = workflow.Snapshot();
    CHECK(running->OperationId == 1);
    CHECK(running->State == HubMaintenanceState::Running);
    HubProductSnapshot runningProduct;
    ApplyHubMaintenanceSnapshot(*running, runningProduct);
    CHECK(runningProduct.VerifiedCacheClearRunning);
    REQUIRE(runningProduct.Tasks.size() == 1);
    CHECK(runningProduct.Tasks.front().Active);
    CHECK(runningProduct.Tasks.front().Phase == "Clearing");
    const auto duplicate = workflow.StartClearVerifiedCache(tasks, cacheRoot);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.Error().Code == HubErrorCode::InvalidTransition);

    release.count_down();
    REQUIRE(PollUntilTerminal(workflow));
    const auto completed = workflow.Snapshot();
    CHECK(completed->OperationId == 1);
    CHECK(completed->State == HubMaintenanceState::Completed);
    CHECK_FALSE(completed->Failure);
    CHECK(serviceThread != ownerThread);
    CHECK(receivedRoot == cacheRoot);
    CHECK(running->State == HubMaintenanceState::Running);
    HubProductSnapshot completedProduct;
    ApplyHubMaintenanceSnapshot(*completed, completedProduct);
    CHECK_FALSE(completedProduct.VerifiedCacheClearRunning);
    REQUIRE(completedProduct.Tasks.size() == 1);
    CHECK_FALSE(completedProduct.Tasks.front().Active);
    CHECK(completedProduct.Tasks.front().Phase == "Completed");
}

TEST_CASE("Hub maintenance rejects active tasks synchronously without starting its service")
{
    KeireHubTests::TemporaryDirectory temporary;
    std::atomic_int serviceCalls = 0;
    HubMaintenanceWorkflow workflow({.ClearVerifiedCache = [&](const std::filesystem::path&)
                                     {
                                         serviceCalls.fetch_add(1, std::memory_order_relaxed);
                                         return HubStatus::Success();
                                     }});
    const std::vector tasks{HubTask{.Id = "download", .State = HubTaskState::Downloading}};

    const auto rejected = workflow.StartClearVerifiedCache(tasks, std::filesystem::absolute(temporary.Path()));

    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(serviceCalls.load(std::memory_order_relaxed) == 0);
    CHECK(workflow.Snapshot()->State == HubMaintenanceState::Idle);
    CHECK(workflow.Snapshot()->OperationId == 0);
}

TEST_CASE("Hub maintenance preserves typed service failures and translates worker exceptions")
{
    KeireHubTests::TemporaryDirectory temporary;
    const std::vector<HubTask> tasks;
    HubMaintenanceWorkflow failed({.ClearVerifiedCache = [](const std::filesystem::path&)
                                   {
                                       return HubStatus::Failure(
                                           {.Code = HubErrorCode::IoWrite,
                                            .Message = "The verified cache fixture could not be cleared.",
                                            .Retryable = true,
                                            .AffectedItem = "cache",
                                            .TechnicalDetails = "fixture write failure"});
                                   }});
    REQUIRE(failed.StartClearVerifiedCache(tasks, std::filesystem::absolute(temporary.Path() / "Failed")));
    REQUIRE(PollUntilTerminal(failed));
    const auto failure = failed.Snapshot();
    CHECK(failure->State == HubMaintenanceState::Failed);
    REQUIRE(failure->Failure);
    CHECK(failure->Failure->Code == HubErrorCode::IoWrite);
    CHECK(failure->Failure->TechnicalDetails == "fixture write failure");
    HubProductSnapshot failedProduct;
    ApplyHubMaintenanceSnapshot(*failure, failedProduct);
    ApplyHubMaintenanceSnapshot(*failure, failedProduct);
    REQUIRE(failedProduct.Tasks.size() == 1);
    CHECK_FALSE(failedProduct.Tasks.front().Retryable);
    CHECK(failedProduct.Tasks.front().Message.find("Return to Settings") != std::string::npos);

    HubMaintenanceWorkflow threw({.ClearVerifiedCache = [](const std::filesystem::path&) -> HubStatus
                                  { throw std::runtime_error("fixture worker exception"); }});
    REQUIRE(threw.StartClearVerifiedCache(tasks, std::filesystem::absolute(temporary.Path() / "Threw")));
    REQUIRE(PollUntilTerminal(threw));
    const auto exception = threw.Snapshot();
    CHECK(exception->State == HubMaintenanceState::Failed);
    REQUIRE(exception->Failure);
    CHECK(exception->Failure->Code == HubErrorCode::WorkerInterrupted);
    CHECK(exception->Failure->TechnicalDetails == "fixture worker exception");
}

TEST_CASE("Hub maintenance owner-thread rejection leaves state unchanged")
{
    KeireHubTests::TemporaryDirectory temporary;
    std::atomic_int serviceCalls = 0;
    HubMaintenanceWorkflow workflow({.ClearVerifiedCache = [&](const std::filesystem::path&)
                                     {
                                         serviceCalls.fetch_add(1, std::memory_order_relaxed);
                                         return HubStatus::Success();
                                     }});
    const std::vector<HubTask> tasks;
    const auto cacheRoot = std::filesystem::absolute(temporary.Path() / "Cache");
    const auto initial = workflow.Snapshot();

    const auto rejectedStart =
        std::async(std::launch::async, [&] { return workflow.StartClearVerifiedCache(tasks, cacheRoot); }).get();
    const auto rejectedPoll = std::async(std::launch::async, [&] { return workflow.Poll(); }).get();

    REQUIRE_FALSE(rejectedStart);
    REQUIRE_FALSE(rejectedPoll);
    CHECK(rejectedStart.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(rejectedPoll.Error().Code == HubErrorCode::InvalidTransition);
    CHECK(workflow.Snapshot() == initial);
    CHECK(serviceCalls.load(std::memory_order_relaxed) == 0);
}
