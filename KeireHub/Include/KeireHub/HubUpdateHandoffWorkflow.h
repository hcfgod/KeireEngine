#pragma once

#include "KeireHubRuntime/HubUpdateManager.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace KeireHub
{
    enum class HubUpdateHandoffState
    {
        Idle,
        Verifying,
        Launched,
        Failed
    };

    struct HubUpdateHandoffSnapshot final
    {
        HubUpdateHandoffState State = HubUpdateHandoffState::Idle;
        std::uint64_t Revision = 0;
        std::optional<HubError> Failure;
    };

    class HubUpdateHandoffWorkflow final
    {
      public:
        HubUpdateHandoffWorkflow();
        ~HubUpdateHandoffWorkflow();

        HubUpdateHandoffWorkflow(const HubUpdateHandoffWorkflow&) = delete;
        HubUpdateHandoffWorkflow& operator=(const HubUpdateHandoffWorkflow&) = delete;

        [[nodiscard]] HubStatus Start(HubUpdateManager& manager, HubUpdateRequest request,
                                      HubUpdateManager::PlatformSignatureVerifier signatureVerifier,
                                      HubUpdateManager::InstallerLauncher launcher);
        void Stop() noexcept;
        [[nodiscard]] std::shared_ptr<const HubUpdateHandoffSnapshot> Snapshot() const;
        [[nodiscard]] std::optional<HubUpdateHandoffSnapshot> TakeCompletion();

      private:
        void Publish(HubUpdateHandoffState state, std::optional<HubError> failure = {});

        mutable std::mutex m_Mutex;
        std::shared_ptr<const HubUpdateHandoffSnapshot> m_Snapshot;
        std::uint64_t m_ConsumedRevision = 0;
        std::jthread m_Worker;
    };
} // namespace KeireHub
