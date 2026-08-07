#include "TestSodium.h"
#include "TestSupport.h"

#include "KeireHubRuntime/DistributionCatalog.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    constexpr std::string_view ServiceUrl = "https://distribution.keire.test";
    constexpr std::string_view Expiry = "2035-01-01T00:00:00Z";
    constexpr std::string_view ParserKeyId = "ed25519-00000000000000000000000000000000";

    [[nodiscard]] std::chrono::system_clock::time_point FixedNow()
    {
        return std::chrono::system_clock::time_point{std::chrono::seconds{1'893'456'000}};
    }

    [[nodiscard]] std::string Package(const std::string_view id = "keire.editor",
                                      const std::string_view version = "1.2.3",
                                      const std::string_view channel = "stable",
                                      const std::string_view platform = "windows",
                                      const std::string_view architecture = "x86_64", const std::string_view extra = {})
    {
        return "{\"schemaVersion\":1,\"packageId\":\"" + std::string(id) + "\",\"version\":\"" + std::string(version) +
               "\",\"type\":\"editor\",\"displayName\":\"Kéire Editor\",\"channel\":\"" + std::string(channel) +
               "\",\"platform\":\"" + std::string(platform) + "\",\"architecture\":\"" + std::string(architecture) +
               "\",\"artifact\":{\"sizeBytes\":3,\"sha256\":\"" + KeireHubTests::Digest('a') +
               "\"},\"installedSizeBytes\":3,\"files\":[{\"path\":\"bin/KeireEditor\",\"sizeBytes\":3,"
               "\"sha256\":\"" +
               KeireHubTests::Digest('b') + "\"}],\"signatureKeyId\":\"" + std::string(ParserKeyId) + "\"" +
               std::string(extra) + "}";
    }

    [[nodiscard]] std::string
    PackageCatalog(const std::string_view keyId, const std::uint64_t sequence, const std::vector<std::string>& packages,
                   const std::string_view channel = "stable", const std::string_view platform = "windows",
                   const std::string_view architecture = "x86_64", const std::string_view extra = {})
    {
        std::string values;
        for (std::size_t index = 0; index < packages.size(); ++index)
        {
            if (index != 0U)
                values += ',';
            values += packages[index];
        }
        return "{\"schemaVersion\":1,\"keyId\":\"" + std::string(keyId) +
               "\",\"sequence\":" + std::to_string(sequence) + ",\"expiresAt\":\"" + std::string(Expiry) +
               "\",\"channel\":\"" + std::string(channel) + "\",\"platform\":\"" + std::string(platform) +
               "\",\"architecture\":\"" + std::string(architecture) + "\",\"packages\":[" + values + "]" +
               std::string(extra) + "}";
    }

    [[nodiscard]] std::string ContentCatalog(const std::string_view keyId, const std::uint64_t sequence)
    {
        return "{\"schemaVersion\":1,\"keyId\":\"" + std::string(keyId) +
               "\",\"sequence\":" + std::to_string(sequence) + ",\"expiresAt\":\"" + std::string(Expiry) +
               "\",\"locale\":\"en-US\",\"learn\":[],\"resources\":[]}";
    }

    [[nodiscard]] DistributionPackageCatalogIdentity Identity(const std::uint64_t sequence = 7)
    {
        return {.KeyId = std::string(ParserKeyId),
                .Sequence = sequence,
                .ExpiresAt = std::string(Expiry),
                .Channel = "stable",
                .Platform = "windows",
                .Architecture = "x86_64"};
    }

    [[nodiscard]] HubResult<DistributionPackageCatalog>
    ParseCatalog(const std::string_view document, const DistributionPackageCatalogIdentity& identity = Identity())
    {
        const auto bytes = KeireHubTests::Bytes(document);
        return ParseDistributionPackageCatalog(bytes, identity);
    }

    [[nodiscard]] CatalogHttpResponse SignedResponse(const KeireHubTests::TestSodiumSigner& signer,
                                                     const CatalogHttpRequest& request, const std::string_view body,
                                                     const std::uint64_t sequence)
    {
        const auto bytes = KeireHubTests::Bytes(body);
        return {.StatusCode = 200,
                .EffectiveUrl = request.Url,
                .Headers = {{"X-Keire-Signature-Algorithm", "Ed25519"},
                            {"X-Keire-Signature-Key-Id", signer.KeyId()},
                            {"X-Keire-Signature", signer.SignBase64(bytes)},
                            {"X-Keire-Sequence", std::to_string(sequence)},
                            {"X-Keire-Expires", std::string(Expiry)},
                            {"ETag", "\"sha256-" + signer.Sha256Hex(bytes) + "\""}},
                .Body = bytes};
    }

    [[nodiscard]] DistributionConfiguration Configuration(const KeireHubTests::TestSodiumSigner& signer)
    {
        return {.OnlineDiscoveryEnabled = true,
                .ServiceBaseUrl = std::string(ServiceUrl),
                .TrustedPublicKeyDocuments = {signer.PublicKeyDocument()},
                .MinimumSequence = 1};
    }

    [[nodiscard]] HubSettings Settings(const std::filesystem::path& cacheRoot, const bool offline = false)
    {
        HubSettings settings;
        settings.CacheRoot = cacheRoot;
        settings.OfflineMode = offline;
        return settings;
    }

    [[nodiscard]] DistributionCatalogEnvironment Environment(const std::filesystem::path& root,
                                                             const KeireHubTests::TestSodiumSigner& signer,
                                                             CatalogTransport transport)
    {
        return {.HubExecutable = root / "bin" / "KeireHub.exe",
                .SignatureVerifierLibrary = signer.LibraryPath(),
                .HostPlatform = "windows",
                .HostArchitecture = "x86_64",
                .Locale = "en-US",
                .Clock = FixedNow,
                .Transport = std::move(transport)};
    }

    [[nodiscard]] CatalogTransport SuccessfulTransport(const KeireHubTests::TestSodiumSigner& signer)
    {
        const auto packages = PackageCatalog(signer.KeyId(), 7, {Package()});
        const auto content = ContentCatalog(signer.KeyId(), 8);
        return [&signer, packages, content](const CatalogHttpRequest& request)
        {
            if (request.Url.find("/v1/content/") != std::string::npos)
                return HubResult<CatalogHttpResponse>::Success(SignedResponse(signer, request, content, 8));
            return HubResult<CatalogHttpResponse>::Success(SignedResponse(signer, request, packages, 7));
        };
    }
} // namespace

TEST_CASE("Distribution package catalogs strictly parse bounded generic manifests")
{
    const auto document = PackageCatalog(
        ParserKeyId, 7, {Package("keire.template", "2.0.0", "stable", "any", "any"), Package("keire.editor", "1.2.3")});
    const auto parsed = ParseCatalog(document);
    REQUIRE(parsed);
    REQUIRE(parsed.Value().Packages.size() == 2U);
    CHECK(parsed.Value().Identity.Sequence == 7U);
    CHECK(parsed.Value().Packages[0].Id == "keire.editor");
    CHECK(parsed.Value().Packages[1].Id == "keire.template");

    const auto updatePolicy = ParseCatalog(
        PackageCatalog(ParserKeyId, 7, {}, "stable", "windows", "x86_64", ",\"minimumSupportedHubVersion\":\"1.4.0\""));
    REQUIRE(updatePolicy);
    REQUIRE(updatePolicy.Value().MinimumSupportedHubVersion);
    CHECK(updatePolicy.Value().MinimumSupportedHubVersion->ToString() == "1.4.0");

    const auto invalidUpdatePolicy = ParseCatalog(PackageCatalog(ParserKeyId, 7, {}, "stable", "windows", "x86_64",
                                                                 ",\"minimumSupportedHubVersion\":\"latest\""));
    REQUIRE_FALSE(invalidUpdatePolicy);
    CHECK(invalidUpdatePolicy.Error().Code == HubErrorCode::InvalidData);

    const auto unknownRoot = PackageCatalog(ParserKeyId, 7, {}, "stable", "windows", "x86_64", ",\"extra\":true");
    const auto unknown = ParseCatalog(unknownRoot);
    REQUIRE_FALSE(unknown);
    CHECK(unknown.Error().Code == HubErrorCode::InvalidData);

    auto duplicateProperty = document;
    duplicateProperty.insert(duplicateProperty.find("\"keyId\""), "\"schemaVersion\":1,");
    const auto duplicateJson = ParseCatalog(duplicateProperty);
    REQUIRE_FALSE(duplicateJson);
    CHECK(duplicateJson.Error().Code == HubErrorCode::InvalidData);

    const auto unknownManifest = PackageCatalog(
        ParserKeyId, 7, {Package("keire.editor", "1.2.3", "stable", "windows", "x86_64", ",\"unexpected\":true")});
    const auto unexpected = ParseCatalog(unknownManifest);
    REQUIRE_FALSE(unexpected);
    CHECK(unexpected.Error().Code == HubErrorCode::PackageManifestInvalid);
}

TEST_CASE("Distribution package catalogs reject excessive, duplicate, and incompatible entries")
{
    std::vector<std::string> tooMany(4097, "{}");
    const auto excessive = PackageCatalog(ParserKeyId, 7, tooMany);
    const auto bounded = ParseCatalog(excessive);
    REQUIRE_FALSE(bounded);
    CHECK(bounded.Error().Code == HubErrorCode::InvalidData);

    const auto duplicates = PackageCatalog(ParserKeyId, 7, {Package(), Package()});
    const auto duplicate = ParseCatalog(duplicates);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.Error().Code == HubErrorCode::DuplicateIdentifier);

    const auto wrongHost = PackageCatalog(ParserKeyId, 7, {Package("keire.editor", "1.2.3", "stable", "linux")});
    const auto incompatible = ParseCatalog(wrongHost);
    REQUIRE_FALSE(incompatible);
    CHECK(incompatible.Error().Code == HubErrorCode::PackageHostIncompatible);

    const auto wrongChannel = PackageCatalog(ParserKeyId, 7, {Package("keire.editor", "1.2.3", "preview")});
    const auto misplaced = ParseCatalog(wrongChannel);
    REQUIRE_FALSE(misplaced);
    CHECK(misplaced.Error().Code == HubErrorCode::PackageManifestInvalid);
}

TEST_CASE("Distribution catalog sessions remain inert when online discovery is disabled")
{
    HubSettings settings;
    settings.EnablePreReleaseChannel = true;
    settings.EnableNightlyChannel = true;
    bool transportCalled = false;
    DistributionCatalogEnvironment environment{.HostPlatform = "windows",
                                               .HostArchitecture = "x86_64",
                                               .Transport = [&](const CatalogHttpRequest&)
                                               {
                                                   transportCalled = true;
                                                   return HubResult<CatalogHttpResponse>::Failure({});
                                               }};
    auto session = DistributionCatalogSession::Create({}, settings, std::move(environment));
    REQUIRE(session);
    REQUIRE(session.Value().Refresh());
    const auto snapshot = session.Value().Snapshot();
    REQUIRE(snapshot);
    REQUIRE(snapshot->PackageCatalogs.size() == 3U);
    CHECK_FALSE(snapshot->OnlineDiscoveryEnabled);
    CHECK(snapshot->PackageCatalogs.front().Status.State == DistributionCatalogSourceState::OnlineDisabled);
    CHECK(snapshot->PackageCatalogs[1].Channel == "preview");
    CHECK(snapshot->PackageCatalogs[2].Channel == "nightly");
    CHECK(snapshot->Content.Status.State == DistributionCatalogSourceState::OnlineDisabled);
    CHECK_FALSE(transportCalled);
}

TEST_CASE("Distribution catalog sessions publish immutable online package and content snapshots")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    std::size_t requestCount = 0;
    auto transport = SuccessfulTransport(signer);
    auto countingTransport = [&](const CatalogHttpRequest& request)
    {
        ++requestCount;
        return transport(request);
    };
    auto session =
        DistributionCatalogSession::Create(Configuration(signer), Settings(temporary.Path() / "cache"),
                                           Environment(temporary.Path(), signer, std::move(countingTransport)));
    REQUIRE(session);
    REQUIRE(session.Value().Refresh());

    const auto snapshot = session.Value().Snapshot();
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*snapshot)>>);
    REQUIRE(snapshot);
    CHECK(snapshot->OnlineDiscoveryEnabled);
    CHECK_FALSE(snapshot->OfflineMode);
    REQUIRE(snapshot->PackageCatalogs.size() == 1U);
    const auto& packages = snapshot->PackageCatalogs.front();
    REQUIRE(packages.Catalog);
    CHECK(packages.Status.State == DistributionCatalogSourceState::Online);
    CHECK(packages.Status.Sequence == 7U);
    CHECK(packages.Catalog->Packages.front().Id == "keire.editor");
    REQUIRE(snapshot->Content.Catalog);
    CHECK(snapshot->Content.Status.State == DistributionCatalogSourceState::Online);
    CHECK(snapshot->Content.Status.Sequence == 8U);
    CHECK(snapshot->Content.Catalog->Locale == "en-US");
    CHECK(requestCount == 2U);
}

TEST_CASE("Distribution catalog sessions distinguish online fallback and offline last-known-good state")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    const auto cacheRoot = temporary.Path() / "cache";
    auto initial = DistributionCatalogSession::Create(
        Configuration(signer), Settings(cacheRoot), Environment(temporary.Path(), signer, SuccessfulTransport(signer)));
    REQUIRE(initial);
    REQUIRE(initial.Value().Refresh());

    auto unavailable = [](const CatalogHttpRequest&)
    {
        return HubResult<CatalogHttpResponse>::Failure(
            {.Code = HubErrorCode::DownloadUnavailable, .Message = "The scripted service is unavailable."});
    };
    auto fallback = DistributionCatalogSession::Create(Configuration(signer), Settings(cacheRoot),
                                                       Environment(temporary.Path(), signer, unavailable));
    REQUIRE(fallback);
    REQUIRE(fallback.Value().Refresh());
    const auto fallbackSnapshot = fallback.Value().Snapshot();
    CHECK(fallbackSnapshot->PackageCatalogs.front().Status.State == DistributionCatalogSourceState::LastKnownGood);
    CHECK(fallbackSnapshot->Content.Status.State == DistributionCatalogSourceState::LastKnownGood);

    bool offlineTransportCalled = false;
    auto offline =
        DistributionCatalogSession::Create(Configuration(signer), Settings(cacheRoot, true),
                                           Environment(temporary.Path(), signer,
                                                       [&](const CatalogHttpRequest&)
                                                       {
                                                           offlineTransportCalled = true;
                                                           return HubResult<CatalogHttpResponse>::Failure({});
                                                       }));
    REQUIRE(offline);
    REQUIRE(offline.Value().Refresh());
    const auto offlineSnapshot = offline.Value().Snapshot();
    CHECK(offlineSnapshot->PackageCatalogs.front().Status.State ==
          DistributionCatalogSourceState::OfflineLastKnownGood);
    CHECK(offlineSnapshot->Content.Status.State == DistributionCatalogSourceState::OfflineLastKnownGood);
    CHECK_FALSE(offlineTransportCalled);
}

TEST_CASE("Distribution catalog sessions publish typed unavailable status without an offline cache")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    auto session =
        DistributionCatalogSession::Create(Configuration(signer), Settings(temporary.Path() / "empty-cache", true),
                                           Environment(temporary.Path(), signer, {}));
    REQUIRE(session);
    const auto status = session.Value().Refresh();
    REQUIRE_FALSE(status);
    CHECK(status.Error().Code == HubErrorCode::CatalogTransportFailed);
    const auto snapshot = session.Value().Snapshot();
    CHECK(snapshot->PackageCatalogs.front().Status.State == DistributionCatalogSourceState::Unavailable);
    CHECK(snapshot->PackageCatalogs.front().Status.Error.has_value());
    CHECK(snapshot->Content.Status.State == DistributionCatalogSourceState::Unavailable);
    CHECK(snapshot->Content.Status.Error.has_value());
}
