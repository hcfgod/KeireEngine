#include <KeireHub/HubAccountIntegration.h>
#include <KeireHub/HubProductUi.h>

#include <doctest/doctest.h>

using namespace KeireHub;

TEST_CASE("Hub account integration ticks stay inert before startup and after repeated shutdown")
{
    HubAccountIntegration integration;
    const HubSettings settings;
    REQUIRE(integration.Tick(settings, 100U, {}, nullptr));

    integration.RequestRefresh();
    REQUIRE(integration.Tick(settings, 110U, {}, nullptr));
    CHECK_FALSE(integration.AccessToken(110U));
    HubProductSnapshot product;
    integration.ApplySnapshot(product);
    CHECK_FALSE(product.AccountBusy);
    CHECK_FALSE(product.AccountConfigured);
    CHECK_FALSE(product.AccountHasError);

    integration.Stop();
    integration.Stop();
    integration.RequestRefresh();
    REQUIRE(integration.Tick(settings, 120U, {}, nullptr));
    CHECK_FALSE(integration.AccessToken(120U));
    integration.ApplySnapshot(product);
    CHECK_FALSE(product.AccountBusy);
    CHECK_FALSE(product.AccountHasError);
    integration.Stop();
}
