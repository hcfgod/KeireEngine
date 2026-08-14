#include "KeireHub/HubMarketplaceIntegration.h"

#include "Keire/Assets/AssetPackage.h"

#include "KeireHubRuntime/CatalogClient.h"
#include "KeireHubRuntime/DownloadManager.h"
#include "KeireHubRuntime/MarketplaceCache.h"
#include "KeireHubRuntime/MarketplaceClient.h"
#include "KeireHubRuntime/NativeHttpTransport.h"
#include "KeireHubRuntime/PackageResolver.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError MarketplaceError(std::string message, std::string details = {})
        {
            return {.Code = HubErrorCode::DownloadUnavailable,
                    .Message = std::move(message),
                    .Retryable = true,
                    .AffectedItem = "marketplace",
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] MarketplaceCacheItem& Upsert(MarketplaceCacheSnapshot& snapshot,
                                                   const MarketplaceProduct& product)
        {
            auto item = std::ranges::find(snapshot.Items, product.Id, &MarketplaceCacheItem::ProductId);
            if (item == snapshot.Items.end())
            {
                snapshot.Items.push_back({.ProductId = product.Id,
                                          .Slug = product.Slug,
                                          .DisplayName = product.DisplayName,
                                          .ShortDescription = product.ShortDescription,
                                          .PublisherName = product.Publisher.DisplayName,
                                          .CategoryName = product.CategoryName,
                                          .LicenseSpdx = product.LicenseSpdx,
                                          .State = MarketplaceCacheState::Unavailable});
                item = std::prev(snapshot.Items.end());
            }
            else
            {
                item->Slug = product.Slug;
                item->DisplayName = product.DisplayName;
                item->ShortDescription = product.ShortDescription;
                item->PublisherName = product.Publisher.DisplayName;
                item->CategoryName = product.CategoryName;
                item->LicenseSpdx = product.LicenseSpdx;
            }
            return *item;
        }

        [[nodiscard]] bool Includes(const std::vector<std::string>& values, const std::string_view value)
        {
            return values.empty() || std::ranges::find(values, value) != values.end();
        }

        [[nodiscard]] bool Compatible(const MarketplaceProductVersion& candidate, const SemanticVersion& engine,
                                      const std::string_view platform, const std::string_view architecture)
        {
            if (candidate.State != "published" || !Includes(candidate.Platforms, platform) ||
                !Includes(candidate.Architectures, architecture))
            {
                return false;
            }
            if (!candidate.MinimumEngineVersion.empty())
            {
                const auto minimum = SemanticVersion::Parse(candidate.MinimumEngineVersion);
                if (!minimum || engine < minimum.Value())
                    return false;
            }
            if (!candidate.MaximumEngineVersion.empty())
            {
                const auto maximum = SemanticVersion::Parse(candidate.MaximumEngineVersion);
                if (!maximum || engine > maximum.Value())
                    return false;
            }
            return static_cast<bool>(SemanticVersion::Parse(candidate.Version));
        }

        [[nodiscard]] const MarketplaceProductVersion& SelectVersion(const MarketplaceProductDetails& product,
                                                                     const HubMarketplaceRequest& request)
        {
            const auto engine = SemanticVersion::Parse(request.EngineVersion);
            if (!engine)
                throw std::runtime_error("The running Editor version is not a valid semantic version.");
            const MarketplaceProductVersion* selected = nullptr;
            std::optional<SemanticVersion> selectedVersion;
            for (const auto& candidate : product.Versions)
            {
                if (!Compatible(candidate, engine.Value(), request.Platform, request.Architecture))
                    continue;
                auto version = SemanticVersion::Parse(candidate.Version);
                if (!selectedVersion || version.Value() > *selectedVersion)
                {
                    selected = &candidate;
                    selectedVersion = std::move(version).Value();
                }
            }
            if (!selected)
                throw std::runtime_error("No published marketplace version is compatible with this Editor and host.");
            return *selected;
        }

        [[nodiscard]] std::string InstallKind(const Keire::AssetPackageInstallKind kind)
        {
            switch (kind)
            {
            case Keire::AssetPackageInstallKind::Registry:
                return "registry";
            case Keire::AssetPackageInstallKind::AssetImport:
                return "asset_import";
            case Keire::AssetPackageInstallKind::CompleteProject:
                return "complete_project";
            }
            return "invalid";
        }

        [[nodiscard]] HubStatus Synchronize(const HubMarketplaceRequest& request,
                                            const MarketplaceTransport& marketplaceTransport,
                                            const std::stop_token stop,
                                            const std::function<void(std::string)>& progress)
        {
            try
            {
                MarketplaceCacheStore cache(request.CacheRoot);
                auto loaded = cache.Load();
                if (!loaded)
                    return HubStatus::Failure(loaded.Error());
                auto snapshot = std::move(loaded).Value();
                if (snapshot.AccountId != request.AccountId)
                    snapshot = {};
                snapshot.AccountId = request.AccountId;
                for (auto& item : snapshot.Items)
                {
                    item.EntitlementId.clear();
                    item.Entitled = false;
                    item.State = MarketplaceCacheState::Unavailable;
                    item.FailureMessage.clear();
                }
                snapshot.RequestedProductId = request.ProductId;
                ++snapshot.Revision;
                if (const auto saved = cache.Save(snapshot); !saved)
                    return saved;

                auto native = NativeHttpTransport::Create(
                    {.CustomProxyUrl = request.CustomProxyUrl,
                     .AllowInsecureLoopbackDevelopment = request.AllowInsecureLoopbackDevelopment});
                if (!native)
                    return HubStatus::Failure(native.Error());
                auto transport = std::move(native).Value();
                auto client = MarketplaceClient::Create(
                    {.ServiceBaseUrl = request.ServiceBaseUrl},
                    marketplaceTransport ? marketplaceTransport
                                         : MarketplaceTransport([&transport](const NativeHttpRequest& http)
                                                                { return transport.Send(http); }));
                if (!client)
                    return HubStatus::Failure(client.Error());

                progress("Registering this Hub session…");
                auto session = client.Value().RegisterDeviceSession(request.AccessToken, "Kéire Hub");
                if (!session)
                    return HubStatus::Failure(session.Error());

                progress("Synchronizing the Kéire Marketplace catalog…");
                std::string cursor;
                for (std::size_t page = 0; page < 64U && !stop.stop_requested(); ++page)
                {
                    auto catalog = client.Value().Catalog({.Cursor = cursor, .Limit = 50});
                    if (!catalog)
                        return HubStatus::Failure(catalog.Error());
                    for (const auto& product : catalog.Value().Products)
                        static_cast<void>(Upsert(snapshot, product));
                    cursor = catalog.Value().NextCursor;
                    if (cursor.empty())
                        break;
                    if (page == 63U)
                        throw std::runtime_error("Marketplace catalog pagination exceeded its safety bound.");
                }

                progress("Synchronizing My Assets…");
                cursor.clear();
                bool entitled = false;
                std::optional<std::string> organizationId;
                for (std::size_t page = 0; page < 128U && !stop.stop_requested(); ++page)
                {
                    auto library = client.Value().Library(request.AccessToken, std::nullopt, cursor);
                    if (!library)
                        return HubStatus::Failure(library.Error());
                    for (const auto& libraryItem : library.Value().Items)
                    {
                        auto& item = Upsert(snapshot, libraryItem.Product);
                        item.EntitlementId = libraryItem.EntitlementId;
                        item.Entitled = true;
                        const auto cachedPackageReady = !item.PackageId.empty() && !item.Version.empty() &&
                                                        !item.VersionId.empty() && !item.ArchiveSha256.empty() &&
                                                        item.ArchiveSizeBytes != 0U && !item.SignedPublication.empty();
                        item.State =
                            cachedPackageReady ? MarketplaceCacheState::Ready : MarketplaceCacheState::Entitled;
                        if (libraryItem.ProductId == request.ProductId)
                        {
                            entitled = true;
                            organizationId = libraryItem.OrganizationId;
                        }
                    }
                    cursor = library.Value().NextCursor;
                    if (cursor.empty())
                        break;
                    if (page == 127U)
                        throw std::runtime_error("Marketplace library pagination exceeded its safety bound.");
                }
                if (stop.stop_requested())
                    return HubStatus::Failure(MarketplaceError("The marketplace request was cancelled."));
                ++snapshot.Revision;
                if (const auto saved = cache.Save(snapshot); !saved)
                    return saved;
                if (!entitled)
                    throw std::runtime_error("This asset is not in the signed-in account's library. Claim it first.");

                auto details = client.Value().Product(request.ProductId);
                if (!details)
                    return HubStatus::Failure(details.Error());
                auto& item = Upsert(snapshot, details.Value().Product);
                const auto& version = SelectVersion(details.Value(), request);
                item.VersionId = version.Id;
                item.Version = version.Version;
                item.InstallKind = version.InstallKind;
                item.ArchiveSha256.clear();
                item.ArchiveSizeBytes = 0;
                item.SignedPublication.clear();
                item.PackageId.clear();
                item.State = MarketplaceCacheState::Downloading;
                item.FailureMessage.clear();
                ++snapshot.Revision;
                if (const auto saved = cache.Save(snapshot); !saved)
                    return saved;

                progress("Authorizing a private marketplace download…");
                auto grant = client.Value().RequestDownload(
                    request.AccessToken,
                    {.VersionId = version.Id, .DeviceSessionId = session.Value().Id, .OrganizationId = organizationId});
                if (!grant)
                    return HubStatus::Failure(grant.Error());

                auto trust = CatalogTrustStore::Create(
                    {.TrustedPublicKeyDocuments = request.TrustedPublicKeyDocuments, .NativeLibraryPath = {}});
                if (!trust)
                    return HubStatus::Failure(trust.Error());
                auto publication = DecodeMarketplacePublication(grant.Value().SignedPublication);
                if (!publication)
                    return HubStatus::Failure(publication.Error());
                if (const auto verified = VerifyMarketplacePublication(publication.Value(), request.ProductId,
                                                                       version.Id, grant.Value().ArchiveSha256,
                                                                       grant.Value().ArchiveSizeBytes, trust.Value());
                    !verified)
                {
                    return verified;
                }

                progress("Downloading and verifying the marketplace package…");
                DownloadRequest download{.PackageId = request.ProductId,
                                         .Url = grant.Value().Url,
                                         .Sha256 = grant.Value().ArchiveSha256,
                                         .SizeBytes = grant.Value().ArchiveSizeBytes,
                                         .CacheRoot = request.CacheRoot,
                                         .AllowInsecureLoopbackDevelopment = request.AllowInsecureLoopbackDevelopment,
                                         .CustomProxyUrl = request.CustomProxyUrl,
                                         .BandwidthLimitBytesPerSecond = request.BandwidthLimitBytesPerSecond,
                                         .CacheKind = DownloadCacheKind::AssetPackage};
                DownloadManager manager;
                auto acquired = manager.Acquire(
                    download, transport,
                    {.Control = [stop]
                     { return stop.stop_requested() ? DownloadControl::Cancel : DownloadControl::Continue; }});
                if (!acquired || acquired.Value().Outcome != DownloadOutcome::Completed)
                {
                    return HubStatus::Failure(acquired ? MarketplaceError("The marketplace download was cancelled.")
                                                       : acquired.Error());
                }

                const auto metadata = Keire::InspectAssetPackageArchive(
                    acquired.Value().CachePath, {.RequireSignature = false,
                                                 .ExpectedArchiveSizeBytes = grant.Value().ArchiveSizeBytes,
                                                 .ExpectedArchiveSha256 = grant.Value().ArchiveSha256});
                if (metadata.Manifest.Version != version.Version ||
                    InstallKind(metadata.Manifest.InstallKind) != version.InstallKind)
                {
                    throw std::runtime_error("The signed package identity does not match the marketplace release.");
                }
                item.PackageId = metadata.Manifest.PackageId;
                item.ArchiveSha256 = grant.Value().ArchiveSha256;
                item.ArchiveSizeBytes = grant.Value().ArchiveSizeBytes;
                item.SignedPublication = grant.Value().SignedPublication;
                item.State = MarketplaceCacheState::Ready;
                item.FailureMessage.clear();
                ++snapshot.Revision;
                return cache.Save(snapshot);
            }
            catch (const std::exception& exception)
            {
                return HubStatus::Failure(
                    MarketplaceError("The marketplace asset could not be prepared.", exception.what()));
            }
        }
    } // namespace

    HubMarketplaceIntegration::HubMarketplaceIntegration() : HubMarketplaceIntegration(MarketplaceTransport{}) {}

    HubMarketplaceIntegration::HubMarketplaceIntegration(MarketplaceTransport marketplaceTransport)
        : m_Snapshot(std::make_shared<const HubMarketplaceSnapshot>()),
          m_MarketplaceTransport(std::move(marketplaceTransport))
    {
    }

    HubMarketplaceIntegration::~HubMarketplaceIntegration() { Stop(); }

    HubStatus HubMarketplaceIntegration::Request(HubMarketplaceRequest request)
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Snapshot->Running)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                           .Message = "Another marketplace asset is already being prepared.",
                                           .AffectedItem = "marketplace"});
            }
        }
        if (request.ProductId.empty() || request.AccountId.empty() || request.AccessToken.empty() ||
            request.ServiceBaseUrl.empty() || request.TrustedPublicKeyDocuments.empty() || request.CacheRoot.empty() ||
            !request.CacheRoot.is_absolute() || request.EngineVersion.empty() || request.Platform.empty() ||
            request.Architecture.empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The marketplace request is incomplete.",
                                       .AffectedItem = "marketplace"});
        }
        if (m_Worker.joinable())
            m_Worker.join();
        const auto completion = Snapshot()->Completion + 1U;
        Publish({.Running = true,
                 .Completion = completion,
                 .ProductId = request.ProductId,
                 .Message = "Preparing the marketplace asset…"});
        m_Worker = std::jthread(
            [this, request = std::move(request), marketplaceTransport = m_MarketplaceTransport,
             completion](const std::stop_token stop)
            {
                auto message = std::make_shared<std::string>("Preparing the marketplace asset…");
                const auto status = Synchronize(
                    request, marketplaceTransport, stop,
                    [this, completion, productId = request.ProductId, message](std::string value)
                    {
                        *message = std::move(value);
                        Publish(
                            {.Running = true, .Completion = completion, .ProductId = productId, .Message = *message});
                    });
                if (!status)
                {
                    MarketplaceCacheStore cache(request.CacheRoot);
                    auto loaded = cache.Load();
                    if (loaded)
                    {
                        auto snapshot = std::move(loaded).Value();
                        snapshot.RequestedProductId = request.ProductId;
                        auto item =
                            std::ranges::find(snapshot.Items, request.ProductId, &MarketplaceCacheItem::ProductId);
                        if (item == snapshot.Items.end())
                        {
                            snapshot.Items.push_back({.ProductId = request.ProductId,
                                                      .DisplayName = "Marketplace asset",
                                                      .State = MarketplaceCacheState::Failed});
                            item = std::prev(snapshot.Items.end());
                        }
                        item->State = MarketplaceCacheState::Failed;
                        item->FailureMessage = status.Error().TechnicalDetails.empty()
                                                   ? status.Error().Message
                                                   : status.Error().TechnicalDetails;
                        ++snapshot.Revision;
                        static_cast<void>(cache.Save(snapshot));
                    }
                    Publish({.Completion = completion,
                             .ProductId = request.ProductId,
                             .Message = status.Error().Message,
                             .Failure = status.Error()});
                    return;
                }
                Publish({.Completion = completion,
                         .ProductId = request.ProductId,
                         .Message = "The verified asset is ready in the Editor Package Manager."});
            });
        return HubStatus::Success();
    }

    void HubMarketplaceIntegration::Stop() noexcept
    {
        if (!m_Worker.joinable())
            return;
        m_Worker.request_stop();
        m_Worker.join();
    }

    std::shared_ptr<const HubMarketplaceSnapshot> HubMarketplaceIntegration::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    void HubMarketplaceIntegration::Publish(HubMarketplaceSnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = std::make_shared<const HubMarketplaceSnapshot>(std::move(snapshot));
    }
} // namespace KeireHub
