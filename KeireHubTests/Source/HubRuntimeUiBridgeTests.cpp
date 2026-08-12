#include "KeireHub/HubRuntimeUiBridge.h"

#include <doctest/doctest.h>

using namespace KeireHub;

TEST_CASE("Hub task phases reflect the task operation")
{
    CHECK(HubTaskPhaseLabel(HubTaskKind::Install, HubTaskState::Installing) == "Installing");
    CHECK(HubTaskPhaseLabel(HubTaskKind::Install, HubTaskState::Completed) == "Installed");
    CHECK(HubTaskPhaseLabel(HubTaskKind::Remove, HubTaskState::Installing) == "Uninstalling");
    CHECK(HubTaskPhaseLabel(HubTaskKind::Remove, HubTaskState::Removing) == "Uninstalling");
    CHECK(HubTaskPhaseLabel(HubTaskKind::Remove, HubTaskState::Configuring) == "Finalizing uninstall");
    CHECK(HubTaskPhaseLabel(HubTaskKind::Remove, HubTaskState::Completed) == "Uninstalled");
    CHECK(HubTaskPhaseLabel(HubTaskKind::Repair, HubTaskState::Installing) == "Repairing");
}

TEST_CASE("Hub task snapshot never presents removal work as installation")
{
    HubProductSnapshot product;
    const std::vector tasks{HubTask{.Id = "remove-editor",
                                    .Kind = HubTaskKind::Remove,
                                    .DisplayName = "Uninstall Kéire Editor 0.3.1",
                                    .State = HubTaskState::Installing}};

    ApplyHubTaskSnapshot(tasks, product);

    REQUIRE(product.Tasks.size() == 1);
    CHECK(product.Tasks.front().Phase == "Uninstalling");
    CHECK(product.Tasks.front().Phase.find("Install") == std::string::npos);
}

TEST_CASE("Hub task snapshot reports determinate removal progress and completion")
{
    HubProductSnapshot product;
    std::vector tasks{HubTask{
        .Id = "remove-editor",
        .Kind = HubTaskKind::Remove,
        .DisplayName = "Remove Kéire Editor",
        .State = HubTaskState::Removing,
        .Progress = {
            .CurrentPackage = "managed-editor", .StepsCompleted = 2, .TotalSteps = 4, .Phase = "Removing files"}}};

    ApplyHubTaskSnapshot(tasks, product);

    REQUIRE(product.Tasks.size() == 1);
    CHECK(product.Tasks.front().Progress == doctest::Approx(0.5F));
    CHECK(product.Tasks.front().Message == "Removing files");
    CHECK(product.Tasks.front().CurrentPackage == "managed-editor");

    tasks.front().State = HubTaskState::Completed;
    tasks.front().Progress = {.CurrentPackage = "managed-editor", .Phase = "Completed"};
    ApplyHubTaskSnapshot(tasks, product);
    REQUIRE(product.Tasks.size() == 1);
    CHECK(product.Tasks.front().Progress == doctest::Approx(1.0F));
    CHECK(product.Tasks.front().Phase == "Uninstalled");
}
