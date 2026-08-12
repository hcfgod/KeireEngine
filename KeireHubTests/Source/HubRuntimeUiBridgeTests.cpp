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
