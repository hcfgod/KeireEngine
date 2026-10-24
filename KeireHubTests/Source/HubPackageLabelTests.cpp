#include "KeireHubInternal/HubPackageLabel.h"
#include "KeireHubRuntime/HubSettingsStore.h"

#include <doctest/doctest.h>

TEST_CASE("Package labels include exactly one trailing version")
{
    CHECK(KeireHub::PackageVersionLabel("Keire Editor 0.4.7", "0.4.7") == "Keire Editor 0.4.7");
    CHECK(KeireHub::PackageVersionLabel("Keire Editor", "0.4.7") == "Keire Editor 0.4.7");
    CHECK(KeireHub::PackageVersionLabel("", "0.4.7") == "0.4.7");
    CHECK(KeireHub::PackageVersionLabel("0.4.7", "0.4.7") == "0.4.7");
    CHECK(KeireHub::PackageVersionLabel("Editor", "") == "Editor");
    CHECK(KeireHub::PackageVersionLabel("Editor 10.4.7", "0.4.7") == "Editor 10.4.7 0.4.7");
}

TEST_CASE("Settings pending comparison includes appearance network and discovery changes")
{
    const KeireHub::HubSettings saved;
    auto edited = saved;
    CHECK(edited == saved);
    edited.Appearance = KeireHub::HubAppearance::Dark;
    CHECK_FALSE(edited == saved);
    edited = saved;
    edited.OfflineMode = !saved.OfflineMode;
    CHECK_FALSE(edited == saved);
    edited = saved;
    edited.ProjectDiscoveryRoots.push_back("new-root");
    CHECK_FALSE(edited == saved);
    edited = saved;
    CHECK(edited == saved);
}
