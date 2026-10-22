#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/AccountSessionStore.h"
#include "KeireHubRuntime/SupabaseAccount.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    [[nodiscard]] SupabaseConfiguration Configuration()
    {
        return {.Enabled = true,
                .ProjectUrl = "https://fixture.supabase.co",
                .PublishableKey = "sb_publishable_fixture_key_0000000000000000"};
    }

    [[nodiscard]] std::vector<std::byte> Body(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        return {bytes.begin(), bytes.end()};
    }

    [[nodiscard]] NativeHttpResponse JsonResponse(const std::uint16_t status, const std::string_view body)
    {
        return {.StatusCode = status,
                .EffectiveUrl = "https://fixture.supabase.co/auth/v1/token",
                .Headers = {{"Content-Type", "application/json"}},
                .Body = Body(body)};
    }

    [[nodiscard]] std::string SessionJson(const std::string_view refresh = "refresh-token")
    {
        return std::string(R"({"access_token":"header.payload.signature","refresh_token":")") + std::string(refresh) +
               R"(","expires_in":3600,"user":{"id":"00112233-4455-6677-8899-aabbccddeeff","email":"user@example.com","email_confirmed_at":"2026-08-07T00:00:00Z"}})";
    }

    [[nodiscard]] std::optional<std::string> Header(const NativeHttpRequest& request, const std::string_view name)
    {
        const auto iterator =
            std::ranges::find_if(request.Headers, [&](const CatalogHttpHeader& header) { return header.Name == name; });
        if (iterator == request.Headers.end())
            return std::nullopt;
        return iterator->Value;
    }
} // namespace

TEST_CASE("Supabase account configuration is explicit, HTTPS-only, and publishable-key-only")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "Config" / "Supabase.json";

    auto missing = LoadSupabaseConfiguration(path);
    REQUIRE(missing);
    CHECK_FALSE(missing.Value().Enabled);

    KeireHubTests::WriteText(path, R"({"schemaVersion":1,"enabled":false})");
    auto disabled = LoadSupabaseConfiguration(path);
    REQUIRE(disabled);
    CHECK_FALSE(disabled.Value().Enabled);

    KeireHubTests::WriteText(
        path,
        R"({"schemaVersion":1,"enabled":true,"projectUrl":"https://fixture.supabase.co","publishableKey":"sb_publishable_fixture_key_0000000000000000"})");
    auto enabled = LoadSupabaseConfiguration(path);
    REQUIRE(enabled);
    CHECK(enabled.Value().ProjectUrl == "https://fixture.supabase.co");
    CHECK(enabled.Value().PublishableKey == "sb_publishable_fixture_key_0000000000000000");
    CHECK_FALSE(enabled.Value().HubOAuthEnabled);

    KeireHubTests::WriteText(
        path,
        R"({"schemaVersion":2,"enabled":true,"projectUrl":"https://fixture.supabase.co","publishableKey":"sb_publishable_fixture_key_0000000000000000","hubOAuthEnabled":true,"hubOAuthClientId":"fixture-public-client","hubOAuthWebsiteCallbackUrl":"https://keire.example/oauth/hub/callback/"})");
    auto oauth = LoadSupabaseConfiguration(path);
    REQUIRE(oauth);
    CHECK(oauth.Value().HubOAuthEnabled);
    CHECK(oauth.Value().HubOAuthClientId == "fixture-public-client");
    CHECK(oauth.Value().HubOAuthWebsiteCallbackUrl == "https://keire.example/oauth/hub/callback/");

    KeireHubTests::WriteText(
        path,
        R"({"schemaVersion":2,"enabled":true,"projectUrl":"https://fixture.supabase.co","publishableKey":"sb_publishable_fixture_key_0000000000000000","hubOAuthEnabled":true,"hubOAuthClientId":"fixture-public-client","hubOAuthWebsiteCallbackUrl":"http://keire.example/oauth/hub/callback/"})");
    CHECK_FALSE(LoadSupabaseConfiguration(path));

    KeireHubTests::WriteText(
        path,
        R"({"schemaVersion":1,"enabled":true,"projectUrl":"http://fixture.supabase.co","publishableKey":"sb_secret_never_ship"})");
    const auto unsafe = LoadSupabaseConfiguration(path);
    REQUIRE_FALSE(unsafe);
    CHECK(unsafe.Error().Code == HubErrorCode::AccountConfigurationInvalid);
}

TEST_CASE("Local account configuration uses registered website OAuth without a Supabase key")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "Config" / "Supabase.json";
    KeireHubTests::WriteText(
        path,
        R"({"schemaVersion":3,"enabled":true,"provider":"mysql","projectUrl":"https://platform.example","hubOAuthClientId":"keire-hub-desktop"})");
    const auto configuration = LoadSupabaseConfiguration(path);
    REQUIRE(configuration);
    CHECK(configuration.Value().LocalPlatform);
    CHECK(configuration.Value().HubOAuthEnabled);
    CHECK(configuration.Value().PublishableKey.empty());
    CHECK(configuration.Value().HubOAuthWebsiteCallbackUrl == "https://platform.example/oauth/hub/callback/");
    auto client = SupabaseAccountClient::Create(
        configuration.Value(),
        [](const NativeHttpRequest& request)
        {
            CHECK_FALSE(Header(request, "apikey"));
            CHECK(Header(request, "Authorization") == "Bearer native-access-token");
            CHECK(request.Url == "https://platform.example/auth/v1/user");
            return HubResult<NativeHttpResponse>::Success(JsonResponse(
                200,
                R"({"id":"00112233-4455-6677-8899-aabbccddeeff","email":"user@example.com","email_confirmed_at":"2026-10-01"})"));
        });
    REQUIRE(client);
    CHECK(client.Value().FetchUser("native-access-token"));
}

TEST_CASE("Local account configuration rejects extra secrets and insecure origins")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto path = temporary.Path() / "Config" / "Supabase.json";
    for (
        const auto* text :
        {R"({"schemaVersion":3,"enabled":true,"provider":"mysql","projectUrl":"http://platform.example","hubOAuthClientId":"keire-hub-desktop"})",
         R"({"schemaVersion":3,"enabled":true,"provider":"mysql","projectUrl":"https://platform.example/path","hubOAuthClientId":"keire-hub-desktop"})",
         R"({"schemaVersion":3,"enabled":true,"provider":"mysql","projectUrl":"https://platform.example","hubOAuthClientId":"client with spaces"})",
         R"({"schemaVersion":3,"enabled":true,"provider":"mysql","projectUrl":"https://platform.example","hubOAuthClientId":"keire-hub-desktop","publishableKey":"unexpected"})"})
    {
        KeireHubTests::WriteText(path, text);
        CHECK_FALSE(LoadSupabaseConfiguration(path));
    }
}

TEST_CASE("Supabase password sign-in sends bounded JSON and parses the rotated session")
{
    NativeHttpRequest captured;
    auto client = SupabaseAccountClient::Create(Configuration(),
                                                [&](const NativeHttpRequest& request)
                                                {
                                                    captured = request;
                                                    return HubResult<NativeHttpResponse>::Success(
                                                        JsonResponse(200, SessionJson("rotated-refresh")));
                                                });
    REQUIRE(client);
    const auto session = client.Value().SignIn("user@example.com", "correct-password");
    REQUIRE(session);
    CHECK(session.Value().User.Email == "user@example.com");
    CHECK(session.Value().User.EmailConfirmed);
    CHECK(session.Value().RefreshToken == "rotated-refresh");
    CHECK(captured.Method == NativeHttpMethod::Post);
    CHECK(captured.Url == "https://fixture.supabase.co/auth/v1/token?grant_type=password");
    REQUIRE(Header(captured, "apikey"));
    CHECK(*Header(captured, "apikey") == "sb_publishable_fixture_key_0000000000000000");
    CHECK_FALSE(Header(captured, "Authorization"));
    const std::string requestBody(reinterpret_cast<const char*>(captured.Body.data()), captured.Body.size());
    CHECK(requestBody.find("user@example.com") != std::string::npos);
    CHECK(requestBody.find("correct-password") != std::string::npos);
}

TEST_CASE("Supabase sign-up represents email confirmation without inventing a session")
{
    auto client = SupabaseAccountClient::Create(
        Configuration(),
        [](const NativeHttpRequest&)
        {
            return HubResult<NativeHttpResponse>::Success(JsonResponse(
                200,
                R"({"id":"00112233-4455-6677-8899-aabbccddeeff","email":"new@example.com","email_confirmed_at":null})"));
        });
    REQUIRE(client);
    const auto result = client.Value().SignUp("new@example.com", "correct-password");
    REQUIRE(result);
    CHECK(result.Value().ConfirmationRequired);
    CHECK_FALSE(result.Value().Session);
    CHECK_FALSE(result.Value().User.EmailConfirmed);
}

TEST_CASE("Supabase sign-up reports confirmation email cooldowns without blaming account data")
{
    auto client = SupabaseAccountClient::Create(
        Configuration(),
        [](const NativeHttpRequest&)
        {
            return HubResult<NativeHttpResponse>::Success(
                JsonResponse(429, R"({"code":"over_email_send_rate_limit","message":"request after 21 seconds"})"));
        });
    REQUIRE(client);
    const auto result = client.Value().SignUp("new@example.com", "correct-password");
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::AccountTransportFailed);
    CHECK(result.Error().Retryable);
    CHECK(result.Error().Message.find("confirmation email") != std::string::npos);
}

TEST_CASE("Supabase sign-up preserves direct session responses when confirmation is disabled")
{
    auto client = SupabaseAccountClient::Create(
        Configuration(), [](const NativeHttpRequest&)
        { return HubResult<NativeHttpResponse>::Success(JsonResponse(200, SessionJson())); });
    REQUIRE(client);
    const auto result = client.Value().SignUp("user@example.com", "correct-password");
    REQUIRE(result);
    CHECK_FALSE(result.Value().ConfirmationRequired);
    REQUIRE(result.Value().Session);
    CHECK(result.Value().Session->User.EmailConfirmed);
}

TEST_CASE("Supabase profile requests carry the user JWT and preserve owner identity")
{
    std::vector<NativeHttpRequest> captured;
    auto client = SupabaseAccountClient::Create(
        Configuration(),
        [&](const NativeHttpRequest& request)
        {
            captured.push_back(request);
            return HubResult<NativeHttpResponse>::Success(
                JsonResponse(200, captured.size() == 1U ? R"([])" : R"([{"display_name":"Kara","avatar_url":null}])"));
        });
    REQUIRE(client);
    AccountSession session{
        .AccessToken = "header.payload.signature",
        .RefreshToken = "refresh-token",
        .User = {.Id = "00112233-4455-6677-8899-aabbccddeeff", .Email = "user@example.com", .EmailConfirmed = true}};
    const auto empty = client.Value().FetchProfile(session);
    REQUIRE(empty);
    CHECK(empty.Value().DisplayName.empty());
    const auto saved = client.Value().SaveProfile(session, "Kara");
    REQUIRE(saved);
    REQUIRE(captured.size() == 2U);
    CHECK(captured.front().Url.find("user_id=eq.00112233-4455-6677-8899-aabbccddeeff") != std::string::npos);
    REQUIRE(Header(captured.front(), "Authorization"));
    CHECK(*Header(captured.front(), "Authorization") == "Bearer header.payload.signature");
    const std::string saveBody(reinterpret_cast<const char*>(captured.back().Body.data()), captured.back().Body.size());
    CHECK(saveBody.find("00112233-4455-6677-8899-aabbccddeeff") != std::string::npos);
    CHECK(saveBody.find("Kara") != std::string::npos);
}

TEST_CASE("Supabase session identity is resolved with the Hub bearer token")
{
    NativeHttpRequest captured;
    auto client = SupabaseAccountClient::Create(
        Configuration(),
        [&](const NativeHttpRequest& request)
        {
            captured = request;
            return HubResult<NativeHttpResponse>::Success(JsonResponse(
                200,
                R"({"id":"00112233-4455-6677-8899-aabbccddeeff","email":"oauth@example.com","email_confirmed_at":"2026-08-11T00:00:00Z"})"));
        });
    REQUIRE(client);
    const auto user = client.Value().FetchUser("header.payload.signature");
    REQUIRE(user);
    CHECK(user.Value().Email == "oauth@example.com");
    CHECK(user.Value().EmailConfirmed);
    CHECK(captured.Method == NativeHttpMethod::Get);
    CHECK(captured.Url == "https://fixture.supabase.co/auth/v1/user");
    REQUIRE(Header(captured, "Authorization"));
    CHECK(*Header(captured, "Authorization") == "Bearer header.payload.signature");
}

TEST_CASE("Account session storage never writes a Windows refresh token in plaintext")
{
    KeireHubTests::TemporaryDirectory temporary;
    AccountSessionStore store(temporary.Path() / "Account" / "session.dat");
#if defined(_WIN32)
    REQUIRE(store.PersistentStorageAvailable());
    REQUIRE(store.SaveRefreshToken("high-value-refresh-token"));
    const auto raw = KeireHubTests::ReadText(store.Path());
    CHECK(raw.find("high-value-refresh-token") == std::string::npos);
    const auto loaded = store.LoadRefreshToken();
    REQUIRE(loaded);
    REQUIRE(loaded.Value());
    CHECK(*loaded.Value() == "high-value-refresh-token");
    REQUIRE(store.SaveSession(AccountSessionKind::DesktopOAuth, "oauth-refresh-token"));
    const auto storedSession = store.LoadSession();
    REQUIRE(storedSession);
    REQUIRE(storedSession.Value());
    CHECK(storedSession.Value()->RefreshToken == "oauth-refresh-token");
    CHECK(storedSession.Value()->Kind == AccountSessionKind::DesktopOAuth);
    CHECK_FALSE(storedSession.Value()->Binding.has_value());
    const auto compatibleToken = store.LoadRefreshToken();
    REQUIRE(compatibleToken);
    REQUIRE(compatibleToken.Value());
    CHECK(*compatibleToken.Value() == "oauth-refresh-token");
    const AccountSessionBinding binding{
        .Provider = "mysql", .Origin = "https://platform.invalid", .ClientId = "keire-hub-desktop"};
    REQUIRE(store.SaveSession(AccountSessionKind::DesktopOAuth, "bound-refresh-token", binding));
    const auto bound = store.LoadSession();
    REQUIRE(bound);
    REQUIRE(bound.Value());
    REQUIRE(bound.Value()->Binding);
    CHECK(*bound.Value()->Binding == binding);
    CHECK(bound.Value()->RefreshToken == "bound-refresh-token");
    CHECK(bound.Value()->Kind == AccountSessionKind::DesktopOAuth);
    CHECK(KeireHubTests::ReadText(store.Path()).find("bound-refresh-token") == std::string::npos);
    auto invalid = binding;
    invalid.Provider = "unknown";
    CHECK_FALSE(store.SaveSession(AccountSessionKind::DesktopOAuth, "replacement", invalid));
    invalid = binding;
    invalid.Origin = "https://user@platform.invalid";
    CHECK_FALSE(store.SaveSession(AccountSessionKind::DesktopOAuth, "replacement", invalid));
    invalid.Origin = "https://platform.invalid/path";
    CHECK_FALSE(store.SaveSession(AccountSessionKind::DesktopOAuth, "replacement", invalid));
    invalid = binding;
    invalid.ClientId = std::string(129U, 'a');
    CHECK_FALSE(store.SaveSession(AccountSessionKind::DesktopOAuth, "replacement", invalid));
    CHECK_FALSE(store.SaveSession(AccountSessionKind::DesktopOAuth, std::string(4097U, 'a'), binding));
    const auto unchanged = store.LoadSession();
    REQUIRE(unchanged);
    REQUIRE(unchanged.Value());
    CHECK(unchanged.Value()->RefreshToken == "bound-refresh-token");
    const std::string maximumToken(4096U, '\"');
    REQUIRE(store.SaveSession(AccountSessionKind::DesktopOAuth, maximumToken, binding));
    const auto maximumStored = store.LoadSession();
    REQUIRE(maximumStored);
    REQUIRE(maximumStored.Value());
    CHECK(maximumStored.Value()->RefreshToken == maximumToken);
    REQUIRE(store.Clear());
    CHECK_FALSE(std::filesystem::exists(store.Path()));
#else
    CHECK_FALSE(store.PersistentStorageAvailable());
    REQUIRE(store.SaveRefreshToken("session-only-refresh-token"));
    CHECK_FALSE(std::filesystem::exists(store.Path()));
#endif
}
