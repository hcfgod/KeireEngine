#pragma once

#include "KeireHubRuntime/CatalogClient.h"
#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/NativeHttpTransport.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    struct MarketplacePublisher final
    {
        std::string Id;
        std::string Slug;
        std::string DisplayName;
        bool Verified = false;
    };

    struct MarketplaceProduct final
    {
        std::string Id;
        std::string Slug;
        std::string DisplayName;
        std::string ShortDescription;
        std::string CategorySlug;
        std::string CategoryName;
        std::string LicenseSpdx;
        std::string LicenseRevision;
        double RatingAverage = 0.0;
        std::uint64_t RatingCount = 0;
        bool Featured = false;
        MarketplacePublisher Publisher;
    };

    struct MarketplaceCatalogQuery final
    {
        std::string Search;
        std::string Category;
        std::string Cursor;
        std::uint32_t Limit = 24;
    };

    struct MarketplaceCatalogPage final
    {
        std::vector<MarketplaceProduct> Products;
        std::string NextCursor;
        std::uint32_t Limit = 24;
        std::string CorrelationId;
    };

    struct MarketplaceLibraryItem final
    {
        std::string EntitlementId;
        std::string ProductId;
        std::optional<std::string> OrganizationId;
        std::string GrantedAt;
        MarketplaceProduct Product;
    };

    struct MarketplaceLibraryPage final
    {
        std::vector<MarketplaceLibraryItem> Items;
        std::string NextCursor;
        std::uint32_t Limit = 24;
        std::string CorrelationId;
    };

    struct MarketplaceProductVersion final
    {
        std::string Id;
        std::string Version;
        std::string State;
        std::string InstallKind;
        std::string MinimumEngineVersion;
        std::string MaximumEngineVersion;
        std::vector<std::string> Platforms;
        std::vector<std::string> Architectures;
        std::vector<std::string> RendererCapabilities;
        std::string ManagedApiVersion;
        std::string ReleaseNotesMarkdown;
        std::string PublishedAt;
    };

    struct MarketplaceProductDetails final
    {
        MarketplaceProduct Product;
        std::vector<MarketplaceProductVersion> Versions;
        std::string CorrelationId;
    };

    struct MarketplaceClaimRequest final
    {
        std::string ProductId;
        std::optional<std::string> OrganizationId;
        std::string AcceptedLicenseSnapshot;
        std::string IdempotencyKey;
    };

    struct MarketplaceClaimResult final
    {
        std::string EntitlementId;
        std::optional<std::string> OrganizationId;
        std::string CorrelationId;
    };

    struct MarketplaceDeviceSession final
    {
        std::string Id;
        std::string OAuthSessionId;
        std::string Client;
        std::string CorrelationId;
    };

    struct MarketplaceDownloadRequest final
    {
        std::string VersionId;
        std::string DeviceSessionId;
        std::optional<std::string> OrganizationId;
    };

    struct MarketplaceDownloadGrant final
    {
        std::string GrantId;
        std::string Url;
        std::string ExpiresAt;
        std::string ArchiveSha256;
        std::uint64_t ArchiveSizeBytes = 0;
        std::string SignedPublication;
        std::string CorrelationId;
    };

    struct MarketplacePublication final
    {
        std::string Envelope;
        std::string Document;
        std::string ProductId;
        std::string VersionId;
        std::string ArtifactSha256;
        std::uint64_t ArtifactSizeBytes = 0;
        std::string ManifestSha256;
        std::string ReleaseStoragePath;
        std::string Algorithm;
        std::string KeyId;
        std::string Signature;
        std::uint64_t Sequence = 0;
        std::string ExpiresAt;
    };

    [[nodiscard]] HubResult<MarketplacePublication> DecodeMarketplacePublication(std::string_view envelope);
    [[nodiscard]] HubStatus
    VerifyMarketplacePublication(const MarketplacePublication& publication, std::string_view expectedProductId,
                                 std::string_view expectedVersionId, std::string_view expectedArchiveSha256,
                                 std::uint64_t expectedArchiveSizeBytes, const CatalogTrustStore& trust,
                                 std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

    struct MarketplaceClientOptions final
    {
        std::string ServiceBaseUrl;
        std::size_t MaximumResponseBytes = std::size_t{4U} * 1024U * 1024U;
    };

    using MarketplaceTransport = std::function<HubResult<NativeHttpResponse>(const NativeHttpRequest&)>;

    class MarketplaceClient final
    {
      public:
        [[nodiscard]] static HubResult<MarketplaceClient> Create(MarketplaceClientOptions options,
                                                                 MarketplaceTransport transport);

        [[nodiscard]] HubResult<MarketplaceCatalogPage> Catalog(const MarketplaceCatalogQuery& query = {}) const;
        [[nodiscard]] HubResult<MarketplaceProductDetails> Product(std::string_view productId) const;
        [[nodiscard]] HubResult<MarketplaceLibraryPage> Library(std::string_view accessToken,
                                                                std::optional<std::string_view> organizationId = {},
                                                                std::string_view cursor = {}) const;
        [[nodiscard]] HubResult<MarketplaceClaimResult> Claim(std::string_view accessToken,
                                                              const MarketplaceClaimRequest& request) const;
        [[nodiscard]] HubResult<MarketplaceDeviceSession> RegisterDeviceSession(std::string_view accessToken,
                                                                                std::string_view deviceName) const;
        [[nodiscard]] HubResult<MarketplaceDownloadGrant>
        RequestDownload(std::string_view accessToken, const MarketplaceDownloadRequest& request) const;

      private:
        MarketplaceClient(MarketplaceClientOptions options, MarketplaceTransport transport);

        MarketplaceClientOptions m_Options;
        MarketplaceTransport m_Transport;
    };
} // namespace KeireHub
