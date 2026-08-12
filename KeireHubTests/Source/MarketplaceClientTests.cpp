#include "KeireHubRuntime/MarketplaceClient.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace KeireHub;

namespace
{
    [[nodiscard]] std::vector<std::byte> Body(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        return {bytes.begin(), bytes.end()};
    }

    [[nodiscard]] NativeHttpResponse JsonResponse(const std::uint16_t status, const std::string_view body)
    {
        return {.StatusCode = status,
                .EffectiveUrl = "https://keire.test/marketplace/v1/catalog/",
                .Headers = {{"Content-Type", "application/json"}, {"X-Correlation-Id", "request-123"}},
                .Body = Body(body)};
    }

    [[nodiscard]] std::optional<std::string> Header(const NativeHttpRequest& request, const std::string_view name)
    {
        for (const auto& header : request.Headers)
        {
            if (header.Name == name)
                return header.Value;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string ProductJson()
    {
        return R"({"id":"00112233-4455-6677-8899-aabbccddeeff","slug":"sandbox-content","display_name":"Kéire Sandbox Content Pack","short_description":"Production-ready sample assets for Kéire projects.","category_slug":"samples","category_name":"Samples","license_spdx":"MIT","license_revision":"1","featured":true,"rating_average":4.75,"rating_count":8,"publisher_id":"10112233-4455-6677-8899-aabbccddeeff","publisher_slug":"keire","publisher_name":"Kéire","publisher_verified":true})";
    }
} // namespace

TEST_CASE("Marketplace catalog requests are bounded and parse the versioned API envelope")
{
    NativeHttpRequest captured;
    auto client = MarketplaceClient::Create(
        {.ServiceBaseUrl = "https://keire.test"},
        [&](const NativeHttpRequest& request)
        {
            captured = request;
            return HubResult<NativeHttpResponse>::Success(
                JsonResponse(200, "{\"data\":[" + ProductJson() +
                                      "],\"page\":{\"nextCursor\":\"next-page\",\"limit\":12},\"meta\":{\"apiVersion\":"
                                      "\"marketplace/v1\",\"correlationId\":\"request-123\"}}"));
        });
    REQUIRE(client);

    const auto catalog = client.Value().Catalog({.Search = "shader graph", .Category = "samples", .Limit = 12});
    REQUIRE(catalog);
    REQUIRE(catalog.Value().Products.size() == 1U);
    CHECK(catalog.Value().Products.front().DisplayName == "Kéire Sandbox Content Pack");
    CHECK(catalog.Value().Products.front().Publisher.Verified);
    CHECK(catalog.Value().NextCursor == "next-page");
    CHECK(catalog.Value().CorrelationId == "request-123");
    CHECK(captured.Method == NativeHttpMethod::Get);
    CHECK(captured.Url == "https://keire.test/marketplace/v1/catalog/?limit=12&q=shader%20graph&category=samples");
    CHECK_FALSE(Header(captured, "Authorization"));

    const auto invalid = client.Value().Catalog({.Limit = 51});
    REQUIRE_FALSE(invalid);
    CHECK(invalid.Error().Code == HubErrorCode::InvalidArgument);
}

TEST_CASE("Marketplace claims carry an independent Hub bearer token and idempotency key")
{
    NativeHttpRequest captured;
    auto client = MarketplaceClient::Create(
        {.ServiceBaseUrl = "https://keire.test"},
        [&](const NativeHttpRequest& request)
        {
            captured = request;
            return HubResult<NativeHttpResponse>::Success(JsonResponse(
                201,
                R"({"data":{"entitlementId":"20112233-4455-6677-8899-aabbccddeeff","ownership":"personal","organizationId":null},"meta":{"apiVersion":"marketplace/v1","correlationId":"claim-123"}})"));
        });
    REQUIRE(client);
    const auto claim =
        client.Value().Claim("header.payload.signature", {.ProductId = "00112233-4455-6677-8899-aabbccddeeff",
                                                          .AcceptedLicenseSnapshot = "MIT license revision 1",
                                                          .IdempotencyKey = "request-0011223344556677"});
    REQUIRE(claim);
    CHECK(claim.Value().EntitlementId == "20112233-4455-6677-8899-aabbccddeeff");
    REQUIRE(Header(captured, "Authorization"));
    CHECK(*Header(captured, "Authorization") == "Bearer header.payload.signature");
    REQUIRE(Header(captured, "Idempotency-Key"));
    CHECK(*Header(captured, "Idempotency-Key") == "request-0011223344556677");
    const std::string requestBody(reinterpret_cast<const char*>(captured.Body.data()), captured.Body.size());
    CHECK(requestBody.find("personal") != std::string::npos);
    CHECK(requestBody.find("MIT license revision 1") != std::string::npos);
}

TEST_CASE("Marketplace error envelopes preserve stable codes and correlation references")
{
    auto client = MarketplaceClient::Create(
        {.ServiceBaseUrl = "https://keire.test"},
        [](const NativeHttpRequest&)
        {
            return HubResult<NativeHttpResponse>::Success(JsonResponse(
                503,
                R"({"error":{"code":"marketplace.disabled","message":"The marketplace is disabled.","correlationId":"failure-123"}})"));
        });
    REQUIRE(client);
    const auto result = client.Value().Catalog();
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::CatalogTransportFailed);
    CHECK(result.Error().Retryable);
    CHECK(result.Error().TechnicalDetails == "marketplace.disabled");
    CHECK(result.Error().LogReference == "failure-123");
}

TEST_CASE("Marketplace device registration and package grant keep tokens in Hub")
{
    std::vector<NativeHttpRequest> captured;
    auto client = MarketplaceClient::Create(
        {.ServiceBaseUrl = "https://keire.test"},
        [&](const NativeHttpRequest& request)
        {
            captured.push_back(request);
            if (request.Url.ends_with("/sessions/"))
            {
                return HubResult<NativeHttpResponse>::Success(JsonResponse(
                    201,
                    R"({"data":{"id":"30112233-4455-6677-8899-aabbccddeeff","sessionId":"oauth-session-001122334455","client":"hub"},"meta":{"apiVersion":"marketplace/v1","correlationId":"session-123"}})"));
            }
            return HubResult<NativeHttpResponse>::Success(JsonResponse(
                201,
                R"({"data":{"grantId":"40112233-4455-6677-8899-aabbccddeeff","url":"https://storage.keire.test/object/sign/marketplace-releases/package?token=signed","expiresAt":"2026-08-12T07:00:00Z","archiveSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","archiveSizeBytes":4096},"meta":{"apiVersion":"marketplace/v1","correlationId":"download-123"}})"));
        });
    REQUIRE(client);
    const auto session = client.Value().RegisterDeviceSession("header.payload.signature", "Studio workstation");
    REQUIRE(session);
    CHECK(session.Value().Client == "hub");
    const auto download =
        client.Value().RequestDownload("header.payload.signature", {.VersionId = "50112233-4455-6677-8899-aabbccddeeff",
                                                                    .DeviceSessionId = session.Value().Id});
    REQUIRE(download);
    CHECK(download.Value().ArchiveSizeBytes == 4096U);
    CHECK(download.Value().ArchiveSha256 == std::string(64U, 'a'));
    REQUIRE(captured.size() == 2U);
    CHECK(captured[0].Url.ends_with("/marketplace/v1/sessions/"));
    CHECK(captured[1].Url.ends_with("/marketplace/v1/downloads/"));
    CHECK(Header(captured[0], "Authorization") == Header(captured[1], "Authorization"));
    const std::string body(reinterpret_cast<const char*>(captured[1].Body.data()), captured[1].Body.size());
    CHECK(body.find("header.payload.signature") == std::string::npos);
}
