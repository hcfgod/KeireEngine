#include "KeireHub/HubMarketplaceIntegration.h"

#include "KeireHubRuntime/MarketplaceCache.h"

#include <KeireHubTests/TestSupport.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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
                .EffectiveUrl = "https://keire.test/marketplace/v1/",
                .Headers = {{"Content-Type", "application/json"}},
                .Body = Body(body)};
    }
} // namespace

TEST_CASE("Marketplace synchronization registers the Hub session before reading My Assets")
{
    KeireHubTests::TemporaryDirectory temporary;
    std::mutex requestMutex;
    std::vector<std::string> requests;
    HubMarketplaceIntegration integration(
        [&](const NativeHttpRequest& request)
        {
            std::scoped_lock lock(requestMutex);
            requests.push_back(request.Url);
            if (request.Url.ends_with("/sessions/"))
            {
                return HubResult<NativeHttpResponse>::Success(JsonResponse(
                    201,
                    R"({"data":{"id":"30112233-4455-6677-8899-aabbccddeeff","sessionId":"oauth-session-001122334455","client":"hub"},"meta":{"apiVersion":"marketplace/v1","correlationId":"session-123"}})"));
            }
            if (request.Url.find("/catalog/") != std::string::npos)
            {
                return HubResult<NativeHttpResponse>::Success(JsonResponse(
                    200,
                    R"({"data":[],"page":{"nextCursor":"","limit":50},"meta":{"apiVersion":"marketplace/v1","correlationId":"catalog-123"}})"));
            }
            return HubResult<NativeHttpResponse>::Success(JsonResponse(
                200,
                R"({"data":[],"page":{"nextCursor":"","limit":24},"meta":{"apiVersion":"marketplace/v1","correlationId":"library-123"}})"));
        });

    REQUIRE(integration.Request({.ProductId = "00112233-4455-6677-8899-aabbccddeeff",
                                 .AccountId = "40112233-4455-6677-8899-aabbccddeeff",
                                 .AccessToken = "header.payload.signature",
                                 .ServiceBaseUrl = "https://keire.test",
                                 .TrustedPublicKeyDocuments = {"{}"},
                                 .CacheRoot = temporary.Path() / "MarketplacePackages",
                                 .EngineVersion = "0.3.1",
                                 .Platform = "windows",
                                 .Architecture = "x86_64"}));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (integration.Snapshot()->Running && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto completed = integration.Snapshot();
    INFO("Marketplace integration message: ", completed->Message);
    INFO("Marketplace integration failure: ", completed->Failure ? completed->Failure->TechnicalDetails : "");
    REQUIRE_FALSE(completed->Running);
    integration.Stop();

    REQUIRE(requests.size() == 3U);
    CHECK(requests[0].ends_with("/marketplace/v1/sessions/"));
    CHECK(requests[1].find("/marketplace/v1/catalog/") != std::string::npos);
    CHECK(requests[2].find("/marketplace/v1/library/") != std::string::npos);

    const MarketplaceCacheStore cache(temporary.Path() / "MarketplacePackages");
    const auto cached = cache.Load();
    REQUIRE(cached);
    CHECK(cached.Value().AccountId == "40112233-4455-6677-8899-aabbccddeeff");
}
