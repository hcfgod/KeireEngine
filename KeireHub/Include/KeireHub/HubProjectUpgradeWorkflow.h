#pragma once

#include "Keire/Project/ProjectUpgrade.h"

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace KeireHub
{
    enum class HubProjectUpgradeWorkflowState
    {
        Idle,
        Inspecting,
        Ready,
        Applying,
        Recovering,
        RollingBack,
        Completed,
        Failed
    };

    enum class HubProjectUpgradeCompletion
    {
        None,
        Refresh,
        Reopen
    };

    struct HubProjectUpgradePreparation final
    {
        bool Interrupted = false;
        std::optional<Keire::ProjectUpgradePlan> Plan;
    };

    struct HubProjectUpgradeWorkflowSnapshot final
    {
        HubProjectUpgradeWorkflowState State = HubProjectUpgradeWorkflowState::Idle;
        std::uint64_t Revision = 0;
        std::filesystem::path Root;
        bool Interrupted = false;
        std::optional<Keire::ProjectUpgradePlan> Plan;
        HubProjectUpgradeCompletion Completion = HubProjectUpgradeCompletion::None;
        std::optional<HubError> Failure;

        [[nodiscard]] bool IsActive() const noexcept;
    };

    struct HubProjectUpgradeWorkflowServices final
    {
        std::function<HubResult<HubProjectUpgradePreparation>(const std::filesystem::path&,
                                                              std::span<const Keire::ProjectUpgradeStep>)>
            Inspect;
        std::function<HubStatus(const std::filesystem::path&, std::span<const Keire::ProjectUpgradeStep>,
                                const Keire::ProjectUpgradePlan&)>
            Apply;
        std::function<HubStatus(const std::filesystem::path&, std::span<const Keire::ProjectUpgradeStep>)> Recover;
        std::function<HubStatus(const std::filesystem::path&, std::span<const Keire::ProjectUpgradeStep>)> Rollback;
    };

    [[nodiscard]] HubProjectUpgradeWorkflowServices CreateHubProjectUpgradeWorkflowServices();

    class HubProjectUpgradeWorkflow final
    {
      public:
        explicit HubProjectUpgradeWorkflow(HubProjectUpgradeWorkflowServices services);
        ~HubProjectUpgradeWorkflow();

        HubProjectUpgradeWorkflow(const HubProjectUpgradeWorkflow&) = delete;
        HubProjectUpgradeWorkflow& operator=(const HubProjectUpgradeWorkflow&) = delete;
        HubProjectUpgradeWorkflow(HubProjectUpgradeWorkflow&&) = delete;
        HubProjectUpgradeWorkflow& operator=(HubProjectUpgradeWorkflow&&) = delete;

        [[nodiscard]] HubStatus Start(std::filesystem::path root, std::span<const Keire::ProjectUpgradeStep> upgrades);
        [[nodiscard]] HubStatus Apply();
        [[nodiscard]] HubStatus Recover();
        [[nodiscard]] HubStatus Rollback();
        [[nodiscard]] HubStatus Retry();
        [[nodiscard]] HubStatus Dismiss();
        [[nodiscard]] std::shared_ptr<const HubProjectUpgradeWorkflowSnapshot> Snapshot() const;

      private:
        using Operation = std::function<HubStatus()>;

        [[nodiscard]] HubStatus RequireOwnerThread(std::string_view action) const;
        [[nodiscard]] HubStatus StartOperation(HubProjectUpgradeWorkflowState state,
                                               HubProjectUpgradeCompletion completion, Operation operation);
        [[nodiscard]] HubStatus StartInspection();
        void Publish(HubProjectUpgradeWorkflowSnapshot snapshot);
        void Stop() noexcept;

        HubProjectUpgradeWorkflowServices m_Services;
        std::filesystem::path m_Root;
        std::vector<Keire::ProjectUpgradeStep> m_Upgrades;
        std::thread::id m_OwnerThread;
        mutable std::mutex m_Mutex;
        std::shared_ptr<const HubProjectUpgradeWorkflowSnapshot> m_Snapshot;
        std::jthread m_Worker;
    };
} // namespace KeireHub
