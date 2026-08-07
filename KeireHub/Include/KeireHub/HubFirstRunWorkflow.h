#pragma once

#include "KeireHub/HubFirstRunImportPreparation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace KeireHub
{
    enum class HubFirstRunWorkflowState
    {
        Idle,
        Running,
        Completed,
        Cancelled,
        Failed
    };

    struct HubFirstRunWorkflowSnapshot final
    {
        HubFirstRunWorkflowState State = HubFirstRunWorkflowState::Idle;
        std::size_t EntriesVisited = 0;
        std::size_t ProjectsFound = 0;
        std::size_t EditorsFound = 0;
        std::string Message;
        std::shared_ptr<const HubFirstRunDiscoverySnapshot> Discovery;
        std::shared_ptr<const HubFirstRunPreparedImport> PreparedImport;
    };

    class HubFirstRunWorkflow final
    {
      public:
        explicit HubFirstRunWorkflow(HubFirstRunImportPreparationHooks preparationHooks = {});
        ~HubFirstRunWorkflow();

        HubFirstRunWorkflow(const HubFirstRunWorkflow&) = delete;
        HubFirstRunWorkflow& operator=(const HubFirstRunWorkflow&) = delete;
        HubFirstRunWorkflow(HubFirstRunWorkflow&&) = delete;
        HubFirstRunWorkflow& operator=(HubFirstRunWorkflow&&) = delete;

        [[nodiscard]] HubStatus Start(HubFirstRunDiscoveryRequest request, std::uint64_t nowUnixSeconds);
        void Cancel() noexcept;
        [[nodiscard]] std::shared_ptr<const HubFirstRunWorkflowSnapshot> Snapshot() const;

      private:
        void Publish(HubFirstRunWorkflowSnapshot snapshot);

        mutable std::mutex m_Mutex;
        HubFirstRunImportPreparationHooks m_PreparationHooks;
        std::shared_ptr<const HubFirstRunWorkflowSnapshot> m_Snapshot;
        std::jthread m_Worker;
    };
} // namespace KeireHub
