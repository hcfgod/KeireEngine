#include "KeireHub/HubProjectUiSupport.h"

#include <doctest/doctest.h>

TEST_CASE("Hub opens older-schema projects with a compatible installed editor")
{
    using KeireHub::HubProjectOpenAction;
    using KeireHub::ResolveProjectOpenAction;

    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::UpgradeAvailable, true) ==
          HubProjectOpenAction::OpenWithEditor);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::UpgradeAvailable, false) ==
          HubProjectOpenAction::ReviewUpgrade);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::RecoveryRequired, true) ==
          HubProjectOpenAction::ReviewUpgrade);
}

TEST_CASE("Hub project opening distinguishes missing editors from unavailable projects")
{
    using KeireHub::HubProjectOpenAction;
    using KeireHub::ResolveProjectOpenAction;

    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::Ready, true) == HubProjectOpenAction::OpenWithEditor);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::RequiresNewerEngine, true) ==
          HubProjectOpenAction::OpenWithEditor);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::UnsupportedSchema, false) ==
          HubProjectOpenAction::FindCompatibleEditor);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::Missing, true) == HubProjectOpenAction::Unavailable);
    CHECK(ResolveProjectOpenAction(Keire::ProjectStatus::InUse, true) == HubProjectOpenAction::Unavailable);
}
