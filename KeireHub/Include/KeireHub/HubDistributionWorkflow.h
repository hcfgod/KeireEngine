#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/DistributionCatalog.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace KeireHub
{
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
        std::shared_ptr<const HubDistributionWorkflowSnapshot> m_Snapshot;
        std::jthread m_Worker;
    };

    void ApplyHubDistributionSnapshot(const HubDistributionWorkflowSnapshot& distribution, HubProductSnapshot& product);
} // namespace KeireHub
