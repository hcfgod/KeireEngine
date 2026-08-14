#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/HubSettingsStore.h"
#include "KeireHubRuntime/MarketplaceClient.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace KeireHub
{
    struct HubMarketplaceRequest final
    {
        std::string ProductId;
        std::string AccountId;
        std::string AccessToken;
        std::string ServiceBaseUrl;
        std::string TrustedPublicKeyDocument;
        std::filesystem::path CacheRoot;
        std::string EngineVersion;
        std::string Platform;
        std::string Architecture;
        std::optional<std::string> CustomProxyUrl;
        std::uint64_t BandwidthLimitBytesPerSecond = 0;
        bool AllowInsecureLoopbackDevelopment = false;
    };

    struct HubMarketplaceSnapshot final
    {
        bool Running = false;
        std::uint64_t Completion = 0;
        std::string ProductId;
        std::string Message;
        std::optional<HubError> Failure;
    };

    class HubMarketplaceIntegration final
    {
      public:
        HubMarketplaceIntegration();
        explicit HubMarketplaceIntegration(MarketplaceTransport marketplaceTransport);
        ~HubMarketplaceIntegration();

        HubMarketplaceIntegration(const HubMarketplaceIntegration&) = delete;
        HubMarketplaceIntegration& operator=(const HubMarketplaceIntegration&) = delete;

        [[nodiscard]] HubStatus Request(HubMarketplaceRequest request);
        void Stop() noexcept;
        [[nodiscard]] std::shared_ptr<const HubMarketplaceSnapshot> Snapshot() const;

      private:
        void Publish(HubMarketplaceSnapshot snapshot);

        mutable std::mutex m_Mutex;
        std::shared_ptr<const HubMarketplaceSnapshot> m_Snapshot;
        MarketplaceTransport m_MarketplaceTransport;
        std::jthread m_Worker;
    };
} // namespace KeireHub
