#include "KeireHubRuntime/MarketplaceCache.h"

#include "KeireHubRuntime/DownloadManager.h"
#include "KeireHubRuntime/MarketplaceClient.h"

#include <KeireHubRuntimeInternal\Persistence.h>

#include <algorithm>
#include <exception>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumCacheBytes = std::size_t{4U} * 1024U * 1024U;
        constexpr std::size_t MaximumCacheItems = 2048U;

        [[nodiscard]] HubError CacheError(std::string message, std::string details = {})
        {
            return {.Code = HubErrorCode::CatalogCacheInvalid,
                    .Message = std::move(message),
                    .AffectedItem = "marketplace-cache",
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool IsUuid(const std::string_view value) noexcept
        {
            if (value.size() != 36U || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-')
                return false;
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                if (index == 8U || index == 13U || index == 18U || index == 23U)
                    continue;
                const auto character = value[index];
                if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                      (character >= 'A' && character <= 'F')))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::string_view StateName(const MarketplaceCacheState state) noexcept
        {
            switch (state)
            {
            case MarketplaceCacheState::Entitled:
                return "entitled";
            case MarketplaceCacheState::Downloading:
                return "downloading";
            case MarketplaceCacheState::Ready:
                return "ready";
            case MarketplaceCacheState::Failed:
                return "failed";
            case MarketplaceCacheState::Unavailable:
                return "unavailable";
            }
            return "invalid";
        }

        [[nodiscard]] MarketplaceCacheState ParseState(const std::string_view value)
        {
            if (value == "entitled")
                return MarketplaceCacheState::Entitled;
            if (value == "downloading")
                return MarketplaceCacheState::Downloading;
            if (value == "ready")
                return MarketplaceCacheState::Ready;
            if (value == "failed")
                return MarketplaceCacheState::Failed;
            if (value == "unavailable")
                return MarketplaceCacheState::Unavailable;
            throw std::invalid_argument("Marketplace cache state is invalid.");
        }

        [[nodiscard]] bool IsInstallKind(const std::string_view value) noexcept
        {
            return value.empty() || value == "registry" || value == "asset_import" || value == "complete_project";
        }

        void ValidateItem(const MarketplaceCacheItem& item, const bool requirePublication = true)
        {
            if (!IsUuid(item.ProductId) || (!item.EntitlementId.empty() && !IsUuid(item.EntitlementId)) ||
                (!item.VersionId.empty() && !IsUuid(item.VersionId)) || item.Slug.size() > 64U ||
                item.DisplayName.empty() || item.DisplayName.size() > 128U || item.ShortDescription.size() > 512U ||
                item.PublisherName.size() > 128U || item.CategoryName.size() > 128U || item.LicenseSpdx.size() > 64U ||
                item.PackageId.size() > 128U || item.Version.size() > 128U || !IsInstallKind(item.InstallKind) ||
                item.FailureMessage.size() > 4096U || item.SignedPublication.size() > 64U * 1024U ||
                StateName(item.State) == "invalid")
            {
                throw std::invalid_argument("Marketplace cache item fields are invalid.");
            }
            if (item.Entitled != !item.EntitlementId.empty())
                throw std::invalid_argument("Marketplace cache entitlement identity is inconsistent.");
            if (item.State == MarketplaceCacheState::Ready &&
                (item.PackageId.empty() || item.Version.empty() || item.VersionId.empty() ||
                 !Detail::IsSha256(item.ArchiveSha256) || item.ArchiveSizeBytes == 0U ||
                 (requirePublication && item.SignedPublication.empty())))
            {
                throw std::invalid_argument("Ready marketplace cache item is incomplete.");
            }
            if (!item.SignedPublication.empty() && !DecodeMarketplacePublication(item.SignedPublication))
                throw std::invalid_argument("Marketplace cache publication proof is invalid.");
            if (item.State != MarketplaceCacheState::Failed && !item.FailureMessage.empty())
                throw std::invalid_argument("Marketplace cache failure message is unexpected.");
        }

        [[nodiscard]] Detail::Json SerializeItem(const MarketplaceCacheItem& item)
        {
            ValidateItem(item);
            return {{"productId", item.ProductId},
                    {"entitlementId", item.EntitlementId},
                    {"versionId", item.VersionId},
                    {"slug", item.Slug},
                    {"displayName", item.DisplayName},
                    {"shortDescription", item.ShortDescription},
                    {"publisherName", item.PublisherName},
                    {"categoryName", item.CategoryName},
                    {"licenseSpdx", item.LicenseSpdx},
                    {"packageId", item.PackageId},
                    {"version", item.Version},
                    {"installKind", item.InstallKind},
                    {"archiveSha256", item.ArchiveSha256},
                    {"archiveSizeBytes", item.ArchiveSizeBytes},
                    {"signedPublication", item.SignedPublication},
                    {"state", StateName(item.State)},
                    {"failureMessage", item.FailureMessage},
                    {"entitled", item.Entitled}};
        }

        [[nodiscard]] MarketplaceCacheItem ParseItem(const Detail::Json& value, const std::uint32_t schemaVersion)
        {
            const auto expectedFields = schemaVersion == 1U ? 17U : 18U;
            if (!value.is_object() || value.size() != expectedFields)
                throw std::invalid_argument("Marketplace cache item schema is invalid.");
            MarketplaceCacheItem item{.ProductId = value.at("productId").get<std::string>(),
                                      .EntitlementId = value.at("entitlementId").get<std::string>(),
                                      .VersionId = value.at("versionId").get<std::string>(),
                                      .Slug = value.at("slug").get<std::string>(),
                                      .DisplayName = value.at("displayName").get<std::string>(),
                                      .ShortDescription = value.at("shortDescription").get<std::string>(),
                                      .PublisherName = value.at("publisherName").get<std::string>(),
                                      .CategoryName = value.at("categoryName").get<std::string>(),
                                      .LicenseSpdx = value.at("licenseSpdx").get<std::string>(),
                                      .PackageId = value.at("packageId").get<std::string>(),
                                      .Version = value.at("version").get<std::string>(),
                                      .InstallKind = value.at("installKind").get<std::string>(),
                                      .ArchiveSha256 = value.at("archiveSha256").get<std::string>(),
                                      .ArchiveSizeBytes = value.at("archiveSizeBytes").get<std::uint64_t>(),
                                      .SignedPublication = schemaVersion == 1U
                                                               ? std::string{}
                                                               : value.at("signedPublication").get<std::string>(),
                                      .State = ParseState(value.at("state").get<std::string>()),
                                      .FailureMessage = value.at("failureMessage").get<std::string>(),
                                      .Entitled = value.at("entitled").get<bool>()};
            ValidateItem(item, schemaVersion != 1U);
            if (schemaVersion == 1U && item.State == MarketplaceCacheState::Ready)
                item.State = MarketplaceCacheState::Entitled;
            return item;
        }
    } // namespace

    MarketplaceCacheStore::MarketplaceCacheStore(std::filesystem::path root) : m_Root(std::move(root))
    {
        if (m_Root.empty() || !m_Root.is_absolute())
            throw std::invalid_argument("Marketplace cache root must be absolute.");
        m_Root = m_Root.lexically_normal();
    }

    HubResult<MarketplaceCacheSnapshot> MarketplaceCacheStore::Load() const
    {
        std::error_code error;
        if (!std::filesystem::exists(IndexPath(), error))
        {
            if (error)
                return HubResult<MarketplaceCacheSnapshot>::Failure(CacheError("The marketplace cache is unreadable."));
            return HubResult<MarketplaceCacheSnapshot>::Success({});
        }
        auto document = Detail::ReadJsonFile(IndexPath(), MaximumCacheBytes);
        if (!document)
            return HubResult<MarketplaceCacheSnapshot>::Failure(document.Error());
        try
        {
            const auto& value = document.Value();
            if (!value.is_object() || value.size() != 4U)
            {
                throw std::invalid_argument("Marketplace cache schema is unsupported.");
            }
            const auto schemaVersion = value.at("schemaVersion").get<std::uint32_t>();
            if (schemaVersion != 1U && schemaVersion != MarketplaceCacheSnapshot::CurrentSchemaVersion)
                throw std::invalid_argument("Marketplace cache schema is unsupported.");
            MarketplaceCacheSnapshot result{.Revision = value.at("revision").get<std::uint64_t>(),
                                            .RequestedProductId = value.at("requestedProductId").get<std::string>()};
            if (!result.RequestedProductId.empty() && !IsUuid(result.RequestedProductId))
                throw std::invalid_argument("Marketplace cache request identity is invalid.");
            const auto& items = value.at("items");
            if (!items.is_array() || items.size() > MaximumCacheItems)
                throw std::invalid_argument("Marketplace cache item count is invalid.");
            std::set<std::string, std::less<>> productIds;
            for (const auto& item : items)
            {
                auto parsed = ParseItem(item, schemaVersion);
                if (!productIds.insert(parsed.ProductId).second)
                    throw std::invalid_argument("Marketplace cache product identities must be unique.");
                result.Items.push_back(std::move(parsed));
            }
            return HubResult<MarketplaceCacheSnapshot>::Success(std::move(result));
        }
        catch (const std::exception& exception)
        {
            return HubResult<MarketplaceCacheSnapshot>::Failure(
                CacheError("The marketplace cache is invalid.", exception.what()));
        }
    }

    HubStatus MarketplaceCacheStore::Save(const MarketplaceCacheSnapshot& snapshot) const
    {
        try
        {
            if (!snapshot.RequestedProductId.empty() && !IsUuid(snapshot.RequestedProductId))
                throw std::invalid_argument("Marketplace cache request identity is invalid.");
            if (snapshot.Items.size() > MaximumCacheItems)
                throw std::invalid_argument("Marketplace cache item count is invalid.");
            std::set<std::string, std::less<>> productIds;
            Detail::Json items = Detail::Json::array();
            for (const auto& item : snapshot.Items)
            {
                if (!productIds.insert(item.ProductId).second)
                    throw std::invalid_argument("Marketplace cache product identities must be unique.");
                items.push_back(SerializeItem(item));
            }
            return Detail::WriteJsonFileAtomically(IndexPath(),
                                                   {{"schemaVersion", MarketplaceCacheSnapshot::CurrentSchemaVersion},
                                                    {"revision", snapshot.Revision},
                                                    {"requestedProductId", snapshot.RequestedProductId},
                                                    {"items", std::move(items)}});
        }
        catch (const std::exception& exception)
        {
            return HubStatus::Failure(CacheError("The marketplace cache could not be saved.", exception.what()));
        }
    }

    std::filesystem::path MarketplaceCacheStore::ArchivePath(const MarketplaceCacheItem& item) const
    {
        ValidateItem(item);
        if (!Detail::IsSha256(item.ArchiveSha256) || item.ArchiveSizeBytes == 0U || item.PackageId.empty())
            throw std::invalid_argument("Marketplace cache archive identity is incomplete.");
        return DownloadManager::CachePath({.PackageId = item.PackageId,
                                           .Url = "https://cache.invalid/package",
                                           .Sha256 = item.ArchiveSha256,
                                           .SizeBytes = item.ArchiveSizeBytes,
                                           .CacheRoot = m_Root,
                                           .CacheKind = DownloadCacheKind::AssetPackage});
    }

    std::filesystem::path MarketplaceCacheStore::IndexPath() const { return m_Root / "marketplace-cache.json"; }
} // namespace KeireHub
