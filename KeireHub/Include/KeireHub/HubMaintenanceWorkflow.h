#pragma once

#include "KeireHubRuntime/HubMaintenance.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>

namespace KeireHub
{
    struct HubProductSnapshot;

    enum class HubMaintenanceState
    {
        Idle,
        Running,
        Completed,
        Failed
    };

    struct HubMaintenanceSnapshot final
    {
        std::uint64_t OperationId = 0;
        HubMaintenanceState State = HubMaintenanceState::Idle;
        std::optional<HubError> Failure;

        [[nodiscard]] bool IsRunning() const noexcept { return State == HubMaintenanceState::Running; }
        [[nodiscard]] bool IsTerminal() const noexcept
        {
            return State == HubMaintenanceState::Completed || State == HubMaintenanceState::Failed;
        }
    };

    struct HubMaintenanceServices final
    {
        std::function<HubStatus(const std::filesystem::path&)> ClearVerifiedCache;
    };

    class HubMaintenanceWorkflow final
    {
      public:
        HubMaintenanceWorkflow();
        explicit HubMaintenanceWorkflow(HubMaintenanceServices services);
        ~HubMaintenanceWorkflow();

        HubMaintenanceWorkflow(const HubMaintenanceWorkflow&) = delete;
        HubMaintenanceWorkflow& operator=(const HubMaintenanceWorkflow&) = delete;

        [[nodiscard]] HubResult<std::uint64_t> StartClearVerifiedCache(std::span<const HubTask> tasks,
                                                                       std::filesystem::path cacheRoot);
        [[nodiscard]] HubResult<bool> Poll();
        [[nodiscard]] std::shared_ptr<const HubMaintenanceSnapshot> Snapshot() const;
        [[nodiscard]] std::optional<HubMaintenanceSnapshot> TakeCompletion();

      private:
        [[nodiscard]] HubStatus RequireOwnerThread(std::string_view action) const;
        void Publish(HubMaintenanceSnapshot snapshot);
        void JoinWorker() noexcept;

        HubMaintenanceServices m_Services;
        std::future<HubStatus> m_Future;
        std::thread::id m_OwnerThread;
        std::uint64_t m_NextOperationId = 1;
        std::uint64_t m_ConsumedOperationId = 0;
        mutable std::mutex m_Mutex;
        std::shared_ptr<const HubMaintenanceSnapshot> m_Snapshot;
    };

    void ApplyHubMaintenanceSnapshot(const HubMaintenanceSnapshot& maintenance, HubProductSnapshot& product);
} // namespace KeireHub
