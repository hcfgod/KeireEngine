#pragma once

#include "KeireHubRuntime/HubTaskStore.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace KeireHub
{
    struct HubTaskManagerSpecification final
    {
        std::uint32_t MaximumConcurrentDownloads = 2;
    };

    struct HubTaskDispatch final
    {
        std::string TaskId;
        HubTaskState InitialState = HubTaskState::Downloading;
    };

    class HubTaskManager final
    {
      public:
        using WorkerProbe = std::function<bool(std::uint64_t)>;

        explicit HubTaskManager(HubTaskStore& store, HubTaskManagerSpecification specification = {});

        [[nodiscard]] HubStatus Enqueue(HubTask task);
        [[nodiscard]] std::vector<HubTaskDispatch> Dispatchable() const;
        [[nodiscard]] HubStatus Claim(const HubTaskDispatch& dispatch, std::uint64_t workerProcessId,
                                      std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus ReportProgress(const std::string& taskId, HubTaskProgress progress,
                                               std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus Advance(const std::string& taskId, HubTaskState state, std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus Fail(const std::string& taskId, HubError failure, std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus Pause(const std::string& taskId, std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus Resume(const std::string& taskId, std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus Retry(const std::string& taskId, std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus RequestCancel(const std::string& taskId, std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus AcknowledgeCancelled(const std::string& taskId, std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus ReconcileWorkers(std::uint64_t nowUnixSeconds, const WorkerProbe& workerProbe);

        [[nodiscard]] std::uint32_t MaximumConcurrentDownloads() const noexcept;

      private:
        [[nodiscard]] static HubTaskState InitialState(const HubTask& task) noexcept;
        [[nodiscard]] static bool MutatesInstallation(const HubTask& task) noexcept;
        [[nodiscard]] const HubTask* Find(std::string_view taskId) const noexcept;

        HubTaskStore& m_Store;
        HubTaskManagerSpecification m_Specification;
    };
} // namespace KeireHub
