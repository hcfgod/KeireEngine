#include "KeireHubRuntime/HubTaskManager.h"

#include <algorithm>
#include <ranges>
#include <set>
#include <stdexcept>

namespace KeireHub
{
    HubTaskManager::HubTaskManager(HubTaskStore& store, HubTaskManagerSpecification specification)
        : m_Store(store), m_Specification(specification)
    {
        if (m_Specification.MaximumConcurrentDownloads == 0 || m_Specification.MaximumConcurrentDownloads > 8)
        {
            throw std::invalid_argument("Hub task download concurrency must be between one and eight.");
        }
    }

    HubStatus HubTaskManager::Enqueue(HubTask task) { return m_Store.Add(std::move(task)); }

    std::vector<HubTaskDispatch> HubTaskManager::Dispatchable() const
    {
        const auto snapshot = m_Store.Snapshot();
        std::uint32_t activeDownloads = 0;
        std::set<std::string, std::less<>> lockedInstallations;
        std::set<std::string, std::less<>> lockedPackages;
        for (const auto& task : *snapshot)
        {
            if (task.State == HubTaskState::Downloading)
                ++activeDownloads;
            if (!IsTerminal(task.State) && task.State != HubTaskState::Queued && task.State != HubTaskState::Paused &&
                MutatesInstallation(task) && task.TargetInstallationId)
            {
                lockedInstallations.insert(*task.TargetInstallationId);
            }
            if (!IsTerminal(task.State) && task.State != HubTaskState::Queued && task.State != HubTaskState::Paused)
                lockedPackages.insert(task.PackageIds.begin(), task.PackageIds.end());
        }

        std::vector<const HubTask*> queued;
        for (const auto& task : *snapshot)
        {
            if (task.State == HubTaskState::Queued)
                queued.push_back(&task);
        }
        std::ranges::sort(queued,
                          [](const HubTask* first, const HubTask* second)
                          {
                              if (first->CreatedUnixSeconds != second->CreatedUnixSeconds)
                                  return first->CreatedUnixSeconds < second->CreatedUnixSeconds;
                              return first->Id < second->Id;
                          });

        std::vector<HubTaskDispatch> result;
        result.reserve(queued.size());
        for (const auto* task : queued)
        {
            const auto state = InitialState(*task);
            if (state == HubTaskState::Downloading && activeDownloads >= m_Specification.MaximumConcurrentDownloads)
                continue;
            if (MutatesInstallation(*task) && task->TargetInstallationId &&
                lockedInstallations.contains(*task->TargetInstallationId))
            {
                continue;
            }
            if (std::ranges::any_of(task->PackageIds,
                                    [&](const std::string& package) { return lockedPackages.contains(package); }))
            {
                continue;
            }
            result.push_back({.TaskId = task->Id, .InitialState = state});
            if (state == HubTaskState::Downloading)
                ++activeDownloads;
            if (MutatesInstallation(*task) && task->TargetInstallationId)
                lockedInstallations.insert(*task->TargetInstallationId);
            lockedPackages.insert(task->PackageIds.begin(), task->PackageIds.end());
        }
        return result;
    }

    HubStatus HubTaskManager::Claim(const HubTaskDispatch& dispatch, const std::uint64_t workerProcessId,
                                    const std::uint64_t nowUnixSeconds)
    {
        const auto ready = Dispatchable();
        const auto found = std::ranges::find(ready, dispatch.TaskId, &HubTaskDispatch::TaskId);
        if (found == ready.end() || found->InitialState != dispatch.InitialState)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The task is not currently eligible for dispatch.",
                                       .AffectedItem = dispatch.TaskId});
        }
        return m_Store.Claim(dispatch.TaskId, dispatch.InitialState, workerProcessId, nowUnixSeconds);
    }

    HubStatus HubTaskManager::ReportProgress(const std::string& taskId, HubTaskProgress progress,
                                             const std::uint64_t nowUnixSeconds)
    {
        return m_Store.UpdateProgress(taskId, std::move(progress), nowUnixSeconds);
    }

    HubStatus HubTaskManager::Advance(const std::string& taskId, const HubTaskState state,
                                      const std::uint64_t nowUnixSeconds)
    {
        return m_Store.Transition(taskId, state, nowUnixSeconds);
    }

    HubStatus HubTaskManager::Fail(const std::string& taskId, HubError failure, const std::uint64_t nowUnixSeconds)
    {
        if (failure.Message.empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "A task failure requires a user-facing message.",
                                       .AffectedItem = taskId});
        }
        return m_Store.Transition(taskId, HubTaskState::Failed, nowUnixSeconds, std::move(failure));
    }

    HubStatus HubTaskManager::Pause(const std::string& taskId, const std::uint64_t nowUnixSeconds)
    {
        return m_Store.Transition(taskId, HubTaskState::Paused, nowUnixSeconds);
    }

    HubStatus HubTaskManager::Resume(const std::string& taskId, const std::uint64_t nowUnixSeconds)
    {
        const auto* task = Find(taskId);
        if (!task || task->State != HubTaskState::Paused)
        {
            return HubStatus::Failure({.Code = task ? HubErrorCode::InvalidTransition : HubErrorCode::NotFound,
                                       .Message = "Only a paused task can be resumed.",
                                       .AffectedItem = taskId});
        }
        return m_Store.Transition(taskId, HubTaskState::Queued, nowUnixSeconds);
    }

    HubStatus HubTaskManager::Retry(const std::string& taskId, const std::uint64_t nowUnixSeconds)
    {
        const auto* task = Find(taskId);
        if (!task)
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The task is no longer available.",
                                       .AffectedItem = taskId});
        if (task->State != HubTaskState::Failed || !task->Failure || !task->Failure->Retryable)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "This task cannot be retried.",
                                       .AffectedItem = taskId});
        }
        return m_Store.Transition(taskId, HubTaskState::Queued, nowUnixSeconds);
    }

    HubStatus HubTaskManager::RequestCancel(const std::string& taskId, const std::uint64_t nowUnixSeconds)
    {
        const auto* task = Find(taskId);
        if (!task)
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The task is no longer available.",
                                       .AffectedItem = taskId});
        if (task->State == HubTaskState::Queued || task->State == HubTaskState::Paused)
            return m_Store.Transition(taskId, HubTaskState::Cancelled, nowUnixSeconds);
        return m_Store.Transition(taskId, HubTaskState::Cancelling, nowUnixSeconds);
    }

    HubStatus HubTaskManager::AcknowledgeCancelled(const std::string& taskId, const std::uint64_t nowUnixSeconds)
    {
        return m_Store.Transition(taskId, HubTaskState::Cancelled, nowUnixSeconds);
    }

    HubStatus HubTaskManager::ReconcileWorkers(const std::uint64_t nowUnixSeconds, const WorkerProbe& workerProbe)
    {
        const auto snapshot = m_Store.Snapshot();
        for (const auto& task : *snapshot)
        {
            if (IsTerminal(task.State) || task.State == HubTaskState::Queued || task.State == HubTaskState::Paused)
                continue;
            if (task.WorkerProcessId && workerProbe && workerProbe(*task.WorkerProcessId))
                continue;
            if (task.State == HubTaskState::Downloading)
            {
                if (auto status = m_Store.Transition(task.Id, HubTaskState::Queued, nowUnixSeconds); !status)
                    return status;
                continue;
            }
            if (task.State == HubTaskState::Cancelling)
            {
                if (auto status = m_Store.Transition(task.Id, HubTaskState::Cancelled, nowUnixSeconds); !status)
                    return status;
                continue;
            }
            HubError failure{.Code = HubErrorCode::WorkerInterrupted,
                             .Message = "The package worker stopped before this operation reached a safe boundary.",
                             .Retryable = true,
                             .AffectedItem = task.Id,
                             .TechnicalDetails = "No live worker owns the persisted operation journal."};
            if (auto status = m_Store.Transition(task.Id, HubTaskState::Failed, nowUnixSeconds, std::move(failure));
                !status)
            {
                return status;
            }
        }
        return HubStatus::Success();
    }

    std::uint32_t HubTaskManager::MaximumConcurrentDownloads() const noexcept
    {
        return m_Specification.MaximumConcurrentDownloads;
    }

    HubTaskState HubTaskManager::InitialState(const HubTask& task) noexcept
    {
        if (!task.PackageIds.empty() || task.Kind == HubTaskKind::Download || task.Kind == HubTaskKind::HubUpdate)
            return HubTaskState::Downloading;
        if (task.Kind == HubTaskKind::Verify || task.Kind == HubTaskKind::ImportPackage)
            return HubTaskState::Verifying;
        return HubTaskState::Installing;
    }

    bool HubTaskManager::MutatesInstallation(const HubTask& task) noexcept
    {
        return task.Kind == HubTaskKind::Install || task.Kind == HubTaskKind::Repair ||
               task.Kind == HubTaskKind::Remove || task.Kind == HubTaskKind::ImportPackage ||
               task.Kind == HubTaskKind::HubUpdate || task.Kind == HubTaskKind::CreateProject;
    }

    const HubTask* HubTaskManager::Find(const std::string_view taskId) const noexcept
    {
        const auto snapshot = m_Store.Snapshot();
        const auto found = std::ranges::find(*snapshot, taskId, &HubTask::Id);
        return found == snapshot->end() ? nullptr : &*found;
    }
} // namespace KeireHub
