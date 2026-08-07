#pragma once

#include "KeireHubRuntime/BuildSupportPlanning.h"

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

namespace KeireHub
{
    enum class HubBuildSupportInventoryState
    {
        Idle,
        Loading,
        Ready,
        Failed
    };

    struct HubBuildSupportInventorySnapshot final
    {
        std::uint64_t Revision = 0;
        HubBuildSupportInventoryState State = HubBuildSupportInventoryState::Idle;
        std::shared_ptr<const std::vector<BuildSupportComponent>> Components;
        std::optional<HubError> Failure;

        [[nodiscard]] bool IsLoading() const noexcept { return State == HubBuildSupportInventoryState::Loading; }
    };

    struct HubBuildSupportInventoryServices final
    {
        std::function<HubResult<std::vector<BuildSupportComponent>>()> Load;
    };

    [[nodiscard]] HubBuildSupportInventoryServices CreateHubBuildSupportInventoryServices();

    class HubBuildSupportInventoryWorkflow final
    {
      public:
        explicit HubBuildSupportInventoryWorkflow(HubBuildSupportInventoryServices services);
        ~HubBuildSupportInventoryWorkflow();

        HubBuildSupportInventoryWorkflow(const HubBuildSupportInventoryWorkflow&) = delete;
        HubBuildSupportInventoryWorkflow& operator=(const HubBuildSupportInventoryWorkflow&) = delete;

        [[nodiscard]] HubStatus Start();
        // Unlike Start(), requests made while a load is active coalesce into one fresh follow-up load.
        [[nodiscard]] HubStatus RequestRefresh();
        [[nodiscard]] HubResult<bool> Poll();
        [[nodiscard]] std::shared_ptr<const HubBuildSupportInventorySnapshot> Snapshot() const;
        void Stop() noexcept;

      private:
        using LoadResult = HubResult<std::vector<BuildSupportComponent>>;

        [[nodiscard]] HubStatus RequireOwnerThread(std::string_view action) const;
        void Publish(HubBuildSupportInventorySnapshot snapshot);

        HubBuildSupportInventoryServices m_Services;
        std::future<LoadResult> m_Future;
        std::thread::id m_OwnerThread;
        mutable std::mutex m_Mutex;
        std::shared_ptr<const HubBuildSupportInventorySnapshot> m_Snapshot;
        bool m_RefreshPending = false;
    };
} // namespace KeireHub
