#include "KeireHub/HubMaintenanceWorkflow.h"

#include "KeireHub/HubProductUi.h"

#include <chrono>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError MaintenanceError(const HubErrorCode code, std::string message, std::string details = {},
                                                const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = "cache",
                    .TechnicalDetails = std::move(details)};
        }
    } // namespace

    HubMaintenanceWorkflow::HubMaintenanceWorkflow()
        : HubMaintenanceWorkflow({.ClearVerifiedCache = &ClearVerifiedPackageCache})
    {
    }

    HubMaintenanceWorkflow::HubMaintenanceWorkflow(HubMaintenanceServices services)
        : m_Services(std::move(services)), m_OwnerThread(std::this_thread::get_id()),
          m_Snapshot(std::make_shared<const HubMaintenanceSnapshot>())
    {
    }

    HubMaintenanceWorkflow::~HubMaintenanceWorkflow() { JoinWorker(); }

    HubResult<std::uint64_t> HubMaintenanceWorkflow::StartClearVerifiedCache(const std::span<const HubTask> tasks,
                                                                             std::filesystem::path cacheRoot)
    {
        if (const auto owner = RequireOwnerThread("start"); !owner)
            return HubResult<std::uint64_t>::Failure(owner.Error());
        if (m_Future.valid() || Snapshot()->IsRunning())
        {
            return HubResult<std::uint64_t>::Failure(MaintenanceError(
                HubErrorCode::InvalidTransition, "The verified package cache is already being cleared.", {}, true));
        }
        if (const auto validation = ValidateVerifiedPackageCacheClear(tasks); !validation)
            return HubResult<std::uint64_t>::Failure(validation.Error());
        if (!m_Services.ClearVerifiedCache)
        {
            return HubResult<std::uint64_t>::Failure(MaintenanceError(
                HubErrorCode::InvalidData, "The verified package cache maintenance service is unavailable."));
        }

        const auto operationId = m_NextOperationId;
        m_NextOperationId = m_NextOperationId == std::numeric_limits<std::uint64_t>::max() ? 1 : m_NextOperationId + 1;
        Publish({.OperationId = operationId, .State = HubMaintenanceState::Running});
        try
        {
            auto clear = m_Services.ClearVerifiedCache;
            m_Future =
                std::async(std::launch::async, [clear = std::move(clear), cacheRoot = std::move(cacheRoot)]() mutable
                           { return clear(cacheRoot); });
        }
        catch (const std::exception& error)
        {
            Publish({.OperationId = operationId,
                     .State = HubMaintenanceState::Failed,
                     .Failure =
                         MaintenanceError(HubErrorCode::WorkerInterrupted,
                                          "The cache maintenance worker could not be started.", error.what(), true)});
        }
        catch (...)
        {
            Publish({.OperationId = operationId,
                     .State = HubMaintenanceState::Failed,
                     .Failure = MaintenanceError(HubErrorCode::WorkerInterrupted,
                                                 "The cache maintenance worker could not be started.",
                                                 "The worker launcher failed with a non-standard exception.", true)});
        }
        return HubResult<std::uint64_t>::Success(operationId);
    }

    HubResult<bool> HubMaintenanceWorkflow::Poll()
    {
        if (const auto owner = RequireOwnerThread("poll"); !owner)
            return HubResult<bool>::Failure(owner.Error());
        if (!m_Future.valid() || m_Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return HubResult<bool>::Success(false);

        const auto active = Snapshot();
        HubStatus status = HubStatus::Failure(MaintenanceError(
            HubErrorCode::WorkerInterrupted, "The cache maintenance worker did not return a result.", {}, true));
        try
        {
            status = m_Future.get();
        }
        catch (const std::exception& error)
        {
            status = HubStatus::Failure(MaintenanceError(HubErrorCode::WorkerInterrupted,
                                                         "The verified package cache could not be cleared.",
                                                         error.what(), true));
        }
        catch (...)
        {
            status = HubStatus::Failure(
                MaintenanceError(HubErrorCode::WorkerInterrupted, "The verified package cache could not be cleared.",
                                 "The maintenance service failed with a non-standard exception.", true));
        }

        if (status)
            Publish({.OperationId = active->OperationId, .State = HubMaintenanceState::Completed});
        else
            Publish(
                {.OperationId = active->OperationId, .State = HubMaintenanceState::Failed, .Failure = status.Error()});
        return HubResult<bool>::Success(true);
    }

    std::shared_ptr<const HubMaintenanceSnapshot> HubMaintenanceWorkflow::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    std::optional<HubMaintenanceSnapshot> HubMaintenanceWorkflow::TakeCompletion()
    {
        std::scoped_lock lock(m_Mutex);
        if (!m_Snapshot->IsTerminal() || m_Snapshot->OperationId == m_ConsumedOperationId)
            return std::nullopt;
        m_ConsumedOperationId = m_Snapshot->OperationId;
        return *m_Snapshot;
    }

    HubStatus HubMaintenanceWorkflow::RequireOwnerThread(const std::string_view action) const
    {
        if (std::this_thread::get_id() == m_OwnerThread)
            return HubStatus::Success();
        return HubStatus::Failure(MaintenanceError(HubErrorCode::InvalidTransition,
                                                   "Hub maintenance must be coordinated by its owner thread.",
                                                   std::string(action)));
    }

    void HubMaintenanceWorkflow::Publish(HubMaintenanceSnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = std::make_shared<const HubMaintenanceSnapshot>(std::move(snapshot));
    }

    void HubMaintenanceWorkflow::JoinWorker() noexcept
    {
        if (!m_Future.valid())
            return;
        try
        {
            m_Future.wait();
        }
        catch (...)
        {
        }
    }

    void ApplyHubMaintenanceSnapshot(const HubMaintenanceSnapshot& maintenance, HubProductSnapshot& product)
    {
        product.VerifiedCacheClearRunning = maintenance.IsRunning();
        if (maintenance.OperationId == 0)
            return;

        const auto failed = maintenance.State == HubMaintenanceState::Failed;
        const auto taskId = "hub-maintenance-clear-cache-" + std::to_string(maintenance.OperationId);
        std::erase_if(product.Tasks, [&](const HubTaskUiRecord& task) { return task.Id == taskId; });
        product.Tasks.push_back({.Id = taskId,
                                 .Title = "Clear verified package cache",
                                 .Phase = maintenance.State == HubMaintenanceState::Running ? "Clearing"
                                          : failed                                          ? "Failed"
                                                                                            : "Completed",
                                 .Message = failed && maintenance.Failure
                                                ? maintenance.Failure->Message + " Return to Settings to try again."
                                            : maintenance.IsRunning() ? "Removing verified packages..."
                                                                      : "Verified package cache cleared.",
                                 .Progress = maintenance.IsRunning() ? 0.0F : 1.0F,
                                 .Active = maintenance.IsRunning(),
                                 .Retryable = false});
    }
} // namespace KeireHub
