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

TEST_CASE("Hub distribution reports a reachable service as online before platform catalogs are published")
{
    DistributionCatalogSnapshot catalogs;
    catalogs.OnlineDiscoveryEnabled = true;
    catalogs.PackageCatalogs.push_back(
        {.Channel = "stable", .Status = {.State = DistributionCatalogSourceState::Online}});
    catalogs.Content.Status.State = DistributionCatalogSourceState::Online;

    HubProductSnapshot product;
    ApplyHubDistributionSnapshot({.Catalogs = std::make_shared<const DistributionCatalogSnapshot>(catalogs)}, product);

    CHECK(product.Online);
    CHECK_FALSE(product.Reconnecting);
    CHECK_FALSE(product.CatalogAvailable);
}

TEST_CASE("Hub distribution explains cache fallback and clears the failure after network recovery")
{
    DistributionCatalogSnapshot catalogs;
    catalogs.OnlineDiscoveryEnabled = true;
    catalogs.PackageCatalogs.push_back({.Channel = "stable",
                                        .Status = {.State = DistributionCatalogSourceState::LastKnownGood,
                                                   .Error = HubError{.Code = HubErrorCode::CatalogTransportFailed,
                                                                     .Message = "Service unavailable.",
                                                                     .Retryable = true,
                                                                     .TechnicalDetails = "HTTP status 503"}}});
    catalogs.Content.Status.State = DistributionCatalogSourceState::Online;
    HubProductSnapshot product;
    ApplyHubDistributionSnapshot({.Catalogs = std::make_shared<const DistributionCatalogSnapshot>(catalogs)}, product);
    CHECK(product.Reconnecting);
    CHECK(product.ConnectionMessage.find("HTTP status 503") != std::string::npos);
    CHECK(product.ConnectionMessage.find("Retrying automatically") != std::string::npos);
    REQUIRE(product.ConnectionFailureCode);
    CHECK(*product.ConnectionFailureCode == HubErrorCode::CatalogTransportFailed);

    product.Settings.OfflineMode = true;
    ApplyHubDistributionSnapshot({.Catalogs = std::make_shared<const DistributionCatalogSnapshot>(catalogs)}, product);
    CHECK_FALSE(product.Reconnecting);
    CHECK_FALSE(product.Online);
    CHECK(product.ConnectionMessage.empty());
    CHECK_FALSE(product.ConnectionFailureCode);
    product.Settings.OfflineMode = false;

    catalogs.PackageCatalogs.front().Status.Error->Retryable = false;
    CHECK_FALSE(HubDistributionNeedsNetworkRetry(catalogs));
    ApplyHubDistributionSnapshot({.Catalogs = std::make_shared<const DistributionCatalogSnapshot>(catalogs)}, product);
    CHECK_FALSE(product.Reconnecting);
    CHECK_FALSE(product.ConnectionMessage.empty());

    catalogs.PackageCatalogs.front().Status.State = DistributionCatalogSourceState::Online;
    catalogs.PackageCatalogs.front().Status.Error.reset();
    ApplyHubDistributionSnapshot({.Catalogs = std::make_shared<const DistributionCatalogSnapshot>(catalogs)}, product);
    CHECK(product.Online);
    CHECK_FALSE(product.Reconnecting);
    CHECK(product.ConnectionMessage.empty());
    CHECK_FALSE(product.ConnectionFailureCode);
}
