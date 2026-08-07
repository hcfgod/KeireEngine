#include "KeireHub/HubBuildSupportInventoryWorkflow.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace
{
    template <typename Predicate> bool WaitUntil(Predicate&& predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
                return true;
            std::this_thread::yield();
        }
        return predicate();
    }
} // namespace

TEST_CASE("Build Support inventory loading remains off the owner thread")
{
    const auto owner = std::this_thread::get_id();
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::atomic_bool usedWorker = false;
    KeireHub::HubBuildSupportInventoryWorkflow workflow(
        {.Load = [owner, gate, &usedWorker]
         {
             usedWorker.store(std::this_thread::get_id() != owner);
             gate.wait();
             std::vector<KeireHub::BuildSupportComponent> inventory{
                 {.Id = "z", .EngineVersion = "1.0.0", .Platform = "windows", .Architecture = "x86_64"},
                 {.Id = "a", .EngineVersion = "1.0.0", .Platform = "linux", .Architecture = "x86_64"}};
             return KeireHub::HubResult<std::vector<KeireHub::BuildSupportComponent>>::Success(std::move(inventory));
         }});

    REQUIRE(workflow.Start());
    CHECK(workflow.Snapshot()->State == KeireHub::HubBuildSupportInventoryState::Loading);
    const auto duplicate = workflow.Start();
    CHECK_FALSE(duplicate);
    CHECK(duplicate.Error().Code == KeireHub::HubErrorCode::InvalidTransition);

    release.set_value();
    REQUIRE(WaitUntil(
        [&]
        {
            const auto polled = workflow.Poll();
            return polled && polled.Value();
        }));
    CHECK(usedWorker.load());
    const auto snapshot = workflow.Snapshot();
    CHECK(snapshot->State == KeireHub::HubBuildSupportInventoryState::Ready);
    REQUIRE(snapshot->Components->size() == 2);
    CHECK(snapshot->Components->front().Id == "a");
}

TEST_CASE("Build Support inventory preserves its last good snapshot after failure")
{
    std::atomic_int attempts = 0;
    KeireHub::HubBuildSupportInventoryWorkflow workflow(
        {.Load = [&attempts]
         {
             if (++attempts == 1)
             {
                 return KeireHub::HubResult<std::vector<KeireHub::BuildSupportComponent>>::Success(
                     {{.Id = "installed"}});
             }
             return KeireHub::HubResult<std::vector<KeireHub::BuildSupportComponent>>::Failure(
                 {.Code = KeireHub::HubErrorCode::IoRead,
                  .Message = "Inventory failed.",
                  .AffectedItem = "build-support"});
         }});

    REQUIRE(workflow.Start());
    REQUIRE(WaitUntil(
        [&]
        {
            const auto polled = workflow.Poll();
            return polled && polled.Value();
        }));
    REQUIRE(workflow.Snapshot()->Components->size() == 1);
    REQUIRE(workflow.Start());
    REQUIRE(WaitUntil(
        [&]
        {
            const auto polled = workflow.Poll();
            return polled && polled.Value();
        }));
    const auto failed = workflow.Snapshot();
    CHECK(failed->State == KeireHub::HubBuildSupportInventoryState::Failed);
    REQUIRE(failed->Failure);
    CHECK(failed->Failure->Code == KeireHub::HubErrorCode::IoRead);
    REQUIRE(failed->Components->size() == 1);
    CHECK(failed->Components->front().Id == "installed");
}

TEST_CASE("Build Support inventory refresh requests coalesce behind an in-flight load")
{
    std::promise<void> releaseFirst;
    std::promise<void> releaseSecond;
    const auto firstGate = releaseFirst.get_future().share();
    const auto secondGate = releaseSecond.get_future().share();
    std::atomic_int attempts = 0;
    KeireHub::HubBuildSupportInventoryWorkflow workflow(
        {.Load = [&attempts, firstGate, secondGate]
         {
             const auto attempt = ++attempts;
             if (attempt == 1)
                 firstGate.wait();
             else
                 secondGate.wait();
             return KeireHub::HubResult<std::vector<KeireHub::BuildSupportComponent>>::Success(
                 {{.Id = attempt == 1 ? "stale" : "fresh"}});
         }});

    REQUIRE(workflow.Start());
    CHECK(WaitUntil([&] { return attempts.load() == 1; }));
    CHECK(workflow.RequestRefresh());
    CHECK(workflow.RequestRefresh());
    CHECK(attempts.load() == 1);

    releaseFirst.set_value();
    CHECK(WaitUntil(
        [&]
        {
            const auto polled = workflow.Poll();
            return polled && polled.Value();
        }));
    CHECK(WaitUntil([&] { return attempts.load() == 2; }));
    CHECK(workflow.Snapshot()->State == KeireHub::HubBuildSupportInventoryState::Loading);

    releaseSecond.set_value();
    REQUIRE(WaitUntil(
        [&]
        {
            const auto polled = workflow.Poll();
            return polled && polled.Value() &&
                   workflow.Snapshot()->State == KeireHub::HubBuildSupportInventoryState::Ready;
        }));
    CHECK(attempts.load() == 2);
    REQUIRE(workflow.Snapshot()->Components->size() == 1);
    CHECK(workflow.Snapshot()->Components->front().Id == "fresh");
}

TEST_CASE("Build Support inventory rejects cross-thread coordination")
{
    KeireHub::HubBuildSupportInventoryWorkflow workflow(
        {.Load = [] { return KeireHub::HubResult<std::vector<KeireHub::BuildSupportComponent>>::Success({}); }});
    auto refreshRequest = std::async(std::launch::async, [&] { return workflow.RequestRefresh(); });
    const auto refreshRejected = refreshRequest.get();
    CHECK_FALSE(refreshRejected);
    CHECK(refreshRejected.Error().Code == KeireHub::HubErrorCode::InvalidTransition);
    auto request = std::async(std::launch::async, [&] { return workflow.Start(); });
    const auto rejected = request.get();
    CHECK_FALSE(rejected);
    CHECK(rejected.Error().Code == KeireHub::HubErrorCode::InvalidTransition);
    CHECK(workflow.Snapshot()->State == KeireHub::HubBuildSupportInventoryState::Idle);
}
