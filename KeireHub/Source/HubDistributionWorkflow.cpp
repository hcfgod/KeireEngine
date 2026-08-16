#include "KeireHub/HubDistributionWorkflow.h"

#include "KeireHubRuntime/HubUpdateCatalog.h"
#include "KeireHubRuntime/HubUpdateManager.h"

#include <algorithm>
#include <exception>
#include <ranges>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string_view DifficultyLabel(const ContentDifficulty value) noexcept
        {
            switch (value)
            {
            case ContentDifficulty::Beginner:
                return "Beginner";
            case ContentDifficulty::Intermediate:
                return "Intermediate";
            case ContentDifficulty::Advanced:
                return "Advanced";
            case ContentDifficulty::Reference:
                return "Reference";
            }
            return {};
        }

        [[nodiscard]] HubContentUiRecord ToRemoteContent(const HubContentItem& item)
        {
            return {.Id = item.Id,
                    .Title = item.Title,
                    .Summary = item.Summary,
                    .Category = item.Category,
                    .Difficulty = std::string(DifficultyLabel(item.Difficulty)),
                    .Url = item.HttpsUrl.value_or(""),
                    .Featured = item.Featured,
                    .Remote = true};
        }

        void AppendRemote(std::vector<HubContentUiRecord>& target, const std::vector<HubContentItem>& source)
        {
            std::erase_if(target, [](const auto& item) { return item.Remote; });
            for (const auto& item : source)
            {
                if (!item.HttpsUrl)
                    continue;
                if (std::ranges::find(target, item.Id, &HubContentUiRecord::Id) == target.end())
                    target.push_back(ToRemoteContent(item));
            }
        }
    } // namespace

    HubDistributionWorkflow::HubDistributionWorkflow()
        : m_Snapshot(std::make_shared<const HubDistributionWorkflowSnapshot>())
    {
    }

    HubDistributionWorkflow::~HubDistributionWorkflow() { Stop(); }

    HubStatus HubDistributionWorkflow::Start(const std::filesystem::path& configurationPath,
                                             const HubSettings& settings, const std::filesystem::path& hubExecutable)
    {
        Stop();
        Publish({.Refreshing = true});
        m_Worker = std::jthread(
            [this, configurationPath, settings, hubExecutable](const std::stop_token stopToken)
            {
                try
                {
                    auto configuration = LoadDistributionConfiguration(configurationPath);
                    if (!configuration)
                    {
                        Publish({.Failure = configuration.Error()});
                        return;
                    }
                    const bool developmentEndpoint =
                        settings.DevelopmentServiceUrl.has_value() && settings.DevelopmentTrustedKey.has_value();
                    const auto serviceBaseUrl =
                        developmentEndpoint ? *settings.DevelopmentServiceUrl : configuration.Value().ServiceBaseUrl;
                    auto session = DistributionCatalogSession::Create(
                        configuration.Value(), settings,
                        {.HubExecutable = hubExecutable,
                         .CatalogCacheRoot = settings.CacheRoot / "Catalogs",
                         .HostPlatform = std::string(HubUpdateManager::HostPlatformIdentity()),
                         .HostArchitecture = std::string(HubUpdateManager::HostArchitectureIdentity())});
                    if (!session)
                    {
                        Publish({.Failure = session.Error()});
                        return;
                    }
                    std::size_t consecutiveFailures = 0;
                    while (!stopToken.stop_requested())
                    {
                        Publish({.Refreshing = true,
                                 .Catalogs = session.Value().Snapshot(),
                                 .ServiceBaseUrl = serviceBaseUrl,
                                 .AllowInsecureLoopbackDevelopment = developmentEndpoint});
                        const auto refreshed = [&]() -> HubStatus
                        {
                            try
                            {
                                return session.Value().Refresh();
                            }
                            catch (const std::exception& error)
                            {
                                return HubStatus::Failure({.Code = HubErrorCode::CatalogTransportFailed,
                                                           .Message = "Distribution discovery failed unexpectedly.",
                                                           .Retryable = true,
                                                           .TechnicalDetails = error.what()});
                            }
                        }();
                        const auto catalogs = session.Value().Snapshot();
                        const auto failure =
                            refreshed ? std::optional<HubError>{} : std::optional<HubError>{refreshed.Error()};
                        Publish({.Catalogs = catalogs,
                                 .Failure = failure,
                                 .ServiceBaseUrl = serviceBaseUrl,
                                 .AllowInsecureLoopbackDevelopment = developmentEndpoint});

                        const bool needsNetworkRetry = HubDistributionNeedsNetworkRetry(*catalogs);
                        const bool refreshComplete = refreshed && !needsNetworkRetry;
                        consecutiveFailures = refreshComplete ? 0 : consecutiveFailures + 1;
                        const auto delay = CalculateHubDistributionRefreshDelay(
                            refreshComplete, needsNetworkRetry || (failure && failure->Retryable), consecutiveFailures);
                        std::unique_lock wakeLock(m_WakeMutex);
                        (void)m_Wake.wait_for(wakeLock, stopToken, delay, [] { return false; });
                    }
                }
                catch (const std::exception& error)
                {
                    Publish({.Failure = HubError{.Code = HubErrorCode::CatalogTransportFailed,
                                                 .Message = "Distribution discovery failed unexpectedly.",
                                                 .Retryable = true,
                                                 .TechnicalDetails = error.what()}});
                }
            });
        return HubStatus::Success();
    }

    void HubDistributionWorkflow::Stop() noexcept
    {
        if (!m_Worker.joinable())
            return;
        m_Worker.request_stop();
        m_Wake.notify_all();
        m_Worker.join();
    }

    std::shared_ptr<const HubDistributionWorkflowSnapshot> HubDistributionWorkflow::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    void HubDistributionWorkflow::Publish(HubDistributionWorkflowSnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = std::make_shared<const HubDistributionWorkflowSnapshot>(std::move(snapshot));
    }

    void ApplyHubDistributionSnapshot(const HubDistributionWorkflowSnapshot& distribution, HubProductSnapshot& product)
    {
        product.Online = false;
        product.Reconnecting = !product.Settings.OfflineMode &&
                               (distribution.Refreshing || (distribution.Failure && distribution.Failure->Retryable));
        product.CatalogAvailable = false;
        product.HubUpdate.reset();
        product.HubUpdateMessage.clear();
        if (!distribution.Catalogs)
            return;
        const auto& catalogs = *distribution.Catalogs;
        product.Reconnecting = product.Reconnecting || HubDistributionNeedsNetworkRetry(catalogs);
        const auto available = [](const DistributionCatalogSourceStatus& status)
        {
            return status.State == DistributionCatalogSourceState::Online ||
                   status.State == DistributionCatalogSourceState::LastKnownGood ||
                   status.State == DistributionCatalogSourceState::OfflineLastKnownGood;
        };
        product.Online =
            !catalogs.OfflineMode &&
            (std::ranges::any_of(catalogs.PackageCatalogs, [&](const auto& value)
                                 { return value.Status.State == DistributionCatalogSourceState::Online; }) ||
             catalogs.Content.Status.State == DistributionCatalogSourceState::Online);
        if (product.Online && !HubDistributionNeedsNetworkRetry(catalogs))
            product.Reconnecting = false;
        product.CatalogAvailable = std::ranges::any_of(catalogs.PackageCatalogs, [&](const auto& value)
                                                       { return value.Catalog && available(value.Status); }) ||
                                   (catalogs.Content.Catalog && available(catalogs.Content.Status));
        if (catalogs.Content.Catalog)
        {
            AppendRemote(product.Learn, catalogs.Content.Catalog->Learn);
            AppendRemote(product.Resources, catalogs.Content.Catalog->Resources);
        }
        // A last-known-good catalog remains useful for browsing cached content while offline, but it must not create
        // a potentially stale update prompt. Update discovery resumes only after the user returns to online mode and
        // the distribution session can apply its normal freshness/trust policy.
        if (product.Settings.CheckForUpdates && !catalogs.OfflineMode)
        {
            auto update = SelectHubUpdate(catalogs, product.HubVersion, HubUpdateManager::HostPackageFormatIdentity());
            if (!update)
                product.HubUpdateMessage = update.Error().Message;
            else if (update.Value())
            {
                product.HubUpdate = HubUpdateUiRecord{.PackageId = update.Value()->Package.Id,
                                                      .Version = update.Value()->Package.Version.ToString(),
                                                      .Channel = update.Value()->CatalogIdentity.Channel,
                                                      .Required = update.Value()->Required};
            }
        }
    }
} // namespace KeireHub
