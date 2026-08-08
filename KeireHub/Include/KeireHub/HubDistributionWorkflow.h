#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/DistributionCatalog.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace KeireHub
{
    inline constexpr std::chrono::seconds HubDistributionHealthyRefreshInterval{300};
    inline constexpr std::chrono::seconds HubDistributionInitialRetryDelay{5};
    inline constexpr std::chrono::seconds HubDistributionMaximumRetryDelay{60};

    [[nodiscard]] constexpr std::chrono::seconds
    CalculateHubDistributionRefreshDelay(const bool succeeded, const bool retryable,
                                         const std::size_t consecutiveFailures) noexcept
    {
        if (succeeded || !retryable)
            return HubDistributionHealthyRefreshInterval;
        const auto exponent = std::min<std::size_t>(consecutiveFailures > 0 ? consecutiveFailures - 1 : 0, 4);
        return std::min(HubDistributionInitialRetryDelay * (1 << exponent), HubDistributionMaximumRetryDelay);
    }

    [[nodiscard]] inline bool HubDistributionSettingsChanged(const HubSettings& previous,
                                                             const HubSettings& next) noexcept
    {
        return previous.CacheRoot != next.CacheRoot || previous.EnableStableChannel != next.EnableStableChannel ||
               previous.EnablePreReleaseChannel != next.EnablePreReleaseChannel ||
               previous.EnableNightlyChannel != next.EnableNightlyChannel || previous.OfflineMode != next.OfflineMode ||
               previous.NetworkProxyMode != next.NetworkProxyMode || previous.CustomProxyUrl != next.CustomProxyUrl ||
               previous.DevelopmentServiceUrl != next.DevelopmentServiceUrl ||
               previous.DevelopmentTrustedKey != next.DevelopmentTrustedKey;
    }

    [[nodiscard]] inline bool HubDistributionNeedsNetworkRetry(const DistributionCatalogSnapshot& snapshot) noexcept
    {
        if (snapshot.OfflineMode || !snapshot.OnlineDiscoveryEnabled)
            return false;
        const auto retryable = [](const DistributionCatalogSourceStatus& status)
        {
            return status.State == DistributionCatalogSourceState::NotLoaded ||
                   status.State == DistributionCatalogSourceState::LastKnownGood ||
                   (status.State == DistributionCatalogSourceState::Unavailable &&
                    (!status.Error || status.Error->Retryable));
        };
        for (const auto& package : snapshot.PackageCatalogs)
        {
            if (retryable(package.Status))
                return true;
        }
        return retryable(snapshot.Content.Status);
    }

    struct HubDistributionWorkflowSnapshot final
    {
        bool Refreshing = false;
        std::shared_ptr<const DistributionCatalogSnapshot> Catalogs;
        std::optional<HubError> Failure;
        std::string ServiceBaseUrl;
        bool AllowInsecureLoopbackDevelopment = false;
    };

    class HubDistributionWorkflow final
    {
      public:
        HubDistributionWorkflow();
        ~HubDistributionWorkflow();

        HubDistributionWorkflow(const HubDistributionWorkflow&) = delete;
        HubDistributionWorkflow& operator=(const HubDistributionWorkflow&) = delete;

        [[nodiscard]] HubStatus Start(const std::filesystem::path& configurationPath, const HubSettings& settings,
                                      const std::filesystem::path& hubExecutable);
        void Stop() noexcept;
        [[nodiscard]] std::shared_ptr<const HubDistributionWorkflowSnapshot> Snapshot() const;

      private:
        void Publish(HubDistributionWorkflowSnapshot snapshot);

        mutable std::mutex m_Mutex;
        std::mutex m_WakeMutex;
        std::condition_variable_any m_Wake;
        std::shared_ptr<const HubDistributionWorkflowSnapshot> m_Snapshot;
        std::jthread m_Worker;
    };

    void ApplyHubDistributionSnapshot(const HubDistributionWorkflowSnapshot& distribution, HubProductSnapshot& product);
} // namespace KeireHub
