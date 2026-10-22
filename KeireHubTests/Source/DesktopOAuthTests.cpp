#include "KeireHubRuntime/DesktopOAuth.h"
#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubTests/TestSodium.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
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

    [[nodiscard]] std::string UrlBase64(std::string value)
    {
        std::replace(value.begin(), value.end(), '+', '-');
        std::replace(value.begin(), value.end(), '/', '_');
        while (value.ends_with('='))
        {
            value.pop_back();
        }
        return value;
    }

    [[nodiscard]] std::string EncodeJwtJson(const Detail::Json& value)
    {
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        const auto text = value.dump();
        std::string encoded;
        unsigned int accumulator = 0;
        unsigned int bits = 0;
        for (const unsigned char character : text)
        {
            accumulator = (accumulator << 8U) | character;
            bits += 8U;
            while (bits >= 6U)
            {
                bits -= 6U;
                encoded.push_back(alphabet[(accumulator >> bits) & 63U]);
            }
        }
        if (bits != 0U)
        {
            encoded.push_back(alphabet[(accumulator << (6U - bits)) & 63U]);
        }
        return encoded;
    }
} // namespace

TEST_CASE("Native OAuth verifies signed identity and access binding on exchange and refresh")
{
    KeireHubTests::TestSodiumSigner signer;
    auto options = Options();
    options.NativePlatform = true;
    options.WebsiteCallbackUrl = options.SupabaseProjectUrl + "/oauth/hub/callback/";
    options.VerificationLibraryPath = signer.LibraryPath();
    const auto publicDocument = Detail::Json::parse(signer.PublicKeyDocument());
    Detail::Json key = {{"kid", signer.KeyId()}, {"kty", "OKP"},
                        {"crv", "Ed25519"},      {"alg", "EdDSA"},
                        {"use", "sig"},          {"x", UrlBase64(publicDocument.at("publicKey").get<std::string>())}};
    Detail::Json keys = {{"keys", Detail::Json::array({key})}};
    Detail::Json header = {{"alg", "EdDSA"}, {"typ", "JWT"}, {"kid", signer.KeyId()}};
    const auto now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    Detail::Json claims = {{"iss", options.SupabaseProjectUrl + "/auth/v1"},
                           {"aud", options.ClientId},
                           {"sub", "00112233-4455-6677-8899-aabbccddeeff"},
                           {"sid", "11223344-5566-7788-99aa-bbccddeeff00"},
                           {"iat", now},
                           {"exp", now + 300},
                           {"auth_time", now - 5},
                           {"acr", "urn:keire:assurance:1"},
                           {"nonce", "AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA"},
                           {"at_hash", "skFvEwiacy5-Jbn6RaH1H9bNO6O5DGqDQFBjZdoBfuM"}};
    bool expected = true;
    bool badSignature = false;
    bool missingIdentity = false;
    bool redirectedKeys = false;
    bool redirectedToken = false;
    bool badKeyStatus = false;
    bool duplicateTokenField = false;
    bool duplicateKeyField = false;
    bool refresh = false;
    std::string accessToken(43U, 'A');
    SUBCASE("valid exchange") {}
    SUBCASE("valid refresh") { refresh = true; }
    SUBCASE("refresh rejects bad signature")
    {
        refresh = true;
        badSignature = true;
        expected = false;
    }
    SUBCASE("signature substitution")
    {
        badSignature = true;
        expected = false;
    }
    SUBCASE("missing ID token")
    {
        missingIdentity = true;
        expected = false;
    }
    SUBCASE("unsigned algorithm")
    {
        header["alg"] = "none";
        expected = false;
    }
    SUBCASE("header key injection")
    {
        header["jku"] = "https://attacker.invalid/keys";
        expected = false;
    }
    SUBCASE("wrong issuer")
    {
        claims["iss"] = "https://attacker.invalid/auth/v1";
        expected = false;
    }
    SUBCASE("wrong audience")
    {
        claims["aud"] = "different-client";
        expected = false;
    }
    SUBCASE("wrong authorized party")
    {
        claims["azp"] = "different-client";
        expected = false;
    }
    SUBCASE("not yet valid")
    {
        claims["nbf"] = now + 120;
        expected = false;
    }
    SUBCASE("duplicate token field")
    {
        duplicateTokenField = true;
        expected = false;
    }
    SUBCASE("duplicate key document field")
    {
        duplicateKeyField = true;
        expected = false;
    }
    SUBCASE("audience array")
    {
        claims["aud"] = Detail::Json::array({options.ClientId});
        expected = false;
    }
    SUBCASE("expired")
    {
        claims["iat"] = now - 301;
        claims["exp"] = now - 1;
        expected = false;
    }
    SUBCASE("future issuance")
    {
        claims["iat"] = now + 120;
        claims["exp"] = now + 420;
        expected = false;
    }
    SUBCASE("excess lifetime")
    {
        claims["exp"] = now + 3600;
        expected = false;
    }
    SUBCASE("negative time")
    {
        claims["iat"] = -1;
        expected = false;
    }
    SUBCASE("fractional time")
    {
        claims["iat"] = 1.5;
        expected = false;
    }
    SUBCASE("future authentication")
    {
        claims["auth_time"] = now + 1;
        expected = false;
    }
    SUBCASE("invalid subject")
    {
        claims["sub"] = "not-a-user";
        expected = false;
    }
    SUBCASE("invalid session")
    {
        claims["sid"] = "not-a-session";
        expected = false;
    }
    SUBCASE("invalid assurance")
    {
        claims["acr"] = "admin";
        expected = false;
    }
    SUBCASE("nonce substitution")
    {
        claims["nonce"] = std::string(43U, 'B');
        expected = false;
    }
    SUBCASE("access token substitution")
    {
        accessToken = std::string(43U, 'B');
        expected = false;
    }
    SUBCASE("missing access binding")
    {
        claims.erase("at_hash");
        expected = false;
    }
    SUBCASE("unknown key")
    {
        header["kid"] = "unknown";
        expected = false;
    }
    SUBCASE("duplicate matching key")
    {
        keys["keys"].push_back(key);
        expected = false;
    }
    SUBCASE("wrong key curve")
    {
        keys["keys"][0]["crv"] = "X25519";
        expected = false;
    }
    SUBCASE("private key material")
    {
        keys["keys"][0]["d"] = "forbidden";
        expected = false;
    }
    SUBCASE("bad key encoding")
    {
        keys["keys"][0]["x"] = "invalid";
        expected = false;
    }
    SUBCASE("redirected keys")
    {
        redirectedKeys = true;
        expected = false;
    }
    SUBCASE("key service unavailable")
    {
        badKeyStatus = true;
        expected = false;
    }
    SUBCASE("redirected token")
    {
        redirectedToken = true;
        expected = false;
    }

    const auto signedPart = EncodeJwtJson(header) + "." + EncodeJwtJson(claims);
    auto signature = UrlBase64(signer.SignBase64(Body(signedPart)));
    if (badSignature)
    {
        signature[0] = signature[0] == 'A' ? 'B' : 'A';
    }
    Detail::Json tokenResponse = {{"access_token", accessToken},
                                  {"refresh_token", "rotated-refresh-token"},
                                  {"id_token", missingIdentity ? "" : signedPart + "." + signature},
                                  {"token_type", "bearer"},
                                  {"expires_in", 900}};
    unsigned int keyRequests = 0;
    unsigned int tokenRequests = 0;
    auto client = DesktopOAuthClient::Create(
        options,
        [&](const NativeHttpRequest& request)
        {
            if (request.Url.ends_with("/.well-known/jwks.json"))
            {
                ++keyRequests;
                CHECK(request.Method == NativeHttpMethod::Get);
                CHECK(request.Body.empty());
                CHECK(request.Headers.size() == 1U);
                CHECK(tokenRequests == 0U);
                return HubResult<NativeHttpResponse>::Success(
                    {.StatusCode = static_cast<std::uint16_t>(badKeyStatus ? 503 : 200),
                     .EffectiveUrl = redirectedKeys ? "https://attacker.invalid/keys" : request.Url,
                     .Body = Body(duplicateKeyField ? "{\"keys\":[]," + keys.dump().substr(1U) : keys.dump())});
            }
            ++tokenRequests;
            CHECK(keyRequests == 1U);
            return HubResult<NativeHttpResponse>::Success(
                {.StatusCode = 200,
                 .EffectiveUrl = redirectedToken ? "https://attacker.invalid/token" : request.Url,
                 .Body =
                     Body(duplicateTokenField ? "{\"access_token\":\"substituted\"," + tokenResponse.dump().substr(1U)
                                              : tokenResponse.dump())});
        });
    REQUIRE(client);
    const auto authorization = client.Value().BeginAuthorization(DeterministicEntropy);
    REQUIRE(authorization);
    const auto result = refresh
                            ? client.Value().Refresh("original-refresh-token")
                            : client.Value().Exchange(authorization.Value(), {.Code = "single-use-authorization-code",
                                                                              .State = authorization.Value().State});
    CHECK(static_cast<bool>(result) == expected);
    CHECK(keyRequests == 1U);
    CHECK(tokenRequests == ((redirectedKeys || badKeyStatus || duplicateKeyField) ? 0U : 1U));
    if (badKeyStatus)
    {
        REQUIRE_FALSE(result);
        CHECK(result.Error().Retryable);
    }
}

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
