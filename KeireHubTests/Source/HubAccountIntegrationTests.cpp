#include "KeireHubInternal/HubAccountNotice.h"
#include <KeireHub/HubAccountIntegration.h>
#include <KeireHub/HubProductUi.h>

#include <doctest/doctest.h>

using namespace KeireHub;

TEST_CASE("Browser sign-in progress notice follows completion without overwriting unrelated notices")
{
    HubAccountWorkflowSnapshot account;
    std::string notice(BrowserSignInProgressNotice);
    bool error = false;
    account.Busy = true;
    ApplyBrowserSignInCompletionNotice(account, notice, error);
    CHECK(notice == BrowserSignInProgressNotice);
    account.Busy = false;
    account.BrowserSignInPending = true;
    ApplyBrowserSignInCompletionNotice(account, notice, error);
    CHECK(notice == BrowserSignInProgressNotice);
    account.BrowserSignInPending = false;
    account.SignedIn = true;
    ApplyBrowserSignInCompletionNotice(account, notice, error);
    CHECK(notice == "Signed in through your browser.");
    CHECK_FALSE(error);
    ApplyBrowserSignInCompletionNotice(account, notice, error);
    CHECK(notice == "Signed in through your browser.");

    notice = "A different action failed.";
    error = true;
    ApplyBrowserSignInCompletionNotice(account, notice, error);
    CHECK(notice == "A different action failed.");
    CHECK(error);
}

TEST_CASE("Browser sign-in progress notice reports exchange failure and cancellation without claiming success")
{
    HubAccountWorkflowSnapshot account;
    account.Failure = HubError{.Code = HubErrorCode::InvalidTransition, .Message = "Authorization expired."};
    std::string notice(BrowserSignInProgressNotice);
    bool error = false;
    ApplyBrowserSignInCompletionNotice(account, notice, error);
    CHECK(notice == "Authorization expired.");
    CHECK(error);
    account.Failure.reset();
    account.Message = "Browser sign-in cancelled.";
    notice = BrowserSignInProgressNotice;
    ApplyBrowserSignInCompletionNotice(account, notice, error);
    CHECK(notice == "Browser sign-in cancelled.");
    CHECK_FALSE(error);
    account.Message.clear();
    notice = BrowserSignInProgressNotice;
    ApplyBrowserSignInCompletionNotice(account, notice, error);
    CHECK(notice == "Browser sign-in is no longer in progress.");
    CHECK_FALSE(error);
}

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
