#include "KeireHubRuntime/MarketplaceBrokerProtocol.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

TEST_CASE("marketplace broker binds requests to a single nonce handshake without credential fields")
{
    const std::string clientNonce(43U, 'c');
    const std::string serverNonce(43U, 's');
    KeireHub::MarketplaceBrokerHandshake handshake(serverNonce);
    KeireHub::MarketplaceBrokerRequest hello{
        .Kind = KeireHub::MarketplaceBrokerRequestKind::Hello, .RequestId = "hello-1", .ClientNonce = clientNonce};
    const auto encodedHello = KeireHub::EncodeMarketplaceBrokerRequest(hello);
    REQUIRE(encodedHello);
    CHECK(encodedHello.Value().find("token") == std::string::npos);
    const auto decodedHello = KeireHub::DecodeMarketplaceBrokerRequest(encodedHello.Value());
    REQUIRE(decodedHello);
    const auto accepted = handshake.AcceptHello(decodedHello.Value());
    REQUIRE(accepted);
    CHECK(accepted.Value().SessionNonce.size() == 64U);
    CHECK_FALSE(handshake.AcceptHello(hello));

    KeireHub::MarketplaceBrokerRequest request{.Kind = KeireHub::MarketplaceBrokerRequestKind::LibrarySnapshot,
                                               .RequestId = "library-1",
                                               .SessionNonce = accepted.Value().SessionNonce,
                                               .ProductId = "product-filter"};
    CHECK(handshake.Authorize(request));
    request.SessionNonce[0] = request.SessionNonce[0] == 'a' ? 'b' : 'a';
    CHECK_FALSE(handshake.Authorize(request));
}

TEST_CASE("marketplace broker returns only verified cache identities")
{
    KeireHub::MarketplaceBrokerResponse response{
        .RequestId = "cache-1",
        .VerifiedCachePath = std::filesystem::absolute("MarketplaceCache/content/package.keireassetpackage"),
        .ArchiveSha256 = std::string(64U, 'a'),
        .ArchiveSizeBytes = 4096U,
        .DownloadState = "verified"};
    const auto encoded = KeireHub::EncodeMarketplaceBrokerResponse(response);
    REQUIRE(encoded);
    const auto decoded = KeireHub::DecodeMarketplaceBrokerResponse(encoded.Value());
    REQUIRE(decoded);
    CHECK(decoded.Value().VerifiedCachePath == response.VerifiedCachePath);
    CHECK(decoded.Value().ArchiveSha256 == response.ArchiveSha256);

    response.ArchiveSha256.clear();
    CHECK_FALSE(KeireHub::EncodeMarketplaceBrokerResponse(response));
}

TEST_CASE("marketplace broker endpoint is user-scoped and platform-specific")
{
    const auto endpoint = KeireHub::ResolveMarketplaceBrokerEndpoint(std::filesystem::absolute("MarketplaceBrokerData"),
                                                                     std::string(64U, 'b'));
    CHECK(endpoint.CurrentUserOnly);
#if defined(_WIN32)
    CHECK(endpoint.Transport == KeireHub::MarketplaceBrokerTransport::WindowsNamedPipe);
    CHECK(endpoint.Address.native().find(L"KeireHub.Marketplace") != std::wstring::npos);
#else
    CHECK(endpoint.Transport == KeireHub::MarketplaceBrokerTransport::UnixDomainSocket);
    CHECK(endpoint.Address.extension() == ".sock");
#endif
}
