#pragma once

#include "KeireHub/HubAccountWorkflow.h"
#include "KeireHub/HubMarketplaceIntegration.h"

#include <cstdint>
#include <filesystem>

namespace KeireHub
{
    class HubDistributionWorkflow;
    class HubProductUi;

    class HubAccountIntegration final
    {
      public:
        [[nodiscard]] HubStatus Start(const std::filesystem::path& configurationPath,
                                      const std::filesystem::path& sessionPath, const HubSettings& settings);
        void Stop() noexcept;
        void RequestRefresh() noexcept;
        [[nodiscard]] HubStatus Tick(const HubSettings& settings, std::uint64_t nowUnixSeconds,
                                     const std::filesystem::path& executable,
                                     const HubDistributionWorkflow* distribution);
        void ApplySnapshot(HubProductSnapshot& product) const;
        void ApplyMarketplaceNotice(std::string& notice, bool& noticeError);
        [[nodiscard]] HubStatus Execute(const HubUiCommand& command);
        [[nodiscard]] HubResult<std::string> BeginBrowserSignIn();
        [[nodiscard]] HubStatus CompleteBrowserSignIn(std::string callbackUrl);
        [[nodiscard]] HubResult<std::string> AccessToken(std::uint64_t nowUnixSeconds) const;
        [[nodiscard]] HubStatus OpenMarketplaceProduct(std::string productId, HubProductUi& productUi,
                                                       const std::filesystem::path& executable,
                                                       const HubDistributionWorkflow* distribution,
                                                       const HubSettings& settings, std::uint64_t nowUnixSeconds);

      private:
        [[nodiscard]] HubStatus StartMarketplaceProduct(const std::filesystem::path& executable,
                                                        const HubDistributionWorkflow* distribution,
                                                        const HubSettings& settings, std::uint64_t nowUnixSeconds);

        HubAccountWorkflow m_Workflow;
        HubMarketplaceIntegration m_Marketplace;
        std::filesystem::path m_ConfigurationPath;
        std::filesystem::path m_SessionPath;
        std::string m_PendingMarketplaceProduct;
        std::uint64_t m_HandledMarketplaceCompletion = 0;
        bool m_RefreshPending = false;
    };
} // namespace KeireHub
