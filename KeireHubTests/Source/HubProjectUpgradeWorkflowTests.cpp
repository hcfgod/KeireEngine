#include "KeireHub/HubProjectUpgradeWorkflow.h"

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
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return predicate();
    }

    [[nodiscard]] KeireHub::HubProjectUpgradePreparation ReadyPreparation()
    {
        Keire::ProjectUpgradePlan plan;
        plan.ProjectRoot = "project";
        plan.CurrentSchema = 2;
        plan.TargetSchema = 3;
        return {.Plan = std::move(plan)};
    }
} // namespace

TEST_CASE("Project upgrade inspection and apply remain off the owner thread")
{
    const auto owner = std::this_thread::get_id();
    std::promise<void> inspectRelease;
    auto inspectGate = inspectRelease.get_future().share();
    std::promise<void> applyRelease;
    auto applyGate = applyRelease.get_future().share();
    std::atomic_bool inspectionUsedWorker = false;
    std::atomic_bool applyUsedWorker = false;

    KeireHub::HubProjectUpgradeWorkflow workflow(
        {.Inspect =
             [owner, inspectGate, &inspectionUsedWorker](const auto&, const auto&)
         {
             inspectionUsedWorker.store(std::this_thread::get_id() != owner);
             inspectGate.wait();
             return KeireHub::HubResult<KeireHub::HubProjectUpgradePreparation>::Success(ReadyPreparation());
         },
         .Apply =
             [owner, applyGate, &applyUsedWorker](const auto&, const auto&, const auto&)
         {
             applyUsedWorker.store(std::this_thread::get_id() != owner);
             applyGate.wait();
             return KeireHub::HubStatus::Success();
         },
         .Recover = [](const auto&, const auto&) { return KeireHub::HubStatus::Success(); },
         .Rollback = [](const auto&, const auto&) { return KeireHub::HubStatus::Success(); }});

    REQUIRE(workflow.Start("project", {}));
    CHECK(workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Inspecting);
    inspectRelease.set_value();
    REQUIRE(WaitUntil([&] { return workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Ready; }));
    CHECK(inspectionUsedWorker.load());

    REQUIRE(workflow.Apply());
    CHECK(workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Applying);
    applyRelease.set_value();
    REQUIRE(
        WaitUntil([&] { return workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Completed; }));
    CHECK(applyUsedWorker.load());
    CHECK(workflow.Snapshot()->Completion == KeireHub::HubProjectUpgradeCompletion::Reopen);
}

TEST_CASE("Project upgrade recovery publishes typed failure and supports retry")
{
    std::atomic_int inspections = 0;
    KeireHub::HubProjectUpgradeWorkflow workflow(
        {.Inspect =
             [&inspections](const auto&, const auto&)
         {
             ++inspections;
             return KeireHub::HubResult<KeireHub::HubProjectUpgradePreparation>::Success({.Interrupted = true});
         },
         .Apply = [](const auto&, const auto&, const auto&) { return KeireHub::HubStatus::Success(); },
         .Recover =
             [](const auto&, const auto&)
         {
             return KeireHub::HubStatus::Failure({.Code = KeireHub::HubErrorCode::MigrationFailed,
                                                  .Message = "Recovery could not be completed.",
                                                  .AffectedItem = "project"});
         },
         .Rollback = [](const auto&, const auto&) { return KeireHub::HubStatus::Success(); }});

    REQUIRE(workflow.Start("project", {}));
    REQUIRE(WaitUntil([&] { return workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Ready; }));
    REQUIRE(workflow.Recover());
    REQUIRE(WaitUntil([&] { return workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Failed; }));
    REQUIRE(workflow.Snapshot()->Failure);
    CHECK(workflow.Snapshot()->Failure->Code == KeireHub::HubErrorCode::MigrationFailed);
    CHECK(workflow.Snapshot()->Failure->Message == "Recovery could not be completed.");

    REQUIRE(workflow.Retry());
    REQUIRE(WaitUntil([&] { return workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Ready; }));
    CHECK(inspections.load() == 2);
    REQUIRE(workflow.Rollback());
    REQUIRE(
        WaitUntil([&] { return workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Completed; }));
    CHECK(workflow.Snapshot()->Completion == KeireHub::HubProjectUpgradeCompletion::Refresh);
}

TEST_CASE("Project upgrade workflow rejects concurrent and cross-thread commands")
{
    std::promise<void> release;
    auto gate = release.get_future().share();
    KeireHub::HubProjectUpgradeWorkflow workflow(
        {.Inspect =
             [gate](const auto&, const auto&)
         {
             gate.wait();
             return KeireHub::HubResult<KeireHub::HubProjectUpgradePreparation>::Success(ReadyPreparation());
         },
         .Apply = [](const auto&, const auto&, const auto&) { return KeireHub::HubStatus::Success(); },
         .Recover = [](const auto&, const auto&) { return KeireHub::HubStatus::Success(); },
         .Rollback = [](const auto&, const auto&) { return KeireHub::HubStatus::Success(); }});

    REQUIRE(workflow.Start("project", {}));
    const auto duplicate = workflow.Start("other-project", {});
    CHECK_FALSE(duplicate);
    CHECK(duplicate.Error().Code == KeireHub::HubErrorCode::InvalidTransition);

    auto crossThread = std::async(std::launch::async, [&] { return workflow.Dismiss(); });
    const auto rejected = crossThread.get();
    CHECK_FALSE(rejected);
    CHECK(rejected.Error().Code == KeireHub::HubErrorCode::InvalidTransition);
    CHECK(workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Inspecting);

    release.set_value();
    REQUIRE(WaitUntil([&] { return workflow.Snapshot()->State == KeireHub::HubProjectUpgradeWorkflowState::Ready; }));
}
