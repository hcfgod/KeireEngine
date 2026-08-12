#include <KeireHubTests/TestSupport.h>

#include "KeireHub/HubAccountWorkflow.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    struct Script final
    {
        std::atomic_bool FailProfile = false;
        std::atomic_bool FailRefresh = false;
        std::atomic_uint32_t RefreshRequests = 0;
    };

    [[nodiscard]] std::vector<std::byte> Body(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        return {bytes.begin(), bytes.end()};
    }

    [[nodiscard]] NativeHttpResponse Response(const NativeHttpRequest& request, const std::string_view body)
    {
        return {.StatusCode = 200,
                .EffectiveUrl = request.Url,
                .Headers = {{"Content-Type", "application/json"}},
                .Body = Body(body)};
    }

    [[nodiscard]] HubAccountClientFactory ClientFactory(const std::shared_ptr<Script>& script)
    {
        return [script](const SupabaseConfiguration& configuration, const HubSettings&)
        {
            return SupabaseAccountClient::Create(
                configuration,
                [script](const NativeHttpRequest& request) -> HubResult<NativeHttpResponse>
                {
                    if (request.Url.find("grant_type=refresh_token") != std::string::npos)
                    {
                        script->RefreshRequests.fetch_add(1, std::memory_order_relaxed);
                        if (script->FailRefresh.load(std::memory_order_relaxed))
                        {
                            return HubResult<NativeHttpResponse>::Failure(
                                {.Code = HubErrorCode::AccountTransportFailed,
                                 .Message = "The scripted account service is temporarily unavailable.",
                                 .Retryable = true,
                                 .AffectedItem = "supabase-account"});
                        }
                    }
                    if (request.Url.find("/rest/v1/profiles") != std::string::npos)
                    {
                        if (request.Method != NativeHttpMethod::Get &&
                            script->FailProfile.load(std::memory_order_relaxed))
                        {
                            return HubResult<NativeHttpResponse>::Failure(
                                {.Code = HubErrorCode::AccountTransportFailed,
                                 .Message = "The scripted profile service is temporarily unavailable.",
                                 .Retryable = true,
                                 .AffectedItem = "supabase-profile"});
                        }
                        return HubResult<NativeHttpResponse>::Success(Response(request, "[]"));
                    }
                    if (request.Url.ends_with("/auth/v1/user"))
                    {
                        return HubResult<NativeHttpResponse>::Success(Response(
                            request,
                            R"({"id":"00112233-4455-6677-8899-aabbccddeeff","email":"user@example.com","email_confirmed_at":"2026-08-07T00:00:00Z"})"));
                    }
                    return HubResult<NativeHttpResponse>::Success(Response(
                        request,
                        R"({"access_token":"header.payload.signature","refresh_token":"refresh-token","expires_in":1,"user":{"id":"00112233-4455-6677-8899-aabbccddeeff","email":"user@example.com","email_confirmed_at":"2026-08-07T00:00:00Z"}})"));
                });
        };
    }

    [[nodiscard]] HubOAuthClientFactory OAuthFactory()
    {
        return [](const SupabaseConfiguration& configuration, const HubSettings&)
        {
            return DesktopOAuthClient::Create(
                {.SupabaseProjectUrl = configuration.ProjectUrl,
                 .ClientId = configuration.HubOAuthClientId,
                 .WebsiteCallbackUrl = configuration.HubOAuthWebsiteCallbackUrl},
                [](const NativeHttpRequest& request)
                {
                    return HubResult<NativeHttpResponse>::Success(Response(
                        request,
                        R"({"access_token":"header.payload.oauth-signature","refresh_token":"oauth-refresh-token","id_token":"e30.eyJub25jZSI6IkFRSURCQVVHQndnSkNnc01EUTRQRUJFU0V4UVZGaGNZR1JvYkhCMGVIeUEifQ.signature","token_type":"bearer","expires_in":3600})"));
                });
        };
    }

    [[nodiscard]] bool DeterministicEntropy(const std::span<std::byte> bytes)
    {
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = std::byte{static_cast<unsigned char>(index + 1U)};
        return true;
    }

    [[nodiscard]] std::string QueryValue(const std::string_view url, const std::string_view name)
    {
        const auto prefix = std::string(name) + '=';
        const auto begin = url.find(prefix);
        if (begin == url.npos)
            return {};
        const auto valueBegin = begin + prefix.size();
        const auto end = url.find('&', valueBegin);
        return std::string(url.substr(valueBegin, end - valueBegin));
    }

    template <typename Predicate>
    [[nodiscard]] std::shared_ptr<const HubAccountWorkflowSnapshot> WaitFor(HubAccountWorkflow& workflow,
                                                                            Predicate predicate)
    {
        for (std::size_t attempt = 0; attempt < 500U; ++attempt)
        {
            auto snapshot = workflow.Snapshot();
            if (predicate(*snapshot))
                return snapshot;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return workflow.Snapshot();
    }
} // namespace

TEST_CASE("Hub account workflow preserves signed-in state and backs off retryable refresh failures")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto configuration = temporary.Path() / "Config" / "Supabase.json";
    KeireHubTests::WriteText(
        configuration,
        R"({"schemaVersion":1,"enabled":true,"projectUrl":"https://fixture.supabase.co","publishableKey":"sb_publishable_fixture_key_0000000000000000"})");
    auto script = std::make_shared<Script>();
    HubAccountWorkflow workflow(ClientFactory(script));
    REQUIRE(workflow.Start(configuration, temporary.Path() / "Account" / "session.dat", {}));
    auto ready = WaitFor(workflow, [](const auto& snapshot) { return snapshot.Configured && !snapshot.Busy; });
    REQUIRE(ready->Configured);
    REQUIRE_FALSE(ready->Busy);

    REQUIRE(workflow.SignIn("user@example.com", "correct-password"));
    auto signedIn = WaitFor(workflow, [](const auto& snapshot) { return snapshot.SignedIn && !snapshot.Busy; });
    REQUIRE(signedIn->SignedIn);
    CHECK(signedIn->Email == "user@example.com");

    script->FailProfile.store(true, std::memory_order_relaxed);
    REQUIRE(workflow.SaveProfile("Kara"));
    auto profileFailure = WaitFor(workflow, [](const auto& snapshot) { return snapshot.Failure && !snapshot.Busy; });
    REQUIRE(profileFailure->Failure);
    CHECK(profileFailure->SignedIn);
    CHECK(profileFailure->Email == "user@example.com");

    script->FailRefresh.store(true, std::memory_order_relaxed);
    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                    std::chrono::system_clock::now().time_since_epoch())
                                                    .count()) +
                     120U;
    workflow.RefreshIfNeeded(now);
    auto refreshFailure = WaitFor(workflow,
                                  [&](const auto& snapshot)
                                  {
                                      return script->RefreshRequests.load(std::memory_order_relaxed) == 1U &&
                                             snapshot.Failure && !snapshot.Busy;
                                  });
    REQUIRE(refreshFailure->Failure);
    CHECK(refreshFailure->SignedIn);
    workflow.RefreshIfNeeded(now);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(script->RefreshRequests.load(std::memory_order_relaxed) == 1U);
    workflow.Stop();
}

TEST_CASE("Hub account workflow completes browser PKCE sign-in with a separate stored session")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto configuration = temporary.Path() / "Config" / "Supabase.json";
    KeireHubTests::WriteText(
        configuration,
        R"({"schemaVersion":2,"enabled":true,"projectUrl":"https://fixture.supabase.co","publishableKey":"sb_publishable_fixture_key_0000000000000000","hubOAuthEnabled":true,"hubOAuthClientId":"fixture-public-client","hubOAuthWebsiteCallbackUrl":"https://keire.test/oauth/hub/callback/"})");
    auto script = std::make_shared<Script>();
    HubAccountWorkflow workflow(ClientFactory(script), OAuthFactory());
    REQUIRE(workflow.Start(configuration, temporary.Path() / "Account" / "oauth-session.dat", {}));
    auto ready = WaitFor(workflow, [](const auto& snapshot) { return snapshot.Configured && !snapshot.Busy; });
    REQUIRE(ready->BrowserSignInAvailable);

    const auto authorization = workflow.BeginBrowserSignIn(DeterministicEntropy);
    REQUIRE(authorization);
    const auto state = QueryValue(authorization.Value(), "state");
    REQUIRE(state.size() >= 32U);
    CHECK(workflow.Snapshot()->BrowserSignInPending);
    REQUIRE(
        workflow.CompleteBrowserSignIn("keirehub://oauth/callback?code=single-use-authorization-code&state=" + state));
    auto signedIn = WaitFor(workflow, [](const auto& snapshot) { return snapshot.SignedIn && !snapshot.Busy; });
    REQUIRE(signedIn->SignedIn);
    CHECK(signedIn->Email == "user@example.com");
    CHECK_FALSE(signedIn->BrowserSignInPending);
    workflow.Stop();
}

TEST_CASE("Hub account workflow can cancel a pending browser sign-in without creating a session")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto configuration = temporary.Path() / "Config" / "Supabase.json";
    KeireHubTests::WriteText(
        configuration,
        R"({"schemaVersion":2,"enabled":true,"projectUrl":"https://fixture.supabase.co","publishableKey":"sb_publishable_fixture_key_0000000000000000","hubOAuthEnabled":true,"hubOAuthClientId":"fixture-public-client","hubOAuthWebsiteCallbackUrl":"https://keire.test/oauth/hub/callback/"})");
    auto script = std::make_shared<Script>();
    HubAccountWorkflow workflow(ClientFactory(script), OAuthFactory());
    REQUIRE(workflow.Start(configuration, temporary.Path() / "Account" / "cancelled-oauth-session.dat", {}));
    auto ready = WaitFor(workflow, [](const auto& snapshot) { return snapshot.Configured && !snapshot.Busy; });
    REQUIRE(ready->BrowserSignInAvailable);
    REQUIRE(workflow.BeginBrowserSignIn(DeterministicEntropy));
    REQUIRE(workflow.Snapshot()->BrowserSignInPending);

    REQUIRE(workflow.CancelBrowserSignIn());
    const auto cancelled = workflow.Snapshot();
    CHECK_FALSE(cancelled->BrowserSignInPending);
    CHECK_FALSE(cancelled->SignedIn);
    CHECK_FALSE(cancelled->Failure);
    CHECK(cancelled->Message == "Browser sign-in cancelled. No Hub session was created.");
    CHECK_FALSE(workflow.CancelBrowserSignIn());
    workflow.Stop();
}
