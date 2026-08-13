#include "KeireHubRuntime/DesktopOAuth.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace KeireHub;

namespace
{
    [[nodiscard]] std::vector<std::byte> Body(const std::string_view value)
    {
        const auto bytes = std::as_bytes(std::span(value.data(), value.size()));
        return {bytes.begin(), bytes.end()};
    }

    [[nodiscard]] DesktopOAuthOptions Options()
    {
        return {.SupabaseProjectUrl = "https://fixture.supabase.co",
                .ClientId = "00112233-4455-6677-8899-aabbccddeeff",
                .WebsiteCallbackUrl = "https://keire.test/oauth/hub/callback/"};
    }

    [[nodiscard]] bool DeterministicEntropy(std::span<std::byte> bytes)
    {
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = std::byte{static_cast<unsigned char>(index + 1U)};
        return true;
    }
} // namespace

TEST_CASE("Desktop OAuth creates a public-client PKCE request without a client secret")
{
    auto client = DesktopOAuthClient::Create(Options(), [](const NativeHttpRequest&) -> HubResult<NativeHttpResponse>
                                             { throw std::logic_error("transport should not be called"); });
    REQUIRE(client);
    const auto authorization = client.Value().BeginAuthorization(DeterministicEntropy);
    REQUIRE(authorization);
    CHECK(authorization.Value().CodeVerifier.size() >= 43U);
    CHECK(authorization.Value().State.size() >= 32U);
    CHECK(authorization.Value().Nonce.size() >= 32U);
    CHECK(authorization.Value().AuthorizationUrl.starts_with(
        "https://fixture.supabase.co/auth/v1/oauth/authorize?response_type=code"));
    CHECK(authorization.Value().AuthorizationUrl.find("code_challenge_method=S256") != std::string::npos);
    CHECK(authorization.Value().AuthorizationUrl.find("client_secret") == std::string::npos);
    CHECK(authorization.Value().AuthorizationUrl.find("nonce=") != std::string::npos);
}

TEST_CASE("Desktop OAuth rejects callback state substitution before token exchange")
{
    auto client = DesktopOAuthClient::Create(Options(), [](const NativeHttpRequest&) -> HubResult<NativeHttpResponse>
                                             { throw std::logic_error("transport should not be called"); });
    REQUIRE(client);
    const auto authorization = client.Value().BeginAuthorization(DeterministicEntropy);
    REQUIRE(authorization);
    const auto callback = client.Value().ValidateCallback(
        "keirehub://oauth/callback?code=single-use-authorization-code&state=substituted-state-value-00000000",
        authorization.Value().State);
    REQUIRE_FALSE(callback);
    CHECK(callback.Error().Code == HubErrorCode::AccountAuthenticationFailed);
}

TEST_CASE("Desktop OAuth exchanges a single-use code itself and rotates refresh tokens")
{
    NativeHttpRequest captured;
    auto client = DesktopOAuthClient::Create(
        Options(),
        [&](const NativeHttpRequest& request)
        {
            captured = request;
            return HubResult<NativeHttpResponse>::Success(
                {.StatusCode = 200,
                 .EffectiveUrl = "https://fixture.supabase.co/auth/v1/oauth/token",
                 .Headers = {{"Content-Type", "application/json"}},
                 .Body = Body(
                     R"({"access_token":"header.payload.oauth-signature","refresh_token":"a1B2c3D4e5F6","id_token":"e30.eyJub25jZSI6IkFRSURCQVVHQndnSkNnc01EUTRQRUJFU0V4UVZGaGNZR1JvYkhCMGVIeUEifQ.signature","token_type":"bearer","expires_in":3600})")});
        });
    REQUIRE(client);
    const auto authorization = client.Value().BeginAuthorization(DeterministicEntropy);
    REQUIRE(authorization);
    const auto callback = client.Value().ValidateCallback(
        "keirehub://oauth/callback?code=single-use-authorization-code&state=" + authorization.Value().State,
        authorization.Value().State);
    REQUIRE(callback);
    const auto tokens = client.Value().Exchange(authorization.Value(), callback.Value());
    REQUIRE(tokens);
    CHECK(tokens.Value().RefreshToken == "a1B2c3D4e5F6");
    CHECK(captured.Method == NativeHttpMethod::Post);
    CHECK(captured.Url == "https://fixture.supabase.co/auth/v1/oauth/token");
    const std::string form(reinterpret_cast<const char*>(captured.Body.data()), captured.Body.size());
    CHECK(form.find("grant_type=authorization_code") != std::string::npos);
    CHECK(form.find("code_verifier=") != std::string::npos);
    CHECK(form.find("client_secret") == std::string::npos);
}

TEST_CASE("Desktop OAuth refresh accepts compact opaque refresh tokens")
{
    NativeHttpRequest captured;
    auto client = DesktopOAuthClient::Create(
        Options(),
        [&](const NativeHttpRequest& request)
        {
            captured = request;
            return HubResult<NativeHttpResponse>::Success(
                {.StatusCode = 200,
                 .EffectiveUrl = "https://fixture.supabase.co/auth/v1/oauth/token",
                 .Headers = {{"Content-Type", "application/json"}},
                 .Body = Body(
                     R"({"access_token":"header.payload.refreshed-signature","refresh_token":"f6E5d4C3b2A1","token_type":"bearer","expires_in":3600})")});
        });
    REQUIRE(client);

    const auto tokens = client.Value().Refresh("a1B2c3D4e5F6");

    REQUIRE(tokens);
    CHECK(tokens.Value().RefreshToken == "f6E5d4C3b2A1");
    const std::string form(reinterpret_cast<const char*>(captured.Body.data()), captured.Body.size());
    CHECK(form.find("grant_type=refresh_token") != std::string::npos);
    CHECK(form.find("refresh_token=a1B2c3D4e5F6") != std::string::npos);
    CHECK(form.find("client_id=00112233-4455-6677-8899-aabbccddeeff") != std::string::npos);
    CHECK(form.find("client_secret") == std::string::npos);
}

TEST_CASE("Desktop OAuth rejects an empty refresh token from the authorization service")
{
    auto client = DesktopOAuthClient::Create(
        Options(),
        [](const NativeHttpRequest& request)
        {
            return HubResult<NativeHttpResponse>::Success(
                {.StatusCode = 200,
                 .EffectiveUrl = request.Url,
                 .Headers = {{"Content-Type", "application/json"}},
                 .Body = Body(
                     R"({"access_token":"header.payload.refreshed-signature","refresh_token":"","token_type":"bearer","expires_in":3600})")});
        });
    REQUIRE(client);

    const auto tokens = client.Value().Refresh("a1B2c3D4e5F6");

    REQUIRE_FALSE(tokens);
    CHECK(tokens.Error().Code == HubErrorCode::AccountSessionInvalid);
}

TEST_CASE("Desktop OAuth rejects an ID token with a substituted nonce")
{
    auto client = DesktopOAuthClient::Create(
        Options(),
        [](const NativeHttpRequest&)
        {
            return HubResult<NativeHttpResponse>::Success(
                {.StatusCode = 200,
                 .EffectiveUrl = "https://fixture.supabase.co/auth/v1/oauth/token",
                 .Headers = {{"Content-Type", "application/json"}},
                 .Body = Body(
                     R"({"access_token":"header.payload.oauth-signature","refresh_token":"rotated-refresh-token","id_token":"e30.eyJub25jZSI6InN1YnN0aXR1dGVkLW5vbmNlLXZhbHVlLTAwMDAwMDAwMDAwMDAwMDAifQ.signature","token_type":"bearer","expires_in":3600})")});
        });
    REQUIRE(client);
    const auto authorization = client.Value().BeginAuthorization(DeterministicEntropy);
    REQUIRE(authorization);
    const auto callback = client.Value().ValidateCallback(
        "keirehub://oauth/callback?code=single-use-authorization-code&state=" + authorization.Value().State,
        authorization.Value().State);
    REQUIRE(callback);
    const auto tokens = client.Value().Exchange(authorization.Value(), callback.Value());
    REQUIRE_FALSE(tokens);
    CHECK(tokens.Error().Code == HubErrorCode::AccountAuthenticationFailed);
}
