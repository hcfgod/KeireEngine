#include "KeireHub/HubDistributionWorkflow.h"

#include <doctest/doctest.h>

using namespace KeireHub;

TEST_CASE("Hub distribution retry policy backs off and periodically revalidates healthy catalogs")
{
    CHECK(CalculateHubDistributionRefreshDelay(false, true, 1) == std::chrono::seconds(5));
    CHECK(CalculateHubDistributionRefreshDelay(false, true, 2) == std::chrono::seconds(10));
    CHECK(CalculateHubDistributionRefreshDelay(false, true, 5) == std::chrono::seconds(60));
    CHECK(CalculateHubDistributionRefreshDelay(false, true, 20) == std::chrono::seconds(60));
    CHECK(CalculateHubDistributionRefreshDelay(false, false, 1) == std::chrono::seconds(300));
    CHECK(CalculateHubDistributionRefreshDelay(true, false, 0) == std::chrono::seconds(300));
}

TEST_CASE("Hub distribution restarts only for settings that affect catalog discovery")
{
    HubSettings previous;
    auto next = previous;

    next.Appearance = HubAppearance::Light;
    next.DefaultProjectLocation = "Projects";
    CHECK_FALSE(HubDistributionSettingsChanged(previous, next));

    next = previous;
    next.OfflineMode = true;
    CHECK(HubDistributionSettingsChanged(previous, next));

    next = previous;
    next.EnablePreReleaseChannel = true;
    CHECK(HubDistributionSettingsChanged(previous, next));

    next = previous;
    next.CustomProxyUrl = "https://proxy.example.test";
    CHECK(HubDistributionSettingsChanged(previous, next));
}

TEST_CASE("Hub distribution retries a cached catalog until the network validates it")
{
    DistributionCatalogSnapshot snapshot;
    snapshot.OnlineDiscoveryEnabled = true;
    snapshot.PackageCatalogs.push_back(
        {.Channel = "stable", .Status = {.State = DistributionCatalogSourceState::LastKnownGood}});
    snapshot.Content.Status.State = DistributionCatalogSourceState::Online;
    CHECK(HubDistributionNeedsNetworkRetry(snapshot));

    snapshot.PackageCatalogs.front().Status.State = DistributionCatalogSourceState::Online;
    CHECK_FALSE(HubDistributionNeedsNetworkRetry(snapshot));

    snapshot.OfflineMode = true;
    snapshot.PackageCatalogs.front().Status.State = DistributionCatalogSourceState::OfflineLastKnownGood;
    CHECK_FALSE(HubDistributionNeedsNetworkRetry(snapshot));
}
