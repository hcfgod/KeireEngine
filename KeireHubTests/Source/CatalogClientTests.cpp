#include <KeireHubTests/TestSodium.h>
#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/CatalogClient.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace KeireHub;

namespace
{
    constexpr std::string_view ServiceUrl = "https://distribution.keire.test";
    constexpr std::string_view ValidExpiry = "2026-09-01T00:00:00Z";
    constexpr std::string_view Expired = "2026-08-06T11:59:59Z";

    [[nodiscard]] std::chrono::system_clock::time_point FixedNow()
    {
        return std::chrono::system_clock::time_point{std::chrono::seconds{1786017600}};
    }

    [[nodiscard]] std::string PackageCatalog(const KeireHubTests::TestSodiumSigner& signer,
                                             const std::uint64_t sequence, const std::string_view expiry = ValidExpiry,
                                             const std::string_view channel = "stable",
                                             const std::string_view platform = "windows",
                                             const std::string_view architecture = "x86_64",
                                             const std::string_view marker = "original")
    {
        return "{\n  \"schemaVersion\": 1,\n  \"keyId\": \"" + signer.KeyId() +
               "\",\n  \"sequence\": " + std::to_string(sequence) + " ,\n  \"expiresAt\": \"" + std::string(expiry) +
               "\",\n  \"channel\": \"" + std::string(channel) + "\",\n  \"platform\": \"" + std::string(platform) +
               "\",\n  \"architecture\": \"" + std::string(architecture) +
               "\",\n  \"packages\": [],\n  \"marker\": \"" + std::string(marker) + "\"\n}\n";
    }

    [[nodiscard]] std::string ContentCatalog(const KeireHubTests::TestSodiumSigner& signer,
                                             const std::uint64_t sequence, const std::string_view locale = "en-US",
                                             const std::string_view expiry = ValidExpiry)
    {
        return "{\n  \"schemaVersion\": 1,\n  \"keyId\": \"" + signer.KeyId() +
               "\",\n  \"sequence\": " + std::to_string(sequence) + " ,\n  \"expiresAt\": \"" + std::string(expiry) +
               "\",\n  \"locale\": \"" + std::string(locale) + "\",\n  \"learn\": [],\n  \"resources\": []\n}\n";
    }

    [[nodiscard]] CatalogHttpResponse Response(const KeireHubTests::TestSodiumSigner& signer,
                                               const std::string_view body, const std::uint64_t sequence,
                                               const std::string_view expiry = ValidExpiry,
                                               const std::string_view effectiveUrl = {})
    {
        const auto bytes = KeireHubTests::Bytes(body);
        const auto signature = signer.SignBase64(bytes);
        return {.StatusCode = 200,
                .EffectiveUrl = std::string(effectiveUrl),
                .Headers = {{"X-Keire-Signature-Algorithm", "Ed25519"},
                            {"X-Keire-Signature-Key-Id", signer.KeyId()},
                            {"X-Keire-Signature", signature},
                            {"X-Keire-Sequence", std::to_string(sequence)},
                            {"X-Keire-Expires", std::string(expiry)},
                            {"ETag", "\"sha256-" + signer.Sha256Hex(bytes) + "\""}},
                .Body = bytes};
    }

    [[nodiscard]] CatalogHttpResponse NotModified(const CatalogSignatureMetadata& signature, const std::string& etag,
                                                  const std::string_view effectiveUrl)
    {
        return {.StatusCode = 304,
                .EffectiveUrl = std::string(effectiveUrl),
                .Headers = {{"X-Keire-Signature-Algorithm", signature.Algorithm},
                            {"X-Keire-Signature-Key-Id", signature.KeyId},
                            {"X-Keire-Signature", signature.Signature},
                            {"X-Keire-Sequence", std::to_string(signature.Sequence)},
                            {"X-Keire-Expires", signature.ExpiresAt},
                            {"ETag", etag}}};
    }

    void ReplaceHeader(CatalogHttpResponse& response, const std::string_view name, std::string value)
    {
        for (auto& header : response.Headers)
        {
            if (header.Name == name)
            {
                header.Value = std::move(value);
                return;
            }
        }
        throw std::runtime_error("The response fixture header does not exist.");
    }

    [[nodiscard]] CatalogTrustStore Trust(const KeireHubTests::TestSodiumSigner& signer)
    {
        auto result = CatalogTrustStore::Create(
            {.TrustedPublicKeyDocuments = {signer.PublicKeyDocument()}, .NativeLibraryPath = signer.LibraryPath()});
        if (!result)
            throw std::runtime_error(result.Error().TechnicalDetails);
        return std::move(result).Value();
    }

    [[nodiscard]] CatalogClientOptions Options(const std::filesystem::path& cacheRoot, const bool offline = false)
    {
        return {.ServiceBaseUrl = std::string(ServiceUrl),
                .Platform = "windows",
                .Architecture = "x86_64",
                .CacheRoot = cacheRoot,
                .Offline = offline,
                .Clock = FixedNow};
    }

    [[nodiscard]] CatalogClient Client(const KeireHubTests::TestSodiumSigner& signer, CatalogClientOptions options,
                                       CatalogTransport transport = {})
    {
        auto result = CatalogClient::Create(std::move(options), Trust(signer), std::move(transport));
        if (!result)
            throw std::runtime_error(result.Error().TechnicalDetails);
        return std::move(result).Value();
    }

    [[nodiscard]] HubResult<CatalogHttpResponse> TransportFailure()
    {
        return HubResult<CatalogHttpResponse>::Failure({.Code = HubErrorCode::DownloadUnavailable,
                                                        .Message = "The scripted transport is unavailable.",
                                                        .TechnicalDetails = "fixture failure"});
    }
} // namespace

TEST_CASE("Catalog client verifies and preserves the exact signed bytes")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    const auto body = PackageCatalog(signer, 7);
    std::optional<CatalogHttpRequest> observed;
    auto client =
        Client(signer, Options(temporary.Path()),
               [&](const CatalogHttpRequest& request)
               {
                   observed = request;
                   return HubResult<CatalogHttpResponse>::Success(Response(signer, body, 7, ValidExpiry, request.Url));
               });

    const auto result = client.FetchPackageCatalog("stable");
    REQUIRE(result);
    REQUIRE(result.Value().ExactBytes);
    CHECK(KeireHubTests::Text(*result.Value().ExactBytes) == body);
    CHECK(result.Value().Signature.KeyId == signer.KeyId());
    CHECK(result.Value().Signature.Sequence == 7);
    CHECK_FALSE(result.Value().FromCache);
    CHECK(result.Value().NetworkValidated);
    REQUIRE(observed);
    CHECK(observed->Url == "https://distribution.keire.test/v2/catalog/stable/windows/x86_64");
    CHECK_FALSE(observed->IfNoneMatch);
    CHECK(observed->MaximumResponseBytes == 32U * 1024U * 1024U);
}

TEST_CASE("Catalog client distinguishes an unpublished endpoint from a transport failure")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    auto client =
        Client(signer, Options(temporary.Path()), [](const CatalogHttpRequest& request)
               { return HubResult<CatalogHttpResponse>::Success({.StatusCode = 404, .EffectiveUrl = request.Url}); });

    const auto result = client.FetchPackageCatalog("stable");
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::NotFound);
    CHECK_FALSE(result.Error().Retryable);
}

TEST_CASE("Catalog client rejects exact-byte tampering and untrusted signing keys")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner trustedSigner;
    KeireHubTests::TestSodiumSigner otherSigner(31);
    const auto original = PackageCatalog(trustedSigner, 2);
    auto tampered = Response(trustedSigner, original, 2);
    tampered.Body =
        KeireHubTests::Bytes(PackageCatalog(trustedSigner, 2, ValidExpiry, "stable", "windows", "x86_64", "tampered"));
    ReplaceHeader(tampered, "ETag", "\"sha256-" + trustedSigner.Sha256Hex(tampered.Body) + "\"");
    auto client = Client(trustedSigner, Options(temporary.Path()), [tampered](const CatalogHttpRequest&) mutable
                         { return HubResult<CatalogHttpResponse>::Success(std::move(tampered)); });
    const auto changedBytes = client.FetchPackageCatalog("stable");
    REQUIRE_FALSE(changedBytes);
    CHECK(changedBytes.Error().Code == HubErrorCode::CatalogSignatureInvalid);

    const auto untrustedBody = PackageCatalog(otherSigner, 3);
    auto untrustedClient = Client(trustedSigner, Options(temporary.Path() / "untrusted"),
                                  [&](const CatalogHttpRequest& request)
                                  {
                                      return HubResult<CatalogHttpResponse>::Success(
                                          Response(otherSigner, untrustedBody, 3, ValidExpiry, request.Url));
                                  });
    const auto wrongKey = untrustedClient.FetchPackageCatalog("stable");
    REQUIRE_FALSE(wrongKey);
    CHECK(wrongKey.Error().Code == HubErrorCode::CatalogUntrustedKey);

    auto malformedKey = trustedSigner.PublicKeyDocument();
    const auto fingerprint = malformedKey.find("\"fingerprint\":\"sha256:");
    REQUIRE(fingerprint != std::string::npos);
    malformedKey[malformedKey.size() - 2U] = malformedKey[malformedKey.size() - 2U] == '0' ? '1' : '0';
    const auto malformedTrust = CatalogTrustStore::Create(
        {.TrustedPublicKeyDocuments = {malformedKey}, .NativeLibraryPath = trustedSigner.LibraryPath()});
    REQUIRE_FALSE(malformedTrust);
    CHECK(malformedTrust.Error().Code == HubErrorCode::DistributionConfigurationInvalid);
}

TEST_CASE("Catalog client persists a sequence floor and rejects signed equivocation")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    auto next = std::make_shared<CatalogHttpResponse>(Response(signer, PackageCatalog(signer, 10), 10));
    auto client = Client(signer, Options(temporary.Path()),
                         [next](const CatalogHttpRequest& request)
                         {
                             auto response = *next;
                             response.EffectiveUrl = request.Url;
                             return HubResult<CatalogHttpResponse>::Success(std::move(response));
                         });

    const auto accepted = client.FetchPackageCatalog("stable");
    REQUIRE(accepted);
    const auto acceptedBytes = KeireHubTests::Text(*accepted.Value().ExactBytes);

    *next = Response(signer, PackageCatalog(signer, 9), 9);
    const auto downgrade = client.FetchPackageCatalog("stable");
    REQUIRE_FALSE(downgrade);
    CHECK(downgrade.Error().Code == HubErrorCode::CatalogReplay);

    *next =
        Response(signer, PackageCatalog(signer, 10, ValidExpiry, "stable", "windows", "x86_64", "equivocation"), 10);
    const auto equivocation = client.FetchPackageCatalog("stable");
    REQUIRE_FALSE(equivocation);
    CHECK(equivocation.Error().Code == HubErrorCode::CatalogReplay);

    auto offline = Client(signer, Options(temporary.Path(), true));
    const auto recovered = offline.FetchPackageCatalog("stable");
    REQUIRE(recovered);
    CHECK(recovered.Value().FromCache);
    CHECK_FALSE(recovered.Value().NetworkValidated);
    CHECK(KeireHubTests::Text(*recovered.Value().ExactBytes) == acceptedBytes);
    CHECK(recovered.Value().Signature.Sequence == 10);
}

TEST_CASE("Catalog trust validates expiry and detached identity metadata")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    auto next = std::make_shared<CatalogHttpResponse>(Response(signer, PackageCatalog(signer, 1, Expired), 1, Expired));
    auto client = Client(signer, Options(temporary.Path()),
                         [next](const CatalogHttpRequest& request)
                         {
                             auto response = *next;
                             response.EffectiveUrl = request.Url;
                             return HubResult<CatalogHttpResponse>::Success(std::move(response));
                         });

    const auto expired = client.FetchPackageCatalog("stable");
    REQUIRE_FALSE(expired);
    CHECK(expired.Error().Code == HubErrorCode::CatalogExpired);

    const auto body = PackageCatalog(signer, 2);
    *next = Response(signer, body, 2);
    ReplaceHeader(*next, "X-Keire-Expires", "2026-10-01T00:00:00Z");
    const auto detachedMismatch = client.FetchPackageCatalog("stable");
    REQUIRE_FALSE(detachedMismatch);
    CHECK(detachedMismatch.Error().Code == HubErrorCode::CatalogIdentityMismatch);

    auto minimumValidityOptions = Options(temporary.Path() / "minimum-validity");
    minimumValidityOptions.MinimumRemainingValidity = std::chrono::hours{24 * 30};
    auto minimumValidityClient = Client(
        signer, std::move(minimumValidityOptions), [&](const CatalogHttpRequest& request)
        { return HubResult<CatalogHttpResponse>::Success(Response(signer, body, 2, ValidExpiry, request.Url)); });
    const auto tooClose = minimumValidityClient.FetchPackageCatalog("stable");
    REQUIRE_FALSE(tooClose);
    CHECK(tooClose.Error().Code == HubErrorCode::CatalogExpired);
}

TEST_CASE("Catalog trust binds signed bytes to package and content endpoint identities")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    auto next = std::make_shared<CatalogHttpResponse>(
        Response(signer, PackageCatalog(signer, 1, ValidExpiry, "stable", "linux"), 1));
    auto client = Client(signer, Options(temporary.Path()),
                         [next](const CatalogHttpRequest& request)
                         {
                             auto response = *next;
                             response.EffectiveUrl = request.Url;
                             return HubResult<CatalogHttpResponse>::Success(std::move(response));
                         });

    auto result = client.FetchPackageCatalog("stable");
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::CatalogIdentityMismatch);

    *next = Response(signer, PackageCatalog(signer, 2, ValidExpiry, "preview"), 2);
    result = client.FetchPackageCatalog("stable");
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::CatalogIdentityMismatch);

    *next = Response(signer, PackageCatalog(signer, 3, ValidExpiry, "stable", "windows", "arm64"), 3);
    result = client.FetchPackageCatalog("stable");
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::CatalogIdentityMismatch);

    *next = Response(signer, ContentCatalog(signer, 4, "fr-FR"), 4);
    const auto content = client.FetchContentCatalog("en-US");
    REQUIRE_FALSE(content);
    CHECK(content.Error().Code == HubErrorCode::CatalogIdentityMismatch);
}

TEST_CASE("Catalog client uses validated last-known-good cache for offline and transient failures")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    const auto body = PackageCatalog(signer, 14);
    auto initial = Client(
        signer, Options(temporary.Path()), [&](const CatalogHttpRequest& request)
        { return HubResult<CatalogHttpResponse>::Success(Response(signer, body, 14, ValidExpiry, request.Url)); });
    const auto accepted = initial.FetchPackageCatalog("stable");
    REQUIRE(accepted);

    auto failedNetwork =
        Client(signer, Options(temporary.Path()), [](const CatalogHttpRequest&) { return TransportFailure(); });
    const auto fallback = failedNetwork.FetchPackageCatalog("stable");
    REQUIRE(fallback);
    CHECK(fallback.Value().FromCache);
    CHECK_FALSE(fallback.Value().NetworkValidated);
    CHECK(KeireHubTests::Text(*fallback.Value().ExactBytes) == body);

    bool offlineTransportCalled = false;
    auto offline = Client(signer, Options(temporary.Path(), true),
                          [&](const CatalogHttpRequest&)
                          {
                              offlineTransportCalled = true;
                              return TransportFailure();
                          });
    const auto offlineResult = offline.FetchPackageCatalog("stable");
    REQUIRE(offlineResult);
    CHECK(offlineResult.Value().FromCache);
    CHECK_FALSE(offlineResult.Value().NetworkValidated);
    CHECK_FALSE(offlineTransportCalled);

    auto notModified = Client(signer, Options(temporary.Path()),
                              [&](const CatalogHttpRequest& request)
                              {
                                  REQUIRE(request.IfNoneMatch);
                                  CHECK(*request.IfNoneMatch == accepted.Value().ETag);
                                  return HubResult<CatalogHttpResponse>::Success(
                                      NotModified(accepted.Value().Signature, accepted.Value().ETag, request.Url));
                              });
    const auto unchanged = notModified.FetchPackageCatalog("stable");
    REQUIRE(unchanged);
    CHECK(unchanged.Value().FromCache);
    CHECK(unchanged.Value().NetworkValidated);
}

TEST_CASE("Catalog cache never promotes malformed persisted data")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    const auto body = PackageCatalog(signer, 4);
    auto online = Client(
        signer, Options(temporary.Path()), [&](const CatalogHttpRequest& request)
        { return HubResult<CatalogHttpResponse>::Success(Response(signer, body, 4, ValidExpiry, request.Url)); });
    REQUIRE(online.FetchPackageCatalog("stable"));

    std::optional<std::filesystem::path> cacheFile;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(temporary.Path()))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            cacheFile = entry.path();
            break;
        }
    }
    REQUIRE(cacheFile);
    KeireHubTests::WriteText(*cacheFile, "{\"schemaVersion\":1,\"schemaVersion\":1}");

    auto offline = Client(signer, Options(temporary.Path(), true));
    const auto corrupted = offline.FetchPackageCatalog("stable");
    REQUIRE_FALSE(corrupted);
    CHECK(corrupted.Error().Code == HubErrorCode::CatalogCacheInvalid);
}

TEST_CASE("Catalog client requires HTTPS outside explicit loopback development")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    const auto transport = [](const CatalogHttpRequest&) { return TransportFailure(); };

    auto remote = Options(temporary.Path() / "remote");
    remote.ServiceBaseUrl = "http://distribution.keire.test";
    remote.AllowInsecureLoopbackDevelopment = true;
    const auto remoteResult = CatalogClient::Create(std::move(remote), Trust(signer), transport);
    REQUIRE_FALSE(remoteResult);
    CHECK(remoteResult.Error().Code == HubErrorCode::DistributionConfigurationInvalid);

    auto disabledLoopback = Options(temporary.Path() / "disabled");
    disabledLoopback.ServiceBaseUrl = "http://127.0.0.1:5123";
    const auto disabledResult = CatalogClient::Create(std::move(disabledLoopback), Trust(signer), transport);
    REQUIRE_FALSE(disabledResult);
    CHECK(disabledResult.Error().Code == HubErrorCode::DistributionConfigurationInvalid);

    auto enabledLoopback = Options(temporary.Path() / "enabled");
    enabledLoopback.ServiceBaseUrl = "http://localhost:5123/";
    enabledLoopback.AllowInsecureLoopbackDevelopment = true;
    auto enabledResult = CatalogClient::Create(std::move(enabledLoopback), Trust(signer), transport);
    REQUIRE(enabledResult);
    const auto unavailable = enabledResult.Value().FetchPackageCatalog("stable");
    REQUIRE_FALSE(unavailable);
    CHECK(unavailable.Error().Code == HubErrorCode::CatalogTransportFailed);
}

TEST_CASE("Catalog client accepts secure redirects and rejects HTTPS downgrades")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    const auto body = PackageCatalog(signer, 1);
    auto secureRedirect = Client(signer, Options(temporary.Path() / "secure"),
                                 [&](const CatalogHttpRequest&)
                                 {
                                     return HubResult<CatalogHttpResponse>::Success(Response(
                                         signer, body, 1, ValidExpiry, "https://cdn.keire.test/catalog/stable"));
                                 });
    REQUIRE(secureRedirect.FetchPackageCatalog("stable"));

    auto downgradeOptions = Options(temporary.Path() / "downgrade");
    downgradeOptions.AllowInsecureLoopbackDevelopment = true;
    auto downgrade = Client(signer, std::move(downgradeOptions),
                            [&](const CatalogHttpRequest&)
                            {
                                return HubResult<CatalogHttpResponse>::Success(
                                    Response(signer, body, 1, ValidExpiry, "http://127.0.0.1:5000/catalog"));
                            });
    const auto rejected = downgrade.FetchPackageCatalog("stable");
    REQUIRE_FALSE(rejected);
    CHECK(rejected.Error().Code == HubErrorCode::CatalogTransportFailed);
}
