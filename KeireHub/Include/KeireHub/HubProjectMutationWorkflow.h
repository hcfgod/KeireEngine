#pragma once

#include "KeireHubRuntime/ProjectWorkflowManager.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace KeireHub
{
    enum class HubProjectMutationState
    {
        Idle,
        Duplicating,
        Cancelling,
        Completed,
        Failed,
        Cancelled
    };

    struct HubProjectMutationSnapshot final
    {
        std::uint64_t OperationId = 0;
        HubProjectMutationState State = HubProjectMutationState::Idle;
        std::string SourceProjectId;
        std::filesystem::path Destination;
        std::uint64_t CopiedBytes = 0;
        std::size_t CopiedEntries = 0;
        std::optional<ProjectDuplicateResult> Result;
        std::optional<HubError> Failure;

        [[nodiscard]] bool IsActive() const noexcept;
        [[nodiscard]] bool IsTerminal() const noexcept;
    };

    struct HubProjectMutationServices final
    {
        std::function<HubResult<ProjectDuplicatePlan>(const std::string&, const std::filesystem::path&, std::string)>
            PrepareDuplicate;
        std::function<HubResult<ProjectDuplicateStagedResult>(ProjectDuplicatePlan, ProjectDuplicateCallbacks)>
            StageDuplicate;
        std::function<HubResult<ProjectDuplicateResult>(ProjectDuplicateStagedResult)> CommitDuplicate;
        std::function<HubStatus(const ProjectDuplicateStagedResult&)> DiscardDuplicate;
    };

    class HubProjectMutationWorkflow final
    {
      public:
        explicit HubProjectMutationWorkflow(HubProjectMutationServices services);
        ~HubProjectMutationWorkflow();

        HubProjectMutationWorkflow(const HubProjectMutationWorkflow&) = delete;
        HubProjectMutationWorkflow& operator=(const HubProjectMutationWorkflow&) = delete;

        [[nodiscard]] HubResult<std::uint64_t>
        StartDuplicate(std::string sourceProjectId, std::filesystem::path destination, std::string displayName);
        [[nodiscard]] HubResult<bool> Poll();
        [[nodiscard]] HubStatus Cancel(std::uint64_t operationId);
        [[nodiscard]] std::shared_ptr<const HubProjectMutationSnapshot> Snapshot() const;

      private:
        using StageResult = HubResult<ProjectDuplicateStagedResult>;

        [[nodiscard]] HubStatus RequireOwnerThread(std::string_view action) const;
        void Publish(HubProjectMutationSnapshot snapshot);
        void CaptureProgress(std::uint64_t bytes, std::size_t entries) noexcept;
        void PublishCapturedProgress(std::uint64_t operationId);
        void DiscardOnShutdown() noexcept;

        HubProjectMutationServices m_Services;
        std::future<StageResult> m_Future;
        std::atomic_bool m_CancelRequested = false;
        std::atomic_uint64_t m_CopiedBytes = 0;
        std::atomic_size_t m_CopiedEntries = 0;
        std::thread::id m_OwnerThread;
        std::uint64_t m_NextOperationId = 1;
        mutable std::mutex m_Mutex;
        std::shared_ptr<const HubProjectMutationSnapshot> m_Snapshot;
    };
} // namespace KeireHub
